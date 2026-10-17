#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HotImageDirWatcher.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/SharedPointer.h"

#include "HotImagesSubsystem.generated.h"

class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FHotImageOnUpdated, FString, SlotName, UTexture2D*, Texture);

/**
 * Runtime entry point for HotImages.
 *
 * In packaged (non-editor) builds it watches the configured Content folder and
 * updates already-loaded proxy textures in place when their source file changes.
 * Editor/PIE builds intentionally leave the runtime watcher off; the editor
 * module owns that path by re-importing the proxy assets instead.
 */
UCLASS()
class HOTIMAGESRUNTIME_API UHotImageSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/**
	 * Returns the loaded proxy texture for a slot ("foo" -> /Game/<Folder>/foo).
	 * When runtime refresh is enabled and the on-disk file is newer than the last
	 * applied version, the texture is refreshed before being returned.
	 */
	UFUNCTION(BlueprintPure, Category = "HotImages")
	UTexture2D* GetTexture(const FString& SlotName);

	/** Immediately re-reads the on-disk image for a slot and updates the texture in place. */
	UFUNCTION(BlueprintCallable, Category = "HotImages")
	void RefreshSlot(const FString& SlotName);

	/** Immediately re-scans every supported image in the watch folder. */
	UFUNCTION(BlueprintCallable, Category = "HotImages")
	void RefreshAll();

	/** True when this build actually runs the directory watcher (packaged Windows game with runtime refresh enabled). */
	UFUNCTION(BlueprintPure, Category = "HotImages")
	bool IsRuntimeWatching() const;

	/** Fired after a texture was successfully updated from a changed file. */
	UPROPERTY(BlueprintAssignable, Category = "HotImages")
	FHotImageOnUpdated OnHotImageUpdated;

private:
	bool IsRuntimeRefreshEnabled() const;
	void StartWatcher();
	void StopWatcher();
	bool TickWatcher(float DeltaTime);
	bool ApplySlotFromDisk(const FString& FolderRel, const FString& SlotName, UTexture2D* Texture, bool bForce);
	void RefreshAllInternal(bool bForce);

	TArray<TSharedPtr<FHotImageDirWatcher>> Watchers;
	FTSTicker::FDelegateHandle TickerHandle;
	TMap<FString, FDateTime> LastAppliedFileTime;
	TMap<FString, FDateTime> LastFailedFileTime;
	bool bRuntimeRefresh = false;
};
