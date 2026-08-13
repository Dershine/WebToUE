#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUEDocument.h"
#include "WebToUEView.h"

#include "Misc/ScopeExit.h"
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

	FWebToUECompiledRule& ButtonRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	ButtonRule.Specificity = 1;
	FWebToUECompiledSelectorSegment& ButtonSelector = ButtonRule.Selector.AddDefaulted_GetRef();
	ButtonSelector.Type = TEXT("button");
	FWebToUECompiledDeclaration& OpacityDeclaration =
		ButtonRule.Declarations.AddDefaulted_GetRef();
	OpacityDeclaration.Property = EWebToUECssProperty::Opacity;
	OpacityDeclaration.TypedValue.Type = EWebToUEStyleValueType::Number;
	OpacityDeclaration.TypedValue.Number = 0.75f;

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
	const void* InitialSharedStyleTemplate =
		FirstView->GetSharedStyleTemplateIdentityForTesting();
	TestNotNull(TEXT("Hydration prepares a shared immutable style template"),
		InitialSharedStyleTemplate);
	TestEqual(TEXT("Two Views share hydrated rules and Selector Metadata"),
		SecondView->GetSharedStyleTemplateIdentityForTesting(), InitialSharedStyleTemplate);

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
	TestEqual(TEXT("Rehydrating the same revision reuses the shared style template"),
		FirstView->GetSharedStyleTemplateIdentityForTesting(), InitialSharedStyleTemplate);

	FWebToUECompiledDocumentData RevisedDocument;
	RevisedDocument.RootNodeIndex = 0;
	RevisedDocument.Nodes = Document->GetCompiledNodes();
	RevisedDocument.Rules = Document->GetCompiledRules();
	FWebToUECompiledNode& AddedNode = RevisedDocument.Nodes.AddDefaulted_GetRef();
	AddedNode.Type = static_cast<uint8>(EWebToUENodeType::Element);
	AddedNode.Tag = TEXT("div");
	AddedNode.ParentIndex = 0;
	AddAttribute(AddedNode, TEXT("id"), TEXT("added-after-reimport"));
	Document->CommitCompiledDocument(MoveTemp(RevisedDocument));

	FirstView->SetDocument(Document);
	const void* RevisedSharedStyleTemplate =
		FirstView->GetSharedStyleTemplateIdentityForTesting();
	TestNotNull(TEXT("A revised document prepares a replacement style template"),
		RevisedSharedStyleTemplate);
	TestTrue(TEXT("A structural revision invalidates the document's cached style template"),
		RevisedSharedStyleTemplate != InitialSharedStyleTemplate);
	TestNull(TEXT("A pre-revision handle cannot resolve after reimport hydration"),
		FirstView->ResolveInstanceHandleForTesting(RehydratedHandle));
	TestEqual(TEXT("An existing View safely retains the old immutable revision until refresh"),
		SecondView->GetSharedStyleTemplateIdentityForTesting(), InitialSharedStyleTemplate);
	SecondView->SetDocument(Document);
	TestEqual(TEXT("Refreshing another View adopts the same revised style template"),
		SecondView->GetSharedStyleTemplateIdentityForTesting(), RevisedSharedStyleTemplate);

	FWebToUEInstanceHandle DestroyedViewHandle;
	{
		const TSharedRef<SWebToUEView> DestroyedView = SNew(SWebToUEView);
		DestroyedView->SetDocument(Document);
		FWebToUENode* DestroyedViewButton =
			DestroyedView->FindRuntimeNodeByIdForTesting(TEXT("target"));
		TestNotNull(TEXT("The disposable View hydrates the target"), DestroyedViewButton);
		if (!DestroyedViewButton)
		{
			return false;
		}
		DestroyedViewHandle = DestroyedView->GetInstanceHandleForTesting(*DestroyedViewButton);
	}
	const TSharedRef<SWebToUEView> ReplacementView = SNew(SWebToUEView);
	ReplacementView->SetDocument(Document);
	TestNull(TEXT("A destroyed View's handle cannot resolve in a replacement View"),
		ReplacementView->ResolveInstanceHandleForTesting(DestroyedViewHandle));

	UWebToUEView* HostedView = NewObject<UWebToUEView>(GetTransientPackage());
	HostedView->AddToRoot();
	TSharedPtr<SWidget> HostedWidget;
	ON_SCOPE_EXIT
	{
		HostedWidget.Reset();
		HostedView->ReleaseSlateResources(true);
		HostedView->RemoveFromRoot();
	};
	HostedView->SetDocument(Document);
	HostedWidget = HostedView->TakeWidget();
	FWebToUENode* HostedButton = HostedView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The UWidget-hosted View hydrates before reimport"), HostedButton);
	if (!HostedButton)
	{
		return false;
	}
	const FWebToUEInstanceHandle PreReimportHandle =
		HostedView->GetInstanceHandleForTesting(*HostedButton);

	FWebToUECompiledDocumentData ReimportedDocument;
	ReimportedDocument.RootNodeIndex = 0;
	ReimportedDocument.Nodes = Document->GetCompiledNodes();
	ReimportedDocument.Rules = Document->GetCompiledRules();
	FWebToUECompiledNode& ReimportedNode = ReimportedDocument.Nodes.AddDefaulted_GetRef();
	ReimportedNode.Type = static_cast<uint8>(EWebToUENodeType::Text);
	ReimportedNode.Tag = TEXT("#text");
	ReimportedNode.Text = TEXT("Reimport revision");
	ReimportedNode.LocalizedText = FText::FromString(ReimportedNode.Text);
	ReimportedNode.ParentIndex = 0;
	Document->CommitCompiledDocument(MoveTemp(ReimportedDocument));
	Document->NotifyDocumentChanged();

	FWebToUENode* ReimportedButton =
		HostedView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The document-changed delegate rehydrates the hosted View"), ReimportedButton);
	TestNull(TEXT("A pre-reimport Handle cannot resolve after automatic View refresh"),
		HostedView->ResolveInstanceHandleForTesting(PreReimportHandle));
	if (ReimportedButton)
	{
		TestNotEqual(TEXT("Automatic reimport refresh advances the Instance generation"),
			HostedView->GetInstanceHandleForTesting(*ReimportedButton).GetGeneration(),
			PreReimportHandle.GetGeneration());
	}

	return true;
}

#endif
