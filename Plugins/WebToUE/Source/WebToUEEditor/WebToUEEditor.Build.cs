using UnrealBuildTool;

public class WebToUEEditor : ModuleRules
{
    public WebToUEEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "WebToUECore", "WebToUERuntime" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnrealEd", "AssetTools", "DirectoryWatcher", "Projects", "Slate", "SlateCore", "UMG", "InputCore",
            "ModelViewViewModel",
            "EditorFramework", "ToolMenus", "UMGEditor"
        });
    }
}
