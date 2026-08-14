#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"
#include "WebToUESemantics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUESemanticFocusTest,
	"WebToUE.Runtime.SemanticFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUESemanticFocusTest::RunTest(const FString& Parameters)
{
	const FString Html =
		TEXT("<body><button id='play'>Play Game</button><div id='custom' data-ue-on-click='Open'>Open Panel</div></body>");
	const FString Css =
		TEXT("body { width: 320px; height: 180px; gap: 8px; } button, #custom { width: 160px; height: 52px; }");
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(Html, Css);
	const TSharedRef<FWebToUEDocument> SecondDocument = FWebToUECompiler::Compile(Html, Css);
	const TSharedRef<SWebToUEView> FirstView = SNew(SWebToUEView);
	const TSharedRef<SWebToUEView> SecondView = SNew(SWebToUEView);
	FirstView->SetRuntimeDocumentForTesting(Document);
	SecondView->SetRuntimeDocumentForTesting(SecondDocument);
	FirstView->LayoutForTesting(FVector2f(320.0f, 180.0f));
	SecondView->LayoutForTesting(FVector2f(320.0f, 180.0f));

	TArray<FWebToUESemanticNode> FirstNodes;
	FirstView->GetSemanticNodes(FirstNodes);
	TestEqual(TEXT("Only interactive internal elements project semantic nodes"), FirstNodes.Num(), 2);
	if (FirstNodes.Num() != 2) return false;

	const FWebToUESemanticNode& Play = FirstNodes[0];
	TestTrue(TEXT("The semantic node keeps its generation-checked Instance Handle"),
		Play.Handle.IsValid());
	TestEqual(TEXT("The semantic node exposes its element id"), Play.ElementId, FName(TEXT("play")));
	TestEqual(TEXT("The semantic node derives a localized descendant-text label"),
		Play.Label.ToString(), FString(TEXT("Play Game")));
	TestTrue(TEXT("A button projects the button semantic role"),
		Play.Role == EWebToUESemanticRole::Button);
	TestTrue(TEXT("A displayed enabled action is focusable"), Play.bFocusable);
	TestTrue(TEXT("Semantic bounds retain the laid-out action size"),
		FMath::IsNearlyEqual(Play.Bounds.Right - Play.Bounds.Left, 160.0f, 0.1f) &&
		FMath::IsNearlyEqual(Play.Bounds.Bottom - Play.Bounds.Top, 52.0f, 0.1f));

	TestTrue(TEXT("The owning View accepts semantic focus by Handle"),
		FirstView->RequestSemanticFocus(Play.Handle));
	TestTrue(TEXT("Semantic focus returns the selected Handle"),
		FirstView->GetFocusedSemanticNode() == Play.Handle);

	TArray<FWebToUESemanticNode> SecondNodes;
	SecondView->GetSemanticNodes(SecondNodes);
	TestFalse(TEXT("Another View rejects the first View's semantic Handle"),
		SecondView->RequestSemanticFocus(Play.Handle));

	const FWebToUEInstanceHandle PreHydrateHandle = Play.Handle;
	FirstView->SetRuntimeDocumentForTesting(Document);
	FirstView->LayoutForTesting(FVector2f(320.0f, 180.0f));
	TestFalse(TEXT("Rehydration rejects a previous-generation semantic Handle"),
		FirstView->RequestSemanticFocus(PreHydrateHandle));
	TArray<FWebToUESemanticNode> RehydratedNodes;
	FirstView->GetSemanticNodes(RehydratedNodes);
	TestEqual(TEXT("Rehydration preserves the semantic node count"), RehydratedNodes.Num(), 2);
	if (RehydratedNodes.Num() == 2)
	{
		TestNotEqual(TEXT("Rehydration advances semantic Handle generation"),
			RehydratedNodes[0].Handle.GetGeneration(), PreHydrateHandle.GetGeneration());
		TestTrue(TEXT("The current generation remains focusable"),
			FirstView->RequestSemanticFocus(RehydratedNodes[0].Handle));
	}

	return true;
}

#endif
