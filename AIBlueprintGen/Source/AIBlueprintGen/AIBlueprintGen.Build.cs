using UnrealBuildTool;

public class AIBlueprintGen : ModuleRules
{
	public AIBlueprintGen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Projects",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"Slate",
				"SlateCore",
				"DeveloperSettings",
				"Json",
				"JsonUtilities",
				"BlueprintGraph",
				"KismetCompiler",
				"GraphEditor",
				"Kismet",
				"MaterialEditor",
				"AssetTools",
				"AssetRegistry",
				"UMG",
				"UMGEditor",
				"ApplicationCore",
			}
		);
	}
}
