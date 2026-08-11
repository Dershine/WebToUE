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
	FWebToUELayoutEngine::Layout(*Document, FVector2f(200, 100),
		[](const FWebToUENode&, const FWebToUELayoutEngine::FMeasureConstraints&) { return FVector2f(8, 16); });
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEConstrainedMeasureTest, "WebToUE.Core.ConstrainedMeasure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEConstrainedMeasureTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><p id='copy'>A long line of text that needs wrapping.</p></body>"),
		TEXT("#copy { width: 80px; }"));
	FWebToUELayoutEngine::FMeasureConstraints TextConstraints;
	bool bMeasuredText = false;
	FWebToUELayoutEngine::Layout(*Document, FVector2f(200, 100),
		[&](const FWebToUENode& Node, const FWebToUELayoutEngine::FMeasureConstraints& Constraints)
		{
			if (Node.Type == EWebToUENodeType::Text)
			{
				bMeasuredText = true;
				TextConstraints = Constraints;
				return FVector2f(Constraints.Width, 40.0f);
			}
			return FVector2f::ZeroVector;
		});

	TestTrue(TEXT("Text leaf is measured through Yoga"), bMeasuredText);
	TestTrue(TEXT("Text measurement receives a finite width constraint"),
		TextConstraints.WidthMode != FWebToUELayoutEngine::EMeasureMode::Undefined);
	TestEqual(TEXT("Text measurement receives the parent width"), TextConstraints.Width, 80.0f);
	FWebToUENode* TextNode = nullptr;
	Document->ForEachNode([&TextNode](FWebToUENode& Node)
	{
		if (Node.Type == EWebToUENodeType::Text) TextNode = &Node;
	});
	TestNotNull(TEXT("Text node exists"), TextNode);
	if (TextNode) TestEqual(TEXT("Measured wrapped height reaches Yoga layout"), TextNode->Size.Y, 40.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERichTextCompileTest, "WebToUE.Core.RichTextCompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUERichTextCompileTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><p id='message'>Press <strong>Start</strong> or <em>go back</em>.<br><u>Choose wisely.</u></p></body>"),
		FString(), TEXT("RichText.html"));
	FWebToUENode* Paragraph = nullptr;
	Document->ForEachNode([&Paragraph](FWebToUENode& Node)
	{
		if (Node.GetAttribute(TEXT("id")) == TEXT("message")) Paragraph = &Node;
	});
	TestNotNull(TEXT("Rich paragraph exists"), Paragraph);
	if (Paragraph)
	{
		TestEqual(TEXT("Inline content collapses into one Yoga text leaf"), Paragraph->Children.Num(), 1);
		if (Paragraph->Children.Num() == 1)
		{
			const FWebToUENode& TextNode = *Paragraph->Children[0];
			TestTrue(TEXT("Collapsed leaf uses rich text layout"), TextNode.bRichText);
			TestTrue(TEXT("Whitespace around inline runs is preserved"), TextNode.Text.StartsWith(TEXT("Press <strong>Start</> or <em>go back</>.")));
			TestTrue(TEXT("Bold run is preserved"), TextNode.Text.Contains(TEXT("<strong>Start</>")));
			TestTrue(TEXT("Italic run is preserved"), TextNode.Text.Contains(TEXT("<em>go back</>")));
			TestTrue(TEXT("Line breaks are preserved"), TextNode.Text.Contains(TEXT("\n")));
			TestTrue(TEXT("Underline run is preserved"), TextNode.Text.Contains(TEXT("<underline>Choose wisely.</>")));
		}
	}

	const TSharedRef<FWebToUEDocument> InvalidStringTable = FWebToUECompiler::Compile(
		TEXT("<body><p data-ue-string-table='/Game/UI/ST_UI.ST_UI'>Missing key</p></body>"), FString(), TEXT("Invalid.html"));
	TestTrue(TEXT("Incomplete String Table identity is a compile error"), InvalidStringTable->HasErrors());

	const TSharedRef<FWebToUEDocument> LineBreakDocument = FWebToUECompiler::Compile(
		TEXT("<body><p id='lines'>First<br>Second</p></body>"), FString(), TEXT("LineBreak.html"));
	FWebToUENode* Lines = nullptr;
	LineBreakDocument->ForEachNode([&Lines](FWebToUENode& Node)
	{
		if (Node.GetAttribute(TEXT("id")) == TEXT("lines")) Lines = &Node;
	});
	TestNotNull(TEXT("Line-break-only rich text exists"), Lines);
	if (Lines && Lines->Children.Num() == 1)
	{
		TestTrue(TEXT("br alone enables the rich text leaf"), Lines->Children[0]->bRichText);
		TestEqual(TEXT("br compiles to one hard line break"), Lines->Children[0]->Text, TEXT("First\nSecond"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEScrollLayoutTest, "WebToUE.Core.ScrollLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEScrollLayoutTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='scroll'><div class='row'></div><div class='row'></div><div class='row'></div></div></body>"),
		TEXT("#scroll { width: 100px; height: 100px; overflow: auto; } .row { width: 100px; height: 80px; flex-shrink: 0; }"));
	FWebToUELayoutEngine::Layout(*Document, FVector2f(200, 200),
		[](const FWebToUENode&, const FWebToUELayoutEngine::FMeasureConstraints&) { return FVector2f::ZeroVector; });

	FWebToUENode* Scroll = nullptr;
	Document->ForEachNode([&Scroll](FWebToUENode& Node)
	{
		if (Node.GetAttribute(TEXT("id")) == TEXT("scroll")) Scroll = &Node;
	});
	TestNotNull(TEXT("Scroll container exists"), Scroll);
	if (Scroll)
	{
		TestEqual(TEXT("Overflow auto resolves to a scroll container"), Scroll->Style.Overflow, EWebToUEOverflow::Auto);
		TestTrue(TEXT("Overflowing children produce a vertical scroll range"),
			FMath::IsNearlyEqual(Scroll->MaxScrollOffset.Y, 140.0f, 0.1f));
		Scroll->ScrollOffset.Y = 1000.0f;
		FWebToUELayoutEngine::Layout(*Document, FVector2f(200, 200),
			[](const FWebToUENode&, const FWebToUELayoutEngine::FMeasureConstraints&) { return FVector2f::ZeroVector; });
		TestTrue(TEXT("Relayout clamps an existing scroll offset"),
			FMath::IsNearlyEqual(Scroll->ScrollOffset.Y, Scroll->MaxScrollOffset.Y, 0.1f));
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
