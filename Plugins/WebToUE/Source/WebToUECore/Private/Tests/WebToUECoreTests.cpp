#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WebToUECompiler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEHtmlCssTest, "WebToUE.Core.HtmlCss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEHtmlCssTest::RunTest(const FString& Parameters)
{
	const FString Html = TEXT(R"(
		<html><head><style>#go { color: #ff0000; } #go:hover { color: #00ff00; }</style></head>
		<body><div class="panel"><button id="go">Start &amp; Play</button></div></body></html>)");
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(Html);
	TestFalse(TEXT("Valid document has no errors"), Document->HasErrors());
	TestTrue(TEXT("Body is selected as the render root"), Document->Root && Document->Root->Tag == TEXT("body"));

	FWebToUENode* Button = nullptr;
	Document->ForEachNode([&Button](FWebToUENode& Node)
	{
		if (Node.Tag == TEXT("button")) Button = &Node;
	});
	TestNotNull(TEXT("Button parsed"), Button);
	if (Button)
	{
		TestEqual(TEXT("ID selector wins"), Button->Style.Color, FLinearColor::Red);
		Button->StateFlags |= EWebToUEPseudoState::Hover;
		FWebToUEStyleResolver::Resolve(*Document);
		TestEqual(TEXT("Higher-specificity hover selector wins"), Button->Style.Color, FLinearColor(0, 1, 0, 1));
		TestTrue(TEXT("Entity decoded"), Button->Children.Num() > 0 && Button->Children[0]->Text == TEXT("Start & Play"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUELayoutTest, "WebToUE.Core.FlexLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUELayoutTest::RunTest(const FString& Parameters)
{
	const FString Html = TEXT("<body><div id='row'><span>A</span><span>B</span></div></body>");
	const FString Css = TEXT("#row { width: 100%; height: 40px; flex-direction: row; gap: 10px; } span { width: 20px; height: 20px; }");
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(Html, Css);
	FWebToUELayoutEngine::Layout(*Document, FVector2f(200, 100), [](const FWebToUENode&) { return FVector2f(8, 16); });
	FWebToUENode* Row = nullptr;
	Document->ForEachNode([&Row](FWebToUENode& Node) { if (Node.GetAttribute(TEXT("id")) == TEXT("row")) Row = &Node; });
	TestNotNull(TEXT("Row exists"), Row);
	if (Row && Row->Children.Num() == 2)
	{
		TestEqual(TEXT("Percentage width resolved"), Row->Size.X, 200.0f);
		TestEqual(TEXT("Gap applied"), Row->Children[1]->Position.X - Row->Children[0]->Position.X, 30.0f);
	}
	return true;
}

#endif
