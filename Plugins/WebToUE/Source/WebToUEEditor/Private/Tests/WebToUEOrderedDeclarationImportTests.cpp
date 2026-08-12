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
	const FString Html = TEXT("<body><style>#target { color: #ff0000; width: 100px; color: invalid; color: #00ff00; width: 120px; }</style><div id='target'></div><div id='inline' style='opacity: 0.5; opacity: 0.75'></div></body>");
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
			TestEqual(TEXT("The compiled IR stores the first property ID"), Declarations[0].Property, EWebToUECssProperty::Color);
			TestEqual(TEXT("The compiled IR stores the first typed color"), Declarations[0].TypedValue.Color, FLinearColor::Red);
			TestEqual(TEXT("The compiled IR retains the interleaved property ID"), Declarations[1].Property, EWebToUECssProperty::Width);
			TestEqual(TEXT("The compiled IR retains the later typed color"), Declarations[2].TypedValue.Color, FLinearColor::Green);
			TestEqual(TEXT("The compiled IR retains the final typed length"), Declarations[3].TypedValue.Length.Value, 120.0f);
			TestTrue(TEXT("Version 4 declarations omit legacy names"), Declarations[0].Name.IsEmpty());
			TestTrue(TEXT("Version 4 declarations omit legacy values"), Declarations[0].Value.IsEmpty());
		}
	}
	const FWebToUECompiledNode* InlineNode = Document->GetCompiledNodes().FindByPredicate(
		[](const FWebToUECompiledNode& Node)
		{
			return Node.Attributes.ContainsByPredicate([](const FWebToUECompiledAttribute& Attribute)
			{
				return Attribute.Name == TEXT("id") && Attribute.Value == TEXT("inline");
			});
		});
	TestNotNull(TEXT("The inline-style node is compiled"), InlineNode);
	if (InlineNode)
	{
		TestEqual(TEXT("The compiled IR retains ordered inline declarations"), InlineNode->InlineStyleDeclarations.Num(), 2);
		if (InlineNode->InlineStyleDeclarations.Num() == 2)
		{
			TestEqual(TEXT("Inline IR stores the opacity property ID"),
				InlineNode->InlineStyleDeclarations[0].Property, EWebToUECssProperty::Opacity);
			TestEqual(TEXT("Inline IR stores the final typed number"),
				InlineNode->InlineStyleDeclarations[1].TypedValue.Number, 0.75f);
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
