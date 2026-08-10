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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUECssDiagnosticsTest, "WebToUE.Core.CssDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUECssDiagnosticsTest::RunTest(const FString& Parameters)
{
	const FString Html = TEXT("<body>\n<style>\n.panel { padding: invalid; }\n</style>\n"
		"<div class='panel' style='height: invalid'>Panel</div>\n</body>");
	TArray<FWebToUEStyleSheetSource> StyleSheets;
	StyleSheets.Add({
		TEXT(".panel {\n  width: invalid;\n  made-up-property: 10px;\n}\n.panel:first-child { color: blue; }"),
		TEXT("Styles/Panel.css"), 1, 1
	});
	StyleSheets.Add({ TEXT(".panel { color: blue; }"), TEXT("Styles/Theme.css"), 1, 1 });

	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		Html, StyleSheets, TEXT("UI/Panel.html"));
	TestFalse(TEXT("CSS warnings do not invalidate the document"), Document->HasErrors());

	auto FindDiagnostic = [&Document](const FString& MessagePart) -> const FWebToUEDiagnostic*
	{
		return Document->Diagnostics.FindByPredicate([&MessagePart](const FWebToUEDiagnostic& Diagnostic)
		{
			return Diagnostic.Message.Contains(MessagePart);
		});
	};

	const FWebToUEDiagnostic* InvalidWidth = FindDiagnostic(TEXT("invalid value 'invalid' for CSS property 'width'"));
	TestNotNull(TEXT("Invalid external value is diagnosed"), InvalidWidth);
	if (InvalidWidth)
	{
		TestEqual(TEXT("External diagnostic preserves source file"), InvalidWidth->File, FString(TEXT("Styles/Panel.css")));
		TestEqual(TEXT("External diagnostic has the correct line"), InvalidWidth->Line, 2);
		TestEqual(TEXT("External diagnostic has the correct column"), InvalidWidth->Column, 3);
	}

	const FWebToUEDiagnostic* UnknownProperty = FindDiagnostic(TEXT("unsupported CSS property 'made-up-property'"));
	TestNotNull(TEXT("Unknown property is diagnosed"), UnknownProperty);
	if (UnknownProperty)
	{
		TestEqual(TEXT("Unknown property has the correct line"), UnknownProperty->Line, 3);
		TestEqual(TEXT("Unknown property has the correct column"), UnknownProperty->Column, 3);
	}

	const FWebToUEDiagnostic* UnsupportedSelector = FindDiagnostic(TEXT("unsupported selector '.panel:first-child'"));
	TestNotNull(TEXT("Unsupported selector is diagnosed"), UnsupportedSelector);
	if (UnsupportedSelector)
	{
		TestEqual(TEXT("Selector diagnostic has the correct line"), UnsupportedSelector->Line, 5);
		TestEqual(TEXT("Selector diagnostic has the correct column"), UnsupportedSelector->Column, 1);
	}

	const FWebToUEDiagnostic* InlineValue = FindDiagnostic(TEXT("invalid value 'invalid' for CSS property 'height'"));
	TestNotNull(TEXT("Invalid inline value is diagnosed"), InlineValue);
	if (InlineValue)
	{
		TestEqual(TEXT("Inline diagnostic preserves HTML source"), InlineValue->File, FString(TEXT("UI/Panel.html")));
	}

	const FWebToUEDiagnostic* StyleElementValue = FindDiagnostic(TEXT("invalid value 'invalid' for CSS property 'padding'"));
	TestNotNull(TEXT("Invalid style element value is diagnosed"), StyleElementValue);
	if (StyleElementValue)
	{
		TestEqual(TEXT("Style element diagnostic preserves HTML source"), StyleElementValue->File, FString(TEXT("UI/Panel.html")));
		TestEqual(TEXT("Style element diagnostic has the correct line"), StyleElementValue->Line, 3);
	}

	FWebToUENode* Panel = nullptr;
	Document->ForEachNode([&Panel](FWebToUENode& Node)
	{
		if (Node.HasClass(TEXT("panel"))) Panel = &Node;
	});
	TestNotNull(TEXT("Panel exists"), Panel);
	if (Panel)
	{
		TestEqual(TEXT("Later stylesheet still wins after source separation"), Panel->Style.Color, FLinearColor::Blue);
		TestFalse(TEXT("Invalid inline height is ignored"), Panel->Style.Height.IsDefined());
	}
	return true;
}

#endif
