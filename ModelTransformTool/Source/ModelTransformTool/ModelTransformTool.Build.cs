// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ModelTransformTool : ModuleRules
{
	public ModelTransformTool(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
                "Projects",
                "Json",
                "JsonUtilities",
            }
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

 
		string Src = Path.Combine(ModuleDirectory, "Binaries", "Win64", "AMFTZ3");
		string Dest = Path.Combine(Target.ProjectFile.Directory.FullName, "Binaries", "Win64", "AMFTZ3");

		if (Directory.Exists(Src))
		{
			CopyDirectory(Src, Dest);
		}

		// Helper
		void CopyDirectory(string sourceDir, string targetDir)
		{
			foreach (var dirPath in Directory.GetDirectories(sourceDir, "*", SearchOption.AllDirectories))
			{
				Directory.CreateDirectory(dirPath.Replace(sourceDir, targetDir));
			}
			foreach (var newPath in Directory.GetFiles(sourceDir, "*.*", SearchOption.AllDirectories))
			{
				File.Copy(newPath, newPath.Replace(sourceDir, targetDir), true);
			}
		}

    }
}
