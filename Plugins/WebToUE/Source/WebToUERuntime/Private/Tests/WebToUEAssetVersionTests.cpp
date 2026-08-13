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
	TestFalse(TEXT("Assets at the latest version do not require recompilation"),
		FWebToUEAssetVersion::RequiresRecompile(FWebToUEAssetVersion::LatestVersion));
	return true;
}

#endif
