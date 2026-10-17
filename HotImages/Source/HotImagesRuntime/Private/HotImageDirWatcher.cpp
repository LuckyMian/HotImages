#include "HotImageDirWatcher.h"
#include "HotImagesLog.h"
#include "HAL/RunnableThread.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	constexpr int32 WatchBufferSize = 64 * 1024;
}

FHotImageDirWatcher::FHotImageDirWatcher(FString InDirectory)
	: Directory(MoveTemp(InDirectory))
{
}

FHotImageDirWatcher::~FHotImageDirWatcher()
{
	Stop();
	delete[] ReadBuffer;
}

bool FHotImageDirWatcher::Start()
{
	if (Thread)
	{
		return true;
	}

#if PLATFORM_WINDOWS
	if (!FPaths::DirectoryExists(Directory))
	{
		UE_LOG(LogHotImages, Warning, TEXT("FHotImageDirWatcher: directory does not exist: %s"), *Directory);
		return false;
	}

	const FString NativePath = Directory;
	HANDLE DirHandle = ::CreateFileW(
		*NativePath,
		FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		nullptr);

	if (DirHandle == INVALID_HANDLE_VALUE)
	{
		UE_LOG(LogHotImages, Warning, TEXT("FHotImageDirWatcher: CreateFileW failed on %s (error %d)"), *Directory, (int32)::GetLastError());
		return false;
	}

	HANDLE Event = ::CreateEventW(nullptr, true, false, nullptr);
	if (!Event)
	{
		::CloseHandle(DirHandle);
		UE_LOG(LogHotImages, Warning, TEXT("FHotImageDirWatcher: CreateEventW failed (error %d)"), (int32)::GetLastError());
		return false;
	}

	OVERLAPPED* Ov = new OVERLAPPED();
	FMemory::Memzero(Ov, sizeof(OVERLAPPED));
	Ov->hEvent = Event;

	uint8* Buffer = new uint8[WatchBufferSize];
	FMemory::Memzero(Buffer, WatchBufferSize);

	DirectoryHandle = DirHandle;
	CompletionEvent = Event;
	Overlapped = Ov;
	ReadBuffer = Buffer;
	ReadBufferSize = WatchBufferSize;

	Thread = FRunnableThread::Create(this, TEXT("HotImageDirWatcher"), 0, TPri_BelowNormal);
	if (!Thread)
	{
		Stop();
		return false;
	}
	return true;
#else
	UE_LOG(LogHotImages, Log, TEXT("FHotImageDirWatcher: directory watching is only implemented on Windows; use RefreshSlot/RefreshAll."));
	return false;
#endif
}

void FHotImageDirWatcher::Stop()
{
	// FRunnableThreadWin::Kill() calls Runnable->Stop() and the thread destructor
	// also calls Kill(). Guard against that re-entrancy to avoid infinite recursion.
	if (bStopRequested.exchange(true))
	{
		return;
	}

#if PLATFORM_WINDOWS
	HANDLE DirHandle = static_cast<HANDLE>(DirectoryHandle);
	OVERLAPPED* Ov = static_cast<OVERLAPPED*>(Overlapped);
	if (DirHandle && Ov)
	{
		::CancelIoEx(DirHandle, Ov);
	}
	if (CompletionEvent)
	{
		::SetEvent(static_cast<HANDLE>(CompletionEvent));
	}
#endif

	if (Thread)
	{
		// Kill() sets the stop condition, waits for the background thread and closes
		// the internal OS handle. The recursive Stop() call made by Kill() is
		// absorbed by the guard above.
		Thread->Kill(/*bShouldWait=*/true);
		delete Thread;
		Thread = nullptr;
	}

#if PLATFORM_WINDOWS
	if (DirectoryHandle)
	{
		::CloseHandle(static_cast<HANDLE>(DirectoryHandle));
		DirectoryHandle = nullptr;
	}
	if (CompletionEvent)
	{
		::CloseHandle(static_cast<HANDLE>(CompletionEvent));
		CompletionEvent = nullptr;
	}
	delete static_cast<OVERLAPPED*>(Overlapped);
	Overlapped = nullptr;
#endif
}

bool FHotImageDirWatcher::ConsumePending()
{
	return bPending.exchange(false);
}

uint32 FHotImageDirWatcher::Run()
{
#if PLATFORM_WINDOWS
	HANDLE DirHandle = static_cast<HANDLE>(DirectoryHandle);
	HANDLE Event = static_cast<HANDLE>(CompletionEvent);
	OVERLAPPED* Ov = static_cast<OVERLAPPED*>(Overlapped);

	auto IssueRead = [&]() -> bool
	{
		::ResetEvent(Event);
		FMemory::Memzero(Ov, sizeof(OVERLAPPED));
		Ov->hEvent = Event;

		DWORD BytesReturned = 0;
		const BOOL bOk = ::ReadDirectoryChangesW(
			DirHandle,
			ReadBuffer,
			ReadBufferSize,
			false, // do not watch subdirectories
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION,
			&BytesReturned,
			Ov,
			nullptr);

		if (!bOk)
		{
			const DWORD Error = ::GetLastError();
			if (Error == ERROR_NOTIFY_ENUM_DIR || Error == ERROR_INVALID_PARAMETER)
			{
				// Buffer overflow or unsupported flags: fall back to a full re-scan.
				bPending.store(true);
				return false;
			}
			UE_LOG(LogHotImages, Warning, TEXT("FHotImageDirWatcher: ReadDirectoryChangesW failed (error %d)."), (int32)Error);
			return false;
		}
		return true;
	};

	if (!IssueRead())
	{
		// Still report one scan so callers reconcile the folder at least once.
		bPending.store(true);
		return 0;
	}

	while (!bStopRequested.load())
	{
		const DWORD WaitResult = ::WaitForSingleObject(Event, 250);
		if (bStopRequested.load())
		{
			break;
		}

		if (WaitResult == WAIT_OBJECT_0)
		{
			DWORD BytesReturned = 0;
			if (::GetOverlappedResult(DirHandle, Ov, &BytesReturned, false) && BytesReturned > 0)
			{
				bPending.store(true);
			}
			else
			{
				const DWORD Error = ::GetLastError();
				if (Error == ERROR_OPERATION_ABORTED)
				{
					break;
				}
				UE_LOG(LogHotImages, Warning, TEXT("FHotImageDirWatcher: GetOverlappedResult error %d; falling back to full re-scan."), (int32)Error);
				bPending.store(true);
			}

			if (!IssueRead())
			{
				break;
			}
		}
		else if (WaitResult == WAIT_FAILED)
		{
			UE_LOG(LogHotImages, Warning, TEXT("FHotImageDirWatcher: WaitForSingleObject failed (error %d)."), (int32)::GetLastError());
			break;
		}
	}
#endif
	return 0;
}
