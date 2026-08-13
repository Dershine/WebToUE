#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WebToUECompiler.h"
#include "WebToUEPerformance.h"
#include "WebToUEStyleProperties.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEHtmlCssTest, "WebToUE.Core.HtmlCss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEOrderedDeclarationsTest, "WebToUE.Core.OrderedDeclarations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETypedPropertiesTest, "WebToUE.Core.TypedProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPropertyMetadataTest, "WebToUE.Core.PropertyMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUESelectorIndexTest, "WebToUE.Core.SelectorIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPseudoInvalidationDependencyTest,
	"WebToUE.Core.PseudoInvalidationDependencies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETypedCascadeTest, "WebToUE.Core.TypedCascade",
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
		TestEqual(TEXT("ID selector wins"), Document->GetComputedStyle(*Button).Color, FLinearColor::Red);
		Document->GetRuntimeNodeState(*Button).PseudoStates |= EWebToUEPseudoState::Hover;
		FWebToUEStyleResolver::Resolve(*Document);
		TestEqual(TEXT("Higher-specificity hover selector wins"),
			Document->GetComputedStyle(*Button).Color, FLinearColor(0, 1, 0, 1));
		TestTrue(TEXT("Entity decoded"), Button->Children.Num() > 0 && Button->Children[0]->Text == TEXT("Start & Play"));
	}
	return true;
}

bool FWebToUESelectorIndexTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><button id='target' class='item primary item'>Label</button></body>"),
		TEXT("#target { color: #ff0000; } .item { background-color: #00ff00; } ")
		TEXT("button { width: 120px; } :hover { opacity: 0.5; } ")
		TEXT("body > .item { height: 40px; } * { z-index: 2; } ")
		TEXT(".missing { margin-left: 99px; }"),
		TEXT("SelectorIndex.html"));
	TestFalse(TEXT("The selector-index document compiles without errors"), Document->HasErrors());

	FWebToUENode* Target = nullptr;
	Document->ForEachNode([&Target](FWebToUENode& Node)
	{
		if (Node.GetAttribute(TEXT("id")) == TEXT("target"))
		{
			Target = &Node;
		}
	});
	TestNotNull(TEXT("The selector-index target exists"), Target);
	if (!Target)
	{
		return false;
	}

	Document->GetRuntimeNodeState(*Target).PseudoStates |= EWebToUEPseudoState::Hover;
	FWebToUEPerformanceSnapshot Snapshot;
	{
		FWebToUEPerformanceCapture Capture;
		FWebToUEStyleResolver::Resolve(*Document);
		Snapshot = Capture.GetSnapshot();
	}

	const FWebToUEComputedStyle& Style = Document->GetComputedStyle(*Target);
	TestEqual(TEXT("The ID candidate applies"), Style.Color, FLinearColor::Red);
	TestEqual(TEXT("The class candidate applies"), Style.BackgroundColor, FLinearColor::Green);
	TestEqual(TEXT("The tag candidate applies"), Style.Width.Value, 120.0f);
	TestEqual(TEXT("The pseudo candidate applies"), Style.Opacity, 0.5f);
	TestEqual(TEXT("The combinator candidate still performs full matching"), Style.Height.Value, 40.0f);
	TestEqual(TEXT("The universal candidate applies"), Style.ZIndex, 2);
	TestEqual(TEXT("A non-candidate rule does not apply"), Style.Margin.Left.Value, 0.0f);

	const uint64 FullScanWork = static_cast<uint64>(Document->Rules.Num()) *
		Snapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits);
	const uint64 CandidateCount = Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorCandidates);
	TestTrue(TEXT("The selector index produces candidates"), CandidateCount > 0);
	TestTrue(TEXT("The selector index prunes the full node-by-rule scan"), CandidateCount < FullScanWork);
	TestEqual(TEXT("Duplicate class tokens do not revisit the same class bucket"), CandidateCount, uint64(7));
	TestEqual(TEXT("Every candidate is evaluated exactly once"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorEvaluations), CandidateCount);
	return true;
}

bool FWebToUEPseudoInvalidationDependencyTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><section id='menu' class='menu'><button id='item' class='item'>Item</button></section></body>"),
		TEXT(".menu:hover .item { color: red; } .item:active { opacity: 0.5; } ")
		TEXT(".menu:focus button { border-color: blue; }"),
		TEXT("PseudoInvalidationDependencies.html"));
	TestFalse(TEXT("Pseudo invalidation dependencies compile without errors"), Document->HasErrors());
	const FWebToUEDiagnostic* BroadDiagnostic = Document->Diagnostics.FindByPredicate(
		[](const FWebToUEDiagnostic& Diagnostic)
		{
			return Diagnostic.Severity == EWebToUEDiagnosticSeverity::Warning &&
				Diagnostic.Message.Contains(TEXT("broad invalidation target"));
		});
	TestNotNull(TEXT("A broad ancestor-pseudo target is diagnosed"), BroadDiagnostic);

	FWebToUERuntimeStyleTemplate StyleTemplate;
	StyleTemplate.Rules = Document->Rules;
	StyleTemplate.SelectorIndex.Initialize(StyleTemplate.Rules);
	StyleTemplate.CompilePseudoInvalidationDependencies();
	TestEqual(TEXT("Every pseudo reason segment compiles one dependency"),
		StyleTemplate.PseudoInvalidationDependencies.Num(), 3);
	TestTrue(TEXT("The ancestor hover dependency retains its reason segment"),
		StyleTemplate.PseudoInvalidationDependencies.ContainsByPredicate(
			[](const FWebToUEPseudoInvalidationDependency& Dependency)
			{
				return Dependency.ReasonState == EWebToUEPseudoState::Hover &&
					Dependency.RuleIndex == 0 && Dependency.ReasonSegmentIndex == 0;
			}));

	const FWebToUESelectorSegment& ItemTarget = StyleTemplate.Rules[0].Selector.Last();
	TArray<FWebToUEInstanceHandle> Targets;
	const int32 CandidateCount = Document->ForEachPotentialSelectorTarget(ItemTarget,
		[&Targets](FWebToUEInstanceHandle Handle)
		{
			Targets.Add(Handle);
		});
	TestEqual(TEXT("The per-instance target index visits only the matching class bucket"),
		CandidateCount, 1);
	TestEqual(TEXT("The indexed target resolves to the intended node id"),
		Targets.Num() == 1 && Document->ResolveNode(Targets[0])
			? Document->ResolveNode(Targets[0])->GetAttribute(TEXT("id")) : FString(),
		FString(TEXT("item")));
	return true;
}

bool FWebToUETypedCascadeTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body>")
		TEXT("<div id='components'></div>")
		TEXT("<div id='specificity' class='specificity'></div>")
		TEXT("<div id='source-order' class='source-order'></div>")
		TEXT("<div id='inline' class='inline' style='margin-left: 31px; margin: 32px; color: invalid; color: blue'></div>")
		TEXT("</body>"),
		TEXT("#components { ")
		TEXT("margin-left: 1px; margin: 2px; padding-left: 3px; padding: 4px; ")
		TEXT("row-gap: 5px; gap: 6px; flex-grow: 7; flex: 8 9 10px; ")
		TEXT("background-color: red; background: green; ")
		TEXT("border-width: 11px; border-color: red; border: 12px solid blue; }")
		TEXT("#specificity { margin: 21px; } .specificity { margin-left: 22px; }")
		TEXT(".source-order { padding: 23px; } .source-order { padding-left: 24px; }")
		TEXT(".inline { margin-left: 30px; color: red; }"),
		TEXT("TypedCascade.html"));
	TestFalse(TEXT("The typed-cascade document compiles without errors"), Document->HasErrors());

	TMap<FString, FWebToUENode*> Nodes;
	Document->ForEachNode([&Nodes](FWebToUENode& Node)
	{
		const FString Id = Node.GetAttribute(TEXT("id"));
		if (!Id.IsEmpty()) Nodes.Add(Id, &Node);
	});
	FWebToUENode* Components = Nodes.FindRef(TEXT("components"));
	FWebToUENode* Specificity = Nodes.FindRef(TEXT("specificity"));
	FWebToUENode* SourceOrder = Nodes.FindRef(TEXT("source-order"));
	FWebToUENode* Inline = Nodes.FindRef(TEXT("inline"));
	TestNotNull(TEXT("The shorthand component target exists"), Components);
	TestNotNull(TEXT("The specificity target exists"), Specificity);
	TestNotNull(TEXT("The source-order target exists"), SourceOrder);
	TestNotNull(TEXT("The inline target exists"), Inline);
	if (!Components || !Specificity || !SourceOrder || !Inline) return false;

	const FWebToUEComputedStyle& ComponentStyle = Document->GetComputedStyle(*Components);
	TestEqual(TEXT("A later margin shorthand wins the left component"),
		ComponentStyle.Margin.Left.Value, 2.0f);
	TestEqual(TEXT("A later padding shorthand wins the left component"),
		ComponentStyle.Padding.Left.Value, 4.0f);
	TestEqual(TEXT("A later gap shorthand wins the row component"), ComponentStyle.RowGap, 6.0f);
	TestEqual(TEXT("A later flex shorthand wins the grow component"), ComponentStyle.FlexGrow, 8.0f);
	TestEqual(TEXT("The flex shorthand supplies shrink"), ComponentStyle.FlexShrink, 9.0f);
	TestEqual(TEXT("The flex shorthand supplies basis"), ComponentStyle.FlexBasis.Value, 10.0f);
	TestEqual(TEXT("A later background shorthand wins its color component"),
		ComponentStyle.BackgroundColor, FLinearColor::Green);
	TestEqual(TEXT("A later border shorthand wins its width component"),
		ComponentStyle.BorderWidth, 12.0f);
	TestEqual(TEXT("A later border shorthand wins its color component"),
		ComponentStyle.BorderColor, FLinearColor::Blue);

	TestEqual(TEXT("Higher specificity shorthand beats lower specificity longhand"),
		Document->GetComputedStyle(*Specificity).Margin.Left.Value, 21.0f);
	TestEqual(TEXT("Later source order longhand beats an earlier shorthand"),
		Document->GetComputedStyle(*SourceOrder).Padding.Left.Value, 24.0f);
	TestEqual(TEXT("Inline shorthand beats stylesheet longhand"),
		Document->GetComputedStyle(*Inline).Margin.Left.Value, 32.0f);
	TestEqual(TEXT("The last valid inline declaration wins after an invalid declaration"),
		Document->GetComputedStyle(*Inline).Color, FLinearColor::Blue);
	return true;
}

bool FWebToUEOrderedDeclarationsTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='target'></div><div id='inline' style='color: #ff0000; color: invalid; color: #0000ff'></div></body>"),
		TEXT("#target { color: #ff0000; width: 100px; color: invalid; color: #00ff00; width: invalid; width: 120px; }"),
		TEXT("OrderedDeclarations.html"));
	TestFalse(TEXT("Invalid declarations remain warnings"), Document->HasErrors());
	TestEqual(TEXT("The stylesheet produces one rule"), Document->Rules.Num(), 1);
	if (Document->Rules.Num() == 1)
	{
		const TArray<FWebToUEStyleDeclaration>& Declarations = Document->Rules[0].Declarations;
		TestEqual(TEXT("Only valid declarations are retained"), Declarations.Num(), 4);
		if (Declarations.Num() == 4)
		{
			TestEqual(TEXT("The first declaration stores a stable property ID"),
				Declarations[0].Property, EWebToUECssProperty::Color);
			TestEqual(TEXT("The first declaration stores a typed color"),
				Declarations[0].TypedValue.Type, EWebToUEStyleValueType::Color);
			TestEqual(TEXT("The first declaration keeps its source position"), Declarations[0].Name, FString(TEXT("color")));
			TestEqual(TEXT("The first declaration keeps its value"), Declarations[0].Value, FString(TEXT("#ff0000")));
			TestEqual(TEXT("An interleaved property keeps its source position"), Declarations[1].Name, FString(TEXT("width")));
			TestEqual(TEXT("A later valid duplicate remains in the sequence"), Declarations[2].Value, FString(TEXT("#00ff00")));
			TestEqual(TEXT("The last valid width remains in the sequence"), Declarations[3].Value, FString(TEXT("120px")));
		}
	}

	FWebToUENode* Target = nullptr;
	FWebToUENode* Inline = nullptr;
	Document->ForEachNode([&](FWebToUENode& Node)
	{
		const FString Id = Node.GetAttribute(TEXT("id"));
		if (Id == TEXT("target")) Target = &Node;
		else if (Id == TEXT("inline")) Inline = &Node;
	});
	TestNotNull(TEXT("The stylesheet target exists"), Target);
	TestNotNull(TEXT("The inline-style target exists"), Inline);
	if (Target)
	{
		TestEqual(TEXT("The last valid repeated color wins"),
			Document->GetComputedStyle(*Target).Color, FLinearColor::Green);
		TestEqual(TEXT("The last valid repeated width keeps its unit"),
			Document->GetComputedStyle(*Target).Width.Unit, EWebToUEUnit::Pixels);
		TestEqual(TEXT("The last valid repeated width wins"),
			Document->GetComputedStyle(*Target).Width.Value, 120.0f);
	}
	if (Inline)
	{
		TestEqual(TEXT("Inline style compiles both valid declarations once"),
			Inline->InlineStyleDeclarations.Num(), 2);
		if (Inline->InlineStyleDeclarations.Num() == 2)
		{
			TestEqual(TEXT("Inline style stores a property ID"),
				Inline->InlineStyleDeclarations[0].Property, EWebToUECssProperty::Color);
			TestEqual(TEXT("Inline style stores a typed color"),
				Inline->InlineStyleDeclarations[1].TypedValue.Type, EWebToUEStyleValueType::Color);
		}
		TestEqual(TEXT("Inline style retains both valid duplicate declarations"),
			Inline->GetAttribute(TEXT("style")), FString(TEXT("color: #ff0000; color: #0000ff")));
		TestEqual(TEXT("The last valid inline declaration wins"),
			Document->GetComputedStyle(*Inline).Color, FLinearColor::Blue);
	}
	return true;
}

bool FWebToUETypedPropertiesTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		const TCHAR* Name;
		const TCHAR* Value;
		EWebToUECssProperty Property;
		EWebToUEStyleValueType Type;
	};
	const FCase Cases[] = {
		{TEXT("display"), TEXT("flex"), EWebToUECssProperty::Display, EWebToUEStyleValueType::Keyword},
		{TEXT("position"), TEXT("absolute"), EWebToUECssProperty::Position, EWebToUEStyleValueType::Keyword},
		{TEXT("visibility"), TEXT("hidden"), EWebToUECssProperty::Visibility, EWebToUEStyleValueType::Keyword},
		{TEXT("overflow"), TEXT("scroll"), EWebToUECssProperty::Overflow, EWebToUEStyleValueType::Keyword},
		{TEXT("width"), TEXT("25%"), EWebToUECssProperty::Width, EWebToUEStyleValueType::Length},
		{TEXT("height"), TEXT("10px"), EWebToUECssProperty::Height, EWebToUEStyleValueType::Length},
		{TEXT("min-width"), TEXT("1px"), EWebToUECssProperty::MinWidth, EWebToUEStyleValueType::Length},
		{TEXT("min-height"), TEXT("1px"), EWebToUECssProperty::MinHeight, EWebToUEStyleValueType::Length},
		{TEXT("max-width"), TEXT("auto"), EWebToUECssProperty::MaxWidth, EWebToUEStyleValueType::Length},
		{TEXT("max-height"), TEXT("100%"), EWebToUECssProperty::MaxHeight, EWebToUEStyleValueType::Length},
		{TEXT("left"), TEXT("0"), EWebToUECssProperty::Left, EWebToUEStyleValueType::Length},
		{TEXT("top"), TEXT("1px"), EWebToUECssProperty::Top, EWebToUEStyleValueType::Length},
		{TEXT("right"), TEXT("2px"), EWebToUECssProperty::Right, EWebToUEStyleValueType::Length},
		{TEXT("bottom"), TEXT("3px"), EWebToUECssProperty::Bottom, EWebToUEStyleValueType::Length},
		{TEXT("margin"), TEXT("1px auto"), EWebToUECssProperty::Margin, EWebToUEStyleValueType::Edges},
		{TEXT("margin-left"), TEXT("auto"), EWebToUECssProperty::MarginLeft, EWebToUEStyleValueType::Length},
		{TEXT("margin-top"), TEXT("1px"), EWebToUECssProperty::MarginTop, EWebToUEStyleValueType::Length},
		{TEXT("margin-right"), TEXT("2px"), EWebToUECssProperty::MarginRight, EWebToUEStyleValueType::Length},
		{TEXT("margin-bottom"), TEXT("3px"), EWebToUECssProperty::MarginBottom, EWebToUEStyleValueType::Length},
		{TEXT("padding"), TEXT("1px 2px"), EWebToUECssProperty::Padding, EWebToUEStyleValueType::Edges},
		{TEXT("padding-left"), TEXT("1px"), EWebToUECssProperty::PaddingLeft, EWebToUEStyleValueType::Length},
		{TEXT("padding-top"), TEXT("1px"), EWebToUECssProperty::PaddingTop, EWebToUEStyleValueType::Length},
		{TEXT("padding-right"), TEXT("1px"), EWebToUECssProperty::PaddingRight, EWebToUEStyleValueType::Length},
		{TEXT("padding-bottom"), TEXT("1px"), EWebToUECssProperty::PaddingBottom, EWebToUEStyleValueType::Length},
		{TEXT("gap"), TEXT("4px"), EWebToUECssProperty::Gap, EWebToUEStyleValueType::Length},
		{TEXT("row-gap"), TEXT("4px"), EWebToUECssProperty::RowGap, EWebToUEStyleValueType::Length},
		{TEXT("column-gap"), TEXT("4px"), EWebToUECssProperty::ColumnGap, EWebToUEStyleValueType::Length},
		{TEXT("flex"), TEXT("1 0 10px"), EWebToUECssProperty::Flex, EWebToUEStyleValueType::Flex},
		{TEXT("flex-direction"), TEXT("row-reverse"), EWebToUECssProperty::FlexDirection, EWebToUEStyleValueType::Keyword},
		{TEXT("flex-wrap"), TEXT("wrap"), EWebToUECssProperty::FlexWrap, EWebToUEStyleValueType::Keyword},
		{TEXT("flex-grow"), TEXT("2"), EWebToUECssProperty::FlexGrow, EWebToUEStyleValueType::Number},
		{TEXT("flex-shrink"), TEXT("0"), EWebToUECssProperty::FlexShrink, EWebToUEStyleValueType::Number},
		{TEXT("flex-basis"), TEXT("20%"), EWebToUECssProperty::FlexBasis, EWebToUEStyleValueType::Length},
		{TEXT("justify-content"), TEXT("space-between"), EWebToUECssProperty::JustifyContent, EWebToUEStyleValueType::Keyword},
		{TEXT("align-items"), TEXT("baseline"), EWebToUECssProperty::AlignItems, EWebToUEStyleValueType::Keyword},
		{TEXT("align-self"), TEXT("auto"), EWebToUECssProperty::AlignSelf, EWebToUEStyleValueType::Keyword},
		{TEXT("color"), TEXT("#12345678"), EWebToUECssProperty::Color, EWebToUEStyleValueType::Color},
		{TEXT("background"), TEXT("transparent"), EWebToUECssProperty::Background, EWebToUEStyleValueType::Color},
		{TEXT("background-color"), TEXT("blue"), EWebToUECssProperty::BackgroundColor, EWebToUEStyleValueType::Color},
		{TEXT("border"), TEXT("1px solid red"), EWebToUECssProperty::Border, EWebToUEStyleValueType::Border},
		{TEXT("border-color"), TEXT("white"), EWebToUECssProperty::BorderColor, EWebToUEStyleValueType::Color},
		{TEXT("border-width"), TEXT("1px"), EWebToUECssProperty::BorderWidth, EWebToUEStyleValueType::Length},
		{TEXT("border-style"), TEXT("none"), EWebToUECssProperty::BorderStyle, EWebToUEStyleValueType::Keyword},
		{TEXT("border-radius"), TEXT("2px"), EWebToUECssProperty::BorderRadius, EWebToUEStyleValueType::Length},
		{TEXT("opacity"), TEXT("0.5"), EWebToUECssProperty::Opacity, EWebToUEStyleValueType::Number},
		{TEXT("font-family"), TEXT("'Inter'"), EWebToUECssProperty::FontFamily, EWebToUEStyleValueType::String},
		{TEXT("font-size"), TEXT("16px"), EWebToUECssProperty::FontSize, EWebToUEStyleValueType::Length},
		{TEXT("font-weight"), TEXT("700"), EWebToUECssProperty::FontWeight, EWebToUEStyleValueType::String},
		{TEXT("text-align"), TEXT("right"), EWebToUECssProperty::TextAlign, EWebToUEStyleValueType::Keyword},
		{TEXT("white-space"), TEXT("nowrap"), EWebToUECssProperty::WhiteSpace, EWebToUEStyleValueType::Keyword},
		{TEXT("object-fit"), TEXT("cover"), EWebToUECssProperty::ObjectFit, EWebToUEStyleValueType::Keyword},
		{TEXT("z-index"), TEXT("7"), EWebToUECssProperty::ZIndex, EWebToUEStyleValueType::Integer}
	};

	TSet<uint8> SeenPropertyIds;
	for (const FCase& Case : Cases)
	{
		FWebToUEStyleDeclaration Declaration;
		TestTrue(*FString::Printf(TEXT("%s parses into a typed declaration"), Case.Name),
			WebToUE::Private::TryParseCssDeclaration(Case.Name, Case.Value, Declaration));
		TestEqual(*FString::Printf(TEXT("%s has the stable property ID"), Case.Name),
			Declaration.Property, Case.Property);
		TestEqual(*FString::Printf(TEXT("%s has the expected value type"), Case.Name),
			Declaration.TypedValue.Type, Case.Type);
		TestEqual(*FString::Printf(TEXT("%s round-trips its canonical name"), Case.Name),
			FString(WebToUE::Private::LexToString(Declaration.Property)), FString(Case.Name));
		TestFalse(*FString::Printf(TEXT("%s has a unique property ID"), Case.Name),
			SeenPropertyIds.Contains(static_cast<uint8>(Declaration.Property)));
		SeenPropertyIds.Add(static_cast<uint8>(Declaration.Property));
	}
	TestEqual(TEXT("Every supported property has one typed parser case"),
		SeenPropertyIds.Num(), static_cast<int32>(UE_ARRAY_COUNT(Cases)));
	FWebToUEStyleDeclaration InvalidDeclaration;
	TestFalse(TEXT("Unsupported properties do not receive an ID"),
		WebToUE::Private::TryParseCssDeclaration(TEXT("made-up-property"), TEXT("1px"), InvalidDeclaration));
	TestFalse(TEXT("Invalid values do not produce typed declarations"),
		WebToUE::Private::TryParseCssDeclaration(TEXT("width"), TEXT("invalid"), InvalidDeclaration));
	return true;
}

bool FWebToUEPropertyMetadataTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Private;
	const TConstArrayView<FWebToUECssPropertyMetadata> Metadata = GetAllCssPropertyMetadata();
	TestEqual(TEXT("All 52 supported properties have metadata"), Metadata.Num(), 52);
	TSet<FString> Names;
	TSet<EWebToUECssProperty> InheritedProperties;
	for (int32 Index = 0; Index < Metadata.Num(); ++Index)
	{
		const FWebToUECssPropertyMetadata& Entry = Metadata[Index];
		const EWebToUECssProperty ExpectedProperty =
			static_cast<EWebToUECssProperty>(Index + 1);
		TestEqual(*FString::Printf(TEXT("Metadata entry %d follows its serialized property ID"), Index),
			Entry.Property, ExpectedProperty);
		TestEqual(*FString::Printf(TEXT("Property %d is directly indexable"), Index),
			&GetCssPropertyMetadata(Entry.Property), &Entry);
		TestFalse(*FString::Printf(TEXT("%s has one canonical name"), Entry.Name),
			Names.Contains(Entry.Name));
		Names.Add(Entry.Name);
		TestTrue(*FString::Printf(TEXT("%s always invalidates computed style"), Entry.Name),
			EnumHasAnyFlags(Entry.Impacts, EWebToUEStyleImpact::Style));
		TestEqual(*FString::Printf(TEXT("%s uses metadata for canonical spelling"), Entry.Name),
			FString(LexToString(Entry.Property)), FString(Entry.Name));
		EWebToUECssProperty RoundTrippedProperty = EWebToUECssProperty::Invalid;
		TestTrue(*FString::Printf(TEXT("%s resolves from its canonical spelling"), Entry.Name),
			TryGetCssProperty(Entry.Name, RoundTrippedProperty));
		TestEqual(*FString::Printf(TEXT("%s round-trips its property ID"), Entry.Name),
			RoundTrippedProperty, Entry.Property);
		if (Entry.bInherited) InheritedProperties.Add(Entry.Property);
	}

	const TSet<EWebToUECssProperty> ExpectedInherited = {
		EWebToUECssProperty::Color,
		EWebToUECssProperty::FontFamily,
		EWebToUECssProperty::FontSize,
		EWebToUECssProperty::FontWeight,
		EWebToUECssProperty::TextAlign,
		EWebToUECssProperty::WhiteSpace
	};
	bool bInheritedPropertiesMatch = InheritedProperties.Num() == ExpectedInherited.Num();
	for (const EWebToUECssProperty Property : ExpectedInherited)
	{
		bInheritedPropertiesMatch &= InheritedProperties.Contains(Property);
	}
	TestTrue(TEXT("Only the six supported inherited properties are marked inherited"),
		bInheritedPropertiesMatch);

	const EWebToUEStyleImpact Paint =
		EWebToUEStyleImpact::Style | EWebToUEStyleImpact::Paint;
	const EWebToUEStyleImpact PaintHitTest = Paint | EWebToUEStyleImpact::HitTest;
	const EWebToUEStyleImpact Layout = PaintHitTest | EWebToUEStyleImpact::Layout;
	const EWebToUEStyleImpact Measure = Layout | EWebToUEStyleImpact::Measure;
	const EWebToUEStyleImpact MeasureResource = Measure | EWebToUEStyleImpact::Resource;
	struct FImpactGroup
	{
		EWebToUEStyleImpact Impact;
		TArray<EWebToUECssProperty> Properties;
	};
	const FImpactGroup ImpactGroups[] = {
		{ Layout, TArray<EWebToUECssProperty>{
			EWebToUECssProperty::Display, EWebToUECssProperty::Position,
			EWebToUECssProperty::Overflow, EWebToUECssProperty::Width,
			EWebToUECssProperty::Height, EWebToUECssProperty::MinWidth,
			EWebToUECssProperty::MinHeight, EWebToUECssProperty::MaxWidth,
			EWebToUECssProperty::MaxHeight, EWebToUECssProperty::Left,
			EWebToUECssProperty::Top, EWebToUECssProperty::Right,
			EWebToUECssProperty::Bottom, EWebToUECssProperty::Margin,
			EWebToUECssProperty::MarginLeft, EWebToUECssProperty::MarginTop,
			EWebToUECssProperty::MarginRight, EWebToUECssProperty::MarginBottom,
			EWebToUECssProperty::Padding, EWebToUECssProperty::PaddingLeft,
			EWebToUECssProperty::PaddingTop, EWebToUECssProperty::PaddingRight,
			EWebToUECssProperty::PaddingBottom, EWebToUECssProperty::Gap,
			EWebToUECssProperty::RowGap, EWebToUECssProperty::ColumnGap,
			EWebToUECssProperty::Flex, EWebToUECssProperty::FlexDirection,
			EWebToUECssProperty::FlexWrap, EWebToUECssProperty::FlexGrow,
			EWebToUECssProperty::FlexShrink, EWebToUECssProperty::FlexBasis,
			EWebToUECssProperty::JustifyContent, EWebToUECssProperty::AlignItems,
			EWebToUECssProperty::AlignSelf, EWebToUECssProperty::Border,
			EWebToUECssProperty::BorderWidth, EWebToUECssProperty::BorderStyle } },
		{ PaintHitTest, TArray<EWebToUECssProperty>{
			EWebToUECssProperty::Visibility, EWebToUECssProperty::ZIndex } },
		{ Paint, TArray<EWebToUECssProperty>{
			EWebToUECssProperty::Color, EWebToUECssProperty::Background,
			EWebToUECssProperty::BackgroundColor, EWebToUECssProperty::BorderColor,
			EWebToUECssProperty::BorderRadius, EWebToUECssProperty::Opacity,
			EWebToUECssProperty::TextAlign, EWebToUECssProperty::ObjectFit } },
		{ Measure, TArray<EWebToUECssProperty>{
			EWebToUECssProperty::FontSize, EWebToUECssProperty::WhiteSpace } },
		{ MeasureResource, TArray<EWebToUECssProperty>{
			EWebToUECssProperty::FontFamily, EWebToUECssProperty::FontWeight } }
	};
	int32 ClassifiedPropertyCount = 0;
	TSet<EWebToUECssProperty> ClassifiedProperties;
	for (const FImpactGroup& Group : ImpactGroups)
	{
		for (const EWebToUECssProperty Property : Group.Properties)
		{
			const FWebToUECssPropertyMetadata& Entry = GetCssPropertyMetadata(Property);
			TestFalse(*FString::Printf(TEXT("%s is classified exactly once"), Entry.Name),
				ClassifiedProperties.Contains(Property));
			ClassifiedProperties.Add(Property);
			TestEqual(*FString::Printf(TEXT("%s has its exact invalidation impact"), Entry.Name),
				Entry.Impacts, Group.Impact);
			++ClassifiedPropertyCount;
		}
	}
	TestEqual(TEXT("Every supported property has one exact impact classification"),
		ClassifiedPropertyCount, Metadata.Num());
	TestEqual(TEXT("The impact groups cover every unique supported property"),
		ClassifiedProperties.Num(), Metadata.Num());
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
		TestEqual(TEXT("Percentage width resolved"), Document->GetLayoutResult(*Row).Size.X, 200.0f);
		TestEqual(TEXT("Gap applied"),
			Document->GetLayoutResult(*Row->Children[1]).Position.X -
				Document->GetLayoutResult(*Row->Children[0]).Position.X,
			30.0f);
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
	if (TextNode)
	{
		TestEqual(TEXT("Measured wrapped height reaches Yoga layout"),
			Document->GetLayoutResult(*TextNode).Size.Y, 40.0f);
	}
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
		FWebToUERuntimeNodeState& ScrollState = Document->GetRuntimeNodeState(*Scroll);
		TestEqual(TEXT("Overflow auto resolves to a scroll container"),
			Document->GetComputedStyle(*Scroll).Overflow, EWebToUEOverflow::Auto);
		TestTrue(TEXT("Overflowing children produce a vertical scroll range"),
			FMath::IsNearlyEqual(ScrollState.MaxScrollOffset.Y, 140.0f, 0.1f));
		ScrollState.ScrollOffset.Y = 1000.0f;
		FWebToUELayoutEngine::Layout(*Document, FVector2f(200, 200),
			[](const FWebToUENode&, const FWebToUELayoutEngine::FMeasureConstraints&) { return FVector2f::ZeroVector; });
		TestTrue(TEXT("Relayout clamps an existing scroll offset"),
			FMath::IsNearlyEqual(ScrollState.ScrollOffset.Y, ScrollState.MaxScrollOffset.Y, 0.1f));
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
		TestEqual(TEXT("Later stylesheet still wins after source separation"),
			Document->GetComputedStyle(*Panel).Color, FLinearColor::Blue);
		TestFalse(TEXT("Invalid inline height is ignored"),
			Document->GetComputedStyle(*Panel).Height.IsDefined());
	}
	return true;
}

#endif
