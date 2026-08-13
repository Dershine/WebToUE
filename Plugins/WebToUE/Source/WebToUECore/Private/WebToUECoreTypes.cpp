#include "WebToUECoreTypes.h"
#include "WebToUEPerformance.h"

#include "Templates/Atomic.h"

uint64 AllocateWebToUEInstanceOwnerId()
{
	static TAtomic<uint64> NextOwnerId(1);
	return NextOwnerId++;
}

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

void FWebToUEDocument::InitializeRuntimeData(uint64 InOwnerId, uint32 InGeneration)
{
	RuntimeInstanceOwnerId = InOwnerId != 0 ? InOwnerId : AllocateWebToUEInstanceOwnerId();
	RuntimeInstanceGeneration = InGeneration != 0 ? InGeneration : 1;
	RuntimeNodeStates.Reset();
	RuntimeRenderData.Reset();
	RuntimeNodesBySlot.Reset();
	RuntimeSelectorTargets.Reset();
	int32 NodeCount = 0;
	ForEachNode([&NodeCount](FWebToUENode&) { ++NodeCount; });
	if (NodeCount > 0)
	{
		RuntimeNodeStates.Reserve(NodeCount);
		RuntimeRenderData.Reserve(NodeCount);
		RuntimeNodesBySlot.Reserve(NodeCount);
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
	if (!SharedStyleTemplate)
	{
		InitializeSelectorIndex();
	}
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

void FWebToUESelectorIndex::Initialize(const TArray<FWebToUEStyleRule>& Rules)
{
	Reset();
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
			IdRules.FindOrAdd(Key.Id.ToLower()).Add(RuleIndex);
		}
		else if (!Key.Classes.IsEmpty())
		{
			ClassRules.FindOrAdd(Key.Classes[0].ToLower()).Add(RuleIndex);
		}
		else if (!Key.Type.IsEmpty())
		{
			TagRules.FindOrAdd(Key.Type.ToLower()).Add(RuleIndex);
		}
		else if (EnumHasAnyFlags(Key.RequiredState, EWebToUEPseudoState::Hover))
		{
			HoverRules.Add(RuleIndex);
		}
		else if (EnumHasAnyFlags(Key.RequiredState, EWebToUEPseudoState::Active))
		{
			ActiveRules.Add(RuleIndex);
		}
		else if (EnumHasAnyFlags(Key.RequiredState, EWebToUEPseudoState::Focus))
		{
			FocusRules.Add(RuleIndex);
		}
		else if (EnumHasAnyFlags(Key.RequiredState, EWebToUEPseudoState::Disabled))
		{
			DisabledRules.Add(RuleIndex);
		}
		else
		{
			UniversalRules.Add(RuleIndex);
		}
	}
}

void FWebToUERuntimeSelectorTargetIndex::Reset()
{
	IdTargets.Reset();
	ClassTargets.Reset();
	TagTargets.Reset();
	UniversalTargets.Reset();
}

void FWebToUERuntimeSelectorTargetIndex::Add(const FWebToUENode& Node)
{
	if (Node.Type != EWebToUENodeType::Element || !Node.InstanceHandle.IsValid())
	{
		return;
	}
	UniversalTargets.Add(Node.InstanceHandle);
	if (!Node.GetSelectorId().IsEmpty())
	{
		IdTargets.FindOrAdd(Node.GetSelectorId()).Add(Node.InstanceHandle);
	}
	for (const FString& ClassName : Node.SelectorClasses)
	{
		ClassTargets.FindOrAdd(ClassName).Add(Node.InstanceHandle);
	}
	if (!Node.Tag.IsEmpty())
	{
		TagTargets.FindOrAdd(Node.Tag).Add(Node.InstanceHandle);
	}
}

int32 FWebToUERuntimeSelectorTargetIndex::ForEachPotentialTarget(
	const FWebToUESelectorSegment& Target,
	TFunctionRef<void(FWebToUEInstanceHandle)> Visitor) const
{
	const TArray<FWebToUEInstanceHandle>* Targets = nullptr;
	if (!Target.Id.IsEmpty())
	{
		Targets = IdTargets.Find(Target.Id.ToLower());
	}
	else if (!Target.Classes.IsEmpty())
	{
		Targets = ClassTargets.Find(Target.Classes[0].ToLower());
	}
	else if (!Target.Type.IsEmpty())
	{
		Targets = TagTargets.Find(Target.Type.ToLower());
	}
	else
	{
		Targets = &UniversalTargets;
	}
	if (!Targets)
	{
		return 0;
	}
	for (const FWebToUEInstanceHandle Handle : *Targets)
	{
		Visitor(Handle);
	}
	return Targets->Num();
}

void FWebToUERuntimeStyleTemplate::CompilePseudoInvalidationDependencies()
{
	PseudoInvalidationDependencies.Reset();
	constexpr EWebToUEPseudoState States[] = {
		EWebToUEPseudoState::Hover,
		EWebToUEPseudoState::Active,
		EWebToUEPseudoState::Focus,
		EWebToUEPseudoState::Disabled
	};
	for (int32 RuleIndex = 0; RuleIndex < Rules.Num(); ++RuleIndex)
	{
		const FWebToUEStyleRule& Rule = Rules[RuleIndex];
		for (int32 SegmentIndex = 0; SegmentIndex < Rule.Selector.Num(); ++SegmentIndex)
		{
			for (const EWebToUEPseudoState State : States)
			{
				if (EnumHasAnyFlags(Rule.Selector[SegmentIndex].RequiredState, State))
				{
					PseudoInvalidationDependencies.Add({ State, RuleIndex, SegmentIndex });
				}
			}
		}
	}
}

void FWebToUEDocument::InitializeSelectorIndex()
{
	SelectorIndex.Initialize(Rules);
}

void FWebToUEDocument::SetSharedStyleTemplate(
	TSharedPtr<const FWebToUERuntimeStyleTemplate> InTemplate)
{
	SharedStyleTemplate = MoveTemp(InTemplate);
}

const TArray<FWebToUEStyleRule>& FWebToUEDocument::GetRules() const
{
	return SharedStyleTemplate ? SharedStyleTemplate->Rules : Rules;
}

const FWebToUESelectorIndex& FWebToUEDocument::GetSelectorIndex() const
{
	return SharedStyleTemplate ? SharedStyleTemplate->SelectorIndex : SelectorIndex;
}

int32 FWebToUEDocument::ForEachSelectorCandidate(const FWebToUENode& Node,
	TFunctionRef<void(const FWebToUEStyleRule&)> Visitor) const
{
	if (Node.Type != EWebToUENodeType::Element)
	{
		return 0;
	}

	int32 CandidateCount = 0;
	const TArray<FWebToUEStyleRule>& ActiveRules = GetRules();
	const FWebToUESelectorIndex& ActiveIndex = GetSelectorIndex();
	const auto VisitRules = [&ActiveRules, &Visitor, &CandidateCount](const TArray<int32>* RuleIndices)
	{
		if (!RuleIndices)
		{
			return;
		}
		CandidateCount += RuleIndices->Num();
		for (const int32 RuleIndex : *RuleIndices)
		{
			Visitor(ActiveRules[RuleIndex]);
		}
	};

	if (!Node.GetSelectorId().IsEmpty())
	{
		VisitRules(ActiveIndex.IdRules.Find(Node.GetSelectorId()));
	}
	for (const FString& ClassName : Node.SelectorClasses)
	{
		VisitRules(ActiveIndex.ClassRules.Find(ClassName));
	}
	VisitRules(ActiveIndex.TagRules.Find(Node.Tag));

	const EWebToUEPseudoState PseudoStates = GetRuntimeNodeState(Node).PseudoStates;
	if (EnumHasAnyFlags(PseudoStates, EWebToUEPseudoState::Hover)) VisitRules(&ActiveIndex.HoverRules);
	if (EnumHasAnyFlags(PseudoStates, EWebToUEPseudoState::Active)) VisitRules(&ActiveIndex.ActiveRules);
	if (EnumHasAnyFlags(PseudoStates, EWebToUEPseudoState::Focus)) VisitRules(&ActiveIndex.FocusRules);
	if (EnumHasAnyFlags(PseudoStates, EWebToUEPseudoState::Disabled)) VisitRules(&ActiveIndex.DisabledRules);
	VisitRules(&ActiveIndex.UniversalRules);
	return CandidateCount;
}

int32 FWebToUEDocument::ForEachPotentialSelectorTarget(
	const FWebToUESelectorSegment& Target,
	TFunctionRef<void(FWebToUEInstanceHandle)> Visitor) const
{
	return RuntimeSelectorTargets.ForEachPotentialTarget(Target, Visitor);
}

void FWebToUEDocument::AddRuntimeNodeData(FWebToUENode& Node)
{
	const int32 RuntimeDataIndex = RuntimeNodeStates.AddDefaulted();
	const int32 RenderDataIndex = RuntimeRenderData.AddDefaulted();
	const int32 NodeSlot = RuntimeNodesBySlot.Add(&Node);
	check(RuntimeDataIndex == RenderDataIndex && RuntimeDataIndex == NodeSlot);
	Node.InstanceHandle = FWebToUEInstanceHandle::Create(
		RuntimeInstanceOwnerId, RuntimeInstanceGeneration, RuntimeDataIndex);
	RuntimeSelectorTargets.Add(Node);
}

FWebToUENode* FWebToUEDocument::ResolveNode(FWebToUEInstanceHandle Handle)
{
	if (!Handle.IsValid() || Handle.GetOwnerId() != RuntimeInstanceOwnerId ||
		Handle.GetGeneration() != RuntimeInstanceGeneration ||
		!RuntimeNodesBySlot.IsValidIndex(Handle.GetSlot()))
	{
		return nullptr;
	}
	return RuntimeNodesBySlot[Handle.GetSlot()];
}

const FWebToUENode* FWebToUEDocument::ResolveNode(FWebToUEInstanceHandle Handle) const
{
	if (!Handle.IsValid() || Handle.GetOwnerId() != RuntimeInstanceOwnerId ||
		Handle.GetGeneration() != RuntimeInstanceGeneration ||
		!RuntimeNodesBySlot.IsValidIndex(Handle.GetSlot()))
	{
		return nullptr;
	}
	return RuntimeNodesBySlot[Handle.GetSlot()];
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
