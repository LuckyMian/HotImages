#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Decoding + in-place platform-data replacement used by the packaged runtime
 * path. Everything here runs on the game thread.
 */
namespace HotImageTextureUpdater
{
	/**
	 * Reads and decodes a png/jpg file into tightly packed BGRA8 pixels.
	 * Returns false on any failure.
	 */
	HOTIMAGESRUNTIME_API bool DecodeImageFile(const FString& InFilePath, TArray64<uint8>& OutBGRA8, int32& OutWidth, int32& OutHeight);

	/**
	 * Replaces the UTexture2D platform data with a single BGRA8 mip built from
	 * InData (size must be Width*Height*4). Keeps the texture object identity so
	 * every existing material/UI reference keeps working.
	 */
	HOTIMAGESRUNTIME_API bool ReplaceTexturePixels(UTexture2D* Texture, const uint8* InData, int32 Width, int32 Height);

	/** Decode + replace in one step. Returns false when the file cannot be decoded/updated. */
	HOTIMAGESRUNTIME_API bool UpdateTextureFromFile(UTexture2D* Texture, const FString& InFilePath);
}
