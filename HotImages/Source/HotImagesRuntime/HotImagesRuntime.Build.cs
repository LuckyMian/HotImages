// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HotImagesRuntime : ModuleRules
{
	public HotImagesRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageWrapper"
		});
	}
}
