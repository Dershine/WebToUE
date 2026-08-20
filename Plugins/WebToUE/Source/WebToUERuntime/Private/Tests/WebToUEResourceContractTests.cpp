#if WITH_DEV_AUTOMATION_TESTS

#include "WebToUEResourceContract.h"

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEResourceContractCanonicalizationTest,
	"WebToUE.Runtime.ResourceContractCanonicalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEResourceContractFailuresTest,
	"WebToUE.Runtime.ResourceContractFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::ResourceContract::Tests
{
	static FString Hash(TCHAR Character)
	{
		return FString::ChrN(64, Character);
	}

	static FWebToUEResourceContractDescriptor MakeContract()
	{
		FWebToUEResourceContractDescriptor Contract;
		Contract.DocumentId = TEXT("documents/main-menu");
		Contract.CompilerFingerprintBlake3 = Hash(TEXT('a'));
		Contract.ArtifactVersions.UiIr = { 1, 2 };
		Contract.ArtifactVersions.ResourceIr = { 1, 0 };
		Contract.ArtifactVersions.InteropSchema = { 1, 1 };
		Contract.Dependencies = {
			{ TEXT("ui/main-menu.html"), EWebToUEResourceDependencyKind::UiSource, Hash(TEXT('1')) },
			{ TEXT("ui/theme.css"), EWebToUEResourceDependencyKind::StyleSource, Hash(TEXT('2')) },
			{ TEXT("assets/menu-background"), EWebToUEResourceDependencyKind::Resource, Hash(TEXT('3')) },
			{ TEXT("schema/personal-game-ui"), EWebToUEResourceDependencyKind::InteropSchema, Hash(TEXT('4')) },
		};
		Contract.Resources = {
			{ TEXT("menu/background"), {
				EWebToUEResourceOrigin::UnrealAsset,
				TEXT("ui/main-menu.html"),
				TEXT("/Game/UI/T_MenuBackground.T_MenuBackground"),
				TEXT("assets/menu-background") } },
		};
		Contract.ResidencyAssignments = {
			{ TEXT("menu/background"), TEXT(""), TEXT("document-fallback"), EWebToUEResidencyClass::Lazy },
			{ TEXT("menu/background"), TEXT("routes/main-menu"), TEXT("first-frame"), EWebToUEResidencyClass::Critical },
		};
		return Contract;
	}

	static bool HasCode(
		TConstArrayView<FWebToUEResourceContractDiagnostic> Diagnostics,
		const TCHAR* Code)
	{
		return Diagnostics.ContainsByPredicate([Code](const FWebToUEResourceContractDiagnostic& Item)
		{
			return Item.Code == Code;
		});
	}
}

bool FWebToUEResourceContractCanonicalizationTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::ResourceContract::Tests;
	FWebToUEResourceContractDescriptor Contract = MakeContract();
	FWebToUEResourceContractSnapshot Snapshot;
	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	TestTrue(TEXT("A sealed resource contract builds"),
		FWebToUEResourceContractPolicy::BuildSnapshot(Contract, Snapshot, Diagnostics));
	TestEqual(TEXT("A valid contract has no diagnostics"), Diagnostics.Num(), 0);
	TestEqual(TEXT("Dependencies are deterministically sorted"),
		Snapshot.Dependencies[0].LogicalId, FString(TEXT("assets/menu-background")));
	TestTrue(TEXT("Dependency closure is lowercase BLAKE3-256"),
		Snapshot.Freshness.DependencyClosureBlake3.Len() == 64 &&
		Snapshot.Freshness.DependencyClosureBlake3 ==
		Snapshot.Freshness.DependencyClosureBlake3.ToLower());

	FWebToUEResourceContractDescriptor Reordered = Contract;
	Algo::Reverse(Reordered.Dependencies);
	Algo::Reverse(Reordered.ResidencyAssignments);
	FWebToUEResourceContractSnapshot ReorderedSnapshot;
	TestTrue(TEXT("Input order does not affect a valid contract"),
		FWebToUEResourceContractPolicy::BuildSnapshot(
			Reordered, ReorderedSnapshot, Diagnostics));
	TestTrue(TEXT("Canonical dependency and manifest hashes are deterministic"),
		Snapshot.Freshness == ReorderedSnapshot.Freshness);

	TestTrue(TEXT("An exact embedded stamp is Cook-fresh"),
		FWebToUEResourceContractPolicy::IsCookFresh(
			Snapshot.Freshness, ReorderedSnapshot.Freshness, Diagnostics));
	FWebToUEResourceContractDescriptor ChangedSource = Contract;
	ChangedSource.Dependencies[0].ContentHashBlake3 = Hash(TEXT('5'));
	FWebToUEResourceContractSnapshot ChangedSnapshot;
	TestTrue(TEXT("Changed source inputs still produce a valid current snapshot"),
		FWebToUEResourceContractPolicy::BuildSnapshot(
			ChangedSource, ChangedSnapshot, Diagnostics));
	TestFalse(TEXT("A previous artifact fails the stale-source Cook gate"),
		FWebToUEResourceContractPolicy::IsCookFresh(
			ChangedSnapshot.Freshness, Snapshot.Freshness, Diagnostics));
	TestTrue(TEXT("Stale-source failure has the resource freshness code"),
		HasCode(Diagnostics, TEXT("WTUE-RES-004")));

	FWebToUEArtifactVersionSet Supported = Contract.ArtifactVersions;
	Supported.UiIr.Minor = 3;
	TestTrue(TEXT("A consumer may accept an older producer minor"),
		FWebToUEResourceContractPolicy::IsRuntimeCompatible(
			Contract.ArtifactVersions, Supported, Diagnostics));
	return true;
}

bool FWebToUEResourceContractFailuresTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::ResourceContract::Tests;
	FWebToUEResourceContractSnapshot Snapshot;
	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;

	FWebToUEResourceContractDescriptor Invalid = MakeContract();
	const FWebToUEResourceDependency DuplicateDependency = Invalid.Dependencies[0];
	Invalid.Dependencies.Add(DuplicateDependency);
	Invalid.Resources[0].Provenance.AuthorReference = TEXT("https://example.invalid/menu.png");
	const FWebToUEResidencyAssignment DuplicateAssignment = Invalid.ResidencyAssignments[0];
	Invalid.ResidencyAssignments.Add(DuplicateAssignment);
	TestFalse(TEXT("Duplicate identities and Runtime network references fail closed"),
		FWebToUEResourceContractPolicy::BuildSnapshot(Invalid, Snapshot, Diagnostics));
	TestTrue(TEXT("A failed contract exposes no partial canonical data"),
		Snapshot.Dependencies.IsEmpty() && Snapshot.Resources.IsEmpty() &&
		Snapshot.ResidencyAssignments.IsEmpty());
	TestTrue(TEXT("Invalid provenance is actionable"),
		HasCode(Diagnostics, TEXT("WTUE-RES-001")));
	TestTrue(TEXT("Duplicate identities are actionable"),
		HasCode(Diagnostics, TEXT("WTUE-RES-002")));

	Invalid = MakeContract();
	Invalid.ResidencyAssignments[0].Residency = EWebToUEResidencyClass::Critical;
	Invalid.ResidencyAssignments[1].Residency = EWebToUEResidencyClass::Lazy;
	TestFalse(TEXT("A route cannot demote document residency"),
		FWebToUEResourceContractPolicy::BuildSnapshot(Invalid, Snapshot, Diagnostics));
	TestTrue(TEXT("Residency failures have a stable code"),
		HasCode(Diagnostics, TEXT("WTUE-RES-003")));

	Invalid = MakeContract();
	Invalid.ArtifactVersions.BehaviorIr = { 0, 1 };
	TestFalse(TEXT("An absent optional layer cannot carry a minor version"),
		FWebToUEResourceContractPolicy::BuildSnapshot(Invalid, Snapshot, Diagnostics));
	TestTrue(TEXT("Layer contract failures have a stable code"),
		HasCode(Diagnostics, TEXT("WTUE-RES-005")));

	FWebToUEArtifactVersionSet Produced = MakeContract().ArtifactVersions;
	FWebToUEArtifactVersionSet Supported = Produced;
	Produced.ResourceIr = { 2, 0 };
	TestFalse(TEXT("A consumer rejects an incompatible Resource IR major"),
		FWebToUEResourceContractPolicy::IsRuntimeCompatible(
			Produced, Supported, Diagnostics));
	TestTrue(TEXT("Runtime incompatibility is diagnosed"),
		HasCode(Diagnostics, TEXT("WTUE-RES-005")));
	return true;
}

#endif
