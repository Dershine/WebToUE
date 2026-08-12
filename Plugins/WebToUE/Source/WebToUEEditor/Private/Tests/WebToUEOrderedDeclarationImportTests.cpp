#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEOrderedDeclarationImportTest,
	"WebToUE.Editor.OrderedDeclarationImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEOrderedDeclarationImportTest::RunTest(const FString& Parameters)
{
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(TestDirectory, TEXT("OrderedDeclarations.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	const FString Html = TEXT("<body><style>#target { color: #ff0000; width: 100px; color: invalid; color: #00ff00; width: 120px; }</style><div id='target'></div></body>");
	TestTrue(TEXT("The ordered declaration source is written"), FFileHelper::SaveStringToFile(Html, *TestFilename));
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	TestTrue(TEXT("The ordered declaration document imports"),
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, true));
	TestEqual(TEXT("The import produces one compiled rule"), Document->GetCompiledRules().Num(), 1);
	if (Document->GetCompiledRules().Num() == 1)
	{
		const TArray<FWebToUECompiledDeclaration>& Declarations = Document->GetCompiledRules()[0].Declarations;
		TestEqual(TEXT("The compiled IR retains every valid declaration"), Declarations.Num(), 4);
		if (Declarations.Num() == 4)
		{
			TestEqual(TEXT("The compiled IR retains the first duplicate"), Declarations[0].Value, FString(TEXT("#ff0000")));
			TestEqual(TEXT("The compiled IR retains the interleaved property"), Declarations[1].Name, FString(TEXT("width")));
			TestEqual(TEXT("The compiled IR retains the later duplicate"), Declarations[2].Value, FString(TEXT("#00ff00")));
			TestEqual(TEXT("The compiled IR retains the final declaration"), Declarations[3].Value, FString(TEXT("120px")));
		}
	}

	const int32 LastGoodNodeCount = Document->GetCompiledNodes().Num();
	const int32 LastGoodRuleCount = Document->GetCompiledRules().Num();
	TestTrue(TEXT("The source can be removed to exercise version-reimport failure"),
		IFileManager::Get().Delete(*TestFilename, false, true));
	AddExpectedError(TEXT("Could not read WebToUE document"), EAutomationExpectedErrorFlags::Contains, 1);
	UWebToUEFactory* Factory = NewObject<UWebToUEFactory>(GetTransientPackage());
	TestEqual(TEXT("A missing source fails reimport explicitly"),
		Factory->Reimport(Document), EReimportResult::Failed);
	TestEqual(TEXT("A missing source preserves last-good nodes"),
		Document->GetCompiledNodes().Num(), LastGoodNodeCount);
	TestEqual(TEXT("A missing source preserves last-good ordered rules"),
		Document->GetCompiledRules().Num(), LastGoodRuleCount);
	const FWebToUEAssetDiagnostic* MissingSource = Document->Diagnostics.FindByPredicate(
		[](const FWebToUEAssetDiagnostic& Diagnostic)
		{
			return Diagnostic.Severity == EWebToUEAssetDiagnosticSeverity::Error &&
				Diagnostic.Message.Contains(TEXT("Could not read WebToUE document"));
		});
	TestNotNull(TEXT("A missing source is recorded in document diagnostics"), MissingSource);
	return true;
}

#endif
