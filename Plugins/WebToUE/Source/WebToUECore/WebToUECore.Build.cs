using UnrealBuildTool;

public class WebToUECore : ModuleRules
{
    public WebToUECore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "WebToUEYoga" });
    }
}
