#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUEDocument.h"

#include "Engine/Texture2D.h"
#include "Internationalization/StringTable.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUECompiledDocumentBoundaryTest,
	"WebToUE.Runtime.CompiledDocumentBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeInstanceIsolationTest,
	"WebToUE.Runtime.RuntimeInstanceIsolation",
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

#endif
