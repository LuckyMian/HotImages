#include "HotImagePaths.h"
#include "HotImagesSettings.h"
#include "Misc/Paths.h"

namespace HotImagePaths
{
	FString NormalizeContentRelativePath(const FString& InPath)
	{
		FString Out = InPath.TrimStartAndEnd();
		Out = Out.Replace(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
		Out = Out.TrimStartAndEnd();

		const FString ContentPrefix = TEXT("Content/");
		if (Out.StartsWith(ContentPrefix, ESearchCase::CaseSensitive))
		{
			Out = Out.RightChop(ContentPrefix.Len());
		}
		else if (Out.Equals(TEXT("Content"), ESearchCase::CaseSensitive))
		{
			Out.Empty();
		}

		// Strip any leading '/' or '//' (content mount style) and trailing slashes.
		while (Out.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			Out = Out.RightChop(1);
		}
		while (Out.EndsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			Out.LeftChopInline(1);
		}
		return Out;
	}

	TArray<FString> GetContentRelativeDirectories()
	{
		const UHotImageSettings* Settings = GetDefault<UHotImageSettings>();
		TArray<FString> Result;
		if (Settings)
		{
			for (const FDirectoryPath& Dir : Settings->WatchFolders)
			{
				FString Rel = NormalizeContentRelativePath(Dir.Path);
				if (!Rel.IsEmpty())
				{
					Result.AddUnique(Rel);
				}
			}
		}
		if (Result.IsEmpty())
		{
			Result.Add(TEXT("HotImages"));
		}
		return Result;
	}

	FString GetContentRelativeDirectory()
	{
		const TArray<FString> All = GetContentRelativeDirectories();
		return All.Num() > 0 ? All[0] : TEXT("HotImages");
	}

	TArray<FString> GetAbsoluteWatchDirectories()
	{
		TArray<FString> Result;
		for (const FString& Rel : GetContentRelativeDirectories())
		{
			Result.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), Rel)));
		}
		return Result;
	}

	FString GetAbsoluteWatchDirectory()
	{
		const TArray<FString> All = GetAbsoluteWatchDirectories();
		return All.Num() > 0 ? All[0] : FString();
	}

	FString ContentRelativeToAssetFolder(const FString& InContentRelativePath)
	{
		const FString Rel = NormalizeContentRelativePath(InContentRelativePath);
		FString AssetFolder = FString(TEXT("/Game"));
		if (!Rel.IsEmpty())
		{
			AssetFolder = AssetFolder / Rel;
		}
		AssetFolder = AssetFolder.Replace(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
		while (AssetFolder.EndsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			AssetFolder.LeftChopInline(1);
		}
		return AssetFolder;
	}

	FString GetAssetPathForSlotInFolder(const FString& InContentRelativePath, const FString& InSlotName)
	{
		FString Slot = InSlotName.TrimStartAndEnd();
		if (Slot.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive))
		{
			Slot = Slot.RightChop(6);
		}
		return ContentRelativeToAssetFolder(InContentRelativePath) / Slot;
	}

	FString GetAssetPathForSlot(const FString& InSlotName)
	{
		return GetAssetPathForSlotInFolder(GetContentRelativeDirectory(), InSlotName);
	}

	bool IsSupportedImageExtension(const FString& InExtension)
	{
		const FString Ext = InExtension.TrimStartAndEnd().ToLower();
		return Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg");
	}

	FString FindImageFileForSlotInFolder(const FString& InContentRelativePath, const FString& InSlotName)
	{
		const FString AbsDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), InContentRelativePath));
		const FString Slot = InSlotName.TrimStartAndEnd();

		static const TCHAR* Extensions[] = { TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg") };
		for (const TCHAR* Ext : Extensions)
		{
			const FString Candidate = FPaths::Combine(AbsDir, Slot + Ext);
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
		return FString();
	}

	FString FindImageFileForSlot(const FString& InSlotName)
	{
		for (const FString& Rel : GetContentRelativeDirectories())
		{
			const FString Found = FindImageFileForSlotInFolder(Rel, InSlotName);
			if (!Found.IsEmpty())
			{
				return Found;
			}
		}
		return FString();
	}
}
