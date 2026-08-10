using UnrealBuildTool;

public class WebToUERuntime : ModuleRules
{
    public WebToUERuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore",
            "DeveloperSettings", "FieldNotification", "WebToUECore"
        });
        PrivateDependencyModuleNames.AddRange(new[] { "ApplicationCore", "RenderCore" });
    }
}
