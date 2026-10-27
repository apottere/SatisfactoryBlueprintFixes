using UnrealBuildTool;

public class BlueprintFixes : ModuleRules
{
	public BlueprintFixes(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject",
			"Engine",
			"SlateCore", "Slate", "UMG",
		});

		// Header stubs
		PublicDependencyModuleNames.AddRange(new string[] {
			"DummyHeaders",
		});

		PublicDependencyModuleNames.AddRange(new string[] {"FactoryGame", "SML"});
	}
}
