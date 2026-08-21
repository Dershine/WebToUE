#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEFeedbackProfile.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEFeedbackProfilePackageDriftTest,
	"WebToUE.Editor.FeedbackProfilePackageDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEFeedbackProfileCookFreshnessTest,
	"WebToUE.Editor.FeedbackProfileCookFreshness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::FeedbackProfileEditor::Tests
{
	static bool SaveTestAsset(UObject& Asset)
	{
		UPackage* Package = Asset.GetPackage();
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_None;
		return UPackage::SavePackage(Package, &Asset, *Filename, SaveArgs);
	}
}

bool FWebToUEFeedbackProfilePackageDriftTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::FeedbackProfileEditor::Tests;
	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString AssetName = TEXT("SC_WTUE_Drift_") + Suffix;
	const FString PackageName = TEXT("/Game/WebToUEAutomation/") + AssetName;
	const FString ObjectPath = PackageName + TEXT(".") + AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	USoundConcurrency* Concurrency = NewObject<USoundConcurrency>(
		Package, *AssetName, RF_Public | RF_Standalone);
	FAssetRegistryModule::AssetCreated(Concurrency);
	AddExpectedError(
		TEXT("package was marked as deleted in editor, but has been modified on disk"),
		EAutomationExpectedErrorFlags::Contains, -1);
	ON_SCOPE_EXIT
	{
		TArray<UObject*> AssetsToDelete { Concurrency };
		ObjectTools::DeleteObjectsUnchecked(AssetsToDelete);
	};
	Concurrency->Concurrency.MaxCount = 2;
	TestTrue(TEXT("The disposable Concurrency package is saved"),
		SaveTestAsset(*Concurrency));

	UWebToUEFeedbackProfile* Profile =
		NewObject<UWebToUEFeedbackProfile>(GetTransientPackage());
	Profile->ProfileId = TEXT("webtoue.tests.package-drift");
	FWebToUEFeedbackCueProfile Cue;
	Cue.CueId = TEXT("webtoue.tests.confirm");
	Cue.Residency = EWebToUEResidencyClass::Critical;
	Cue.Variants.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(
		TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"))));
	Cue.Concurrency = TSoftObjectPtr<USoundConcurrency>(FSoftObjectPath(ObjectPath));
	Profile->Cues.Add(Cue);
	TestTrue(TEXT("The Profile seals the saved Concurrency dependency"),
		Profile->RebuildResourceSeal());
	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	TestTrue(TEXT("The newly sealed Concurrency package is Cook-fresh"),
		UWebToUEFeedbackProfile::ValidateCurrentCookFreshness(*Profile, Diagnostics));

	Concurrency->Concurrency.MaxCount = 3;
	Concurrency->MarkPackageDirty();
	TestTrue(TEXT("The changed Concurrency package is saved"),
		SaveTestAsset(*Concurrency));
	Diagnostics.Reset();
	TestFalse(TEXT("Direct Concurrency package drift fails Cook freshness"),
		UWebToUEFeedbackProfile::ValidateCurrentCookFreshness(*Profile, Diagnostics));
	TestTrue(TEXT("Package drift reports WTUE-RES-004 dependency closure"),
		Diagnostics.ContainsByPredicate([](
			const FWebToUEResourceContractDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == TEXT("WTUE-RES-004") &&
				Diagnostic.Path == TEXT("dependency-closure");
		}));
	return true;
}

bool FWebToUEFeedbackProfileCookFreshnessTest::RunTest(const FString& Parameters)
{
	UWebToUEFeedbackProfile* Profile = LoadObject<UWebToUEFeedbackProfile>(nullptr,
		TEXT("/Game/WebToUEExamples/Audio/DA_WTUE_FeedbackProfile.DA_WTUE_FeedbackProfile"));
	if (!TestNotNull(TEXT("The persisted Feedback Profile fixture loads"), Profile))
	{
		return false;
	}
	TestEqual(TEXT("The fixture uses Profile schema major 1"), Profile->SchemaMajor, 1);
	TestEqual(TEXT("The fixture uses Profile schema minor 0"), Profile->SchemaMinor, 0);
	TestEqual(TEXT("The fixture contains the frozen five-Cue policy set"),
		Profile->Cues.Num(), 5);
	TestEqual(TEXT("The fixture seals Resource IR 1.3"),
		Profile->GetResourceFreshness().ArtifactVersions.ResourceIr.Minor, uint16(3));

	const TMap<FName, FString> ExpectedClasses = {
		{ TEXT("webtoue.feedback.confirm"), TEXT("SoundWave") },
		{ TEXT("webtoue.feedback.cancel"), TEXT("SoundCue") },
		{ TEXT("webtoue.feedback.navigate"), TEXT("MetaSoundSource") }
	};
	for (const TPair<FName, FString>& Expected : ExpectedClasses)
	{
		const FWebToUEFeedbackCueProfile* Cue = Profile->FindCue(Expected.Key);
		TestNotNull(*FString::Printf(TEXT("Fixture Cue exists: %s"),
			*Expected.Key.ToString()), Cue);
		if (!Cue || Cue->Variants.Num() != 1) continue;
		USoundBase* Sound = Cue->Variants[0].LoadSynchronous();
		TestTrue(*FString::Printf(TEXT("Fixture Cue resolves the expected class: %s"),
			*Expected.Key.ToString()), Sound && Sound->GetClass()->GetName() == Expected.Value);
	}
	const FWebToUEFeedbackCueProfile* Confirm =
		Profile->FindCue(TEXT("webtoue.feedback.confirm"));
	TestTrue(TEXT("The fixture passes an explicit UE Concurrency asset"),
		Confirm && Confirm->Concurrency.LoadSynchronous() != nullptr);

	const TArray<FString> DirectDependencies = {
		TEXT("asset/Game/WebToUEExamples/Audio/SW_WTUE_Feedback"),
		TEXT("asset/Game/WebToUEExamples/Audio/SCue_WTUE_Feedback"),
		TEXT("asset/Game/WebToUEExamples/Audio/MSS_WTUE_Feedback"),
		TEXT("asset/Game/WebToUEExamples/Audio/SC_WTUE_Feedback")
	};
	for (const FString& LogicalId : DirectDependencies)
	{
		TestTrue(*FString::Printf(TEXT("The Profile closure seals %s"), *LogicalId),
			Profile->GetSealedResourceDependencies().ContainsByPredicate(
				[&LogicalId](const FWebToUEResourceDependency& Dependency)
				{
					return Dependency.LogicalId == LogicalId;
				}));
	}

	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	TestTrue(TEXT("The persisted Profile satisfies its runtime Resource Contract"),
		Profile->ValidateResourceContract(Diagnostics));
	Diagnostics.Reset();
	const bool bFresh = UWebToUEFeedbackProfile::ValidateCurrentCookFreshness(
		*Profile, Diagnostics);
	for (const FWebToUEResourceContractDiagnostic& Diagnostic : Diagnostics)
	{
		AddError(FString::Printf(TEXT("%s %s: %s"), *Diagnostic.Code,
			*Diagnostic.Path, *Diagnostic.Detail));
	}
	TestTrue(TEXT("The persisted Feedback Profile is Cook-fresh across processes"), bFresh);
	return true;
}

#endif
