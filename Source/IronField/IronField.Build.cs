using UnrealBuildTool;

public class IronField : ModuleRules
{
	public IronField(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UMG",
			"Slate",
			"SlateCore",
			"AIModule",
			"NavigationSystem",
			"GameplayTasks",
			"Niagara"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
		});
	}
}
