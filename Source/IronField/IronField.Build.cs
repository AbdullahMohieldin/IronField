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
			"EnhancedInput",
			"UMG",
			"AIModule",
			"Niagara",
			"SlateCore",
			"IFSystems"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"NavigationSystem",
			"RenderCore"
		});

	}
}
