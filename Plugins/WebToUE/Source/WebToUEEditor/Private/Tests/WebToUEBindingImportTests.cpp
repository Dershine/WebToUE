#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEBindingImportTest,
	"WebToUE.Editor.BindingImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEBindingImportTest::RunTest(const FString& Parameters)
{
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(TestDirectory, TEXT("BindingImport.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*TestFilename, false, true);
	};
	const FString Html = TEXT(
		"<body><span id='first' data-ue-bind-text='SharedLabel' "
		"data-ue-bind-visible='IsVisible'></span>"
		"<span id='second' data-ue-bind-text='SharedLabel' data-ue-rich-text='true' "
		"data-ue-bind-enabled='IsEnabled'></span></body>");
	TestTrue(TEXT("The binding source is written"),
		FFileHelper::SaveStringToFile(Html, *TestFilename));
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	TestTrue(TEXT("The binding source imports"),
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, true));

	const TArray<FWebToUECompiledBindingOp>& Ops = Document->GetCompiledBindingOps();
	TestEqual(TEXT("The compiler emits one typed op per declaration"), Ops.Num(), 4);
	TestEqual(TEXT("The first op stores the root field"), Ops[0].RootField,
		FName(TEXT("SharedLabel")));
	TestEqual(TEXT("The first op is a text binding"), Ops[0].Kind,
		EWebToUEBindingKind::Text);
	TestEqual(TEXT("The second declaration on the first node is visible"), Ops[1].Kind,
		EWebToUEBindingKind::Visible);
	TestEqual(TEXT("The same field produces a second direct text op"), Ops[2].RootField,
		FName(TEXT("SharedLabel")));
	TestTrue(TEXT("Rich-text mode is compiled into the op"), Ops[2].bRichText);
	TestEqual(TEXT("The final op is enabled"), Ops[3].Kind,
		EWebToUEBindingKind::Enabled);
	TestNotEqual(TEXT("The shared field targets two different compiled nodes"),
		Ops[0].TargetNodeIndex, Ops[2].TargetNodeIndex);
	TestEqual(TEXT("Two fields on the first node share its target index"),
		Ops[0].TargetNodeIndex, Ops[1].TargetNodeIndex);
	return true;
}

#endif
