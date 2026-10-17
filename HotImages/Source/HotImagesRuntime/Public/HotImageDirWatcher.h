#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

#include <atomic>

class FRunnableThread;

/**
 * Minimal asynchronous directory watcher.
 *
 * On Windows it uses ReadDirectoryChangesW in a background thread and only
 * reports "something in this folder changed". Consumers poll ConsumePending()
 * on the game thread and then re-scan the folder (flat, small, debounced).
 *
 * On non-Windows platforms Start() returns false and callers can fall back to
 * manual RefreshSlot/RefreshAll calls.
 */
class HOTIMAGESRUNTIME_API FHotImageDirWatcher : public FRunnable
{
public:
	explicit FHotImageDirWatcher(FString InDirectory);
	virtual ~FHotImageDirWatcher() override;

	/** Starts the background thread. Returns false when watching is unavailable. */
	bool Start();
	/** Stops the background thread (safe to call from any thread). */
	void Stop();

	/** Returns the directory being watched. */
	const FString& GetDirectory() const { return Directory; }

	/** Thread-safe: returns true once when a change has been observed. */
	bool ConsumePending();

protected:
	//~ Begin FRunnable interface
	virtual uint32 Run() override;
	//~ End FRunnable interface

private:
	FString Directory;
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> bStopRequested{ false };
	std::atomic<bool> bPending{ false };

	// Handles are stored as void* so the header stays platform independent.
	void* DirectoryHandle = nullptr;
	void* CompletionEvent = nullptr;
	void* Overlapped = nullptr;
	uint8* ReadBuffer = nullptr;
	int32 ReadBufferSize = 0;
};
