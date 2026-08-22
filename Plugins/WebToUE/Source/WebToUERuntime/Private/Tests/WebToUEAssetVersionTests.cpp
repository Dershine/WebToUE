#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/CustomVersion.h"
#include "WebToUEAssetVersion.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEAssetVersionTest, "WebToUE.Runtime.AssetVersion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEAssetVersionTest::RunTest(const FString& Parameters)
{
	const TOptional<FCustomVersion> RegisteredVersion = FCurrentCustomVersions::Get(FWebToUEAssetVersion::GUID);
	TestTrue(TEXT("WebToUE custom asset version is registered"), RegisteredVersion.IsSet());
	if (RegisteredVersion.IsSet())
	{
		TestEqual(TEXT("Registered version is the latest compiled document version"),
			RegisteredVersion->Version, static_cast<int32>(FWebToUEAssetVersion::LatestVersion));
	}

	TestTrue(TEXT("Assets without the custom version require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(INDEX_NONE));
	TestTrue(TEXT("Assets without localized rich text fields require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(FWebToUEAssetVersion::InitialCompiledDocument));
	TestTrue(TEXT("Assets without ordered declarations require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(FWebToUEAssetVersion::LocalizedRichText));
	TestTrue(TEXT("Assets without typed style declarations require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(FWebToUEAssetVersion::OrderedDeclarations));
	TestTrue(TEXT("Assets without compiled binding ops require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(FWebToUEAssetVersion::TypedStyleDeclarations));
	TestTrue(TEXT("Assets without a typed resource manifest require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(FWebToUEAssetVersion::CompiledBindingOps));
	TestTrue(TEXT("Assets with pre-sRGB typed colors require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(
			FWebToUEAssetVersion::CompiledResourceManifest));
	TestTrue(TEXT("Assets without the Resource Contract manifest require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(FWebToUEAssetVersion::CssSrgbColors));
	TestTrue(TEXT("Assets without ResourceId node bindings require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(
			FWebToUEAssetVersion::ResourceContractManifest));
	TestTrue(TEXT("Assets without relative texture metadata require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(
			FWebToUEAssetVersion::ResourceConsumerContract));
	TestTrue(TEXT("Assets without static Material brush metadata require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(
			FWebToUEAssetVersion::RelativeTextureSources));
	TestTrue(TEXT("Assets without typed visual transforms require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(
			FWebToUEAssetVersion::StaticMaterialBrushes));
	TestTrue(TEXT("Assets without versioned Animation IR require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(
			FWebToUEAssetVersion::VisualTransformAndClip));
	TestTrue(TEXT("Assets without Transition lowering require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(FWebToUEAssetVersion::AnimationIR));
	TestFalse(TEXT("Assets at the latest version do not require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(FWebToUEAssetVersion::LatestVersion));
	return true;
}

#endif
