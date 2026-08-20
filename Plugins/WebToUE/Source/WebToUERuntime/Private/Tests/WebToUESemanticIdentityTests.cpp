#if WITH_DEV_AUTOMATION_TESTS

#include "WebToUESemanticIdentity.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUESemanticIdentityPlanTest,
	"WebToUE.Runtime.SemanticIdentityPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUESemanticIdentityFailuresTest,
	"WebToUE.Runtime.SemanticIdentityFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::SemanticIdentity::Tests
{
	static FWebToUEComponentInstanceIdentity Component(
		FString Route, std::initializer_list<const TCHAR*> Path = {})
	{
		FWebToUEComponentInstanceIdentity Result;
		Result.RouteId = MoveTemp(Route);
		for (const TCHAR* Segment : Path)
		{
			Result.KeyPath.Add(Segment);
		}
		return Result;
	}

	static FWebToUESourceProvenance At(int32 Line, int32 Column = 1)
	{
		FWebToUESourceProvenance Result;
		Result.SourceUnit = TEXT("UI/MainMenu.wtue");
		Result.Line = Line;
		Result.Column = Column;
		Result.EndLine = Line;
		Result.EndColumn = Column + 4;
		return Result;
	}

	static FWebToUESemanticNodeDescriptor Node(
		int32 Slot,
		uint32 Generation,
		FWebToUEComponentInstanceIdentity Owner,
		const TCHAR* Key,
		int32 Line,
		EWebToUECrossReloadState Retainable = EWebToUECrossReloadState::None,
		EWebToUESemanticNodeKind Kind = EWebToUESemanticNodeKind::Element,
		uint32 StateContractVersion = 1)
	{
		FWebToUESemanticNodeDescriptor Result;
		Result.Handle = FWebToUEInstanceHandle::Create(91, Generation, Slot);
		Result.Component = MoveTemp(Owner);
		Result.StableKey = FWebToUEStableSemanticKey::FromString(Key);
		Result.Provenance = At(Line);
		Result.Kind = Kind;
		Result.StateContractVersion = StateContractVersion;
		Result.RetainableState = Retainable;
		return Result;
	}

	static const FWebToUEReimportStateAction* FindCurrent(
		const FWebToUEReimportStatePlan& Plan, int32 Slot)
	{
		return Plan.Actions.FindByPredicate([Slot](const FWebToUEReimportStateAction& Action)
		{
			return Action.CurrentHandle.IsValid() &&
				Action.CurrentHandle.GetSlot() == Slot;
		});
	}
}

bool FWebToUESemanticIdentityPlanTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::SemanticIdentity::Tests;
	const FWebToUEReimportIdentityContext Context{ 91, 7, 8 };
	const EWebToUECrossReloadState AllRetainable = EWebToUECrossReloadState::All;
	const FWebToUEComponentInstanceIdentity Root = Component(TEXT("main-menu"));
	const FWebToUEComponentInstanceIdentity FirstPanel =
		Component(TEXT("main-menu"), { TEXT("settings:first") });
	const FWebToUEComponentInstanceIdentity SecondPanel =
		Component(TEXT("main-menu"), { TEXT("settings:second") });

	TArray<FWebToUESemanticNodeDescriptor> Previous = {
		Node(0, 7, Root, TEXT("play"), 10, AllRetainable),
		Node(1, 7, FirstPanel, TEXT("row"), 20, EWebToUECrossReloadState::LocalState),
		Node(2, 7, SecondPanel, TEXT("row"), 30, EWebToUECrossReloadState::LocalState),
		Node(3, 7, Root, TEXT("removed"), 40, EWebToUECrossReloadState::LocalState),
		Node(4, 7, Root, TEXT("changed-kind"), 50, AllRetainable,
			EWebToUESemanticNodeKind::Element),
		Node(5, 7, Root, TEXT("contract"), 60, AllRetainable,
			EWebToUESemanticNodeKind::Element, 1),
	};
	TArray<FWebToUESemanticNodeDescriptor> Current = {
		Node(0, 8, Root, TEXT("play"), 110,
			EWebToUECrossReloadState::ScrollIntent | EWebToUECrossReloadState::FocusIntent),
		Node(1, 8, SecondPanel, TEXT("row"), 130, EWebToUECrossReloadState::LocalState),
		Node(2, 8, FirstPanel, TEXT("row"), 120, EWebToUECrossReloadState::LocalState),
		Node(3, 8, Root, TEXT("added"), 140, EWebToUECrossReloadState::LocalState),
		Node(4, 8, Root, TEXT("changed-kind"), 150, AllRetainable,
			EWebToUESemanticNodeKind::Text),
		Node(5, 8, Root, TEXT("contract"), 160, AllRetainable,
			EWebToUESemanticNodeKind::Element, 2),
		Node(6, 8, Root, TEXT(""), 170, EWebToUECrossReloadState::FocusIntent),
	};

	FWebToUEReimportStatePlan Plan;
	TestTrue(TEXT("A valid cross-generation identity plan succeeds"),
		FWebToUESemanticIdentityPolicy::BuildReimportPlan(
			Context, Previous, Current, Plan));
	TestEqual(TEXT("Every current node and removed previous node has one action"),
		Plan.Actions.Num(), 8);

	const FWebToUEReimportStateAction* Play = FindCurrent(Plan, 0);
	TestNotNull(TEXT("The explicitly keyed node is planned"), Play);
	if (Play)
	{
		TestTrue(TEXT("Source movement does not change Stable Semantic Identity"),
			Play->Disposition == EWebToUEReimportStateDisposition::Matched);
		TestTrue(TEXT("Retention is the explicit intersection of both revisions"),
			Play->PreservedState ==
				(EWebToUECrossReloadState::ScrollIntent | EWebToUECrossReloadState::FocusIntent));
		TestNotEqual(TEXT("The transfer binds to the new Instance Handle generation"),
			Play->PreviousHandle.GetGeneration(), Play->CurrentHandle.GetGeneration());
	}

	const FWebToUEReimportStateAction* ReorderedSecond = FindCurrent(Plan, 1);
	const FWebToUEReimportStateAction* ReorderedFirst = FindCurrent(Plan, 2);
	TestTrue(TEXT("Component instance identity survives sibling reorder"),
		ReorderedSecond && ReorderedSecond->PreviousHandle.GetSlot() == 2 &&
		ReorderedFirst && ReorderedFirst->PreviousHandle.GetSlot() == 1);
	TestTrue(TEXT("The same node key is legal in two component instances"),
		ReorderedSecond && ReorderedSecond->Disposition == EWebToUEReimportStateDisposition::Matched &&
		ReorderedFirst && ReorderedFirst->Disposition == EWebToUEReimportStateDisposition::Matched);

	TestTrue(TEXT("A newly added key resets"),
		FindCurrent(Plan, 3) && FindCurrent(Plan, 3)->Disposition ==
			EWebToUEReimportStateDisposition::ResetAdded);
	TestTrue(TEXT("A node-kind change resets instead of copying state"),
		FindCurrent(Plan, 4) && FindCurrent(Plan, 4)->Disposition ==
			EWebToUEReimportStateDisposition::ResetIncompatible);
	TestTrue(TEXT("A state-contract version change resets"),
		FindCurrent(Plan, 5) && FindCurrent(Plan, 5)->Disposition ==
			EWebToUEReimportStateDisposition::ResetIncompatible);
	TestTrue(TEXT("An unkeyed node resets even when its slot is reusable"),
		FindCurrent(Plan, 6) && FindCurrent(Plan, 6)->Disposition ==
			EWebToUEReimportStateDisposition::ResetUnkeyed);
	TestTrue(TEXT("Removed nodes are explicit and have no current handle"),
		Plan.Actions.ContainsByPredicate([](const FWebToUEReimportStateAction& Action)
		{
			return Action.Disposition == EWebToUEReimportStateDisposition::Removed &&
				Action.PreviousHandle.GetSlot() == 3 && !Action.CurrentHandle.IsValid();
		}));
	TestEqual(TEXT("Incompatible kind/version and unkeyed retention are diagnosed"),
		Plan.Diagnostics.Num(), 3);
	TestTrue(TEXT("Diagnostics use stable identity codes and deterministic order"),
		Plan.Diagnostics[0].Code == TEXT("WTUE-ID-003") &&
		Plan.Diagnostics[1].Code == TEXT("WTUE-ID-003") &&
		Plan.Diagnostics[2].Code == TEXT("WTUE-ID-004"));
	return true;
}

bool FWebToUESemanticIdentityFailuresTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::SemanticIdentity::Tests;
	const FWebToUEComponentInstanceIdentity Root = Component(TEXT("main-menu"));
	FWebToUEReimportStatePlan Plan;

	TArray<FWebToUESemanticNodeDescriptor> Previous = {
		Node(0, 7, Root, TEXT("duplicate"), 20),
		Node(1, 7, Root, TEXT("duplicate"), 10),
	};
	TArray<FWebToUESemanticNodeDescriptor> Current = {
		Node(0, 8, Root, TEXT("duplicate"), 30),
	};
	TestFalse(TEXT("Duplicate keys in one component fail closed"),
		FWebToUESemanticIdentityPolicy::BuildReimportPlan(
			{ 91, 7, 8 }, Previous, Current, Plan));
	TestTrue(TEXT("Duplicate rejection is actionable"),
		Plan.Diagnostics.Num() == 1 && Plan.Diagnostics[0].Code == TEXT("WTUE-ID-002"));
	TestEqual(TEXT("A failed plan exposes no partial transfer actions"), Plan.Actions.Num(), 0);
	if (Plan.Diagnostics.Num() == 1)
	{
		TestTrue(TEXT("Duplicate diagnostics name the earlier declaration deterministically"),
			Plan.Diagnostics[0].Detail.Contains(TEXT(":20:1")));
	}

	Previous = { Node(0, 7, Root, TEXT("play"), 10) };
	Current = { Node(0, 7, Root, TEXT("play"), 20) };
	TestFalse(TEXT("Current descriptors cannot reuse the previous Handle generation"),
		FWebToUESemanticIdentityPolicy::BuildReimportPlan(
			{ 91, 7, 8 }, Previous, Current, Plan));
	TestTrue(TEXT("Handle-domain rejection keeps ADR-0002 authoritative"),
		Plan.Diagnostics.Num() == 1 && Plan.Diagnostics[0].Code == TEXT("WTUE-ID-001"));

	Current = { Node(0, 8, Root, TEXT("play"), 20) };
	Current[0].Component.KeyPath = { TEXT("") };
	TestFalse(TEXT("Empty component identity segments fail closed"),
		FWebToUESemanticIdentityPolicy::BuildReimportPlan(
			{ 91, 7, 8 }, Previous, Current, Plan));
	TestTrue(TEXT("Invalid component identity uses the domain diagnostic"),
		Plan.Diagnostics.Num() == 1 && Plan.Diagnostics[0].Code == TEXT("WTUE-ID-001"));
	return true;
}

#endif
