#include "HotImageExportSync.h"

#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "HotImagePaths.h"
#include "HotImagesLog.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageCore.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace HotImageExportSyncPrivate
{
	bool IsRelevantPowerOfTwoProperty(const FName& PropertyName)
	{
		static const TArray<FName> RelevantProperties = {
			TEXT("PowerOfTwoMode"),
			TEXT("PaddingColor"),
			TEXT("bPadWithBorderColor"),
			TEXT("ResizeDuringBuildX"),
			TEXT("ResizeDuringBuildY"),
			TEXT("bChromaKeyTexture"),
			TEXT("ChromaKeyColor"),
			TEXT("ChromaKeyColorThreshold"),
			TEXT("ChromaKeyColorFeathering")
		};
		return RelevantProperties.Contains(PropertyName);
	}

	int64 NextPowerOfTwo(int64 Value)
	{
		if (Value <= 1)
		{
			return 1;
		}
		int64 Result = 1;
		while (Result < Value)
		{
			Result <<= 1;
		}
		return Result;
	}

	void FillPaddedPixels(uint8* Dest, const FImage& SourceImage, int32 DestW, int32 DestH, uint8 FillB, uint8 FillG, uint8 FillR, uint8 FillA)
	{
		const int32 SrcW = (int32)SourceImage.GetWidth();
		const int32 SrcH = (int32)SourceImage.GetHeight();
		const uint8* Src = SourceImage.RawData.GetData();

		uint8* Write = Dest;
		for (int32 Y = 0; Y < DestH; ++Y)
		{
			for (int32 X = 0; X < DestW; ++X)
			{
				// Border-pixel extension keeps padding from creating visible seams.
				const int32 SrcX = FMath::Min(X, SrcW - 1);
				const int32 SrcY = FMath::Min(Y, SrcH - 1);
				const uint8* P = Src + ((int64)SrcY * SrcW + SrcX) * 4;
				Write[0] = P[0];
				Write[1] = P[1];
				Write[2] = P[2];
				Write[3] = P[3];
				Write += 4;
			}
		}
	}

	bool EncodePngToFile(const uint8* BGRA8Data, int64 SizeX, int64 SizeY, const FString& DestinationPath)
	{
		if (!BGRA8Data || SizeX <= 0 || SizeY <= 0 || SizeX * SizeY * 4 <= 0)
		{
			return false;
		}

		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() ||
			!Wrapper->SetRaw(BGRA8Data, (int64)SizeX * SizeY * 4, (int32)SizeX, (int32)SizeY, ERGBFormat::BGRA, 8))
		{
			return false;
		}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
		TArray<uint8> Compressed = Wrapper->GetCompressed();
		if (Compressed.Num() == 0)
		{
			return false;
		}
#else
		TArray64<uint8> Compressed = Wrapper->GetCompressed();
		if (Compressed.Num() == 0)
		{
			return false;
		}
#endif

		const FString TempPath = DestinationPath + TEXT(".tmp");
		if (!FFileHelper::SaveArrayToFile(Compressed, *TempPath))
		{
			return false;
		}

		// Replace atomically so folder watchers never observe a partially written png.
		const bool bMoved = IFileManager::Get().Move(*DestinationPath, *TempPath, /*bReplace=*/true, /*bEvenIfReadOnly=*/true);
		if (!bMoved)
		{
			IFileManager::Get().Delete(*TempPath, false, true);
		}
		return bMoved;
	}
}

void FHotImageExportSync::Initialize()
{
	if (bInitialized)
	{
		return;
	}
	bInitialized = true;

	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ImportSubsystem->OnAssetPostImport.AddRaw(this, &FHotImageExportSync::HandleAssetPostImport);
			ImportSubsystem->OnAssetReimport.AddRaw(this, &FHotImageExportSync::HandleAssetReimport);
		}
	}
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FHotImageExportSync::HandleObjectPropertyChanged);
}

void FHotImageExportSync::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}
	bInitialized = false;

	if (GEditor)
	{
		if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ImportSubsystem->OnAssetPostImport.RemoveAll(this);
			ImportSubsystem->OnAssetReimport.RemoveAll(this);
		}
	}
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);

	if (DebounceHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DebounceHandle);
		DebounceHandle.Reset();
	}
	PendingTextures.Reset();
	LastExportedHashes.Reset();
	SelfExportedFileTimes.Reset();
}

bool FHotImageExportSync::ShouldSkipSelfExport(const FString& InPngPath, const FDateTime& InCurrentTimestamp) const
{
	const FDateTime* ExportedTime = SelfExportedFileTimes.Find(InPngPath);
	return ExportedTime && *ExportedTime == InCurrentTimestamp;
}

void FHotImageExportSync::HandleAssetPostImport(UFactory* Factory, UObject* CreatedObject)
{
	if (UTexture2D* Texture = Cast<UTexture2D>(CreatedObject))
	{
		ScheduleExport(Texture);
	}
}

void FHotImageExportSync::HandleAssetReimport(UObject* Asset)
{
	if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
	{
		ScheduleExport(Texture);
	}
}

void FHotImageExportSync::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	UTexture2D* Texture = Cast<UTexture2D>(Object);
	if (!Texture)
	{
		return;
	}

	if (PropertyChangedEvent.Property)
	{
		const FName PropertyName = PropertyChangedEvent.Property->GetFName();
		if (!HotImageExportSyncPrivate::IsRelevantPowerOfTwoProperty(PropertyName))
		{
			return;
		}
	}

	ScheduleExport(Texture);
}

void FHotImageExportSync::ScheduleExport(UTexture2D* Texture)
{
	if (!Texture || IsRunningCommandlet())
	{
		return;
	}

	// This editor module only mirrors assets that live directly inside a configured folder.
	bool bInWatchFolder = false;
	for (const FString& Rel : HotImagePaths::GetContentRelativeDirectories())
	{
		const FString Prefix = TEXT("/Game/") + Rel + TEXT("/");
		const FString PackageName = Texture->GetOutermost()->GetName();
		if (PackageName.StartsWith(Prefix, ESearchCase::CaseSensitive) &&
			!PackageName.RightChop(Prefix.Len()).Contains(TEXT("/")))
		{
			bInWatchFolder = true;
			break;
		}
	}
	if (!bInWatchFolder)
	{
		return;
	}

	PendingTextures.AddUnique(TWeakObjectPtr<UTexture2D>(Texture));

	const float Debounce = FMath::Max(0.15f, 0.5f);
	if (!DebounceHandle.IsValid())
	{
		FTickerDelegate TickDelegate = FTickerDelegate::CreateRaw(this, &FHotImageExportSync::RunPendingExports);
		DebounceHandle = FTSTicker::GetCoreTicker().AddTicker(TickDelegate, Debounce);
	}
}

bool FHotImageExportSync::RunPendingExports(float DeltaTime)
{
	FTSTicker::GetCoreTicker().RemoveTicker(DebounceHandle);
	DebounceHandle.Reset();

	TArray<UTexture2D*> Textures;
	Textures.Reserve(PendingTextures.Num());
	for (const TWeakObjectPtr<UTexture2D>& Weak : PendingTextures)
	{
		if (UTexture2D* Texture = Weak.Get())
		{
			Textures.AddUnique(Texture);
		}
	}
	PendingTextures.Reset();

	for (UTexture2D* Texture : Textures)
	{
		ExportTextureToPng(Texture);
	}
	return false; // one-shot ticker
}

bool FHotImageExportSync::ExportTextureToPng(UTexture2D* Texture)
{
	if (!Texture || !Texture->Source.IsValid())
	{
		return false;
	}

	FString FolderRel;
	FString SlotName;
	{
		const FString PackageName = Texture->GetOutermost()->GetName();
		bool bFound = false;
		for (const FString& Rel : HotImagePaths::GetContentRelativeDirectories())
		{
			const FString Prefix = TEXT("/Game/") + Rel + TEXT("/");
			if (PackageName.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				FString Remainder = PackageName.RightChop(Prefix.Len());
				if (!Remainder.Contains(TEXT("/")))
				{
					FolderRel = Rel;
					SlotName = Remainder;
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
		{
			return false;
		}
	}

	// Skip when this very png was the import source (that is the normal pairing that
	// already exists; re-exporting would only cause unnecessary watcher round-trips).
	if (Texture->AssetImportData && Texture->AssetImportData->GetFirstFilename().Len() > 0)
	{
		const FString ExistingSource = FPaths::ConvertRelativePathToFull(Texture->AssetImportData->GetFirstFilename());
		const FString DestPng = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), FolderRel, SlotName + TEXT(".png")));
		if (FPaths::FileExists(DestPng) && ExistingSource.Equals(DestPng, ESearchCase::IgnoreCase))
		{
			return false;
		}
	}

	FImage SourceImage;
	if (!Texture->Source.GetMipImage(SourceImage, 0))
	{
		UE_LOG(LogHotImages, Warning, TEXT("HotImageExportSync: cannot read source image for '%s'."), *Texture->GetName());
		return false;
	}

	FImage WorkingImage;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
	SourceImage.CopyTo(WorkingImage, ERawImageFormat::BGRA8, true);
#else
	SourceImage.CopyTo(WorkingImage, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
#endif

	// Apply the same dimension transforms UE applies during texture building.
	const int64 SrcW = WorkingImage.GetWidth();
	const int64 SrcH = WorkingImage.GetHeight();
	int64 DestW = SrcW;
	int64 DestH = SrcH;

	const ETexturePowerOfTwoSetting::Type Mode = Texture->PowerOfTwoMode;
	bool bPadToTarget = false;
	if (Mode == ETexturePowerOfTwoSetting::PadToPowerOfTwo)
	{
		DestW = HotImageExportSyncPrivate::NextPowerOfTwo(SrcW);
		DestH = HotImageExportSyncPrivate::NextPowerOfTwo(SrcH);
		bPadToTarget = true;
	}
	else if (Mode == ETexturePowerOfTwoSetting::PadToSquarePowerOfTwo)
	{
		DestW = DestH = HotImageExportSyncPrivate::NextPowerOfTwo(FMath::Max(SrcW, SrcH));
		bPadToTarget = true;
	}
	else if (Mode == ETexturePowerOfTwoSetting::StretchToPowerOfTwo)
	{
		DestW = HotImageExportSyncPrivate::NextPowerOfTwo(SrcW);
		DestH = HotImageExportSyncPrivate::NextPowerOfTwo(SrcH);
	}
	else if (Mode == ETexturePowerOfTwoSetting::StretchToSquarePowerOfTwo)
	{
		DestW = DestH = HotImageExportSyncPrivate::NextPowerOfTwo(FMath::Max(SrcW, SrcH));
	}
	else if (Mode == ETexturePowerOfTwoSetting::ResizeToSpecificResolution)
	{
		DestW = Texture->ResizeDuringBuildX > 0 ? Texture->ResizeDuringBuildX : SrcW;
		DestH = Texture->ResizeDuringBuildY > 0 ? Texture->ResizeDuringBuildY : SrcH;
	}

	if (bPadToTarget && (DestW != SrcW || DestH != SrcH))
	{
		FImage Padded((int32)DestW, (int32)DestH, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		HotImageExportSyncPrivate::FillPaddedPixels(
			Padded.RawData.GetData(),
			WorkingImage,
			(int32)DestW,
			(int32)DestH,
			0, 0, 0, 255);
		WorkingImage.Swap(Padded);
	}
	else if (DestW != SrcW || DestH != SrcH)
	{
		FImage Resized;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
		WorkingImage.ResizeTo(Resized, (int32)DestW, (int32)DestH, ERawImageFormat::BGRA8, true);
#else
		WorkingImage.ResizeTo(Resized, (int32)DestW, (int32)DestH, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
#endif
		WorkingImage.Swap(Resized);
	}

	const FString DestinationPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectContentDir(), FolderRel, SlotName + TEXT(".png")));

	FMD5Hash ContentHash;
	{
		FMD5 Md5Gen;
		Md5Gen.Update(WorkingImage.RawData.GetData(), WorkingImage.RawData.Num());
		ContentHash.Set(Md5Gen);
	}
	if (const FMD5Hash* Existing = LastExportedHashes.Find(DestinationPath))
	{
		if (*Existing == ContentHash && FPaths::FileExists(DestinationPath))
		{
			return false; // Content already exported.
		}
	}

	if (!HotImageExportSyncPrivate::EncodePngToFile(WorkingImage.RawData.GetData(), DestW, DestH, DestinationPath))
	{
		UE_LOG(LogHotImages, Warning, TEXT("HotImageExportSync: failed to export '%s' to '%s'."), *Texture->GetName(), *DestinationPath);
		return false;
	}

	LastExportedHashes.Add(DestinationPath, ContentHash);
	SelfExportedFileTimes.Add(DestinationPath, IFileManager::Get().GetTimeStamp(*DestinationPath));
	UE_LOG(LogHotImages, Log, TEXT("HotImageExportSync: exported '%s' -> '%s' (%lldx%lld)."), *Texture->GetName(), *DestinationPath, DestW, DestH);
	return true;
}
