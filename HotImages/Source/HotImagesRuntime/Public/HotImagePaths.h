#pragma once

#include "CoreMinimal.h"

/**
 * Shared path/naming helpers for the HotImages plugin.
 *
 * The plugin keeps source image files (png/jpg/jpeg) next to their proxy
 * UTexture2D assets inside <Project>/Content/<Folder> (default "HotImages").
 * Slot names are the image base names (foo.png -> "foo"), and the proxy asset
 * lives at /Game/<Folder>/foo.
 */
namespace HotImagePaths
{
	/** Strips leading/trailing slashes and a possible leading "Content/" prefix. */
	HOTIMAGESRUNTIME_API FString NormalizeContentRelativePath(const FString& InPath);

	/** Returns all configured content-relative folders in order (e.g. ["HotImages"]). */
	HOTIMAGESRUNTIME_API TArray<FString> GetContentRelativeDirectories();

	/** Returns the first configured folder (or "HotImages" when none are set). */
	HOTIMAGESRUNTIME_API FString GetContentRelativeDirectory();

	/** Returns the absolute on-disk watch directories (Content dir + each relative folder). */
	HOTIMAGESRUNTIME_API TArray<FString> GetAbsoluteWatchDirectories();

	/** Returns the absolute on-disk watch directory for the first configured folder. */
	HOTIMAGESRUNTIME_API FString GetAbsoluteWatchDirectory();

	/** Converts a content-relative folder (e.g. "HotImages") to an asset folder (/Game/HotImages). */
	HOTIMAGESRUNTIME_API FString ContentRelativeToAssetFolder(const FString& InContentRelativePath);

	/** Asset path for one configured folder + slot, e.g. /Game/HotImages/foo. */
	HOTIMAGESRUNTIME_API FString GetAssetPathForSlotInFolder(const FString& InContentRelativePath, const FString& InSlotName);

	/** Asset path for the first configured folder, e.g. /Game/HotImages/foo. */
	HOTIMAGESRUNTIME_API FString GetAssetPathForSlot(const FString& InSlotName);

	/** Extension (without dot) -> true when png/jpg/jpeg. */
	HOTIMAGESRUNTIME_API bool IsSupportedImageExtension(const FString& InExtension);

	/** Returns the image file for a slot inside one folder, preferring .png then .jpg then .jpeg. Empty when missing. */
	HOTIMAGESRUNTIME_API FString FindImageFileForSlotInFolder(const FString& InContentRelativePath, const FString& InSlotName);

	/** Searches every configured folder and returns the first image file found for the slot. */
	HOTIMAGESRUNTIME_API FString FindImageFileForSlot(const FString& InSlotName);
}
