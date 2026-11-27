using UnrealBuildTool;

public class WS2Gameplay : ModuleRules
{
	public WS2Gameplay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore", "Sockets",
			"WS2Core", "WS2UI"
		});
	}
}
