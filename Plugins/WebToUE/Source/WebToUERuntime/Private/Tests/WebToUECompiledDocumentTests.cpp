#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUEDocument.h"
#include "WebToUERuntimeInstance.h"

#include "Engine/Texture2D.h"
#include "Internationalization/StringTable.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUECompiledDocumentBoundaryTest,
	"WebToUE.Runtime.CompiledDocumentBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeInstanceIsolationTest,
	"WebToUE.Runtime.RuntimeInstanceIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeCacheSeparationTest,
	"WebToUE.Runtime.RuntimeCacheSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEOrderedDeclarationHydrationTest,
	"WebToUE.Runtime.OrderedDeclarationHydration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::CompiledDocument::Tests
{
	static void AddAttribute(FWebToUECompiledNode& Node, const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUECompiledAttribute& Attribute = Node.Attributes.AddDefaulted_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
	}

	static void AddDeclaration(FWebToUECompiledRule& Rule, const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUECompiledDeclaration& Declaration = Rule.Declarations.AddDefaulted_GetRef();
		Declaration.Name = Name;
		Declaration.Value = Value;
	}
}

bool FWebToUECompiledDocumentBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::CompiledDocument::Tests;

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.LocalizationNamespace = TEXT("BoundaryTest");
	CompiledDocument.RootNodeIndex = 0;
	CompiledDocument.ReferencedTextures.Add(TSoftObjectPtr<UTexture2D>(
		FSoftObjectPath(TEXT("/WebToUETests/T_Boundary.T_Boundary"))));
	CompiledDocument.ReferencedStringTables.Add(TSoftObjectPtr<UStringTable>(
		FSoftObjectPath(TEXT("/WebToUETests/ST_Boundary.ST_Boundary"))));

	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	FWebToUECompiledNode& Button = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Button.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Button.Tag = TEXT("button");
	Button.ParentIndex = 0;
	AddAttribute(Button, TEXT("id"), TEXT("boundary-target"));

	FWebToUECompiledRule& BaseRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	BaseRule.Specificity = 1;
	BaseRule.Selector.AddDefaulted_GetRef().Type = TEXT("button");
	FWebToUECompiledDeclaration& Width = BaseRule.Declarations.AddDefaulted_GetRef();
	Width.Name = TEXT("width");
	Width.Value = TEXT("120px");
	FWebToUECompiledDeclaration& Height = BaseRule.Declarations.AddDefaulted_GetRef();
	Height.Name = TEXT("height");
	Height.Value = TEXT("40px");

	FWebToUECompiledRule& HoverRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	HoverRule.Specificity = 11;
	FWebToUECompiledSelectorSegment& HoverSelector = HoverRule.Selector.AddDefaulted_GetRef();
	HoverSelector.Type = TEXT("button");
	HoverSelector.RequiredState = static_cast<uint8>(EWebToUEPseudoState::Hover);
	FWebToUECompiledDeclaration& HoverColor = HoverRule.Declarations.AddDefaulted_GetRef();
	HoverColor.Name = TEXT("background-color");
	HoverColor.Value = TEXT("#ff0000");

	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
	const FWebToUECompiledNode* InitialNodeStorage = Document->GetCompiledNodes().GetData();
	const FWebToUECompiledRule* InitialRuleStorage = Document->GetCompiledRules().GetData();

	TestEqual(TEXT("The compiled namespace is available through the read-only boundary"),
		Document->GetLocalizationNamespace(), FString(TEXT("BoundaryTest")));
	TestEqual(TEXT("The compiled node view exposes both nodes"), Document->GetCompiledNodes().Num(), 2);
	TestEqual(TEXT("The compiled rule view exposes both rules"), Document->GetCompiledRules().Num(), 2);
	TestEqual(TEXT("The compiled root index is available through the read-only boundary"),
		Document->GetRootNodeIndex(), 0);
	TestEqual(TEXT("The compiled texture dependency is retained"),
		Document->GetReferencedTextures().Num(), 1);
	TestEqual(TEXT("The compiled String Table dependency is retained"),
		Document->GetReferencedStringTables().Num(), 1);

	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetDocument(Document);
	View->LayoutForTesting(FVector2f(320.0f, 180.0f));
	FWebToUENode* RuntimeButton = View->HitTestForTesting(FVector2f(10.0f, 10.0f));
	TestNotNull(TEXT("Hydration creates a hittable runtime node"), RuntimeButton);
	if (RuntimeButton)
	{
		View->SetHoveredNodeForTesting(RuntimeButton);
		TestTrue(TEXT("Hover state belongs to the hydrated runtime node"),
			EnumHasAllFlags(View->GetRuntimeStateForTesting(*RuntimeButton).PseudoStates,
				EWebToUEPseudoState::Hover));
	}

	TestEqual(TEXT("Hydration does not replace or mutate compiled node storage"),
		Document->GetCompiledNodes().GetData(), InitialNodeStorage);
	TestEqual(TEXT("Hydration does not replace or mutate compiled rule storage"),
		Document->GetCompiledRules().GetData(), InitialRuleStorage);
	TestEqual(TEXT("Runtime state does not change the compiled button id"),
		Document->GetCompiledNodes()[1].Attributes[0].Value, FString(TEXT("boundary-target")));
	TestEqual(TEXT("Runtime style resolution does not change the compiled hover declaration"),
		Document->GetCompiledRules()[1].Declarations[0].Value, FString(TEXT("#ff0000")));
	return true;
}

bool FWebToUEOrderedDeclarationHydrationTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::CompiledDocument::Tests;

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;
	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	FWebToUECompiledNode& Target = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Target.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Target.Tag = TEXT("div");
	Target.ParentIndex = 0;
	AddAttribute(Target, TEXT("id"), TEXT("ordered-target"));
	FWebToUECompiledRule& Rule = CompiledDocument.Rules.AddDefaulted_GetRef();
	Rule.Specificity = 100;
	Rule.Selector.AddDefaulted_GetRef().Id = TEXT("ordered-target");
	AddDeclaration(Rule, TEXT("color"), TEXT("#ff0000"));
	AddDeclaration(Rule, TEXT("width"), TEXT("80px"));
	AddDeclaration(Rule, TEXT("color"), TEXT("#00ff00"));
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));

	FWebToUERuntimeInstance RuntimeInstance;
	TestTrue(TEXT("The compiled document hydrates"), RuntimeInstance.Hydrate(*Document));
	const FWebToUEDocument* RuntimeDocument = RuntimeInstance.GetDocument();
	TestNotNull(TEXT("Hydration creates a runtime document"), RuntimeDocument);
	if (!RuntimeDocument) return false;
	TestEqual(TEXT("Hydration preserves the declaration count"), RuntimeDocument->Rules[0].Declarations.Num(), 3);
	if (RuntimeDocument->Rules[0].Declarations.Num() == 3)
	{
		TestEqual(TEXT("Hydration preserves the first duplicate"),
			RuntimeDocument->Rules[0].Declarations[0].Value, FString(TEXT("#ff0000")));
		TestEqual(TEXT("Hydration preserves the interleaved declaration"),
			RuntimeDocument->Rules[0].Declarations[1].Name, FString(TEXT("width")));
		TestEqual(TEXT("Hydration preserves the last duplicate"),
			RuntimeDocument->Rules[0].Declarations[2].Value, FString(TEXT("#00ff00")));
	}

	FWebToUENode* RuntimeTarget = nullptr;
	RuntimeDocument->ForEachNode([&RuntimeTarget](FWebToUENode& Node)
	{
		if (Node.GetAttribute(TEXT("id")) == TEXT("ordered-target")) RuntimeTarget = &Node;
	});
	TestNotNull(TEXT("The hydrated target exists"), RuntimeTarget);
	if (RuntimeTarget)
	{
		TestEqual(TEXT("The hydrated cascade uses the last duplicate"),
			RuntimeDocument->GetComputedStyle(*RuntimeTarget).Color, FLinearColor::Green);
		TestEqual(TEXT("The interleaved declaration keeps its unit"),
			RuntimeDocument->GetComputedStyle(*RuntimeTarget).Width.Unit, EWebToUEUnit::Pixels);
		TestEqual(TEXT("The interleaved declaration is applied"),
			RuntimeDocument->GetComputedStyle(*RuntimeTarget).Width.Value, 80.0f);
	}
	TestEqual(TEXT("Hydration leaves the compiled declaration sequence intact"),
		Document->GetCompiledRules()[0].Declarations.Num(), 3);
	return true;
}

bool FWebToUERuntimeInstanceIsolationTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::CompiledDocument::Tests;

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;

	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	FWebToUECompiledNode& Scroll = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Scroll.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Scroll.Tag = TEXT("div");
	Scroll.ParentIndex = 0;
	AddAttribute(Scroll, TEXT("id"), TEXT("scroll"));
	for (const TCHAR* Id : { TEXT("one"), TEXT("two"), TEXT("three") })
	{
		FWebToUECompiledNode& Button = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Button.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Button.Tag = TEXT("button");
		Button.ParentIndex = 1;
		AddAttribute(Button, TEXT("id"), Id);
	}
	FWebToUECompiledNode& Text = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Text.Type = static_cast<uint8>(EWebToUENodeType::Text);
	Text.Tag = TEXT("#text");
	Text.Text = TEXT("Original");
	Text.LocalizedText = FText::FromString(Text.Text);
	Text.ParentIndex = 2;

	FWebToUECompiledRule& ScrollRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	ScrollRule.Specificity = 100;
	ScrollRule.Selector.AddDefaulted_GetRef().Id = TEXT("scroll");
	AddDeclaration(ScrollRule, TEXT("width"), TEXT("100px"));
	AddDeclaration(ScrollRule, TEXT("height"), TEXT("100px"));
	AddDeclaration(ScrollRule, TEXT("overflow"), TEXT("auto"));
	FWebToUECompiledRule& ButtonRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	ButtonRule.Specificity = 1;
	ButtonRule.Selector.AddDefaulted_GetRef().Type = TEXT("button");
	AddDeclaration(ButtonRule, TEXT("width"), TEXT("100px"));
	AddDeclaration(ButtonRule, TEXT("height"), TEXT("80px"));
	AddDeclaration(ButtonRule, TEXT("flex-shrink"), TEXT("0"));
	FWebToUECompiledRule& HoverRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	HoverRule.Specificity = 11;
	FWebToUECompiledSelectorSegment& HoverSelector = HoverRule.Selector.AddDefaulted_GetRef();
	HoverSelector.Type = TEXT("button");
	HoverSelector.RequiredState = static_cast<uint8>(EWebToUEPseudoState::Hover);
	AddDeclaration(HoverRule, TEXT("background-color"), TEXT("#ff0000"));

	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
	const TSharedRef<SWebToUEView> FirstView = SNew(SWebToUEView);
	const TSharedRef<SWebToUEView> SecondView = SNew(SWebToUEView);
	FirstView->SetDocument(Document);
	SecondView->SetDocument(Document);
	FirstView->LayoutForTesting(FVector2f(200.0f, 200.0f));
	SecondView->LayoutForTesting(FVector2f(200.0f, 200.0f));

	FWebToUENode* FirstButton = FirstView->FindRuntimeNodeByIdForTesting(TEXT("one"));
	FWebToUENode* SecondButton = SecondView->FindRuntimeNodeByIdForTesting(TEXT("one"));
	FWebToUENode* FirstScroll = FirstView->FindRuntimeNodeByIdForTesting(TEXT("scroll"));
	FWebToUENode* SecondScroll = SecondView->FindRuntimeNodeByIdForTesting(TEXT("scroll"));
	TestNotNull(TEXT("The first instance hydrates the button"), FirstButton);
	TestNotNull(TEXT("The second instance hydrates the button"), SecondButton);
	TestNotNull(TEXT("The first instance hydrates the scroll container"), FirstScroll);
	TestNotNull(TEXT("The second instance hydrates the scroll container"), SecondScroll);
	if (!FirstButton || !SecondButton || !FirstScroll || !SecondScroll ||
		FirstButton->Children.IsEmpty() || SecondButton->Children.IsEmpty())
	{
		return false;
	}

	TestNotEqual(TEXT("Each view owns distinct hydrated nodes"), FirstButton, SecondButton);
	FirstView->SetHoveredNodeForTesting(FirstButton);
	FirstView->SetFocusedNodeForTesting(FirstButton);
	TestTrue(TEXT("The first instance owns hover state"), EnumHasAllFlags(
		FirstView->GetRuntimeStateForTesting(*FirstButton).PseudoStates, EWebToUEPseudoState::Hover));
	TestTrue(TEXT("The first instance owns focus state"), EnumHasAllFlags(
		FirstView->GetRuntimeStateForTesting(*FirstButton).PseudoStates, EWebToUEPseudoState::Focus));
	TestFalse(TEXT("Pseudo state does not leak to the second instance"), EnumHasAnyFlags(
		SecondView->GetRuntimeStateForTesting(*SecondButton).PseudoStates,
		EWebToUEPseudoState::Hover | EWebToUEPseudoState::Focus));

	TestTrue(TEXT("The first instance scrolls independently"),
		FirstView->ScrollAtForTesting(FVector2f(50.0f, 50.0f), -4.0f));
	TestTrue(TEXT("The first instance retains its scroll offset"),
		FirstView->GetRuntimeStateForTesting(*FirstScroll).ScrollOffset.Y > 0.0f);
	TestTrue(TEXT("Scroll state does not leak to the second instance"), FMath::IsNearlyZero(
		SecondView->GetRuntimeStateForTesting(*SecondScroll).ScrollOffset.Y));

	FWebToUENode& FirstText = *FirstButton->Children[0];
	FWebToUENode& SecondText = *SecondButton->Children[0];
	FirstView->SetBoundTextForTesting(FirstText, FText::FromString(TEXT("First Instance")));
	TestEqual(TEXT("Bound text belongs to the first instance"),
		FirstView->GetDisplayTextForTesting(FirstText).ToString(), FString(TEXT("First Instance")));
	TestEqual(TEXT("Bound text does not leak to the second instance"),
		SecondView->GetDisplayTextForTesting(SecondText).ToString(), FString(TEXT("Original")));
	TestEqual(TEXT("Runtime state does not modify the compiled text"),
		Document->GetCompiledNodes()[5].Text, FString(TEXT("Original")));
	return true;
}

bool FWebToUERuntimeCacheSeparationTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::CompiledDocument::Tests;

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

	FWebToUECompiledRule& BaseRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	BaseRule.Specificity = 1;
	BaseRule.Selector.AddDefaulted_GetRef().Type = TEXT("button");
	AddDeclaration(BaseRule, TEXT("width"), TEXT("120px"));
	AddDeclaration(BaseRule, TEXT("height"), TEXT("40px"));
	AddDeclaration(BaseRule, TEXT("background-color"), TEXT("#0000ff"));
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
	FirstView->LayoutForTesting(FVector2f(320.0f, 180.0f));
	SecondView->LayoutForTesting(FVector2f(640.0f, 360.0f));

	FWebToUENode* FirstRoot = FirstView->FindRuntimeNodeByIdForTesting(TEXT("root"));
	FWebToUENode* SecondRoot = SecondView->FindRuntimeNodeByIdForTesting(TEXT("root"));
	FWebToUENode* FirstButton = FirstView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	FWebToUENode* SecondButton = SecondView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The first instance hydrates the cache test root"), FirstRoot);
	TestNotNull(TEXT("The second instance hydrates the cache test root"), SecondRoot);
	TestNotNull(TEXT("The first instance hydrates the cache test button"), FirstButton);
	TestNotNull(TEXT("The second instance hydrates the cache test button"), SecondButton);
	if (!FirstRoot || !SecondRoot || !FirstButton || !SecondButton)
	{
		return false;
	}

	TestNotEqual(TEXT("Computed styles use per-view storage"),
		&FirstView->GetComputedStyleForTesting(*FirstButton),
		&SecondView->GetComputedStyleForTesting(*SecondButton));
	TestNotEqual(TEXT("Layout results use per-view storage"),
		&FirstView->GetLayoutResultForTesting(*FirstButton),
		&SecondView->GetLayoutResultForTesting(*SecondButton));
	TestEqual(TEXT("The first instance retains its viewport-sized root"),
		FirstView->GetLayoutResultForTesting(*FirstRoot).Size, FVector2f(320.0f, 180.0f));
	TestEqual(TEXT("The second instance retains its distinct viewport-sized root"),
		SecondView->GetLayoutResultForTesting(*SecondRoot).Size, FVector2f(640.0f, 360.0f));
	TestEqual(TEXT("Both instances start from the base computed style"),
		FirstView->GetComputedStyleForTesting(*FirstButton).BackgroundColor, FLinearColor::Blue);
	TestEqual(TEXT("The second instance starts from the base computed style"),
		SecondView->GetComputedStyleForTesting(*SecondButton).BackgroundColor, FLinearColor::Blue);

	FirstView->SetHoveredNodeForTesting(FirstButton);
	TestEqual(TEXT("Hover recomputes the first instance style"),
		FirstView->GetComputedStyleForTesting(*FirstButton).BackgroundColor, FLinearColor::Red);
	TestEqual(TEXT("Computed style does not leak to the second instance"),
		SecondView->GetComputedStyleForTesting(*SecondButton).BackgroundColor, FLinearColor::Blue);
	TestEqual(TEXT("Style refresh does not overwrite the first layout result"),
		FirstView->GetLayoutResultForTesting(*FirstRoot).Size, FVector2f(320.0f, 180.0f));
	TestEqual(TEXT("Style refresh does not overwrite the second layout result"),
		SecondView->GetLayoutResultForTesting(*SecondRoot).Size, FVector2f(640.0f, 360.0f));
	TestEqual(TEXT("Runtime caches do not replace compiled node storage"),
		Document->GetCompiledNodes().GetData(), InitialNodeStorage);
	TestEqual(TEXT("Runtime caches do not replace compiled rule storage"),
		Document->GetCompiledRules().GetData(), InitialRuleStorage);
	TestEqual(TEXT("Runtime caches do not modify the compiled button id"),
		Document->GetCompiledNodes()[1].Attributes[0].Value, FString(TEXT("target")));
	return true;
}

#endif
