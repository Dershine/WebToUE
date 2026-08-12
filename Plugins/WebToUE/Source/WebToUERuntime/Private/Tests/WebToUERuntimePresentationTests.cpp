#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUEDocument.h"
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

#endif
