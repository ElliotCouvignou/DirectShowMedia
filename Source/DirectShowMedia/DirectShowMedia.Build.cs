// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class DirectShowMedia : ModuleRules
{
	public DirectShowMedia(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });

		//PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bEnableExceptions = true;
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"Media",
			});
				
		
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
// 			if (Target.bBuildDeveloperTools)
// 			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "DirectShow");
/*			}*/
			
		}
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"MediaUtils",
				"RenderCore",
				"DirectShowMediaFactory",
				"RHI",
				"RHICore"
			});

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Media",
			});

        
        
        // PrivateIncludePaths.AddRange(
        // 	new string[] {
        // 		// "DirectShowMedia/Private",
        // 		"DirectShowMedia/Private/Player",
        // 	});


    }
}
