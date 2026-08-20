#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUEResourceContractTestUtils.h"
#include "WebToUEStyleProperties.h"

#include "Input/HittestGrid.h"
#include "Engine/Texture2D.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ScopeExit.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETextCacheKeyAndDirtyPathTest,
	"WebToUE.Runtime.TextCacheKeyAndDirtyPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPersistentLayoutStateTest,
	"WebToUE.Runtime.PersistentLayoutState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPersistentLayoutDependenciesTest,
	"WebToUE.Runtime.PersistentLayoutDependencies",
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

	WebToUE::Tests::SealResourceContractForTesting(CompiledDocument);
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
	const void* FirstButtonBrush =
		FirstView->GetPresentationBrushIdentityForTesting(*FirstButton);

	FirstView->SetHoveredNodeForTesting(FirstButton);
	TestEqual(TEXT("A paint-only style refresh preserves its unrelated text cache"),
		FirstView->GetPresentationTextCacheCountForTesting(), 1);
	TestEqual(TEXT("A paint-only style refresh preserves text cache identity"),
		FirstView->GetPresentationTextCacheIdentityForTesting(FirstText), FirstTextCache);
	TestNotEqual(TEXT("The changed background rebuilds only the affected element brush"),
		FirstView->GetPresentationBrushIdentityForTesting(*FirstButton), FirstButtonBrush);
	TestEqual(TEXT("The second view retains its warm text cache"),
		SecondView->GetPresentationTextCacheCountForTesting(), 1);
	TestFalse(TEXT("Paint-only style does not mark the first presentation layout dirty"),
		FirstView->IsPresentationLayoutDirtyForTesting());
	TestFalse(TEXT("The second presentation remains layout-clean"),
		SecondView->IsPresentationLayoutDirtyForTesting());

	PaintView(FirstView);
	TestEqual(TEXT("The first presentation keeps its own text cache after repaint"),
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

bool FWebToUETextCacheKeyAndDirtyPathTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::RuntimePresentation::Tests;
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body id='root'><section id='branch'><span id='target'>1111</span>")
		TEXT("<span id='sibling'>Stable</span></section><aside id='other'>Other</aside></body>"),
		TEXT("#target { white-space: normal; font-size: 16px; color: white; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	FWebToUENode* Root = View->FindRuntimeNodeByIdForTesting(TEXT("root"));
	FWebToUENode* Branch = View->FindRuntimeNodeByIdForTesting(TEXT("branch"));
	FWebToUENode* Target = View->FindRuntimeNodeByIdForTesting(TEXT("target"));
	FWebToUENode* Sibling = View->FindRuntimeNodeByIdForTesting(TEXT("sibling"));
	FWebToUENode* Other = View->FindRuntimeNodeByIdForTesting(TEXT("other"));
	TestNotNull(TEXT("The text-cache root exists"), Root);
	TestNotNull(TEXT("The text-cache branch exists"), Branch);
	TestNotNull(TEXT("The text-cache target exists"), Target);
	TestNotNull(TEXT("The text-cache sibling exists"), Sibling);
	TestNotNull(TEXT("The unrelated branch exists"), Other);
	if (!Root || !Branch || !Target || !Sibling || !Other || Target->Children.IsEmpty() ||
		Sibling->Children.IsEmpty() || Other->Children.IsEmpty())
	{
		return false;
	}
	FWebToUENode& TargetText = *Target->Children[0];
	FWebToUENode& SiblingText = *Sibling->Children[0];
	FWebToUENode& OtherText = *Other->Children[0];
	PaintView(View);
	const void* TargetCache = View->GetPresentationTextCacheIdentityForTesting(TargetText);
	const void* SiblingCache = View->GetPresentationTextCacheIdentityForTesting(SiblingText);
	TestNotNull(TEXT("Final paint warms the target text cache"), TargetCache);
	TestNotNull(TEXT("Final paint warms the sibling text cache"), SiblingCache);

	const FWebToUEComputedStyle BaseStyle = View->GetComputedStyleForTesting(TargetText);
	const float Constraint = View->GetLayoutResultForTesting(TargetText).Size.X;
	View->PrepareTextLayoutForTesting(TargetText, BaseStyle, Constraint);
	FWebToUEPerformanceSnapshot CacheHitSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->PrepareTextLayoutForTesting(TargetText, BaseStyle, Constraint);
		CacheHitSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("An identical text key skips Desired Size computation"),
		CacheHitSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(0));

	FWebToUEComputedStyle ColorStyle = BaseStyle;
	ColorStyle.Color = FLinearColor::Red;
	FWebToUEPerformanceSnapshot StyleSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->PrepareTextLayoutForTesting(TargetText, ColorStyle, Constraint);
		StyleSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("A text style change invalidates the cache key"),
		StyleSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(1));
	FWebToUEComputedStyle FontStyle = ColorStyle;
	FontStyle.FontSize += 2.0f;
	FWebToUEPerformanceSnapshot FontSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->PrepareTextLayoutForTesting(TargetText, FontStyle, Constraint);
		FontSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("A font change invalidates the cache key"),
		FontSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(1));
	FWebToUEPerformanceSnapshot ConstraintSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->PrepareTextLayoutForTesting(TargetText, FontStyle, Constraint * 0.5f);
		ConstraintSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("A constraint change invalidates the cache key"),
		ConstraintSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(1));

	const FString OriginalCulture = FInternationalization::Get().GetCurrentCulture()->GetName();
	const FString AlternateCulture = OriginalCulture.StartsWith(TEXT("fr"))
		? TEXT("en") : TEXT("fr");
	ON_SCOPE_EXIT
	{
		FInternationalization::Get().SetCurrentCulture(OriginalCulture);
	};
	if (FInternationalization::Get().SetCurrentCulture(AlternateCulture))
	{
		FWebToUEPerformanceSnapshot CultureSnapshot;
		{
			FWebToUEPerformanceCapture Capture;
			View->PrepareTextLayoutForTesting(TargetText, FontStyle, Constraint * 0.5f);
			CultureSnapshot = Capture.GetSnapshot();
		}
		TestEqual(TEXT("A locale change invalidates the cache key"),
			CultureSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(1));
		TestEqual(TEXT("The cache records the active locale"),
			View->GetPresentationTextCacheCultureForTesting(TargetText), AlternateCulture);
	}
	else
	{
		AddWarning(TEXT("The alternate culture is unavailable; locale-key invalidation was not exercised."));
	}
	FInternationalization::Get().SetCurrentCulture(OriginalCulture);

	View->PrepareTextLayoutForTesting(TargetText, BaseStyle, Constraint);
	View->LayoutForTesting(FVector2f(320.0f, 180.0f));
	FWebToUEPerformanceSnapshot EqualSizeSnapshot;
	bool bEqualSizeChanged = true;
	{
		FWebToUEPerformanceCapture Capture;
		bEqualSizeChanged = View->ApplyBoundTextChangeForTesting(
			TargetText, FText::FromString(TEXT("2222")), false);
		EqualSizeSnapshot = Capture.GetSnapshot();
	}
	TestFalse(TEXT("Tabular digit text with unchanged Desired Size skips layout"),
		bEqualSizeChanged);
	TestFalse(TEXT("Unchanged Desired Size keeps presentation layout clean"),
		View->IsPresentationLayoutDirtyForTesting());
	TestEqual(TEXT("Unchanged Desired Size performs no Yoga work"),
		EqualSizeSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));
	TestEqual(TEXT("Only the changed text recomputes its Desired Size"),
		EqualSizeSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(1));
	TestEqual(TEXT("Plain-text key changes preserve the target cache allocation"),
		View->GetPresentationTextCacheIdentityForTesting(TargetText), TargetCache);
	TestEqual(TEXT("The unrelated sibling cache is untouched"),
		View->GetPresentationTextCacheIdentityForTesting(SiblingText), SiblingCache);

	const bool bLongTextChanged = View->ApplyBoundTextChangeForTesting(TargetText,
		FText::FromString(TEXT("A substantially longer value changes desired size")), false);
	TestTrue(TEXT("A changed Desired Size requests layout"), bLongTextChanged);
	TestTrue(TEXT("Only the changed text is measure-dirty"),
		View->IsPresentationMeasureDirtyForTesting(TargetText));
	TestFalse(TEXT("The sibling text is not measure-dirty"),
		View->IsPresentationMeasureDirtyForTesting(SiblingText));
	TestTrue(TEXT("The changed text is on the layout dependency path"),
		View->IsPresentationLayoutPathDirtyForTesting(TargetText));
	TestTrue(TEXT("Its parent is on the layout dependency path"),
		View->IsPresentationLayoutPathDirtyForTesting(*Target));
	TestTrue(TEXT("Its ancestor is on the layout dependency path"),
		View->IsPresentationLayoutPathDirtyForTesting(*Branch));
	TestTrue(TEXT("The root is on the layout dependency path"),
		View->IsPresentationLayoutPathDirtyForTesting(*Root));
	TestFalse(TEXT("An unrelated sibling branch is outside the layout dependency path"),
		View->IsPresentationLayoutPathDirtyForTesting(*Other));
	TestFalse(TEXT("Unrelated text is outside the layout dependency path"),
		View->IsPresentationLayoutPathDirtyForTesting(OtherText));

	FWebToUEPerformanceSnapshot RichTextSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->ApplyBoundTextChangeForTesting(TargetText,
			FText::FromString(TEXT("<strong>Rich</>")), true);
		RichTextSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Changing RichText mode rebuilds only its text layout"),
		RichTextSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutBuilds), uint64(1));
	TestEqual(TEXT("RichText changes do not evict the sibling cache"),
		View->GetPresentationTextCacheIdentityForTesting(SiblingText), SiblingCache);
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
	CompiledDocument.ResourceManifest.Add({ EWebToUEResourceKind::Texture,
		FSoftObjectPath(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture")) });

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

	WebToUE::Tests::SealResourceContractForTesting(CompiledDocument);
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
	TestNotNull(TEXT("The test texture is resident before runtime presentation"),
		LoadObject<UTexture2D>(nullptr,
			TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture")));
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
	TestEqual(TEXT("Initial cache construction performs no synchronous resource load"),
		View->GetPresentationResourceLoadAttemptsForTesting(), uint64(0));

	const void* InitialButtonBrush = View->GetPresentationBrushIdentityForTesting(*RuntimeButton);
	FWebToUEPerformanceSnapshot HoverSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetHoveredNodeForTesting(RuntimeButton);
		HoverSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("The paint-only pseudo updates computed opacity"),
		View->GetComputedStyleForTesting(*RuntimeButton).Opacity, 0.25f);
	TestEqual(TEXT("Paint-only pseudo does not re-enter synchronous image loading"),
		View->GetPresentationResourceLoadAttemptsForTesting(), uint64(0));
	TestEqual(TEXT("Paint-only pseudo preserves the unrelated image brush identity"),
		View->GetPresentationBrushIdentityForTesting(*RuntimeImage), InitialImageBrush);
	TestEqual(TEXT("Opacity-only pseudo preserves the affected element brush identity"),
		View->GetPresentationBrushIdentityForTesting(*RuntimeButton), InitialButtonBrush);
	TestFalse(TEXT("Opacity-only pseudo keeps the warm layout clean"),
		View->IsPresentationLayoutDirtyForTesting());
	TestEqual(TEXT("Opacity-only pseudo does not rebuild Yoga"),
		HoverSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));
	TestEqual(TEXT("Opacity-only pseudo does not invalidate text caches"),
		HoverSnapshot.GetCounter(EWebToUEPerformanceCounter::TextCacheInvalidations), uint64(0));
	TestEqual(TEXT("Opacity-only pseudo does not rebuild paint order"),
		HoverSnapshot.GetCounter(EWebToUEPerformanceCounter::PaintOrderCacheBuilds), uint64(0));
	TestEqual(TEXT("Opacity-only pseudo does not rebuild a brush"),
		HoverSnapshot.GetCounter(EWebToUEPerformanceCounter::BrushBuilds), uint64(0));
	TestEqual(TEXT("Opacity-only pseudo performs no resource load attempt"),
		HoverSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceLoadAttempts), uint64(0));

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

bool FWebToUEPersistentLayoutStateTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='row'><button id='item'>Item</button><span>Other</span></div></body>"),
		TEXT("#row { width: 200px; flex-direction: row; flex-wrap: wrap; }")
		TEXT("#item { width: 40px; height: 20px; } #item:hover { width: 80px; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	FWebToUENode* Item = View->FindRuntimeNodeByIdForTesting(TEXT("item"));
	TestNotNull(TEXT("Persistent layout target exists"), Item);
	if (!Item) return false;

	FWebToUEPerformanceSnapshot ColdSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->LayoutForTesting(FVector2f(320.0f, 180.0f));
		ColdSnapshot = Capture.GetSnapshot();
	}
	TestTrue(TEXT("Cold layout creates the Yoga tree once"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt) > 0);

	FWebToUEPerformanceSnapshot WarmSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->LayoutForTesting(FVector2f(320.0f, 180.0f));
		WarmSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Warm layout reuses every Yoga node"),
		WarmSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));
	TestEqual(TEXT("Warm layout does not recompute unchanged text"),
		WarmSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(0));

	FWebToUEPerformanceSnapshot UpdateSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetHoveredNodeForTesting(Item);
		UpdateSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("One width change writes one Yoga property"),
		UpdateSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaStyleWrites), uint64(1));
	TestEqual(TEXT("A style change does not rebuild the Yoga tree"),
		UpdateSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));
	TestTrue(TEXT("The width change marks presentation layout dirty"),
		View->IsPresentationLayoutDirtyForTesting());

	FWebToUEPerformanceSnapshot RelayoutSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->LayoutForTesting(FVector2f(320.0f, 180.0f));
		RelayoutSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Incremental relayout retains the persistent Yoga tree"),
		RelayoutSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));
	TestTrue(TEXT("Incremental relayout reports changed layout results"),
		RelayoutSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaLayoutResultsChanged) > 0);
	TestTrue(TEXT("Updated width reaches the runtime layout result"),
		FMath::IsNearlyEqual(View->GetLayoutResultForTesting(*Item).Size.X, 80.0f));
	return true;
}

bool FWebToUEPersistentLayoutDependenciesTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='outer'><div id='wrap'><button id='item'>A long constrained label</button>")
		TEXT("<div id='sibling'>Sibling</div><div id='absolute'>Absolute</div></div></div></body>"),
		TEXT("#outer { width: 120px; } #wrap { width: 120px; flex-direction: row; ")
		TEXT("flex-wrap: wrap; position: relative; } #item { width: 40px; height: 40px; }")
		TEXT("#item:hover { width: 80px; } #sibling { width: 60px; height: 20px; flex-shrink: 0; }")
		TEXT("#absolute { position: absolute; left: 5px; top: 70px; width: 20px; height: 10px; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	FWebToUENode* Item = View->FindRuntimeNodeByIdForTesting(TEXT("item"));
	FWebToUENode* Sibling = View->FindRuntimeNodeByIdForTesting(TEXT("sibling"));
	FWebToUENode* Absolute = View->FindRuntimeNodeByIdForTesting(TEXT("absolute"));
	TestNotNull(TEXT("Deep layout item exists"), Item);
	TestNotNull(TEXT("Deep layout sibling exists"), Sibling);
	TestNotNull(TEXT("Deep layout absolute child exists"), Absolute);
	if (!Item || !Sibling || !Absolute || Item->Children.IsEmpty()) return false;

	View->LayoutForTesting(FVector2f(320.0f, 180.0f));
	const FVector2f InitialSiblingPosition = View->GetLayoutResultForTesting(*Sibling).Position;
	const FVector2f InitialAbsolutePosition = View->GetLayoutResultForTesting(*Absolute).Position;
	FWebToUEPerformanceSnapshot Snapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetHoveredNodeForTesting(Item);
		View->LayoutForTesting(FVector2f(320.0f, 180.0f));
		Snapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Deep width change writes one Yoga property"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaStyleWrites), uint64(1));
	TestEqual(TEXT("Deep width change reuses the Yoga tree"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));
	TestTrue(TEXT("Wrap moves the affected flow sibling"),
		!View->GetLayoutResultForTesting(*Sibling).Position.Equals(InitialSiblingPosition));
	TestTrue(TEXT("The absolute child retains its independent anchored position"),
		View->GetLayoutResultForTesting(*Absolute).Position.Equals(InitialAbsolutePosition));
	TestTrue(TEXT("The changed constraint remeasures only bounded text work"),
		Snapshot.Get(EWebToUEPerformancePhase::Measure).CallCount > 0 &&
		Snapshot.Get(EWebToUEPerformancePhase::Measure).CallCount <= 2);
	TestTrue(TEXT("The changed wrap constraint recomputes a bounded text cache"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes) > 0 &&
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes) <= 2);
	TestTrue(TEXT("Layout result changes stay below the full test tree"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaLayoutResultsChanged) > 0 &&
		Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaLayoutResultsChanged) < 10);
	return true;
}

#endif
