#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "WebToUEDocument.h"
#include "WebToUERuntimeInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeBindingIndexTest,
	"WebToUE.Runtime.BindingIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUERuntimeBindingIndexTest::RunTest(const FString& Parameters)
{
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;

	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FWebToUECompiledNode& Label = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Label.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Label.Tag = TEXT("span");
		Label.ParentIndex = 0;
	}
	const auto AddBinding = [&CompiledDocument](const TCHAR* Field,
		EWebToUEBindingKind Kind, int32 TargetNodeIndex, bool bRichText = false)
	{
		FWebToUECompiledBindingOp& Op = CompiledDocument.BindingOps.AddDefaulted_GetRef();
		Op.RootField = FName(Field);
		Op.Kind = Kind;
		Op.TargetNodeIndex = TargetNodeIndex;
		Op.bRichText = bRichText;
	};
	AddBinding(TEXT("SharedLabel"), EWebToUEBindingKind::Text, 1);
	AddBinding(TEXT("SharedLabel"), EWebToUEBindingKind::Text, 2, true);
	AddBinding(TEXT("IsVisible"), EWebToUEBindingKind::Visible, 1);
	AddBinding(TEXT("IsEnabled"), EWebToUEBindingKind::Enabled, 1);
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));

	FWebToUERuntimeInstance Instance;
	TestTrue(TEXT("The compiled document hydrates"), Instance.Hydrate(*Document));
	TestEqual(TEXT("The root-field index contains exactly three fields"),
		Instance.GetBindingIndex().Num(), 3);
	const TConstArrayView<FWebToUERuntimeBindingOp> SharedLabelOps =
		Instance.GetBindingOps(FName(TEXT("SharedLabel")));
	TestEqual(TEXT("One field directly indexes both dependent nodes"), SharedLabelOps.Num(), 2);
	if (SharedLabelOps.Num() == 2)
	{
		TestTrue(TEXT("Each op retains its own runtime target"),
			SharedLabelOps[0].Target != SharedLabelOps[1].Target);
		TestNotNull(TEXT("The first indexed handle resolves"),
			Instance.ResolveNode(SharedLabelOps[0].Target));
		TestNotNull(TEXT("The second indexed handle resolves"),
			Instance.ResolveNode(SharedLabelOps[1].Target));
		TestFalse(TEXT("The first text op is plain text"), SharedLabelOps[0].bRichText);
		TestTrue(TEXT("The second text op is rich text"), SharedLabelOps[1].bRichText);
	}
	TestEqual(TEXT("Visible and enabled ops can target the same node"),
		Instance.GetBindingOps(FName(TEXT("IsVisible")))[0].Target,
		Instance.GetBindingOps(FName(TEXT("IsEnabled")))[0].Target);
	TestEqual(TEXT("An unrelated field has no fallback scan"),
		Instance.GetBindingOps(FName(TEXT("UnboundField"))).Num(), 0);
	return true;
}

#endif
