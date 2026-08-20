#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"

#include "Input/Events.h"
#include "InputCoreTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEInteractionIdentityTest,
	"WebToUE.Runtime.InteractionIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::InteractionIdentity::Tests
{
	static FVector2D GetNodeCenter(const SWebToUEView& View, const FWebToUENode& Node)
	{
		const FVector2f Position = View.GetVisualPositionForTesting(Node);
		const FVector2f Size = View.GetLayoutResultForTesting(Node).Size;
		return FVector2D(Position + Size * 0.5f);
	}

	static FPointerEvent MakePointerEvent(
		uint32 UserIndex,
		uint32 PointerIndex,
		FVector2D Position,
		bool bPressed)
	{
		TSet<FKey> PressedButtons;
		if (bPressed)
		{
			PressedButtons.Add(EKeys::LeftMouseButton);
		}
		return FPointerEvent(UserIndex, PointerIndex, Position, Position,
			PressedButtons, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
	}
}

bool FWebToUEInteractionIdentityTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::InteractionIdentity::Tests;
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body id='root'><button id='a' data-ue-on-click='A'>A</button><button id='b' data-ue-on-click='B'>B</button></body>"),
		TEXT("body { width: 320px; height: 180px; gap: 12px; } button { width: 120px; height: 60px; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	View->LayoutForTesting(FVector2f(320.0f, 180.0f));
	FWebToUENode* Root = View->FindRuntimeNodeByIdForTesting(TEXT("root"));
	FWebToUENode* A = View->FindRuntimeNodeByIdForTesting(TEXT("a"));
	FWebToUENode* B = View->FindRuntimeNodeByIdForTesting(TEXT("b"));
	TestNotNull(TEXT("The interaction corpus contains its root"), Root);
	TestNotNull(TEXT("The interaction corpus contains button A"), A);
	TestNotNull(TEXT("The interaction corpus contains button B"), B);
	if (!Root || !A || !B)
	{
		return false;
	}

	const FGeometry Geometry = FGeometry::MakeRoot(
		FVector2D(320.0, 180.0), FSlateLayoutTransform());
	const FVector2D ACenter = GetNodeCenter(*View, *A);
	const FVector2D BCenter = GetNodeCenter(*View, *B);
	const FWebToUEInteractionIdentity PointerA =
		FWebToUEInteractionIdentity::Pointer(0, 2);
	const FWebToUEInteractionIdentity PointerB =
		FWebToUEInteractionIdentity::Pointer(1, 7);
	const FPointerEvent HoverA = MakePointerEvent(0, 2, ACenter, false);
	const FPointerEvent HoverB = MakePointerEvent(1, 7, ACenter, false);
	View->OnMouseMove(Geometry, HoverA);
	View->OnMouseMove(Geometry, HoverB);
	TestTrue(TEXT("Two Slate pointer identities independently retain one hovered target"),
		View->GetHoveredNodeForTesting(PointerA) == A &&
		View->GetHoveredNodeForTesting(PointerB) == A);
	View->OnMouseLeave(HoverA);
	TestTrue(TEXT("One pointer leaving does not clear another pointer's hover pseudo state"),
		View->GetHoveredNodeForTesting(PointerA) == nullptr &&
		View->GetHoveredNodeForTesting(PointerB) == A &&
		EnumHasAnyFlags(View->GetRuntimeStateForTesting(*A).PseudoStates,
			EWebToUEPseudoState::Hover));
	View->OnMouseLeave(HoverB);
	TestFalse(TEXT("Hover clears after the final owning pointer leaves"),
		EnumHasAnyFlags(View->GetRuntimeStateForTesting(*A).PseudoStates,
			EWebToUEPseudoState::Hover));

	const FPointerEvent DownA = MakePointerEvent(0, 2, ACenter, true);
	const FPointerEvent DownB = MakePointerEvent(1, 7, BCenter, true);
	View->OnMouseButtonDown(Geometry, DownA);
	View->OnMouseButtonDown(Geometry, DownB);
	TestTrue(TEXT("Pressed and captured nodes are keyed by Slate User and Pointer"),
		View->GetPressedNodeForTesting(PointerA) == A &&
		View->GetCapturedNodeForTesting(PointerA) == A &&
		View->GetPressedNodeForTesting(PointerB) == B &&
		View->GetCapturedNodeForTesting(PointerB) == B);
	TestTrue(TEXT("Internal focus is independently keyed by Slate User"),
		View->GetFocusedNodeForTesting(0) == A &&
		View->GetFocusedNodeForTesting(1) == B &&
		View->GetFocusedSemanticNode(0) == View->GetInstanceHandleForTesting(*A) &&
		View->GetFocusedSemanticNode(1) == View->GetInstanceHandleForTesting(*B));

	int32 CaptureLostCount = 0;
	bool bLostObservedAfterCleanup = false;
	FWebToUEInteractionIdentity ObservedInteraction;
	View->AddEventListener(View->GetInstanceHandleForTesting(*A),
		EWebToUERuntimeEventType::PointerCaptureLost,
		EWebToUERuntimeEventPhase::Target,
		[View, PointerA, &CaptureLostCount, &bLostObservedAfterCleanup,
			&ObservedInteraction](
			FWebToUERuntimeEvent& Event, FWebToUEUpdateTransaction&)
		{
			++CaptureLostCount;
			ObservedInteraction = Event.GetSnapshot().Interaction;
			bLostObservedAfterCleanup =
				View->GetPressedNodeForTesting(PointerA) == nullptr &&
				View->GetCapturedNodeForTesting(PointerA) == nullptr;
			Event.PreventDefault();
		});
	View->OnMouseCaptureLost(FCaptureLostEvent(0, 2));
	TestTrue(TEXT("Capture loss clears only its matching pointer before event delivery"),
		CaptureLostCount == 1 && bLostObservedAfterCleanup &&
		ObservedInteraction == PointerA &&
		View->GetPressedNodeForTesting(PointerB) == B &&
		View->GetCapturedNodeForTesting(PointerB) == B);
	TestTrue(TEXT("PointerCaptureLost is non-cancelable and remains dispatched"),
		View->GetLastEventDispatchResultForTesting() ==
			EWebToUEEventDispatchResult::Dispatched);
	TestTrue(TEXT("Capture loss clears only A's active pseudo state"),
		!EnumHasAnyFlags(View->GetRuntimeStateForTesting(*A).PseudoStates,
			EWebToUEPseudoState::Active) &&
		EnumHasAnyFlags(View->GetRuntimeStateForTesting(*B).PseudoStates,
			EWebToUEPseudoState::Active));

	const FPointerEvent WrongRelease = MakePointerEvent(0, 99, BCenter, false);
	TestFalse(TEXT("A release from a different pointer identity is not consumed"),
		View->OnMouseButtonUp(Geometry, WrongRelease).IsEventHandled());
	TestTrue(TEXT("A mismatched release cannot clear another pointer's capture"),
		View->GetPressedNodeForTesting(PointerB) == B &&
		View->GetCapturedNodeForTesting(PointerB) == B);

	int32 ClickDefaultCount = 0;
	View->SetDefaultEventObserverForTesting(
		[&ClickDefaultCount, PointerB](const FWebToUEEventPathSnapshot& Snapshot)
		{
			if (Snapshot.Type == EWebToUERuntimeEventType::Click &&
				Snapshot.Interaction == PointerB)
			{
				++ClickDefaultCount;
			}
		});
	const FPointerEvent UpB = MakePointerEvent(1, 7, BCenter, false);
	TestTrue(TEXT("The matching pointer release is consumed"),
		View->OnMouseButtonUp(Geometry, UpB).IsEventHandled());
	TestTrue(TEXT("The matching release clears capture and dispatches one correlated click"),
		View->GetPressedNodeForTesting(PointerB) == nullptr &&
		View->GetCapturedNodeForTesting(PointerB) == nullptr &&
		ClickDefaultCount == 1);

	View->OnFocusLost(FFocusEvent(EFocusCause::SetDirectly, 0));
	TestTrue(TEXT("Focus loss clears only the matching Slate User"),
		View->GetFocusedNodeForTesting(0) == nullptr &&
		View->GetFocusedNodeForTesting(1) == B &&
		EnumHasAnyFlags(View->GetRuntimeStateForTesting(*B).PseudoStates,
			EWebToUEPseudoState::Focus));
	View->OnFocusLost(FFocusEvent(EFocusCause::SetDirectly, 1));
	TestFalse(TEXT("Focus pseudo state clears after its final Slate User leaves"),
		EnumHasAnyFlags(View->GetRuntimeStateForTesting(*B).PseudoStates,
			EWebToUEPseudoState::Focus));
	return true;
}

#endif
