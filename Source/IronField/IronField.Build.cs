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
			"SlateCore",
			"AIModule"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"InputCore",
			"Slate",
			"NavigationSystem",
			"Niagara",
			"GeometryCollectionEngine",
			"Chaos",
			"FieldSystemEngine",
			"PhysicsCore"
		});
	}
}
