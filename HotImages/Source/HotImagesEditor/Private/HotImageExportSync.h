#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UFactory;
class UTexture2D;
struct FPropertyChangedEvent;

/**
 * Editor-only "reverse sync": keeps a png next to every proxy texture asset
 * that lives directly inside a configured HotImages folder.
 *
 * When a texture is imported with the normal UE import flow, re-imported from
 * another file, or edited in the texture editor (PowerOfTwo padding/resizing,
 * chroma key, ...), the current content is re-exported to
 * <Content>/<folder>/<assetname>.png so the runtime hot folder stays in sync.
 */
class FHotImageExportSync
{
public:
	void Initialize();
	void Shutdown();

	/**
	 * True when the given png was just written by this class (reverse export) and its
	 * on-disk timestamp is still unchanged. Such self-exported files must not trigger
	 * a second re-import of the same texture; only later external edits should.
	 */
	bool ShouldSkipSelfExport(const FString& InPngPath, const FDateTime& InCurrentTimestamp) const;

private:
	void HandleAssetPostImport(UFactory* Factory, UObject* CreatedObject);
	void HandleAssetReimport(UObject* Asset);
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);

	void ScheduleExport(UTexture2D* Texture);
	bool RunPendingExports(float DeltaTime);
	bool ExportTextureToPng(UTexture2D* Texture);

	bool bInitialized = false;
	FTSTicker::FDelegateHandle DebounceHandle;
	TArray<TWeakObjectPtr<UTexture2D>> PendingTextures;
	TMap<FString, FMD5Hash> LastExportedHashes;
	TMap<FString, FDateTime> SelfExportedFileTimes;
};
