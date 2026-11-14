using UnrealBuildTool;
using System.Collections.Generic;

public class IronFieldTarget : TargetRules
{
	public IronFieldTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("IronField");
	}
}
