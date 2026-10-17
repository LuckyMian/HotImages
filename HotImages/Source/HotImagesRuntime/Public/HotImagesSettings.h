#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/NoExportTypes.h"
#include "HotImagesSettings.generated.h"

/**
 * Project settings shown under Project Settings > Plugins > Hot Images.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Hot Images"))
class HOTIMAGESRUNTIME_API UHotImageSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHotImageSettings();

	/**
	 * Content-relative folders (e.g. "HotImages") that hold source images and
	 * proxy texture assets. Add/remove folders here to control what is watched.
	 *
	 * NOTE: each added folder also needs to be included in Project Settings >
	 * Packaging > "Additional Non-Asset Directories to Copy" so its loose image
	 * files are staged next to a packaged game.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Folders",
		meta = (TitleProperty = "Path",
			Tooltip = "Content-relative folders containing png/jpg files and their proxy texture assets (e.g. HotImages)."))
	TArray<FDirectoryPath> WatchFolders;

	/** Automatically import/refresh proxy texture assets while the editor is running. */
	UPROPERTY(EditAnywhere, config, Category = "Sync",
		meta = (Tooltip = "Automatically import new images and re-import changed images while the editor is running."))
	bool bAutoSyncInEditor;

	/** Debounce window applied to bursts of file-system events before re-scanning. */
	UPROPERTY(EditAnywhere, config, Category = "Sync",
		meta = (Tooltip = "Debounce (seconds) applied to bursts of file events.", ClampMin = "0.05", ClampMax = "10.0"))
	float UpdateDebounceSeconds;

	/** Refresh loaded textures in memory when files change in packaged (non-editor) builds. */
	UPROPERTY(EditAnywhere, config, Category = "Runtime",
		meta = (Tooltip = "Watch the folder at runtime and refresh loaded proxy textures when source files change. Editor builds always use the asset re-import path instead."))
	bool bApplyChangesAtRuntime;

	//~ Begin UObject/UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	//~ End UObject/UDeveloperSettings interface
};
