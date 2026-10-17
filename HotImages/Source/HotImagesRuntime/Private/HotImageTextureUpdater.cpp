#include "HotImageTextureUpdater.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HotImagesLog.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
#include "HAL/Platform.h"
#endif

namespace HotImageTextureUpdater
{
	namespace
	{
		EImageFormat DetectImageFormat(const TArray<uint8>& FileData, const FString& InFilePath)
		{
			static const uint8 PngSignature[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
			if (FileData.Num() >= UE_ARRAY_COUNT(PngSignature) &&
				FMemory::Memcmp(FileData.GetData(), PngSignature, UE_ARRAY_COUNT(PngSignature)) == 0)
			{
				return EImageFormat::PNG;
			}

			if (FileData.Num() >= 3 &&
				FileData[0] == 0xff && FileData[1] == 0xd8 && FileData[2] == 0xff)
			{
				return EImageFormat::JPEG;
			}

			// Keep the extension fallback for unusual but otherwise valid files whose
			// container signature is not available in the first few bytes.
			const FString Ext = FPaths::GetExtension(InFilePath).ToLower();
			return (Ext == TEXT("jpg") || Ext == TEXT("jpeg")) ? EImageFormat::JPEG : EImageFormat::PNG;
		}

		const TCHAR* ImageFormatName(EImageFormat Format)
		{
			return Format == EImageFormat::JPEG ? TEXT("JPEG") : TEXT("PNG");
		}
	}

	bool DecodeImageFile(const FString& InFilePath, TArray64<uint8>& OutBGRA8, int32& OutWidth, int32& OutHeight)
	{
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *InFilePath))
		{
			UE_LOG(LogHotImages, Warning, TEXT("DecodeImageFile: cannot read file '%s'."), *InFilePath);
			return false;
		}

		const EImageFormat Format = DetectImageFormat(FileData, InFilePath);

		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(Format);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
		{
			UE_LOG(LogHotImages, Warning, TEXT("DecodeImageFile: '%s' is not a valid %s image."), *InFilePath, ImageFormatName(Format));
			return false;
		}

		const int64 Width = Wrapper->GetWidth();
		const int64 Height = Wrapper->GetHeight();
		if (Width <= 0 || Height <= 0 || Width > TNumericLimits<int32>::Max() || Height > TNumericLimits<int32>::Max())
		{
			UE_LOG(LogHotImages, Warning, TEXT("DecodeImageFile: '%s' has an invalid size %lldx%lld."), *InFilePath, Width, Height);
			return false;
		}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 0
		TArray<uint8> Raw32;
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw32))
		{
			UE_LOG(LogHotImages, Warning, TEXT("DecodeImageFile: failed to decode '%s' to BGRA8."), *InFilePath);
			return false;
		}
		OutBGRA8.Reset();
		OutBGRA8.SetNumUninitialized(Raw32.Num());
		FMemory::Memcpy(OutBGRA8.GetData(), Raw32.GetData(), Raw32.Num());
#else
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, OutBGRA8))
		{
			UE_LOG(LogHotImages, Warning, TEXT("DecodeImageFile: failed to decode '%s' to BGRA8."), *InFilePath);
			return false;
		}
#endif

		const int64 ExpectedBytes = Width * Height * 4;
		if ((int64)OutBGRA8.Num() != ExpectedBytes)
		{
			UE_LOG(LogHotImages, Warning, TEXT("DecodeImageFile: decoded size mismatch for '%s' (%lld expected, got %lld)."),
				*InFilePath, ExpectedBytes, (int64)OutBGRA8.Num());
			return false;
		}

		OutWidth = (int32)Width;
		OutHeight = (int32)Height;
		return true;
	}

	bool ReplaceTexturePixels(UTexture2D* Texture, const uint8* InData, int32 Width, int32 Height)
	{
		if (!Texture || !InData || Width <= 0 || Height <= 0)
		{
			return false;
		}

		const int64 BytesForImage = (int64)Width * (int64)Height * 4;
		if (BytesForImage <= 0 || BytesForImage > TNumericLimits<int32>::Max())
		{
			UE_LOG(LogHotImages, Warning, TEXT("ReplaceTexturePixels: image is too large (%lld bytes)."), BytesForImage);
			return false;
		}

		const int32 PreviousWidth = Texture->GetSizeX();
		const int32 PreviousHeight = Texture->GetSizeY();

		FTexturePlatformData* NewData = new FTexturePlatformData();
		NewData->SizeX = Width;
		NewData->SizeY = Height;
		NewData->PixelFormat = PF_B8G8R8A8;
		NewData->SetNumSlices(1);

		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		Mip->SizeX = Width;
		Mip->SizeY = Height;
		NewData->Mips.Add(Mip);

		void* DestImageData = Mip->BulkData.Lock(LOCK_READ_WRITE);
		DestImageData = Mip->BulkData.Realloc((int32)BytesForImage);
		FMemory::Memcpy(DestImageData, InData, (int32)BytesForImage);
		Mip->BulkData.Unlock();

		// Runtime hot images own a single resident mip. Disabling texture/virtual
		// texture streaming prevents cooked mip data with the old dimensions from
		// being streamed back over the replacement later.
		Texture->NeverStream = true;
		Texture->VirtualTextureStreaming = false;
		Texture->SetPlatformData(NewData);
		Texture->UpdateResource();
		UE_LOG(LogHotImages, Verbose, TEXT("ReplaceTexturePixels: '%s' resized from %dx%d to %dx%d."),
			*Texture->GetName(), PreviousWidth, PreviousHeight, Width, Height);
		return true;
	}

	bool UpdateTextureFromFile(UTexture2D* Texture, const FString& InFilePath)
	{
		TArray64<uint8> Raw;
		int32 Width = 0;
		int32 Height = 0;
		if (!DecodeImageFile(InFilePath, Raw, Width, Height))
		{
			return false;
		}
		return ReplaceTexturePixels(Texture, Raw.GetData(), Width, Height);
	}
}
