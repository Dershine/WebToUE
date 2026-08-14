#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"

#include "Framework/Application/NavigationConfig.h"
#include "Input/Events.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEGamepadNavigationTest,
	"WebToUE.Runtime.GamepadNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEGamepadNavigationTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='scroll'><button id='one'>One</button><button id='two'>Two</button><button id='three'>Three</button><button id='four'>Four</button></div></body>"),
		TEXT("#scroll { width: 160px; height: 120px; overflow: auto; gap: 8px; } button { width: 160px; height: 60px; flex-shrink: 0; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	View->LayoutForTesting(FVector2f(200.0f, 160.0f));

	TArray<FWebToUESemanticNode> Nodes;
	View->GetSemanticNodes(Nodes);
	TestEqual(TEXT("The navigation corpus exposes four semantic actions"), Nodes.Num(), 4);
	if (Nodes.Num() != 4) return false;
	TestTrue(TEXT("The first semantic action accepts initial focus"),
		View->RequestSemanticFocus(Nodes[0].Handle));

	const FNavigationEvent DownEvent(FModifierKeysState(), 0,
		EUINavigation::Down, ENavigationGenesis::Controller);
	for (int32 Index = 1; Index < Nodes.Num(); ++Index)
	{
		const FNavigationReply Reply = View->OnNavigation(FGeometry(), DownEvent);
		TestTrue(*FString::Printf(TEXT("Controller Down %d is handled inside the leaf"), Index),
			Reply.GetBoundaryRule() == EUINavigationRule::Stop);
		TestTrue(*FString::Printf(TEXT("Controller Down %d selects the spatial successor"), Index),
			View->GetFocusedSemanticNode() == Nodes[Index].Handle);
	}
	FWebToUENode* Scroll = View->FindRuntimeNodeByIdForTesting(TEXT("scroll"));
	TestNotNull(TEXT("The navigation corpus retains its scroll container"), Scroll);
	if (Scroll)
	{
		TestTrue(TEXT("Focusing a clipped descendant scrolls it into view"),
			View->GetRuntimeStateForTesting(*Scroll).ScrollOffset.Y > 0.0f);
	}
	TestTrue(TEXT("Navigation escapes to an outer CommonUI focus scope at the boundary"),
		View->OnNavigation(FGeometry(), DownEvent).GetBoundaryRule() == EUINavigationRule::Escape);

	const FNavigationConfig NavigationConfig;
	const FKeyEvent DPadDown(EKeys::Gamepad_DPad_Down, FModifierKeysState(),
		0, false, 0, 0);
	TestTrue(TEXT("Slate/CommonUI maps controller D-pad Down to the same navigation contract"),
		NavigationConfig.GetNavigationDirectionFromKey(DPadDown) == EUINavigation::Down);
	const FKeyEvent Accept(EKeys::Virtual_Gamepad_Accept.GetVirtualKey(),
		FModifierKeysState(), 0, false, 0, 0);
	TestTrue(TEXT("The CommonUI virtual accept key is consumed by the focused internal action"),
		View->OnKeyDown(FGeometry(), Accept).IsEventHandled());

	return true;
}

#endif
