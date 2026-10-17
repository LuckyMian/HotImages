#include "HotImagesEditorModule.h"

#include "AssetToolsModule.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "Framework/Commands/UIAction.h"
#include "HAL/PlatformProcess.h"
#include "HotImageDirWatcher.h"
#include "HotImageExportSync.h"
#include "HotImagePaths.h"
#include "HotImagesLog.h"
#include "HotImagesSettings.h"
#include "IAssetTools.h"
#include "Misc/CoreMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Settings/ProjectPackagingSettings.h"
#include "Textures/SlateIcon.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "FHotImagesEditorModule"

namespace HotImagesEditorPrivate
{
	bool IsSourceStale(UTexture2D* Texture, const FString& ImagePath)
	{
		if (!Texture || !Texture->AssetImportData)
		{
			return true;
		}

		const FAssetImportInfo& ImportInfo = Texture->AssetImportData->GetSourceData();
		if (ImportInfo.SourceFiles.Num() == 0)
		{
			return true;
		}

		const FDateTime ImportedTime = ImportInfo.SourceFiles[0].Timestamp;
		const FDateTime DiskTime = IFileManager::Get().GetTimeStamp(*ImagePath);
		if (DiskTime == FDateTime::MinValue() || DiskTime == FDateTime(0))
		{
			return false;
		}
		return ImportedTime == FDateTime(0) || DiskTime > ImportedTime;
	}

	bool SavePackageForObject(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			return false;
		}

		Package->MarkPackageDirty();

		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());
		if (FileName.IsEmpty())
		{
			UE_LOG(LogHotImages, Warning, TEXT("HotImages sync: cannot resolve package file name for '%s'."), *Package->GetName());
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GWarn;
		SaveArgs.bSlowTask = false;
		return UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
	}

	bool ReimportTexture(UTexture2D* Texture, const FString& ImagePath)
	{
		if (!Texture)
		{
			return false;
		}

		// Force the recorded source path to the current file when it is missing/wrong,
		// so the engine reimport pipeline reads the right companion image.
		if (Texture->AssetImportData && !ImagePath.IsEmpty() &&
			(Texture->AssetImportData->GetFirstFilename().IsEmpty() ||
			 !FPaths::FileExists(Texture->AssetImportData->GetFirstFilename())))
		{
			Texture->AssetImportData->Update(ImagePath);
		}

		// Public engine entry point (no dialogs, runs the registered texture reimport handler).
		UTextureFactory::SuppressImportOverwriteDialog(true);
		const bool bOk = FReimportManager::Instance()->Reimport(
			Texture,
			/*bAskForNewFileIfMissing=*/false,
			/*bShowNotification=*/false,
			ImagePath);
		UTextureFactory::SuppressImportOverwriteDialog(false);
		if (!bOk)
		{
			UE_LOG(LogHotImages, Warning, TEXT("HotImages sync: re-import of '%s' failed (%s)."),
				*ImagePath, TEXT("see prior editor log"));
			return false;
		}

		return SavePackageForObject(Texture);
	}

	bool ImportNewTexture(const FString& ImagePath, const FString& AssetFolder)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		TArray<FString> Files;
		Files.Add(ImagePath);

		UTextureFactory::SuppressImportOverwriteDialog(true);
		const TArray<UObject*> Imported = AssetTools.ImportAssets(Files, AssetFolder, nullptr, /*bSyncToBrowser=*/false);
		UTextureFactory::SuppressImportOverwriteDialog(false);
		if (Imported.Num() == 0)
		{
			UE_LOG(LogHotImages, Warning, TEXT("HotImages sync: import failed for '%s'."), *ImagePath);
			return false;
		}

		bool bAllSaved = true;
		for (UObject* Object : Imported)
		{
			bAllSaved = SavePackageForObject(Object) && bAllSaved;
		}
		if (UTexture2D* Texture = Cast<UTexture2D>(Imported[0]))
		{
			UE_LOG(LogHotImages, Log, TEXT("HotImages sync: imported new proxy texture '%s'%s."),
				*Texture->GetName(), bAllSaved ? TEXT("") : TEXT(" (package save failed)"));
		}
		return bAllSaved;
	}

	struct FSyncFolderResult
	{
		int32 SyncedCount = 0;
		int32 ImageFileCount = 0;
	};

	FSyncFolderResult SyncOneFolder(const FString& FolderRel, const class FHotImageExportSync* ExportSync)
	{
		FSyncFolderResult Result;

		const FString WatchDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), FolderRel));
		if (!FPaths::DirectoryExists(WatchDir))
		{
			return Result;
		}

		const FString AssetFolder = HotImagePaths::ContentRelativeToAssetFolder(FolderRel);
		TArray<FString> FileNames;
		IFileManager::Get().FindFiles(FileNames, *(WatchDir / TEXT("*")), /*Files=*/true, /*Directories=*/false);

		for (const FString& FileName : FileNames)
		{
			if (FileName.StartsWith(TEXT(".")))
			{
				continue;
			}

			const FString Ext = FPaths::GetExtension(FileName);
			if (!HotImagePaths::IsSupportedImageExtension(Ext))
			{
				continue;
			}

			const FString ImagePath = FPaths::Combine(WatchDir, FileName);
			const FString SlotName = FPaths::GetBaseFilename(FileName);
			// Prefer png when the same base name exists in both png and jpg.
			if (Ext != TEXT("png") && FPaths::FileExists(FPaths::Combine(WatchDir, SlotName + TEXT(".png"))))
			{
				continue;
			}
			const FString AssetPath = HotImagePaths::GetAssetPathForSlotInFolder(FolderRel, SlotName);
			const FString AssetFileOnDisk = FPaths::Combine(WatchDir, SlotName + TEXT(".uasset"));
			++Result.ImageFileCount;

			// Ignore png files that this plugin just exported from the same texture
			// (reverse sync). Without this, every manual import would cause one extra
			// import/re-import round trip and raise engine overwrite dialogs.
			const FDateTime DiskTime = IFileManager::Get().GetTimeStamp(*ImagePath);
			if (ExportSync && ExportSync->ShouldSkipSelfExport(ImagePath, DiskTime))
			{
				continue;
			}

			const bool bFileOnDisk = FPaths::FileExists(AssetFileOnDisk);
			UTexture2D* Texture = nullptr;
			if (bFileOnDisk)
			{
				Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);
			}
			else
			{
				// An import can still be finishing and the package may not be flushed to
				// disk yet. Only run ImportNewTexture when there is truly no asset at all.
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
				Texture = static_cast<UTexture2D*>(StaticFindObject(UTexture2D::StaticClass(), nullptr, *AssetPath, EFindObjectFlags::None));
#else
				Texture = static_cast<UTexture2D*>(StaticFindObject(UTexture2D::StaticClass(), nullptr, *AssetPath, /*ExactClass=*/false));
#endif
			}

			if (!Texture && !bFileOnDisk)
			{
				if (ImportNewTexture(ImagePath, AssetFolder))
				{
					++Result.SyncedCount;
				}
				continue;
			}

			if (!Texture)
			{
				UE_LOG(LogHotImages, Warning, TEXT("HotImages sync: '%s' exists on disk but does not contain a UTexture2D; skipping."), *AssetFileOnDisk);
				continue;
			}

			if (!IsSourceStale(Texture, ImagePath))
			{
				continue;
			}

			if (ReimportTexture(Texture, ImagePath))
			{
				++Result.SyncedCount;
				UE_LOG(LogHotImages, Log, TEXT("HotImages sync: re-imported '%s'."), *FileName);
			}
		}

		return Result;
	}
}

void FHotImagesEditorModule::StartupModule()
{
	// Never auto-sync inside commandlets/cooks or -game runs; those runs must not mutate assets.
	if (!GIsEditor || IsRunningCommandlet() || IsRunningGame())
	{
		return;
	}

	RegisterMenus();

	UHotImageSettings* MutableSettings = GetMutableDefault<UHotImageSettings>();
	if (MutableSettings)
	{
		BoundSettingsObject = MutableSettings;
		MutableSettings->OnSettingChanged().AddRaw(this, &FHotImagesEditorModule::OnSettingsChanged);
	}
	SyncStagingDirectories();

	ExportSync = MakeShared<FHotImageExportSync>();
	ExportSync->Initialize();

	const UHotImageSettings* Settings = GetDefault<UHotImageSettings>();
	if (!Settings || !Settings->bAutoSyncInEditor)
	{
		UE_LOG(LogHotImages, Log, TEXT("HotImages editor auto-sync is disabled in project settings."));
		return;
	}

	StartWatcher();

	// Give the asset registry/editor a moment before the initial import pass.
	RequestFullSync(1.0f);
}

void FHotImagesEditorModule::ShutdownModule()
{
	// During engine exit the settings default object/class may already be torn
	// down; skip delegate cleanup entirely in that case. Live Coding unloads
	// (not exiting) still unbind safely through the cached pointer.
	if (BoundSettingsObject && !IsEngineExitRequested())
	{
		BoundSettingsObject->OnSettingChanged().RemoveAll(this);
		BoundSettingsObject = nullptr;
	}

	if (ExportSync.IsValid())
	{
		ExportSync->Shutdown();
		ExportSync.Reset();
	}

	if (SyncTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SyncTickerHandle);
		SyncTickerHandle.Reset();
	}
	if (WatchTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WatchTickerHandle);
		WatchTickerHandle.Reset();
	}
	StopWatcher();

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->UnregisterOwner(TEXT("HotImages"));
	}
}

void FHotImagesEditorModule::RegisterMenus()
{
	if (!UToolMenus::IsToolMenuUIEnabled())
	{
		return;
	}

	FToolMenuOwnerScoped OwnerScoped(TEXT("HotImages"));
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	UToolMenu* ToolsMenu = ToolMenus->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!ToolsMenu)
	{
		return;
	}

	FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("HotImages"), LOCTEXT("HotImagesSection", "Hot Images"));
	Section.AddEntry(
		FToolMenuEntry::InitMenuEntry(
			TEXT("HotImagesRescan"),
			LOCTEXT("RescanLabel", "Rescan Hot Images Folders"),
			LOCTEXT("RescanTooltip", "Scan every configured Content folder and import/re-import proxy textures."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FHotImagesEditorModule::MenuRescanFolder))));

	Section.AddEntry(
		FToolMenuEntry::InitMenuEntry(
			TEXT("HotImagesOpenFolder"),
			LOCTEXT("OpenFolderLabel", "Open First Hot Images Folder"),
			LOCTEXT("OpenFolderTooltip", "Open the first watched image folder in the operating system."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FHotImagesEditorModule::MenuOpenFolder))));
}

void FHotImagesEditorModule::MenuRescanFolder()
{
	RequestFullSync(0.05f);
}

void FHotImagesEditorModule::MenuOpenFolder()
{
	const FString WatchDir = HotImagePaths::GetAbsoluteWatchDirectory();
	FPlatformProcess::ExploreFolder(*WatchDir);
}

void FHotImagesEditorModule::StartWatcher()
{
	const UHotImageSettings* Settings = GetDefault<UHotImageSettings>();
	if (!Settings || !Settings->bAutoSyncInEditor)
	{
		return;
	}

	for (const FString& Rel : HotImagePaths::GetContentRelativeDirectories())
	{
		const FString WatchDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), Rel));
		if (!FPaths::DirectoryExists(WatchDir))
		{
			IFileManager::Get().MakeDirectory(*WatchDir, /*Tree=*/true);
		}

		TSharedPtr<FHotImageDirWatcher> Watcher = MakeShared<FHotImageDirWatcher>(WatchDir);
		if (Watcher->Start())
		{
			Watchers.Add(Watcher);
			UE_LOG(LogHotImages, Log, TEXT("HotImages editor is watching '%s'."), *WatchDir);
		}
	}

	if (Watchers.Num() > 0)
	{
		const float PollInterval = FMath::Max(0.1f, Settings->UpdateDebounceSeconds * 0.5f);
		FTickerDelegate TickDelegate = FTickerDelegate::CreateRaw(this, &FHotImagesEditorModule::WatchTick);
		WatchTickerHandle = FTSTicker::GetCoreTicker().AddTicker(TickDelegate, PollInterval);
	}
}

void FHotImagesEditorModule::StopWatcher()
{
	for (TSharedPtr<FHotImageDirWatcher>& Watcher : Watchers)
	{
		if (Watcher.IsValid())
		{
			Watcher->Stop();
		}
	}
	Watchers.Reset();

	if (WatchTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WatchTickerHandle);
		WatchTickerHandle.Reset();
	}
}

void FHotImagesEditorModule::RestartWatcher()
{
	StopWatcher();
	StartWatcher();
}

void FHotImagesEditorModule::SyncStagingDirectories()
{
	if (IsEngineExitRequested())
	{
		return;
	}

	UProjectPackagingSettings* PackagingSettings = GetMutableDefault<UProjectPackagingSettings>();
	if (!PackagingSettings)
	{
		return;
	}

	bool bChanged = false;
	for (const FString& Rel : HotImagePaths::GetContentRelativeDirectories())
	{
		const FString Normalized = HotImagePaths::NormalizeContentRelativePath(Rel);
		if (Normalized.IsEmpty())
		{
			continue;
		}

		bool bAlreadyListed = false;
		for (const FDirectoryPath& Existing : PackagingSettings->DirectoriesToAlwaysStageAsNonUFS)
		{
			const FString ExistingNormalized = HotImagePaths::NormalizeContentRelativePath(Existing.Path);
			if (ExistingNormalized.Equals(Normalized, ESearchCase::IgnoreCase))
			{
				bAlreadyListed = true;
				break;
			}
		}

		if (!bAlreadyListed)
		{
			FDirectoryPath NewEntry;
			NewEntry.Path = Normalized;
			PackagingSettings->DirectoriesToAlwaysStageAsNonUFS.Add(NewEntry);
			bChanged = true;
			UE_LOG(LogHotImages, Log,
				TEXT("HotImages: added '%s' to packaging 'Additional Non-Asset Directories To Copy'."), *Normalized);
		}
	}

	if (bChanged)
	{
		PackagingSettings->TryUpdateDefaultConfigFile(TEXT(""), /*bWarnIfFail=*/false);
	}
}

bool FHotImagesEditorModule::WatchTick(float DeltaTime)
{
	if (Watchers.Num() == 0)
	{
		return true;
	}

	bool bPending = false;
	for (TSharedPtr<FHotImageDirWatcher>& Watcher : Watchers)
	{
		if (Watcher.IsValid() && Watcher->ConsumePending())
		{
			bPending = true;
		}
	}
	if (bPending)
	{
		const UHotImageSettings* Settings = GetDefault<UHotImageSettings>();
		RequestFullSync(Settings ? FMath::Max(0.05f, Settings->UpdateDebounceSeconds) : 0.5f);
	}
	return true;
}

bool FHotImagesEditorModule::RunScheduledSync(float DeltaTime)
{
	bSyncQueued = false;
	SyncTickerHandle.Reset();

	if (IsRunningCommandlet() || IsRunningGame())
	{
		return false;
	}

	int32 TotalSynced = 0;
	int32 TotalImages = 0;
	for (const FString& Rel : HotImagePaths::GetContentRelativeDirectories())
	{
		const HotImagesEditorPrivate::FSyncFolderResult FolderResult = HotImagesEditorPrivate::SyncOneFolder(Rel, ExportSync.Get());
		TotalSynced += FolderResult.SyncedCount;
		TotalImages += FolderResult.ImageFileCount;
	}

	UE_LOG(LogHotImages, Log, TEXT("HotImages folder sync finished (%d change(s) applied across %d image file(s) in %d configured folder(s))."),
		TotalSynced, TotalImages, HotImagePaths::GetContentRelativeDirectories().Num());
	return false;
}

void FHotImagesEditorModule::RequestFullSync(float InDelaySeconds)
{
	bSyncQueued = true;
	if (SyncTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SyncTickerHandle);
		SyncTickerHandle.Reset();
	}

	FTickerDelegate SyncDelegate = FTickerDelegate::CreateRaw(this, &FHotImagesEditorModule::RunScheduledSync);
	SyncTickerHandle = FTSTicker::GetCoreTicker().AddTicker(SyncDelegate, FMath::Max(0.02f, InDelaySeconds));
}

void FHotImagesEditorModule::OnSettingsChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	RestartWatcher();
	SyncStagingDirectories();
	const UHotImageSettings* Settings = GetDefault<UHotImageSettings>();
	if (Settings && Settings->bAutoSyncInEditor)
	{
		RequestFullSync(0.5f);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FHotImagesEditorModule, HotImagesEditor)
