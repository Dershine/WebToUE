#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"
#include "WebToUEPerformance.h"
#include "WebToUERuntimePresentation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEScrollInteractionTest, "WebToUE.Runtime.ScrollInteraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEScrollInteractionTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='scroll'><button id='one'>One</button><button id='two'>Two</button><button id='three'>Three</button></div></body>"),
		TEXT("#scroll { width: 100px; height: 100px; overflow: auto; } button { width: 100px; height: 80px; flex-shrink: 0; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	View->LayoutForTesting(FVector2f(200.0f, 200.0f));

	FWebToUENode* Scroll = nullptr;
	FWebToUENode* Third = nullptr;
	Document->ForEachNode([&](FWebToUENode& Node)
	{
		if (Node.GetAttribute(TEXT("id")) == TEXT("scroll")) Scroll = &Node;
		if (Node.GetAttribute(TEXT("id")) == TEXT("three")) Third = &Node;
	});
	TestNotNull(TEXT("Scroll container exists"), Scroll);
	TestNotNull(TEXT("Third button exists"), Third);
	if (!Scroll || !Third) return false;

	TestNull(TEXT("A clipped child cannot be hit outside the scroll viewport"),
		View->HitTestForTesting(FVector2f(50.0f, 150.0f)));
	FWebToUEPerformanceSnapshot ScrollSnapshot;
	bool bScrolled = false;
	{
		FWebToUEPerformanceCapture Capture;
		bScrolled = View->ScrollAtForTesting(FVector2f(50.0f, 50.0f), -4.0f);
		ScrollSnapshot = Capture.GetSnapshot();
	}
	TestTrue(TEXT("Wheel input over the container scrolls it"), bScrolled);
	TestEqual(TEXT("Scrolling patches the seven-command scroll subtree in place"),
		ScrollSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayCommandsPatched), uint64(7));
	TestEqual(TEXT("Scrolling updates the same seven spatial entries"),
		ScrollSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplaySpatialIndexPatches), uint64(7));
	const FWebToUERuntimeNodeState& ScrollState = View->GetRuntimeStateForTesting(*Scroll);
	TestTrue(TEXT("Wheel scrolling clamps to the content range"),
		FMath::IsNearlyEqual(ScrollState.ScrollOffset.Y, ScrollState.MaxScrollOffset.Y, 0.1f));
	TestTrue(TEXT("Scrolling changes the descendant visual position"),
		FMath::IsNearlyEqual(View->GetVisualPositionForTesting(*Third).Y, 20.0f, 0.1f));
	const FWebToUEPaintCommand* ThirdCommand =
		View->GetDisplayCommandForTesting(*Third);
	TestNotNull(TEXT("The revealed button retains a display command"), ThirdCommand);
	if (ThirdCommand)
	{
		TestTrue(TEXT("Scrolling patches the retained command's visual bound"),
			FMath::IsNearlyEqual(ThirdCommand->Bounds.Top, 20.0f, 0.1f));
	}
	TestEqual(TEXT("The revealed button participates in hit testing"),
		View->HitTestForTesting(FVector2f(50.0f, 50.0f)), Third);
	TestFalse(TEXT("Scrolling farther at the boundary is not consumed"),
		View->ScrollAtForTesting(FVector2f(50.0f, 50.0f), -1.0f));
	return true;
}

#endif
