// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HotImageDemo : ModuleRules
{
	public HotImageDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HotImagesRuntime"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore"
		});
	}
}
