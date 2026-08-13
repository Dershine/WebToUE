#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUEStyleProperties.h"

#include "Input/HittestGrid.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimePresentationIsolationTest,
	"WebToUE.Runtime.RuntimePresentationIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPaintOnlyPseudoResourceSafetyTest,
	"WebToUE.Runtime.PaintOnlyPseudoResourceSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETypedCascadeSlateOutputTest,
	"WebToUE.Runtime.TypedCascadeSlateOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPseudoInvalidationPathTest,
	"WebToUE.Runtime.PseudoInvalidationPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::RuntimePresentation::Tests
{
	static void AddAttribute(FWebToUECompiledNode& Node, const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUECompiledAttribute& Attribute = Node.Attributes.AddDefaulted_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
	}

	static void AddDeclaration(FWebToUECompiledRule& Rule, const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUEStyleDeclaration Parsed;
		check(WebToUE::Private::TryParseCssDeclaration(Name, Value, Parsed));
		FWebToUECompiledDeclaration& Declaration = Rule.Declarations.AddDefaulted_GetRef();
		Declaration.Property = Parsed.Property;
		Declaration.TypedValue = Parsed.TypedValue;
	}

	static void PaintView(const TSharedRef<SWebToUEView>& View)
	{
		FHittestGrid HittestGrid;
		FSlateWindowElementList DrawElements(nullptr);
		const FGeometry Geometry = FGeometry::MakeRoot(
			FVector2D(320.0, 180.0), FSlateLayoutTransform());
		const FPaintArgs PaintArgs(
			&View.Get(), HittestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
		View->OnPaint(PaintArgs, Geometry, FSlateRect(0.0f, 0.0f, 320.0f, 180.0f),
			DrawElements, 0, FWidgetStyle(), true);
	}

	static TArray<FLinearColor> PaintViewAndGetRoundedBoxTints(
		const TSharedRef<SWebToUEView>& View)
	{
		FHittestGrid HittestGrid;
		FSlateWindowElementList DrawElements(nullptr);
		const FGeometry Geometry = FGeometry::MakeRoot(
			FVector2D(320.0, 180.0), FSlateLayoutTransform());
		const FPaintArgs PaintArgs(
			&View.Get(), HittestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
		View->OnPaint(PaintArgs, Geometry, FSlateRect(0.0f, 0.0f, 320.0f, 180.0f),
			DrawElements, 0, FWidgetStyle(), true);
		TArray<FLinearColor> Result;
		const FSlateDrawElementArray<FSlateRoundedBoxElement>& RoundedBoxes =
			DrawElements.GetUncachedDrawElements().Get<
				static_cast<uint8>(EElementType::ET_RoundedBox)>();
		for (const FSlateRoundedBoxElement& Element : RoundedBoxes)
		{
			Result.Add(Element.GetTint());
		}
		return Result;
	}
}

bool FWebToUERuntimePresentationIsolationTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::RuntimePresentation::Tests;

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;

	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	AddAttribute(Body, TEXT("id"), TEXT("root"));
	FWebToUECompiledNode& Button = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Button.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Button.Tag = TEXT("button");
	Button.ParentIndex = 0;
	AddAttribute(Button, TEXT("id"), TEXT("target"));
	FWebToUECompiledNode& Text = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Text.Type = static_cast<uint8>(EWebToUENodeType::Text);
	Text.Tag = TEXT("#text");
	Text.Text = TEXT("Presentation cache");
	Text.LocalizedText = FText::FromString(Text.Text);
	Text.ParentIndex = 1;

	FWebToUECompiledRule& ButtonRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	ButtonRule.Specificity = 1;
	ButtonRule.Selector.AddDefaulted_GetRef().Type = TEXT("button");
	AddDeclaration(ButtonRule, TEXT("width"), TEXT("160px"));
	AddDeclaration(ButtonRule, TEXT("height"), TEXT("48px"));
	AddDeclaration(ButtonRule, TEXT("background-color"), TEXT("#0000ff"));
	FWebToUECompiledRule& HoverRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	HoverRule.Specificity = 11;
	FWebToUECompiledSelectorSegment& HoverSelector = HoverRule.Selector.AddDefaulted_GetRef();
	HoverSelector.Type = TEXT("button");
	HoverSelector.RequiredState = static_cast<uint8>(EWebToUEPseudoState::Hover);
	AddDeclaration(HoverRule, TEXT("background-color"), TEXT("#ff0000"));

	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
	const FWebToUECompiledNode* InitialNodeStorage = Document->GetCompiledNodes().GetData();
	const FWebToUECompiledRule* InitialRuleStorage = Document->GetCompiledRules().GetData();

	const TSharedRef<SWebToUEView> FirstView = SNew(SWebToUEView);
	const TSharedRef<SWebToUEView> SecondView = SNew(SWebToUEView);
	FirstView->SetDocument(Document);
	SecondView->SetDocument(Document);
	TestNotEqual(TEXT("Each view owns a distinct presentation service"),
		FirstView->GetPresentationIdentityForTesting(),
		SecondView->GetPresentationIdentityForTesting());

	FWebToUENode* FirstButton = FirstView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	FWebToUENode* SecondButton = SecondView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The first presentation hydrates the target"), FirstButton);
	TestNotNull(TEXT("The second presentation hydrates the target"), SecondButton);
	if (!FirstButton || !SecondButton || FirstButton->Children.IsEmpty() ||
		SecondButton->Children.IsEmpty())
	{
		return false;
	}

	PaintView(FirstView);
	PaintView(SecondView);
	FWebToUENode& FirstText = *FirstButton->Children[0];
	FWebToUENode& SecondText = *SecondButton->Children[0];
	TestEqual(TEXT("The first presentation owns one text cache"),
		FirstView->GetPresentationTextCacheCountForTesting(), 1);
	TestEqual(TEXT("The second presentation owns one text cache"),
		SecondView->GetPresentationTextCacheCountForTesting(), 1);
	TestTrue(TEXT("The first presentation owns element brushes"),
		FirstView->GetPresentationBrushCacheCountForTesting() >= 2);
	TestTrue(TEXT("The second presentation owns element brushes"),
		SecondView->GetPresentationBrushCacheCountForTesting() >= 2);
	const void* FirstTextCache =
		FirstView->GetPresentationTextCacheIdentityForTesting(FirstText);
	const void* SecondTextCache =
		SecondView->GetPresentationTextCacheIdentityForTesting(SecondText);
	TestNotNull(TEXT("The first text cache is materialized by final Slate paint"), FirstTextCache);
	TestNotNull(TEXT("The second text cache is materialized by final Slate paint"), SecondTextCache);
	TestNotEqual(TEXT("Text layout caches do not leak across views"), FirstTextCache, SecondTextCache);
	TestFalse(TEXT("Final paint satisfies the first presentation layout"),
		FirstView->IsPresentationLayoutDirtyForTesting());
	TestFalse(TEXT("Final paint satisfies the second presentation layout"),
		SecondView->IsPresentationLayoutDirtyForTesting());

	FirstView->SetHoveredNodeForTesting(FirstButton);
	TestEqual(TEXT("A first-view style refresh invalidates only its text cache"),
		FirstView->GetPresentationTextCacheCountForTesting(), 0);
	TestEqual(TEXT("The second view retains its warm text cache"),
		SecondView->GetPresentationTextCacheCountForTesting(), 1);
	TestTrue(TEXT("The first presentation marks layout dirty after style refresh"),
		FirstView->IsPresentationLayoutDirtyForTesting());
	TestFalse(TEXT("The second presentation remains layout-clean"),
		SecondView->IsPresentationLayoutDirtyForTesting());

	PaintView(FirstView);
	TestEqual(TEXT("The first presentation rebuilds its own text cache"),
		FirstView->GetPresentationTextCacheCountForTesting(), 1);
	FirstView->SetDocument(nullptr);
	TestEqual(TEXT("Clearing a document releases the first text cache"),
		FirstView->GetPresentationTextCacheCountForTesting(), 0);
	TestEqual(TEXT("Clearing a document releases the first brush cache"),
		FirstView->GetPresentationBrushCacheCountForTesting(), 0);
	TestEqual(TEXT("The second view keeps its presentation cache after the first resets"),
		SecondView->GetPresentationTextCacheCountForTesting(), 1);
	PaintView(SecondView);

	TestEqual(TEXT("Presentation work does not replace compiled node storage"),
		Document->GetCompiledNodes().GetData(), InitialNodeStorage);
	TestEqual(TEXT("Presentation work does not replace compiled rule storage"),
		Document->GetCompiledRules().GetData(), InitialRuleStorage);
	return true;
}

bool FWebToUETypedCascadeSlateOutputTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::RuntimePresentation::Tests;
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;

	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	FWebToUECompiledNode& FirstButton = CompiledDocument.Nodes.AddDefaulted_GetRef();
	FirstButton.Type = static_cast<uint8>(EWebToUENodeType::Element);
	FirstButton.Tag = TEXT("button");
	FirstButton.ParentIndex = 0;
	AddAttribute(FirstButton, TEXT("id"), TEXT("first"));
	FWebToUECompiledNode& SecondButton = CompiledDocument.Nodes.AddDefaulted_GetRef();
	SecondButton.Type = static_cast<uint8>(EWebToUENodeType::Element);
	SecondButton.Tag = TEXT("button");
	SecondButton.ParentIndex = 0;
	AddAttribute(SecondButton, TEXT("id"), TEXT("second"));

	FWebToUECompiledRule& BodyRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	BodyRule.Specificity = 1;
	BodyRule.Selector.AddDefaulted_GetRef().Type = TEXT("body");
	AddDeclaration(BodyRule, TEXT("row-gap"), TEXT("2px"));
	AddDeclaration(BodyRule, TEXT("gap"), TEXT("18px"));
	FWebToUECompiledRule& ButtonRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	ButtonRule.Specificity = 1;
	ButtonRule.SourceOrder = 1;
	ButtonRule.Selector.AddDefaulted_GetRef().Type = TEXT("button");
	AddDeclaration(ButtonRule, TEXT("width"), TEXT("100px"));
	AddDeclaration(ButtonRule, TEXT("height"), TEXT("20px"));
	AddDeclaration(ButtonRule, TEXT("background-color"), TEXT("transparent"));
	AddDeclaration(ButtonRule, TEXT("background"), TEXT("blue"));

	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetDocument(Document);
	View->LayoutForTesting(FVector2f(320.0f, 180.0f));
	FWebToUENode* RuntimeFirst = View->FindRuntimeNodeByIdForTesting(TEXT("first"));
	FWebToUENode* RuntimeSecond = View->FindRuntimeNodeByIdForTesting(TEXT("second"));
	TestNotNull(TEXT("The first cascade output node hydrates"), RuntimeFirst);
	TestNotNull(TEXT("The second cascade output node hydrates"), RuntimeSecond);
	if (!RuntimeFirst || !RuntimeSecond) return false;

	const FWebToUERuntimeLayoutResult& FirstLayout =
		View->GetLayoutResultForTesting(*RuntimeFirst);
	const FWebToUERuntimeLayoutResult& SecondLayout =
		View->GetLayoutResultForTesting(*RuntimeSecond);
	TestTrue(TEXT("The final Yoga layout uses the later gap shorthand winner"),
		FMath::IsNearlyEqual(SecondLayout.Position.Y - FirstLayout.Position.Y - FirstLayout.Size.Y,
			18.0f, 0.1f));

	const TArray<FLinearColor> Tints = PaintViewAndGetRoundedBoxTints(View);
	TestTrue(TEXT("The later opaque background shorthand produces final Slate draw elements"),
		!Tints.IsEmpty());
	return true;
}

bool FWebToUEPaintOnlyPseudoResourceSafetyTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::RuntimePresentation::Tests;
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;

	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	FWebToUECompiledNode& Button = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Button.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Button.Tag = TEXT("button");
	Button.ParentIndex = 0;
	AddAttribute(Button, TEXT("id"), TEXT("paint-target"));
	FWebToUECompiledNode& Image = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Image.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Image.Tag = TEXT("img");
	Image.ParentIndex = 0;
	AddAttribute(Image, TEXT("id"), TEXT("unrelated-image"));
	AddAttribute(Image, TEXT("src"),
		TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));

	FWebToUECompiledRule& ButtonRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	ButtonRule.Specificity = 1;
	ButtonRule.Selector.AddDefaulted_GetRef().Type = TEXT("button");
	AddDeclaration(ButtonRule, TEXT("width"), TEXT("160px"));
	AddDeclaration(ButtonRule, TEXT("height"), TEXT("48px"));
	AddDeclaration(ButtonRule, TEXT("background-color"), TEXT("#0000ff"));
	FWebToUECompiledRule& HoverRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	HoverRule.Specificity = 11;
	FWebToUECompiledSelectorSegment& HoverSelector = HoverRule.Selector.AddDefaulted_GetRef();
	HoverSelector.Type = TEXT("button");
	HoverSelector.RequiredState = static_cast<uint8>(EWebToUEPseudoState::Hover);
	AddDeclaration(HoverRule, TEXT("opacity"), TEXT("0.25"));

	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetDocument(Document);
	FWebToUENode* RuntimeButton = View->FindRuntimeNodeByIdForTesting(TEXT("paint-target"));
	FWebToUENode* RuntimeImage = View->FindRuntimeNodeByIdForTesting(TEXT("unrelated-image"));
	TestNotNull(TEXT("The paint-only pseudo target hydrates"), RuntimeButton);
	TestNotNull(TEXT("The unrelated image hydrates"), RuntimeImage);
	if (!RuntimeButton || !RuntimeImage) return false;

	const TArray<FLinearColor> InitialTints = PaintViewAndGetRoundedBoxTints(View);
	TestTrue(TEXT("Initial final Slate output contains an opaque rounded box"),
		InitialTints.ContainsByPredicate([](const FLinearColor& Tint)
		{
			return FMath::IsNearlyEqual(Tint.A, 1.0f);
		}));
	const void* InitialImageBrush = View->GetPresentationBrushIdentityForTesting(*RuntimeImage);
	TestNotNull(TEXT("The unrelated image has a materialized brush"), InitialImageBrush);
	TestEqual(TEXT("Initial cache construction attempts the image resource once"),
		View->GetPresentationResourceLoadAttemptsForTesting(), uint64(1));

	View->SetHoveredNodeForTesting(RuntimeButton);
	TestEqual(TEXT("The paint-only pseudo updates computed opacity"),
		View->GetComputedStyleForTesting(*RuntimeButton).Opacity, 0.25f);
	TestEqual(TEXT("Paint-only pseudo does not re-enter synchronous image loading"),
		View->GetPresentationResourceLoadAttemptsForTesting(), uint64(1));
	TestEqual(TEXT("Paint-only pseudo preserves the unrelated image brush identity"),
		View->GetPresentationBrushIdentityForTesting(*RuntimeImage), InitialImageBrush);

	const TArray<FLinearColor> HoverTints = PaintViewAndGetRoundedBoxTints(View);
	TestTrue(TEXT("Final Slate output carries the hovered opacity into a draw element"),
		HoverTints.ContainsByPredicate([](const FLinearColor& Tint)
		{
			return FMath::IsNearlyEqual(Tint.A, 0.25f);
		}));
	return true;
}

bool FWebToUEPseudoInvalidationPathTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><section id='menu'><button id='item'>Item</button>")
		TEXT("<span id='label' class='label'>Label</span></section>")
		TEXT("<button id='unrelated'>Other</button></body>"),
		TEXT("#item { background-color: blue; opacity: 1; }")
		TEXT("#item:hover { opacity: 0.5; }")
		TEXT("#menu:hover > #item { background-color: red; }")
		TEXT("#menu:hover .label { color: red; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	FWebToUENode* Item = View->FindRuntimeNodeByIdForTesting(TEXT("item"));
	FWebToUENode* Label = View->FindRuntimeNodeByIdForTesting(TEXT("label"));
	FWebToUENode* Unrelated = View->FindRuntimeNodeByIdForTesting(TEXT("unrelated"));
	TestNotNull(TEXT("The pseudo path item exists"), Item);
	TestNotNull(TEXT("The pseudo path label exists"), Label);
	TestNotNull(TEXT("The unrelated hover target exists"), Unrelated);
	if (!Item || !Label || !Unrelated || Label->Children.IsEmpty()) return false;

	FWebToUEPerformanceSnapshot EnterSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetHoveredNodeForTesting(Item);
		EnterSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Hover changes only the target-to-root state path"),
		EnterSnapshot.GetCounter(EWebToUEPerformanceCounter::PseudoStateNodesChanged), uint64(3));
	TestEqual(TEXT("Two selector targets become dirty after dependency filtering"),
		EnterSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleDirtyTargets), uint64(2));
	TestEqual(TEXT("Cascade visits two targets plus one inherited text descendant"),
		EnterSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(3));
	TestEqual(TEXT("The two target properties and inherited color produce four changes"),
		EnterSnapshot.GetCounter(EWebToUEPerformanceCounter::StylePropertyChanges), uint64(4));
	TestTrue(TEXT("Self hover updates opacity"),
		FMath::IsNearlyEqual(View->GetComputedStyleForTesting(*Item).Opacity, 0.5f));
	TestEqual(TEXT("Ancestor hover updates a direct-child target"),
		View->GetComputedStyleForTesting(*Item).BackgroundColor, FLinearColor::Red);
	TestEqual(TEXT("Ancestor hover updates a descendant target"),
		View->GetComputedStyleForTesting(*Label).Color, FLinearColor::Red);
	TestEqual(TEXT("Inherited paint style reaches the label text"),
		View->GetComputedStyleForTesting(*Label->Children[0]).Color, FLinearColor::Red);
	TestTrue(TEXT("The read-only report exposes the causal chain"),
		View->GetLastPseudoInvalidationReportForTesting().Contains(TEXT("Hover")) &&
		View->GetLastPseudoInvalidationReportForTesting().Contains(TEXT("background-color")) &&
		View->GetLastPseudoInvalidationReportForTesting().Contains(TEXT("Style|Paint")));

	FWebToUEPerformanceSnapshot ExitSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetHoveredNodeForTesting(Unrelated);
		ExitSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Moving hover updates only the old/new path difference"),
		ExitSnapshot.GetCounter(EWebToUEPerformanceCounter::PseudoStateNodesChanged), uint64(3));
	TestEqual(TEXT("Leaving the path revisits only the same affected style nodes"),
		ExitSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(3));
	TestEqual(TEXT("Leaving hover restores the item background"),
		View->GetComputedStyleForTesting(*Item).BackgroundColor, FLinearColor::Blue);
	TestEqual(TEXT("Leaving hover restores inherited label color"),
		View->GetComputedStyleForTesting(*Label).Color, FLinearColor::White);
	return true;
}

#endif
