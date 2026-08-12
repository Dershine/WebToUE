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

namespace WebToUE::CompiledDocument::Tests
{
	static void AddAttribute(FWebToUECompiledNode& Node, const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUECompiledAttribute& Attribute = Node.Attributes.AddDefaulted_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
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
			EnumHasAllFlags(RuntimeButton->StateFlags, EWebToUEPseudoState::Hover));
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

#endif
