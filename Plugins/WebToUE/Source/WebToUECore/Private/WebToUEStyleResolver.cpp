#include "WebToUECompiler.h"

#include "WebToUEPerformance.h"
#include "WebToUEStyleProperties.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace WebToUE::Private
{
	static bool SegmentMatches(const FWebToUESelectorSegment& Segment, const FWebToUENode& Node,
		const FWebToUERuntimeNodeState& State)
	{
		if (Node.Type != EWebToUENodeType::Element) return false;
		if (!Segment.Type.IsEmpty() && Node.Tag != Segment.Type) return false;
		if (!Segment.Id.IsEmpty() && !Node.GetAttribute(TEXT("id")).Equals(Segment.Id, ESearchCase::IgnoreCase)) return false;
		for (const FString& Class : Segment.Classes)
		{
			if (!Node.HasClass(Class)) return false;
		}
		return EnumHasAllFlags(State.PseudoStates, Segment.RequiredState);
	}

	static void ResolveNode(FWebToUENode& Node, FWebToUEDocument& Document,
		const FWebToUEComputedStyle* ParentStyle)
	{
		FWebToUERuntimeNodeState& RuntimeState = Document.GetRuntimeNodeState(Node);
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::StyleNodeVisits);
		FWebToUEComputedStyle Style;
		if (Node.Type == EWebToUENodeType::Text)
		{
			Style.FlexDirection = EWebToUEFlexDirection::Row;
		}
		if (Node.Tag == TEXT("body"))
		{
			Style.Width = FWebToUELength::Percent(100.0f);
			Style.Height = FWebToUELength::Percent(100.0f);
		}
		if (Node.Tag == TEXT("button"))
		{
			Style.FlexDirection = EWebToUEFlexDirection::Row;
			Style.JustifyContent = TEXT("center");
			Style.AlignItems = TEXT("center");
		}
		if (ParentStyle)
		{
			Style.Color = ParentStyle->Color;
			Style.FontFamily = ParentStyle->FontFamily;
			Style.FontSize = ParentStyle->FontSize;
			Style.FontWeight = ParentStyle->FontWeight;
			Style.TextAlign = ParentStyle->TextAlign;
			Style.WhiteSpace = ParentStyle->WhiteSpace;
		}

		TArray<const FWebToUEStyleRule*> Matches;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::SelectorEvaluations, Document.Rules.Num());
		for (const FWebToUEStyleRule& Rule : Document.Rules)
		{
			if (FWebToUEStyleResolver::Matches(Rule, Node, Document))
			{
				FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::SelectorMatches);
				if (Matches.Num() == Matches.Max())
				{
					FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
				}
				Matches.Add(&Rule);
			}
		}
		Matches.Sort([](const FWebToUEStyleRule& A, const FWebToUEStyleRule& B)
		{
			return A.Specificity == B.Specificity
				? A.SourceOrder < B.SourceOrder
				: A.Specificity < B.Specificity;
		});
		TMap<EWebToUECssProperty, FWebToUEStyleValue> Properties;
		for (const FWebToUEStyleRule* Rule : Matches)
		{
			for (const FWebToUEStyleDeclaration& Declaration : Rule->Declarations)
			{
				Properties.Add(Declaration.Property, Declaration.TypedValue);
			}
		}
		for (const FWebToUEStyleDeclaration& Declaration : Node.InlineStyleDeclarations)
		{
			Properties.Add(Declaration.Property, Declaration.TypedValue);
		}
		ApplyProperties(Properties, Style);
		Style.bVisible = Style.bVisible && RuntimeState.bRuntimeVisible;
		Style.bEnabled = !Node.Attributes.Contains(TEXT("disabled")) && RuntimeState.bRuntimeEnabled;
		if (!Style.bEnabled) RuntimeState.PseudoStates |= EWebToUEPseudoState::Disabled;
		else RuntimeState.PseudoStates &= ~EWebToUEPseudoState::Disabled;
		Document.GetComputedStyle(Node) = MoveTemp(Style);
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			ResolveNode(*Child, Document, &Document.GetComputedStyle(Node));
		}
	}
}

bool FWebToUEStyleResolver::Matches(const FWebToUEStyleRule& Rule, const FWebToUENode& Node,
	const FWebToUEDocument& Document)
{
	using namespace WebToUE::Private;
	if (Rule.Selector.IsEmpty()) return false;
	TFunction<bool(int32, const FWebToUENode*)> MatchAt = [&](int32 Index, const FWebToUENode* Current)
	{
		if (!Current || Index < 0 ||
			!SegmentMatches(Rule.Selector[Index], *Current, Document.GetRuntimeNodeState(*Current)))
		{
			return false;
		}
		if (Index == 0) return true;
		if (Rule.Selector[Index].RelationToPrevious == EWebToUECombinator::Child)
		{
			return MatchAt(Index - 1, Current->Parent);
		}
		for (const FWebToUENode* Ancestor = Current->Parent; Ancestor; Ancestor = Ancestor->Parent)
		{
			if (MatchAt(Index - 1, Ancestor)) return true;
		}
		return false;
	};
	return MatchAt(Rule.Selector.Num() - 1, &Node);
}

void FWebToUEStyleResolver::Resolve(FWebToUEDocument& Document)
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_Style);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_Style);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::Style);
	if (Document.Root)
	{
		WebToUE::Private::ResolveNode(*Document.Root, Document, nullptr);
	}
}
