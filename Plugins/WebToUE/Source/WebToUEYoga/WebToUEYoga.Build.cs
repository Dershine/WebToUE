using System.IO;
using UnrealBuildTool;

public class WebToUEYoga : ModuleRules
{
    public WebToUEYoga(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.CPlusPlus;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        bUseUnity = false;
        IWYUSupport = IWYUSupport.None;
        PublicDependencyModuleNames.Add("Core");
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "ThirdParty"));
        PrivateDefinitions.Add("YOGA_EXPORT=WEBTOUEYOGA_API");
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDefinitions.Add("NOMINMAX=1");
        }
    }
}
