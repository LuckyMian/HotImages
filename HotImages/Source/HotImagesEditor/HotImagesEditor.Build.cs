// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HotImagesEditor : ModuleRules
{
	public HotImagesEditor(ReadOnlyTargetRules Target) : base(Target)
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
			"AssetTools",
			"DeveloperSettings",
			"DeveloperToolSettings",
			"ImageCore",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"UnrealEd"
		});
	}
}
