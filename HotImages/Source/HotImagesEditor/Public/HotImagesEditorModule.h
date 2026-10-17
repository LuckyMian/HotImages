#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleInterface.h"

class FHotImageDirWatcher;
class FHotImageExportSync;
struct FPropertyChangedEvent;

class FHotImagesEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface interface

	/** Schedules a full folder re-scan/re-import (used by the menu and the watcher). */
	void RequestFullSync(float InDelaySeconds);

private:
	void RegisterMenus();
	void MenuRescanFolder();
	void MenuOpenFolder();

	void StartWatcher();
	void StopWatcher();
	void RestartWatcher();
	void SyncStagingDirectories();
	bool WatchTick(float DeltaTime);
	bool RunScheduledSync(float DeltaTime);

	void OnSettingsChanged(UObject* Object, struct FPropertyChangedEvent& PropertyChangedEvent);

	TArray<TSharedPtr<FHotImageDirWatcher>> Watchers;
	TSharedPtr<FHotImageExportSync> ExportSync;
	class UHotImageSettings* BoundSettingsObject = nullptr;
	FTSTicker::FDelegateHandle SyncTickerHandle;
	FTSTicker::FDelegateHandle WatchTickerHandle;
	bool bSyncQueued = false;
};
