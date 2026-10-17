#include "HotImagesSubsystem.h"

#include "Engine/Texture2D.h"
#include "HotImageDirWatcher.h"
#include "HotImagePaths.h"
#include "HotImagesLog.h"
#include "HotImagesSettings.h"
#include "HotImageTextureUpdater.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

void UHotImageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UHotImageSettings* Settings = GetDefault<UHotImageSettings>();
	const float Interval = Settings && Settings->UpdateDebounceSeconds > 0.f
		? Settings->UpdateDebounceSeconds
		: 0.5f;

	FTickerDelegate TickDelegate = FTickerDelegate::CreateUObject(this, &UHotImageSubsystem::TickWatcher);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TickDelegate, FMath::Max(0.05f, Interval));

	bRuntimeRefresh = IsRuntimeRefreshEnabled();
	if (bRuntimeRefresh)
	{
		StartWatcher();
	}
	else
	{
		UE_LOG(LogHotImages, Log,
			TEXT("HotImageSubsystem: runtime folder watching is disabled%s (editor builds use the editor re-import path)."),
			WITH_EDITOR ? TEXT(" in this editor process") : TEXT(" by project settings"));
	}
}

void UHotImageSubsystem::Deinitialize()
{
	StopWatcher();
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	Super::Deinitialize();
}

UTexture2D* UHotImageSubsystem::GetTexture(const FString& SlotName)
{
	if (SlotName.IsEmpty())
	{
		return nullptr;
	}

	// Slot names are unique inside one folder, but several configured folders may
	// contain the same name. Prefer the first configured folder that has the file,
	// then fall back to any folder where the asset is already present (e.g. before
	// its companion png has been exported, or while running inside the editor).
	for (const FString& Rel : HotImagePaths::GetContentRelativeDirectories())
	{
		const FString ImagePath = HotImagePaths::FindImageFileForSlotInFolder(Rel, SlotName);
		if (ImagePath.IsEmpty())
		{
			continue;
		}

		const FString AssetPath = HotImagePaths::GetAssetPathForSlotInFolder(Rel, SlotName);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);
		if (!Texture)
		{
			continue;
		}

		if (bRuntimeRefresh)
		{
			ApplySlotFromDisk(Rel, SlotName, Texture, /*bForce=*/false);
		}
		return Texture;
	}

	for (const FString& Rel : HotImagePaths::GetContentRelativeDirectories())
	{
		const FString AssetPath = HotImagePaths::GetAssetPathForSlotInFolder(Rel, SlotName);
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath))
		{
			return Texture;
		}
	}
	return nullptr;
}

void UHotImageSubsystem::RefreshSlot(const FString& SlotName)
{
	if (!bRuntimeRefresh)
	{
		UE_LOG(LogHotImages, Log, TEXT("RefreshSlot('%s') ignored: runtime refresh is disabled in this build."), *SlotName);
		return;
	}

	for (const FString& Rel : HotImagePaths::GetContentRelativeDirectories())
	{
		const FString ImagePath = HotImagePaths::FindImageFileForSlotInFolder(Rel, SlotName);
		if (ImagePath.IsEmpty())
		{
			continue;
		}

		const FString AssetPath = HotImagePaths::GetAssetPathForSlotInFolder(Rel, SlotName);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath);
		if (Texture)
		{
			ApplySlotFromDisk(Rel, SlotName, Texture, /*bForce=*/true);
		}
	}
}

void UHotImageSubsystem::RefreshAll()
{
	if (!bRuntimeRefresh)
	{
		UE_LOG(LogHotImages, Log, TEXT("RefreshAll ignored: runtime refresh is disabled in this build."));
		return;
	}
	RefreshAllInternal(/*bForce=*/true);
}

bool UHotImageSubsystem::IsRuntimeWatching() const
{
	return Watchers.Num() > 0;
}

bool UHotImageSubsystem::IsRuntimeRefreshEnabled() const
{
	const UHotImageSettings* Settings = GetDefault<UHotImageSettings>();
	if (!Settings || !Settings->bApplyChangesAtRuntime)
	{
		return false;
	}
#if WITH_EDITOR
	// Editor builds normally let the editor module refresh proxy assets by re-import.
	// This command-line flag enables the in-memory runtime path for testing without
	// cooking a full package (used by -game runs in the demo).
	return FParse::Param(FCommandLine::Get(), TEXT("HotImagesRuntimeSimulation"));
#else
	return true;
#endif
}

void UHotImageSubsystem::StartWatcher()
{
	const TArray<FString> WatchDirs = HotImagePaths::GetAbsoluteWatchDirectories();
	for (const FString& WatchDir : WatchDirs)
	{
		if (!FPaths::DirectoryExists(WatchDir))
		{
			UE_LOG(LogHotImages, Warning, TEXT("HotImageSubsystem: watch directory '%s' does not exist; runtime watching disabled for it."), *WatchDir);
			continue;
		}

		TSharedPtr<FHotImageDirWatcher> Watcher = MakeShared<FHotImageDirWatcher>(WatchDir);
		if (Watcher->Start())
		{
			Watchers.Add(Watcher);
			UE_LOG(LogHotImages, Log, TEXT("HotImageSubsystem: watching '%s' for image updates."), *WatchDir);
		}
	}
}

void UHotImageSubsystem::StopWatcher()
{
	for (TSharedPtr<FHotImageDirWatcher>& Watcher : Watchers)
	{
		if (Watcher.IsValid())
		{
			Watcher->Stop();
		}
	}
	Watchers.Reset();
}

bool UHotImageSubsystem::TickWatcher(float DeltaTime)
{
	if (!bRuntimeRefresh)
	{
		return true;
	}

	bool bChanged = false;
	for (TSharedPtr<FHotImageDirWatcher>& Watcher : Watchers)
	{
		if (Watcher.IsValid() && Watcher->ConsumePending())
		{
			bChanged = true;
		}
	}
	if (bChanged)
	{
		// A new file-system event permits one retry of files that previously failed
		// to decode. GetTexture may be called every frame, so failures are otherwise
		// cached until the file actually changes.
		LastFailedFileTime.Reset();
		RefreshAllInternal(/*bForce=*/false);
	}
	return true;
}

bool UHotImageSubsystem::ApplySlotFromDisk(const FString& FolderRel, const FString& SlotName, UTexture2D* Texture, bool bForce)
{
	const FString ImagePath = HotImagePaths::FindImageFileForSlotInFolder(FolderRel, SlotName);
	if (ImagePath.IsEmpty())
	{
		UE_LOG(LogHotImages, Warning, TEXT("ApplySlotFromDisk('%s'): no png/jpg file found in folder '%s'."), *SlotName, *FolderRel);
		return false;
	}

	const FDateTime DiskTime = IFileManager::Get().GetTimeStamp(*ImagePath);
	const FString CacheKey = FolderRel / SlotName;
	const FDateTime* Cached = LastAppliedFileTime.Find(CacheKey);
	if (!bForce && Cached && *Cached == DiskTime)
	{
		return false; // Already applied this exact file revision.
	}
	const FDateTime* Failed = LastFailedFileTime.Find(CacheKey);
	if (!bForce && Failed && *Failed == DiskTime)
	{
		return false; // This exact revision already failed; wait for another file event.
	}

	if (!HotImageTextureUpdater::UpdateTextureFromFile(Texture, ImagePath))
	{
		// Keep the previous pixels; the next file event / manual refresh will retry.
		LastFailedFileTime.Add(CacheKey, DiskTime);
		UE_LOG(LogHotImages, Warning, TEXT("ApplySlotFromDisk('%s'): keeping previous texture content."), *SlotName);
		return false;
	}

	LastFailedFileTime.Remove(CacheKey);
	LastAppliedFileTime.Add(CacheKey, DiskTime);
	OnHotImageUpdated.Broadcast(SlotName, Texture);
	UE_LOG(LogHotImages, Log, TEXT("HotImageSubsystem: refreshed slot '%s' from '%s'."), *SlotName, *ImagePath);
	return true;
}

void UHotImageSubsystem::RefreshAllInternal(bool bForce)
{
	int32 RefreshedCount = 0;

	for (const FString& Rel : HotImagePaths::GetContentRelativeDirectories())
	{
		const FString WatchDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), Rel));
		if (!FPaths::DirectoryExists(WatchDir))
		{
			continue;
		}

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

			const FString SlotName = FPaths::GetBaseFilename(FileName);
			// Prefer png when the same base name exists in both png and jpg.
			if (Ext != TEXT("png") && FPaths::FileExists(FPaths::Combine(WatchDir, SlotName + TEXT(".png"))))
			{
				continue;
			}
			const FString AssetPath = HotImagePaths::GetAssetPathForSlotInFolder(Rel, SlotName);

			// Only refresh textures that are actually loaded/used.
			UTexture2D* Texture = nullptr;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			Texture = static_cast<UTexture2D*>(StaticFindObject(UTexture2D::StaticClass(), nullptr, *AssetPath, EFindObjectFlags::None));
#else
			Texture = static_cast<UTexture2D*>(StaticFindObject(UTexture2D::StaticClass(), nullptr, *AssetPath, /*ExactClass=*/false));
#endif
			if (!Texture)
			{
				continue;
			}

			if (ApplySlotFromDisk(Rel, SlotName, Texture, bForce))
			{
				++RefreshedCount;
			}
		}
	}

	if (RefreshedCount > 0)
	{
		UE_LOG(LogHotImages, Log, TEXT("HotImageSubsystem: refreshed %d texture(s) from the configured folders."), RefreshedCount);
	}
}
