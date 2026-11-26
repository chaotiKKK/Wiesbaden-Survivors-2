using UnrealBuildTool;
using System.Collections.Generic;

public class WiesbadenSurvivors2EditorTarget : TargetRules
{
	public WiesbadenSurvivors2EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "WS2Core", "WS2UI", "WS2Gameplay" });
	}
}
