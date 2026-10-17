#include "HotImagesSettings.h"

UHotImageSettings::UHotImageSettings()
{
	FDirectoryPath DefaultFolder;
	DefaultFolder.Path = TEXT("HotImages");
	WatchFolders.Add(DefaultFolder);
	bAutoSyncInEditor = true;
	UpdateDebounceSeconds = 0.5f;
	bApplyChangesAtRuntime = true;
}

FName UHotImageSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
