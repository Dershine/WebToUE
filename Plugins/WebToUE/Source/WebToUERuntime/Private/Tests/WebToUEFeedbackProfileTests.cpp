#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEFeedbackProfile.h"

#include "Misc/AutomationTest.h"
#include "Sound/SoundBase.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEFeedbackProfileContractTest,
	"WebToUE.Runtime.FeedbackProfileContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEFeedbackProfileFailuresTest,
	"WebToUE.Runtime.FeedbackProfileFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::FeedbackProfile::Tests
{
	static TSoftObjectPtr<USoundBase> Sound(const TCHAR* Path)
	{
		return TSoftObjectPtr<USoundBase>(FSoftObjectPath(Path));
	}

	static FWebToUEFeedbackCueProfile MakeCue(FName CueId, const TCHAR* Path)
	{
		FWebToUEFeedbackCueProfile Cue;
		Cue.CueId = CueId;
		Cue.Residency = EWebToUEResidencyClass::Critical;
		Cue.Variants.Add(Sound(Path));
		return Cue;
	}

}

bool FWebToUEFeedbackProfileContractTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::FeedbackProfile::Tests;
	UWebToUEFeedbackProfile* Profile =
		NewObject<UWebToUEFeedbackProfile>(GetTransientPackage());
	Profile->ProfileId = TEXT("webtoue.tests.feedback");
	FWebToUEFeedbackCueProfile Confirm = MakeCue(TEXT("webtoue.tests.confirm"),
		TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"));
	Confirm.VolumeMultiplier = 0.75f;
	Confirm.ProjectRouteId = TEXT("project.ui.primary");
	Confirm.CooldownSeconds = 0.05;
	Confirm.DeduplicationGroup = TEXT("webtoue.tests.activation");
	Confirm.DeduplicationWindowSeconds = 0.03;
	Confirm.ThrottleGroup = TEXT("webtoue.tests.navigate");
	Confirm.ThrottleMaximum = 3;
	Confirm.ThrottleWindowSeconds = 0.1;
	Profile->Cues.Add(Confirm);
	Profile->Cues.Add(MakeCue(TEXT("webtoue.tests.cancel"),
		TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue")));

	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	TestTrue(TEXT("A versioned SoundWave/SoundCue Profile validates"),
		Profile->ValidateProfile(Diagnostics));
	TestTrue(TEXT("Editor sealing captures Profile and transitive asset dependencies"),
		Profile->RebuildResourceSeal());
	TestTrue(TEXT("The sealed Profile consumes Resource IR 1.3"),
		Profile->GetResourceFreshness().ArtifactVersions.ResourceIr ==
			FWebToUEArtifactLayerVersion{ 1, 3 });
	TestTrue(TEXT("The dependency closure contains the canonical Profile input"),
		Profile->GetSealedResourceDependencies().ContainsByPredicate(
			[](const FWebToUEResourceDependency& Dependency)
			{
				return Dependency.LogicalId == TEXT("profile/webtoue.tests.feedback") &&
					Dependency.Kind == EWebToUEResourceDependencyKind::GeneratedInput;
			}));
	TestTrue(TEXT("The dependency closure contains the SoundWave package"),
		Profile->GetSealedResourceDependencies().ContainsByPredicate(
			[](const FWebToUEResourceDependency& Dependency)
			{
				return Dependency.LogicalId ==
					TEXT("asset/Engine/EngineSounds/1kSineTonePing");
			}));
	Diagnostics.Reset();
	TestTrue(TEXT("A sealed Profile passes its runtime Resource Contract"),
		Profile->ValidateResourceContract(Diagnostics));
	Diagnostics.Reset();
	TestTrue(TEXT("A sealed Profile is Cook-fresh before inputs change"),
		UWebToUEFeedbackProfile::ValidateCurrentCookFreshness(*Profile, Diagnostics));

	Profile->Cues[0].VolumeMultiplier = 0.5f;
	Diagnostics.Reset();
	TestFalse(TEXT("In-memory Profile policy drift makes the stored seal stale"),
		Profile->ValidateResourceContract(Diagnostics));
	TestTrue(TEXT("Profile policy drift reports the shared WTUE-RES-004 code"),
		Diagnostics.ContainsByPredicate([](
			const FWebToUEResourceContractDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == TEXT("WTUE-RES-004") &&
				Diagnostic.Path == TEXT("dependency-closure");
		}));
	return true;
}

bool FWebToUEFeedbackProfileFailuresTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::FeedbackProfile::Tests;
	UWebToUEFeedbackProfile* Profile =
		NewObject<UWebToUEFeedbackProfile>(GetTransientPackage());
	Profile->ProfileId = TEXT("not-namespaced");
	FWebToUEFeedbackCueProfile First = MakeCue(TEXT("webtoue.tests.duplicate"),
		TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"));
	First.VolumeMultiplier = std::numeric_limits<float>::quiet_NaN();
	First.ThrottleGroup = TEXT("webtoue.tests.throttle");
	First.ThrottleMaximum = 65;
	First.ThrottleWindowSeconds = 0.1;
	Profile->Cues.Add(First);
	Profile->Cues.Add(First);
	Profile->SchemaMinor = 1;

	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	TestFalse(TEXT("Unsupported version, duplicate Cue and hostile policy fail closed"),
		Profile->ValidateProfile(Diagnostics));
	TestTrue(TEXT("Version and Cue identity failures use WTUE-FEEDBACK-001"),
		Diagnostics.ContainsByPredicate([](
			const FWebToUEResourceContractDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == TEXT("WTUE-FEEDBACK-001");
		}));
	TestTrue(TEXT("Non-finite and unbounded rate policy uses WTUE-FEEDBACK-003"),
		Diagnostics.ContainsByPredicate([](
			const FWebToUEResourceContractDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == TEXT("WTUE-FEEDBACK-003");
		}));
	TestEqual(TEXT("A failed Profile exposes no sealed dependency snapshot"),
		Profile->GetSealedResourceDependencies().Num(), 0);
	return true;
}

#endif
