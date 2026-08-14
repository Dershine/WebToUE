using System.IO;
using UnrealBuildTool;

public class WebToUEYoga : ModuleRules
{
    public WebToUEYoga(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.CPlusPlus;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        // Yoga represents undefined dimensions with NaN. MSVC's Game/Client/Server default
        // is /fp:fast, which may assume NaNs do not exist and collapse packaged layouts to zero.
        FPSemantics = FPSemanticsMode.Precise;
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
