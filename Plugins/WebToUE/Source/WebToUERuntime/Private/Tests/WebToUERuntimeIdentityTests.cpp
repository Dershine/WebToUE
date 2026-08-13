#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUEDocument.h"

#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeIdentityTest,
	"WebToUE.Runtime.RuntimeIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::RuntimeIdentity::Tests
{
	static void AddAttribute(FWebToUECompiledNode& Node, const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUECompiledAttribute& Attribute = Node.Attributes.AddDefaulted_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
	}
}

bool FWebToUERuntimeIdentityTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::RuntimeIdentity::Tests;

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
	Text.Text = TEXT("Identity");
	Text.LocalizedText = FText::FromString(Text.Text);
	Text.ParentIndex = 1;

	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
	const TSharedRef<SWebToUEView> FirstView = SNew(SWebToUEView);
	const TSharedRef<SWebToUEView> SecondView = SNew(SWebToUEView);
	FirstView->SetDocument(Document);
	SecondView->SetDocument(Document);

	FWebToUENode* FirstButton = FirstView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	FWebToUENode* SecondButton = SecondView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The first View hydrates the target"), FirstButton);
	TestNotNull(TEXT("The second View hydrates the target"), SecondButton);
	if (!FirstButton || !SecondButton)
	{
		return false;
	}

	const FWebToUETemplateNodeId FirstTemplateId =
		FirstView->GetTemplateNodeIdForTesting(*FirstButton);
	const FWebToUETemplateNodeId SecondTemplateId =
		SecondView->GetTemplateNodeIdForTesting(*SecondButton);
	const FWebToUEInstanceHandle FirstHandle =
		FirstView->GetInstanceHandleForTesting(*FirstButton);
	const FWebToUEInstanceHandle SecondHandle =
		SecondView->GetInstanceHandleForTesting(*SecondButton);
	TestTrue(TEXT("Compiled nodes receive a valid TemplateNodeId"), FirstTemplateId.IsValid());
	TestTrue(TEXT("The same Compiled Template node keeps one TemplateNodeId across Views"),
		FirstTemplateId == SecondTemplateId);
	TestTrue(TEXT("The first runtime node receives a valid Instance Handle"), FirstHandle.IsValid());
	TestTrue(TEXT("The second runtime node receives a valid Instance Handle"), SecondHandle.IsValid());
	TestTrue(TEXT("Each View owns a distinct Instance Handle"), FirstHandle != SecondHandle);
	TestEqual(TEXT("The first View resolves its own handle"),
		FirstView->ResolveInstanceHandleForTesting(FirstHandle), FirstButton);
	TestEqual(TEXT("The second View resolves its own handle"),
		SecondView->ResolveInstanceHandleForTesting(SecondHandle), SecondButton);
	TestNull(TEXT("A handle cannot resolve in another View"),
		SecondView->ResolveInstanceHandleForTesting(FirstHandle));

	FWebToUENode* DynamicText = FirstView->AddDynamicTextNodeForTesting(*FirstButton);
	TestNotNull(TEXT("A dynamic runtime text node can be registered"), DynamicText);
	if (!DynamicText)
	{
		return false;
	}
	const FWebToUEInstanceHandle DynamicHandle =
		FirstView->GetInstanceHandleForTesting(*DynamicText);
	TestTrue(TEXT("A dynamic node receives a valid Instance Handle"), DynamicHandle.IsValid());
	TestFalse(TEXT("A dynamic node does not impersonate a Compiled Template node"),
		FirstView->GetTemplateNodeIdForTesting(*DynamicText).IsValid());
	TestEqual(TEXT("The dynamic handle resolves while its generation is current"),
		FirstView->ResolveInstanceHandleForTesting(DynamicHandle), DynamicText);

	FirstView->SetDocument(Document);
	FWebToUENode* RehydratedButton = FirstView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The target rehydrates after a document refresh"), RehydratedButton);
	if (!RehydratedButton)
	{
		return false;
	}
	const FWebToUEInstanceHandle RehydratedHandle =
		FirstView->GetInstanceHandleForTesting(*RehydratedButton);
	TestTrue(TEXT("Rehydration preserves the compiled TemplateNodeId"),
		FirstView->GetTemplateNodeIdForTesting(*RehydratedButton) == FirstTemplateId);
	TestEqual(TEXT("Rehydration may reuse the same compact slot"),
		RehydratedHandle.GetSlot(), FirstHandle.GetSlot());
	TestNotEqual(TEXT("Rehydration advances the Instance Handle generation"),
		RehydratedHandle.GetGeneration(), FirstHandle.GetGeneration());
	TestNull(TEXT("A previous-generation handle cannot hit a rehydrated node"),
		FirstView->ResolveInstanceHandleForTesting(FirstHandle));
	TestNull(TEXT("A previous-generation dynamic handle cannot hit a new node"),
		FirstView->ResolveInstanceHandleForTesting(DynamicHandle));
	TestEqual(TEXT("The current-generation handle resolves the rehydrated node"),
		FirstView->ResolveInstanceHandleForTesting(RehydratedHandle), RehydratedButton);

	return true;
}

#endif
