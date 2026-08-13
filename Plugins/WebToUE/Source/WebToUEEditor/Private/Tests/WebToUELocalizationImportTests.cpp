#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WebToUECoreTypes.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUELocalizationImportTest, "WebToUE.Editor.LocalizationImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUELocalizationImportTest::RunTest(const FString& Parameters)
{
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(TestDirectory, TEXT("LocalizedText.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());

	const auto ImportHtml = [&](const FString& Html)
	{
		return FFileHelper::SaveStringToFile(Html, *TestFilename) &&
			UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, true);
	};
	TestTrue(TEXT("Initial localized document imports"), ImportHtml(
		TEXT("<body><p id='greeting'>Hello</p><p data-ue-loc-namespace='GameUI' data-ue-loc-key='Menu.Start'>Start</p></body>")));

	TArray<const FWebToUECompiledNode*> InitialTextNodes;
	for (const FWebToUECompiledNode& Node : Document->GetCompiledNodes())
	{
		if (Node.Type == static_cast<uint8>(EWebToUENodeType::Text)) InitialTextNodes.Add(&Node);
	}
	TestEqual(TEXT("Two localized text nodes are compiled"), InitialTextNodes.Num(), 2);
	if (InitialTextNodes.Num() != 2) return false;
	const FString AutomaticKey = InitialTextNodes[0]->LocalizationKey;
	const FString DocumentNamespace = InitialTextNodes[0]->LocalizationNamespace;
	TestFalse(TEXT("Document localization namespace is persistent"), DocumentNamespace.IsEmpty());
	TestFalse(TEXT("Automatic localization key is generated"), AutomaticKey.IsEmpty());
	TestEqual(TEXT("Explicit namespace is honored"), InitialTextNodes[1]->LocalizationNamespace, TEXT("GameUI"));
	TestEqual(TEXT("Explicit key is honored"), InitialTextNodes[1]->LocalizationKey, TEXT("Menu.Start"));

	TestTrue(TEXT("Localized document reimports after source text changes"), ImportHtml(
		TEXT("<body><p id='greeting'>Hello again</p><p data-ue-loc-namespace='GameUI' data-ue-loc-key='Menu.Start'>Begin</p></body>")));
	TArray<const FWebToUECompiledNode*> ReimportedTextNodes;
	for (const FWebToUECompiledNode& Node : Document->GetCompiledNodes())
	{
		if (Node.Type == static_cast<uint8>(EWebToUENodeType::Text)) ReimportedTextNodes.Add(&Node);
	}
	TestEqual(TEXT("Reimport preserves the text node count"), ReimportedTextNodes.Num(), 2);
	if (ReimportedTextNodes.Num() == 2)
	{
		TestEqual(TEXT("Automatic key survives reimport"), ReimportedTextNodes[0]->LocalizationKey, AutomaticKey);
		TestEqual(TEXT("Document namespace survives reimport"), ReimportedTextNodes[0]->LocalizationNamespace, DocumentNamespace);
		TestEqual(TEXT("Updated source is stored in FText"), ReimportedTextNodes[0]->LocalizedText.ToString(), TEXT("Hello again"));
	}

	const FName TestTableId(TEXT("/WebToUETests/ST_Localization.ST_Localization"));
	FStringTableRegistry::Get().Internal_NewLocTable(TestTableId, TEXT("WebToUETests"));
	FStringTableRegistry::Get().Internal_SetLocTableEntry(TestTableId, TEXT("Menu.Start"), TEXT("Continue"));
	TestTrue(TEXT("String Table document imports"), ImportHtml(
		TEXT("<body><p data-ue-string-table='/WebToUETests/ST_Localization.ST_Localization' data-ue-string-key='Menu.Start'>Fallback</p></body>")));
	const FWebToUECompiledNode* TableTextNode = Document->GetCompiledNodes().FindByPredicate([](const FWebToUECompiledNode& Node)
	{
		return Node.Type == static_cast<uint8>(EWebToUENodeType::Text);
	});
	TestNotNull(TEXT("String Table text node is compiled"), TableTextNode);
	if (TableTextNode)
	{
		FName TableId;
		FString TableKey;
		TestTrue(TEXT("Compiled FText retains String Table history"),
			FTextInspector::GetTableIdAndKey(TableTextNode->LocalizedText, TableId, TableKey));
		TestEqual(TEXT("Compiled String Table id is retained"), TableId, TestTableId);
		TestEqual(TEXT("Compiled String Table key is retained"), TableKey, TEXT("Menu.Start"));
	}
	const int32 LastGoodNodeCount = Document->GetCompiledNodes().Num();
	const int32 LastGoodRuleCount = Document->GetCompiledRules().Num();
	const int32 LastGoodRootNodeIndex = Document->GetRootNodeIndex();
	const FString LastGoodNamespace = Document->GetLocalizationNamespace();
	const auto CountStringTables = [&Document]()
	{
		int32 Count = 0;
		for (const FWebToUECompiledResource& Resource : Document->GetResourceManifest())
		{
			Count += Resource.Kind == EWebToUEResourceKind::StringTable ? 1 : 0;
		}
		return Count;
	};
	const int32 LastGoodStringTableCount = CountStringTables();
	AddExpectedError(TEXT("data-ue-string-table and data-ue-string-key must be specified together"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Invalid reimport reports failure"), ImportHtml(
		TEXT("<body><p data-ue-string-table='/WebToUETests/ST_Localization.ST_Localization'>Broken</p></body>")));
	TestTrue(TEXT("Invalid reimport exposes an error diagnostic"), Document->HasCompileErrors());
	TestEqual(TEXT("Invalid reimport preserves last-good compiled nodes"),
		Document->GetCompiledNodes().Num(), LastGoodNodeCount);
	TestEqual(TEXT("Invalid reimport preserves last-good compiled rules"),
		Document->GetCompiledRules().Num(), LastGoodRuleCount);
	TestEqual(TEXT("Invalid reimport preserves the last-good root"),
		Document->GetRootNodeIndex(), LastGoodRootNodeIndex);
	TestEqual(TEXT("Invalid reimport preserves the document namespace"),
		Document->GetLocalizationNamespace(), LastGoodNamespace);
	TestEqual(TEXT("Invalid reimport preserves compiled String Table dependencies"),
		CountStringTables(), LastGoodStringTableCount);
	const FWebToUECompiledNode* PreservedTextNode = Document->GetCompiledNodes().FindByPredicate(
		[](const FWebToUECompiledNode& Node)
		{
			return Node.Type == static_cast<uint8>(EWebToUENodeType::Text);
		});
	TestNotNull(TEXT("Invalid reimport retains the last-good text node"), PreservedTextNode);
	if (PreservedTextNode)
	{
		FName PreservedTableId;
		FString PreservedTableKey;
		TestTrue(TEXT("Invalid reimport retains last-good String Table text history"),
			FTextInspector::GetTableIdAndKey(PreservedTextNode->LocalizedText, PreservedTableId, PreservedTableKey));
		TestEqual(TEXT("Invalid reimport retains the last-good String Table id"), PreservedTableId, TestTableId);
		TestEqual(TEXT("Invalid reimport retains the last-good String Table key"), PreservedTableKey, TEXT("Menu.Start"));
	}

	FStringTableRegistry::Get().UnregisterStringTable(TestTableId);
	IFileManager::Get().Delete(*TestFilename, false, true);
	return true;
}

#endif
