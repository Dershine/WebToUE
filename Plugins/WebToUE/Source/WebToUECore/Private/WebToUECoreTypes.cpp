#include "WebToUECoreTypes.h"
#include "WebToUEPerformance.h"

FString FWebToUENode::GetAttribute(const FString& Name) const
{
	if (const FString* Value = Attributes.Find(Name.ToLower()))
	{
		return *Value;
	}
	return FString();
}

bool FWebToUENode::HasClass(const FString& ClassName) const
{
	if (bSelectorIdentityInitialized)
	{
		return SelectorClasses.ContainsByPredicate([&ClassName](const FString& Candidate)
		{
			return Candidate.Equals(ClassName, ESearchCase::IgnoreCase);
		});
	}
	TArray<FString> Classes;
	GetAttribute(TEXT("class")).ParseIntoArrayWS(Classes);
	return Classes.ContainsByPredicate([&ClassName](const FString& Candidate)
	{
		return Candidate.Equals(ClassName, ESearchCase::IgnoreCase);
	});
}

void FWebToUENode::InitializeSelectorIdentity()
{
	SelectorId = GetAttribute(TEXT("id")).ToLower();
	SelectorClasses.Reset();
	TArray<FString> ParsedClasses;
	GetAttribute(TEXT("class")).ParseIntoArrayWS(ParsedClasses);
	for (FString& ClassName : ParsedClasses)
	{
		ClassName.ToLowerInline();
		SelectorClasses.AddUnique(MoveTemp(ClassName));
	}
	bSelectorIdentityInitialized = true;
}

const FString& FWebToUENode::GetSelectorId() const
{
	check(bSelectorIdentityInitialized);
	return SelectorId;
}

bool FWebToUENode::IsInteractive() const
{
	return Tag == TEXT("button") || !GetAttribute(TEXT("data-ue-on-click")).IsEmpty();
}

bool FWebToUEDocument::HasErrors() const
{
	return Diagnostics.ContainsByPredicate([](const FWebToUEDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EWebToUEDiagnosticSeverity::Error;
	});
}

void FWebToUEDocument::InitializeRuntimeData()
{
	RuntimeNodeStates.Reset();
	RuntimeRenderData.Reset();
	int32 NodeCount = 0;
	ForEachNode([&NodeCount](FWebToUENode&) { ++NodeCount; });
	if (NodeCount > 0)
	{
		RuntimeNodeStates.Reserve(NodeCount);
		RuntimeRenderData.Reserve(NodeCount);
		FWebToUEPerformanceCapture::RecordAllocationPayload(
			static_cast<uint64>(NodeCount) * sizeof(FWebToUERuntimeNodeState));
		FWebToUEPerformanceCapture::RecordAllocationPayload(
			static_cast<uint64>(NodeCount) * sizeof(FWebToUERuntimeRenderData));
	}
	ForEachNode([this](FWebToUENode& Node)
	{
		Node.InitializeSelectorIdentity();
		AddRuntimeNodeData(Node);
	});
	InitializeSelectorIndex();
}

void FWebToUESelectorIndex::Reset()
{
	IdRules.Reset();
	ClassRules.Reset();
	TagRules.Reset();
	HoverRules.Reset();
	ActiveRules.Reset();
	FocusRules.Reset();
	DisabledRules.Reset();
	UniversalRules.Reset();
}

void FWebToUEDocument::InitializeSelectorIndex()
{
	SelectorIndex.Reset();
	for (int32 RuleIndex = 0; RuleIndex < Rules.Num(); ++RuleIndex)
	{
		const FWebToUEStyleRule& Rule = Rules[RuleIndex];
		if (Rule.Selector.IsEmpty())
		{
			continue;
		}

		const FWebToUESelectorSegment& Key = Rule.Selector.Last();
		if (!Key.Id.IsEmpty())
		{
			SelectorIndex.IdRules.FindOrAdd(Key.Id.ToLower()).Add(RuleIndex);
		}
		else if (!Key.Classes.IsEmpty())
		{
			SelectorIndex.ClassRules.FindOrAdd(Key.Classes[0].ToLower()).Add(RuleIndex);
		}
		else if (!Key.Type.IsEmpty())
		{
			SelectorIndex.TagRules.FindOrAdd(Key.Type.ToLower()).Add(RuleIndex);
		}
		else if (EnumHasAnyFlags(Key.RequiredState, EWebToUEPseudoState::Hover))
		{
			SelectorIndex.HoverRules.Add(RuleIndex);
		}
		else if (EnumHasAnyFlags(Key.RequiredState, EWebToUEPseudoState::Active))
		{
			SelectorIndex.ActiveRules.Add(RuleIndex);
		}
		else if (EnumHasAnyFlags(Key.RequiredState, EWebToUEPseudoState::Focus))
		{
			SelectorIndex.FocusRules.Add(RuleIndex);
		}
		else if (EnumHasAnyFlags(Key.RequiredState, EWebToUEPseudoState::Disabled))
		{
			SelectorIndex.DisabledRules.Add(RuleIndex);
		}
		else
		{
			SelectorIndex.UniversalRules.Add(RuleIndex);
		}
	}
}

int32 FWebToUEDocument::ForEachSelectorCandidate(const FWebToUENode& Node,
	TFunctionRef<void(const FWebToUEStyleRule&)> Visitor) const
{
	if (Node.Type != EWebToUENodeType::Element)
	{
		return 0;
	}

	int32 CandidateCount = 0;
	const auto VisitRules = [this, &Visitor, &CandidateCount](const TArray<int32>* RuleIndices)
	{
		if (!RuleIndices)
		{
			return;
		}
		CandidateCount += RuleIndices->Num();
		for (const int32 RuleIndex : *RuleIndices)
		{
			Visitor(Rules[RuleIndex]);
		}
	};

	if (!Node.GetSelectorId().IsEmpty())
	{
		VisitRules(SelectorIndex.IdRules.Find(Node.GetSelectorId()));
	}
	for (const FString& ClassName : Node.SelectorClasses)
	{
		VisitRules(SelectorIndex.ClassRules.Find(ClassName));
	}
	VisitRules(SelectorIndex.TagRules.Find(Node.Tag));

	const EWebToUEPseudoState PseudoStates = GetRuntimeNodeState(Node).PseudoStates;
	if (EnumHasAnyFlags(PseudoStates, EWebToUEPseudoState::Hover)) VisitRules(&SelectorIndex.HoverRules);
	if (EnumHasAnyFlags(PseudoStates, EWebToUEPseudoState::Active)) VisitRules(&SelectorIndex.ActiveRules);
	if (EnumHasAnyFlags(PseudoStates, EWebToUEPseudoState::Focus)) VisitRules(&SelectorIndex.FocusRules);
	if (EnumHasAnyFlags(PseudoStates, EWebToUEPseudoState::Disabled)) VisitRules(&SelectorIndex.DisabledRules);
	VisitRules(&SelectorIndex.UniversalRules);
	return CandidateCount;
}

void FWebToUEDocument::AddRuntimeNodeData(FWebToUENode& Node)
{
	Node.RuntimeDataIndex = RuntimeNodeStates.AddDefaulted();
	const int32 RenderDataIndex = RuntimeRenderData.AddDefaulted();
	check(Node.RuntimeDataIndex == RenderDataIndex);
}

bool FWebToUEDocument::IsDisplayed(const FWebToUENode& Node) const
{
	const FWebToUEComputedStyle& Style = GetComputedStyle(Node);
	return Style.Display != EWebToUEDisplay::None && Style.bVisible && GetRuntimeNodeState(Node).bRuntimeVisible;
}

bool FWebToUEDocument::ClipsOverflow(const FWebToUENode& Node) const
{
	return GetComputedStyle(Node).Overflow != EWebToUEOverflow::Visible;
}

bool FWebToUEDocument::IsScrollable(const FWebToUENode& Node) const
{
	const EWebToUEOverflow Overflow = GetComputedStyle(Node).Overflow;
	return Overflow == EWebToUEOverflow::Auto || Overflow == EWebToUEOverflow::Scroll;
}

void FWebToUEDocument::ForEachNode(TFunctionRef<void(FWebToUENode&)> Visitor) const
{
	TFunction<void(const TSharedPtr<FWebToUENode>&)> Walk = [&](const TSharedPtr<FWebToUENode>& Node)
	{
		if (!Node)
		{
			return;
		}
		Visitor(*Node);
		for (const TSharedPtr<FWebToUENode>& Child : Node->Children)
		{
			Walk(Child);
		}
	};
	Walk(Root);
}
