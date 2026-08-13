#include "WebToUECompiler.h"

#include "WebToUEPerformance.h"
#include "WebToUEStyleProperties.h"

#include "Containers/StaticArray.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace WebToUE::Private
{
	static constexpr int32 CascadeSlotCount =
		static_cast<int32>(EWebToUECssProperty::ZIndex) + 1;

	struct FCascadePriority
	{
		int32 Specificity = 0;
		int32 SourceOrder = 0;
		int32 DeclarationOrder = 0;
		bool bInline = false;

		bool IsHigherThan(const FCascadePriority& Other) const
		{
			if (bInline != Other.bInline) return bInline;
			if (Specificity != Other.Specificity) return Specificity > Other.Specificity;
			if (SourceOrder != Other.SourceOrder) return SourceOrder > Other.SourceOrder;
			return DeclarationOrder >= Other.DeclarationOrder;
		}
	};

	struct FCascadeWinner
	{
		const FWebToUEStyleValue* Value = nullptr;
		EWebToUECssProperty SourceProperty = EWebToUECssProperty::Invalid;
		FCascadePriority Priority;
	};

	class FTypedCascade
	{
	public:
		void Reset()
		{
			for (FCascadeWinner& Winner : Winners)
			{
				Winner.Value = nullptr;
			}
		}

		void Submit(const FWebToUEStyleDeclaration& Declaration,
			const FCascadePriority& Priority)
		{
			const auto SubmitSlot = [this, &Declaration, &Priority](EWebToUECssProperty Slot)
			{
				FCascadeWinner& Winner = Winners[static_cast<int32>(Slot)];
				if (!Winner.Value || Priority.IsHigherThan(Winner.Priority))
				{
					Winner.Value = &Declaration.TypedValue;
					Winner.SourceProperty = Declaration.Property;
					Winner.Priority = Priority;
				}
			};

			switch (Declaration.Property)
			{
			case EWebToUECssProperty::Margin:
				SubmitSlot(EWebToUECssProperty::MarginLeft);
				SubmitSlot(EWebToUECssProperty::MarginTop);
				SubmitSlot(EWebToUECssProperty::MarginRight);
				SubmitSlot(EWebToUECssProperty::MarginBottom);
				break;
			case EWebToUECssProperty::Padding:
				SubmitSlot(EWebToUECssProperty::PaddingLeft);
				SubmitSlot(EWebToUECssProperty::PaddingTop);
				SubmitSlot(EWebToUECssProperty::PaddingRight);
				SubmitSlot(EWebToUECssProperty::PaddingBottom);
				break;
			case EWebToUECssProperty::Gap:
				SubmitSlot(EWebToUECssProperty::RowGap);
				SubmitSlot(EWebToUECssProperty::ColumnGap);
				break;
			case EWebToUECssProperty::Flex:
				if (Declaration.TypedValue.Flex.bHasGrow) SubmitSlot(EWebToUECssProperty::FlexGrow);
				if (Declaration.TypedValue.Flex.bHasShrink) SubmitSlot(EWebToUECssProperty::FlexShrink);
				if (Declaration.TypedValue.Flex.bHasBasis) SubmitSlot(EWebToUECssProperty::FlexBasis);
				break;
			case EWebToUECssProperty::Background:
				SubmitSlot(EWebToUECssProperty::BackgroundColor);
				break;
			case EWebToUECssProperty::Border:
				if (Declaration.TypedValue.Border.bHasWidth) SubmitSlot(EWebToUECssProperty::BorderWidth);
				if (Declaration.TypedValue.Border.bHasColor) SubmitSlot(EWebToUECssProperty::BorderColor);
				break;
			default:
				SubmitSlot(Declaration.Property);
				break;
			}
		}

		void Apply(FWebToUEComputedStyle& Style) const
		{
			for (int32 SlotIndex = static_cast<int32>(EWebToUECssProperty::Display);
				SlotIndex < CascadeSlotCount; ++SlotIndex)
			{
				const FCascadeWinner& Winner = Winners[SlotIndex];
				if (!Winner.Value) continue;
				ApplyCascadedProperty(static_cast<EWebToUECssProperty>(SlotIndex),
					Winner.SourceProperty, *Winner.Value, Style);
			}
		}

	private:
		TStaticArray<FCascadeWinner, CascadeSlotCount> Winners;
	};

	static bool SegmentMatches(const FWebToUESelectorSegment& Segment, const FWebToUENode& Node,
		const FWebToUERuntimeNodeState& State)
	{
		if (Node.Type != EWebToUENodeType::Element) return false;
		if (!Segment.Type.IsEmpty() && Node.Tag != Segment.Type) return false;
		if (!Segment.Id.IsEmpty() && !Node.GetSelectorId().Equals(Segment.Id, ESearchCase::IgnoreCase)) return false;
		for (const FString& Class : Segment.Classes)
		{
			if (!Node.HasClass(Class)) return false;
		}
		return EnumHasAllFlags(State.PseudoStates, Segment.RequiredState);
	}

	static void ResolveNode(FWebToUENode& Node, FWebToUEDocument& Document,
		const FWebToUEComputedStyle* ParentStyle, FTypedCascade& Cascade)
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
			ApplyInheritedProperties(*ParentStyle, Style);
		}

		Cascade.Reset();
		const int32 CandidateCount = Document.ForEachSelectorCandidate(Node,
			[&](const FWebToUEStyleRule& Rule)
		{
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::SelectorEvaluations);
			if (FWebToUEStyleResolver::Matches(Rule, Node, Document))
			{
				FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::SelectorMatches);
				for (int32 DeclarationOrder = 0;
					DeclarationOrder < Rule.Declarations.Num(); ++DeclarationOrder)
				{
					Cascade.Submit(Rule.Declarations[DeclarationOrder],
						{ Rule.Specificity, Rule.SourceOrder, DeclarationOrder, false });
				}
			}
		});
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::SelectorCandidates, CandidateCount);
		for (int32 DeclarationOrder = 0;
			DeclarationOrder < Node.InlineStyleDeclarations.Num(); ++DeclarationOrder)
		{
			Cascade.Submit(Node.InlineStyleDeclarations[DeclarationOrder],
				{ 0, 0, DeclarationOrder, true });
		}
		Cascade.Apply(Style);
		Style.bVisible = Style.bVisible && RuntimeState.bRuntimeVisible;
		Style.bEnabled = !Node.Attributes.Contains(TEXT("disabled")) && RuntimeState.bRuntimeEnabled;
		if (!Style.bEnabled) RuntimeState.PseudoStates |= EWebToUEPseudoState::Disabled;
		else RuntimeState.PseudoStates &= ~EWebToUEPseudoState::Disabled;
		Document.GetComputedStyle(Node) = MoveTemp(Style);
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			ResolveNode(*Child, Document, &Document.GetComputedStyle(Node), Cascade);
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
		WebToUE::Private::FTypedCascade Cascade;
		WebToUE::Private::ResolveNode(*Document.Root, Document, nullptr, Cascade);
	}
}
