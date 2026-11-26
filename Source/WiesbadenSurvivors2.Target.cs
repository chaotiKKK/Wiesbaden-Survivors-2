using UnrealBuildTool;
using System.Collections.Generic;

public class WiesbadenSurvivors2Target : TargetRules
{
	public WiesbadenSurvivors2Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "WS2Core", "WS2UI", "WS2Gameplay" });
	}
}
