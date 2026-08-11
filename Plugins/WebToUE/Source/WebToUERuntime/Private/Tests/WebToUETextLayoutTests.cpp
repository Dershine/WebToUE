#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Internationalization/StringTableRegistry.h"
#include "Internationalization/Text.h"
#include "SWebToUEView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETextWrappingTest, "WebToUE.Runtime.TextWrapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUETextWrappingTest::RunTest(const FString& Parameters)
{
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	const FString Text = TEXT("A localized paragraph needs enough words to wrap across several lines in a narrow menu panel.");
	const FVector2f Wide = View->MeasureTextForTesting(Text, 1000.0f, true);
	const FVector2f Narrow = View->MeasureTextForTesting(Text, 120.0f, true);
	const FVector2f NoWrap = View->MeasureTextForTesting(Text, 120.0f, false);

	TestTrue(TEXT("Narrow text wraps to a greater height"), Narrow.Y > Wide.Y);
	TestTrue(TEXT("Wrapping reduces the desired width"), Narrow.X < NoWrap.X);
	TestTrue(TEXT("Nowrap keeps the single-line height"), FMath::IsNearlyEqual(NoWrap.Y, Wide.Y, 1.0f));
	TestTrue(TEXT("Nowrap text can exceed the available width"), NoWrap.X > 120.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUELocalizedRichTextTest, "WebToUE.Runtime.LocalizedRichText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUELocalizedRichTextTest::RunTest(const FString& Parameters)
{
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	FWebToUENode Node;
	Node.Type = EWebToUENodeType::Text;
	Node.Text = TEXT("Fallback");
	Node.LocalizedText = FText::ChangeKey(TEXT("WebToUE_Test"), TEXT("Greeting"), FText::FromString(TEXT("Hello")));
	Node.bHasLocalizedText = true;
	const FText ResolvedText = View->GetDisplayTextForTesting(Node);
	TestEqual(TEXT("FText namespace survives the runtime node"), FTextInspector::GetNamespace(ResolvedText).Get(TEXT("")), TEXT("WebToUE_Test"));
	TestEqual(TEXT("FText key survives the runtime node"), FTextInspector::GetKey(ResolvedText).Get(TEXT("")), TEXT("Greeting"));

	const FName TestTableId(TEXT("/WebToUETests/ST_Runtime.ST_Runtime"));
	FStringTableRegistry::Get().Internal_NewLocTable(TestTableId, TEXT("WebToUETests"));
	FStringTableRegistry::Get().Internal_SetLocTableEntry(TestTableId, TEXT("Menu.Start"), TEXT("Start"));
	Node.LocalizedText = FText::FromStringTable(TestTableId, TEXT("Menu.Start"));
	const FText TableText = View->GetDisplayTextForTesting(Node);
	FName TableId;
	FString TableKey;
	TestTrue(TEXT("String Table history survives the runtime node"), FTextInspector::GetTableIdAndKey(TableText, TableId, TableKey));
	TestEqual(TEXT("String Table id is preserved"), TableId, TestTableId);
	TestEqual(TEXT("String Table key is preserved"), TableKey, TEXT("Menu.Start"));
	FStringTableRegistry::Get().UnregisterStringTable(TestTableId);

	const FString Markup = TEXT("Choose <strong>Start</> or <em>Return</> to continue through this localized message.");
	const FVector2f Wide = View->MeasureRichTextForTesting(Markup, 1000.0f, true);
	const FVector2f Narrow = View->MeasureRichTextForTesting(Markup, 130.0f, true);
	const FVector2f MarkupAsPlainText = View->MeasureTextForTesting(Markup, 1000.0f, false);
	TestTrue(TEXT("Rich text wraps under a narrow constraint"), Narrow.Y > Wide.Y);
	TestTrue(TEXT("Markup tokens are not measured as visible text"), Wide.X < MarkupAsPlainText.X);
	return true;
}

#endif
