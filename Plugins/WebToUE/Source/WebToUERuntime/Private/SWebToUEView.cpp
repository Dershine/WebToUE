#include "SWebToUEView.h"

#include "WebToUECompiler.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUEView.h"
#include "WebToUERuntimeInstance.h"
#include "WebToUERuntimePresentation.h"

#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogWebToUE, Log, All);

SWebToUEView::SWebToUEView() = default;

SWebToUEView::~SWebToUEView() = default;

void SWebToUEView::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	RuntimeInstance = MakeUnique<FWebToUERuntimeInstance>();
	Presentation = MakeUnique<FWebToUERuntimePresentation>(*this, *RuntimeInstance);
	SetCanTick(false);
}

FWebToUEDocument* SWebToUEView::GetRuntimeDocument()
{
	return RuntimeInstance ? RuntimeInstance->GetDocument() : nullptr;
}

const FWebToUEDocument* SWebToUEView::GetRuntimeDocument() const
{
	return RuntimeInstance ? RuntimeInstance->GetDocument() : nullptr;
}

FWebToUERuntimeNodeState& SWebToUEView::GetRuntimeState(FWebToUENode& Node)
{
	return RuntimeInstance->GetState(Node);
}

const FWebToUERuntimeNodeState& SWebToUEView::GetRuntimeState(const FWebToUENode& Node) const
{
	return RuntimeInstance->GetState(Node);
}

FWebToUEComputedStyle& SWebToUEView::GetComputedStyle(FWebToUENode& Node)
{
	return RuntimeInstance->GetStyle(Node);
}

const FWebToUEComputedStyle& SWebToUEView::GetComputedStyle(const FWebToUENode& Node) const
{
	return RuntimeInstance->GetStyle(Node);
}

FWebToUERuntimeLayoutResult& SWebToUEView::GetLayoutResult(FWebToUENode& Node)
{
	return RuntimeInstance->GetLayout(Node);
}

const FWebToUERuntimeLayoutResult& SWebToUEView::GetLayoutResult(const FWebToUENode& Node) const
{
	return RuntimeInstance->GetLayout(Node);
}

void SWebToUEView::SetDocument(UWebToUEDocument* InDocument)
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_Hydrate);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_Hydrate);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::Hydrate);
	DocumentAsset = InDocument;
	Presentation->Reset();
	if (InDocument)
	{
		if (!RuntimeInstance->Hydrate(*InDocument))
		{
			UE_LOG(LogWebToUE, Error,
				TEXT("Failed to hydrate WebToUE document '%s' (nodes=%d, rules=%d, binding_ops=%d, resources=%d, root=%d)."),
				*InDocument->GetPathName(), InDocument->GetCompiledNodes().Num(),
				InDocument->GetCompiledRules().Num(),
				InDocument->GetCompiledBindingOps().Num(),
				InDocument->GetResourceManifest().Num(), InDocument->GetRootNodeIndex());
		}
	}
	else
	{
		RuntimeInstance->Reset();
	}
	RebuildStylesAndBrushes();
}

FVector2D SWebToUEView::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(320.0, 180.0);
}

FText SWebToUEView::GetDisplayText(const FWebToUENode& Node) const
{
	return Presentation->GetDisplayText(Node);
}
#if WITH_DEV_AUTOMATION_TESTS
FVector2f SWebToUEView::MeasureTextForTesting(const FString& Text, float Width, bool bWrap) const
{
	return Presentation->MeasureTextForTesting(Text, Width, bWrap);
}

FVector2f SWebToUEView::MeasureRichTextForTesting(const FString& Markup, float Width, bool bWrap) const
{
	return Presentation->MeasureRichTextForTesting(Markup, Width, bWrap);
}

FText SWebToUEView::GetDisplayTextForTesting(const FWebToUENode& Node) const
{
	return GetDisplayText(Node);
}

void SWebToUEView::SetRuntimeDocumentForTesting(TSharedRef<FWebToUEDocument> InDocument)
{
	RuntimeInstance->AdoptDocumentForTesting(MoveTemp(InDocument));
	RebuildStylesAndBrushes();
}

void SWebToUEView::LayoutForTesting(const FVector2f& ViewportSize) const
{
	Presentation->Layout(ViewportSize);
}

bool SWebToUEView::GetRuntimeMemoryCensusForTesting(
	FWebToUERuntimeMemoryCensus& OutCensus) const
{
	if (!RuntimeInstance || !RuntimeInstance->GetDocument() || !Presentation)
	{
		return false;
	}
	OutCensus.SharedStyleTemplateKnownOwnedBytes =
		RuntimeInstance->GetSharedStyleTemplateKnownOwnedBytesForTesting();
	OutCensus.RuntimeKnownOwnedBytes = RuntimeInstance->GetKnownOwnedBytesForTesting();
	OutCensus.PresentationKnownOwnedBytes = Presentation->GetKnownOwnedBytesForTesting();
	OutCensus.RuntimeNodeCount = RuntimeInstance->GetRuntimeNodeCountForTesting();
	OutCensus.RuntimeRuleCount = RuntimeInstance->GetRuntimeRuleCountForTesting();
	return true;
}

int32 SWebToUEView::GetDisplayCommandCountForTesting() const
{
	return Presentation->GetDisplayCommandCountForTesting();
}

const FWebToUEPaintCommand* SWebToUEView::GetDisplayCommandForTesting(
	const FWebToUENode& Node) const
{
	return Presentation->GetDisplayCommandForTesting(Node);
}

const FWebToUEDisplayCommandRange* SWebToUEView::GetDisplayCommandRangeForTesting(
	const FWebToUENode& Node) const
{
	return Presentation->GetDisplayCommandRangeForTesting(Node);
}

int32 SWebToUEView::GetDisplaySpatialCellCountForTesting() const
{
	return Presentation->GetDisplaySpatialCellCountForTesting();
}

int32 SWebToUEView::GetDirtyRectCountForTesting() const
{
	return Presentation->GetDirtyRectCountForTesting();
}

int32 SWebToUEView::GetDirtyCommandCountForTesting() const
{
	return Presentation->GetDirtyCommandCountForTesting();
}

const FSlateRect* SWebToUEView::GetDirtyRectForTesting(int32 Index) const
{
	return Presentation->GetDirtyRectForTesting(Index);
}

FVector2f SWebToUEView::GetVisualPositionForTesting(const FWebToUENode& Node) const
{
	return Presentation->GetVisualPosition(Node);
}

TConstArrayView<FWebToUEInstanceHandle> SWebToUEView::GetPaintOrderForTesting(
	const FWebToUENode& Parent) const
{
	return Presentation->GetPaintOrder(Parent);
}

FWebToUEInstanceHandle SWebToUEView::GetInstanceHandleForTesting(
	const FWebToUENode& Node) const
{
	return RuntimeInstance->GetHandle(&Node);
}

FWebToUETemplateNodeId SWebToUEView::GetTemplateNodeIdForTesting(
	const FWebToUENode& Node) const
{
	return Node.TemplateNodeId;
}

FWebToUENode* SWebToUEView::ResolveInstanceHandleForTesting(
	FWebToUEInstanceHandle Handle) const
{
	return const_cast<FWebToUENode*>(RuntimeInstance->ResolveNode(Handle));
}

const void* SWebToUEView::GetSharedStyleTemplateIdentityForTesting() const
{
	return RuntimeInstance->GetSharedStyleTemplateIdentityForTesting();
}

FWebToUENode* SWebToUEView::AddDynamicTextNodeForTesting(FWebToUENode& Parent)
{
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument || !RuntimeInstance->GetHandle(&Parent).IsValid())
	{
		return nullptr;
	}
	TSharedPtr<FWebToUENode> TextNode = MakeShared<FWebToUENode>();
	TextNode->Type = EWebToUENodeType::Text;
	TextNode->Tag = TEXT("#text");
	TextNode->Parent = &Parent;
	RuntimeDocument->AddRuntimeNodeData(*TextNode);
	Parent.Children.Add(TextNode);
	return TextNode.Get();
}

const FWebToUERuntimeNodeState& SWebToUEView::GetRuntimeStateForTesting(const FWebToUENode& Node) const
{
	return GetRuntimeState(Node);
}

const FWebToUEComputedStyle& SWebToUEView::GetComputedStyleForTesting(const FWebToUENode& Node) const
{
	return GetComputedStyle(Node);
}

const FWebToUERuntimeLayoutResult& SWebToUEView::GetLayoutResultForTesting(const FWebToUENode& Node) const
{
	return GetLayoutResult(Node);
}

void SWebToUEView::SetBoundTextForTesting(FWebToUENode& Node, const FText& Text)
{
	FWebToUERuntimeNodeState& State = GetRuntimeState(Node);
	State.BoundText = Text;
	State.bHasBoundText = true;
}

bool SWebToUEView::ApplyBoundTextChangeForTesting(FWebToUENode& Node, const FText& Text,
	bool bRichText)
{
	FWebToUERuntimeNodeState& State = GetRuntimeState(Node);
	State.BoundText = Text;
	State.bHasBoundText = true;
	State.bHasRichTextOverride = true;
	State.bRichTextOverride = bRichText;
	return Presentation->ApplyBoundTextChange(Node);
}

FVector2f SWebToUEView::PrepareTextLayoutForTesting(const FWebToUENode& Node,
	const FWebToUEComputedStyle& Style, float WrapWidth) const
{
	return Presentation->PrepareTextLayoutForTesting(Node, Style, WrapWidth);
}

bool SWebToUEView::IsPresentationMeasureDirtyForTesting(const FWebToUENode& Node) const
{
	return Presentation->IsMeasureDirtyForTesting(Node);
}

bool SWebToUEView::IsPresentationLayoutPathDirtyForTesting(const FWebToUENode& Node) const
{
	return Presentation->IsLayoutPathDirtyForTesting(Node);
}

FString SWebToUEView::GetPresentationTextCacheCultureForTesting(
	const FWebToUENode& Node) const
{
	return Presentation->GetTextCacheCultureForTesting(Node);
}

FWebToUENode* SWebToUEView::FindRuntimeNodeByIdForTesting(const FString& Id) const
{
	FWebToUENode* Result = nullptr;
	if (const FWebToUEDocument* RuntimeDocument = GetRuntimeDocument())
	{
		RuntimeDocument->ForEachNode([&Result, &Id](FWebToUENode& Node)
		{
			if (!Result && Node.GetAttribute(TEXT("id")) == Id)
			{
				Result = &Node;
			}
		});
	}
	return Result;
}

int32 SWebToUEView::GetPresentationTextCacheCountForTesting() const
{
	return Presentation->GetTextLayoutCacheCountForTesting();
}

int32 SWebToUEView::GetPresentationBrushCacheCountForTesting() const
{
	return Presentation->GetBrushCacheCountForTesting();
}

const void* SWebToUEView::GetPresentationBrushIdentityForTesting(
	const FWebToUENode& Node) const
{
	return Presentation->GetBrushIdentityForTesting(Node);
}

uint64 SWebToUEView::GetPresentationResourceLoadAttemptsForTesting() const
{
	return Presentation->GetResourceLoadAttemptsForTesting();
}

uint64 SWebToUEView::GetPresentationResourceAsyncRequestsForTesting() const
{
	return Presentation->GetResourceAsyncRequestsForTesting();
}

uint64 SWebToUEView::GetPresentationResourceFailuresForTesting() const
{
	return Presentation->GetResourceFailuresForTesting();
}

uint64 SWebToUEView::GetPresentationResourceCancellationsForTesting() const
{
	return Presentation->GetResourceCancellationsForTesting();
}

int32 SWebToUEView::FindPresentationResourceHandleForTesting(
	EWebToUEResourceKind Kind, const FSoftObjectPath& Path) const
{
	return Presentation->FindResourceHandleForTesting(Kind, Path);
}

const UObject* SWebToUEView::GetPresentationResourceObjectForTesting(int32 Handle) const
{
	return Presentation->GetResourceObjectForTesting(Handle);
}

bool SWebToUEView::FinalizePresentationResourcesForTesting() const
{
	return Presentation->FinalizeResourcesForTesting();
}

const void* SWebToUEView::GetPresentationTextCacheIdentityForTesting(
	const FWebToUENode& Node) const
{
	return Presentation->GetTextLayoutCacheIdentityForTesting(Node);
}

bool SWebToUEView::IsPresentationLayoutDirtyForTesting() const
{
	return Presentation->IsLayoutDirtyForTesting();
}
#endif

int32 SWebToUEView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_PaintBuild);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_PaintBuild);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::PaintBuild);
	return Presentation->Paint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
}

void SWebToUEView::RebuildStylesAndBrushes(EWebToUEStyleImpact Impacts)
{
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (RuntimeDocument)
	{
		FWebToUEStyleResolver::Resolve(*RuntimeDocument);
	}
	Presentation->RebuildCaches(EnumHasAnyFlags(Impacts, EWebToUEStyleImpact::Resource));
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

static bool ReadPropertyAsText(UObject* Context, const FProperty* Property, FText& Out)
{
	if (!Context || !Property) return false;
	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		Out = TextProperty->GetPropertyValue_InContainer(Context);
		return true;
	}
	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		Out = FText::FromString(StringProperty->GetPropertyValue_InContainer(Context));
		return true;
	}
	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		Out = FText::FromName(NameProperty->GetPropertyValue_InContainer(Context));
		return true;
	}
	FString ExportedValue;
	Property->ExportText_InContainer(0, ExportedValue, Context, Context, Context, PPF_None);
	Out = FText::FromString(MoveTemp(ExportedValue));
	return true;
}

static bool ReadPropertyAsBool(UObject* Context, const FProperty* Property, bool& Out)
{
	if (!Context) return false;
	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
	{
		Out = BoolProperty->GetPropertyValue_InContainer(Context);
		return true;
	}
	return false;
}

void SWebToUEView::RefreshBindings(UObject* DataContext, FName ChangedField)
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_Binding);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_Binding);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::Binding);
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument || !DataContext) return;
	TSet<FWebToUEInstanceHandle> UpdatedNodes;
	TArray<FWebToUEInstanceHandle> StyleTargets;
	TArray<FWebToUEInstanceHandle> RuntimeStateTargets;
	bool bTextChanged = false;
	bool bTextMeasureChanged = false;
	bool bPaintChanged = false;
	const auto ApplyField = [&](FName RootField)
	{
		const TConstArrayView<FWebToUERuntimeBindingOp> Ops =
			RuntimeInstance->GetBindingOps(RootField);
		if (Ops.IsEmpty()) return;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::BindingFieldsRead);
		const FProperty* Property = FindFProperty<FProperty>(
			DataContext->GetClass(), RootField);
		if (!Property)
		{
			ReportBindingErrorOnce(RootField.ToString(),
				TEXT("Binding property was not found."));
			return;
		}
		FText TextValue;
		bool bTextRead = false;
		bool bBoolValue = true;
		bool bBoolRead = false;
		for (const FWebToUERuntimeBindingOp& Op : Ops)
		{
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::BindingOpsExecuted);
			FWebToUENode* Node = RuntimeInstance->ResolveNode(Op.Target);
			if (!Node || Node->Type != EWebToUENodeType::Element) continue;
			if (Op.Kind == EWebToUEBindingKind::Text)
			{
				if (!bTextRead)
				{
					bTextRead = ReadPropertyAsText(DataContext, Property, TextValue);
					if (!bTextRead)
					{
						ReportBindingErrorOnce(RootField.ToString(),
							TEXT("Text binding property could not be read."));
					}
				}
				if (!bTextRead) continue;
				TSharedPtr<FWebToUENode> TextNode;
				if (TSharedPtr<FWebToUENode>* Existing = Node->Children.FindByPredicate([](const TSharedPtr<FWebToUENode>& Child)
				{
					return Child->Type == EWebToUENodeType::Text;
				}))
				{
					TextNode = *Existing;
				}
				if (!TextNode)
				{
					FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
					TextNode = MakeShared<FWebToUENode>();
					TextNode->Type = EWebToUENodeType::Text;
					TextNode->Tag = TEXT("#text");
					TextNode->Parent = Node;
					RuntimeDocument->AddRuntimeNodeData(*TextNode);
					Node->Children.Insert(TextNode, 0);
					StyleTargets.AddUnique(TextNode->InstanceHandle);
				}
				FWebToUERuntimeNodeState& TextState = GetRuntimeState(*TextNode);
				if (TextState.bHasBoundText && TextState.BoundText.EqualTo(TextValue) &&
					TextState.bHasRichTextOverride &&
					TextState.bRichTextOverride == Op.bRichText)
				{
					continue;
				}
				TextState.BoundText = TextValue;
				TextState.bHasBoundText = true;
				TextState.bHasRichTextOverride = true;
				TextState.bRichTextOverride = Op.bRichText;
				UpdatedNodes.Add(TextNode->InstanceHandle);
				bTextMeasureChanged |= Presentation->ApplyBoundTextChange(*TextNode);
				bTextChanged = true;
			}
			else
			{
				if (!bBoolRead)
				{
					bBoolRead = ReadPropertyAsBool(DataContext, Property, bBoolValue);
					if (!bBoolRead)
					{
						ReportBindingErrorOnce(RootField.ToString(),
							TEXT("Visible and enabled bindings require a bool UPROPERTY."));
					}
				}
				if (!bBoolRead) continue;
				FWebToUERuntimeNodeState& State = GetRuntimeState(*Node);
				bool& Current = Op.Kind == EWebToUEBindingKind::Visible
					? State.bRuntimeVisible : State.bRuntimeEnabled;
				if (Current == bBoolValue) continue;
				if (Op.Kind == EWebToUEBindingKind::Enabled)
				{
					CollectPseudoDependencyTargets(*Node,
						EWebToUEPseudoState::Disabled, StyleTargets);
				}
				Current = bBoolValue;
				if (Op.Kind == EWebToUEBindingKind::Enabled)
				{
					const bool bDisabled = Node->Attributes.Contains(TEXT("disabled")) ||
						!State.bRuntimeEnabled;
					if (bDisabled) State.PseudoStates |= EWebToUEPseudoState::Disabled;
					else State.PseudoStates &= ~EWebToUEPseudoState::Disabled;
					CollectPseudoDependencyTargets(*Node,
						EWebToUEPseudoState::Disabled, StyleTargets);
				}
				UpdatedNodes.Add(Node->InstanceHandle);
				StyleTargets.AddUnique(Node->InstanceHandle);
				RuntimeStateTargets.AddUnique(Node->InstanceHandle);
				bPaintChanged = true;
			}
		}
	};

	if (ChangedField.IsNone())
	{
		for (const TPair<FName, TArray<FWebToUERuntimeBindingOp>>& Pair :
			RuntimeInstance->GetBindingIndex())
		{
			ApplyField(Pair.Key);
		}
	}
	else
	{
		ApplyField(ChangedField);
	}
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::BindingNodesUpdated, UpdatedNodes.Num());

	TArray<FWebToUEStyleUpdate> StyleUpdates;
	if (!StyleTargets.IsEmpty())
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::StyleDirtyTargets, StyleTargets.Num());
		FWebToUEStyleResolver::ResolveIncremental(
			*RuntimeDocument, StyleTargets, StyleUpdates);
		Presentation->ApplyStyleUpdates(StyleUpdates);
	}
	if (!RuntimeStateTargets.IsEmpty())
	{
		RuntimeStateTargets.RemoveAll([&StyleUpdates](FWebToUEInstanceHandle Handle)
		{
			return StyleUpdates.ContainsByPredicate(
				[Handle](const FWebToUEStyleUpdate& Update)
				{
					return Update.Target == Handle && EnumHasAnyFlags(
						Update.Changes.Impacts, EWebToUEStyleImpact::HitTest);
				});
		});
		Presentation->ApplyRuntimeStateChanges(RuntimeStateTargets);
	}
	if (bTextChanged || bPaintChanged || !StyleUpdates.IsEmpty())
	{
		const bool bNeedsLayout = bTextMeasureChanged || StyleUpdates.ContainsByPredicate(
			[](const FWebToUEStyleUpdate& Update)
			{
				return EnumHasAnyFlags(Update.Changes.Impacts,
					EWebToUEStyleImpact::Measure | EWebToUEStyleImpact::Layout);
			});
		Invalidate(bNeedsLayout ? EInvalidateWidgetReason::LayoutAndVolatility
			: EInvalidateWidgetReason::Paint);
	}
}

TSet<FName> SWebToUEView::GetBoundFields() const
{
	TSet<FName> Result;
	for (const TPair<FName, TArray<FWebToUERuntimeBindingOp>>& Pair :
		RuntimeInstance->GetBindingIndex())
	{
		Result.Add(Pair.Key);
	}
	return Result;
}

void SWebToUEView::ReportBindingErrorOnce(const FString& Field, const FString& Message)
{
	if (!LoggedBindingErrors.Contains(Field))
	{
		LoggedBindingErrors.Add(Field);
		UE_LOG(LogWebToUE, Warning, TEXT("Binding '%s': %s"), *Field, *Message);
	}
}

FWebToUENode* SWebToUEView::HitTest(const FVector2f& LocalPosition) const
{
	return Presentation->HitTest(LocalPosition);
}
bool SWebToUEView::ScrollAt(const FVector2f& LocalPosition, float WheelDelta)
{
	if (Presentation->ScrollAt(LocalPosition, WheelDelta))
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		SetHoveredNode(HitTest(LocalPosition));
		return true;
	}
	return false;
}

namespace WebToUE::Runtime::PseudoInvalidation::Private
{
	static const TCHAR* StateName(EWebToUEPseudoState State)
	{
		switch (State)
		{
		case EWebToUEPseudoState::Hover: return TEXT("Hover");
		case EWebToUEPseudoState::Active: return TEXT("Active");
		case EWebToUEPseudoState::Focus: return TEXT("Focus");
		case EWebToUEPseudoState::Disabled: return TEXT("Disabled");
		default: return TEXT("None");
		}
	}

	static FString FormatSelector(const FWebToUEStyleRule& Rule)
	{
		FString Result;
		for (int32 Index = 0; Index < Rule.Selector.Num(); ++Index)
		{
			const FWebToUESelectorSegment& Segment = Rule.Selector[Index];
			if (Index > 0)
			{
				Result += Segment.RelationToPrevious == EWebToUECombinator::Child
					? TEXT(" > ") : TEXT(" ");
			}
			Result += Segment.Type.IsEmpty() ? TEXT("*") : Segment.Type;
			if (!Segment.Id.IsEmpty()) Result += TEXT("#") + Segment.Id;
			for (const FString& ClassName : Segment.Classes) Result += TEXT(".") + ClassName;
			if (EnumHasAnyFlags(Segment.RequiredState, EWebToUEPseudoState::Hover)) Result += TEXT(":hover");
			if (EnumHasAnyFlags(Segment.RequiredState, EWebToUEPseudoState::Active)) Result += TEXT(":active");
			if (EnumHasAnyFlags(Segment.RequiredState, EWebToUEPseudoState::Focus)) Result += TEXT(":focus");
			if (EnumHasAnyFlags(Segment.RequiredState, EWebToUEPseudoState::Disabled)) Result += TEXT(":disabled");
		}
		return Result;
	}

	static FString FormatNode(const FWebToUENode& Node)
	{
		const FString Id = Node.GetAttribute(TEXT("id"));
		return Id.IsEmpty() ? Node.Tag : FString::Printf(TEXT("%s#%s"), *Node.Tag, *Id);
	}

	static FString FormatImpacts(EWebToUEStyleImpact Impacts)
	{
		TArray<FString> Names;
		if (EnumHasAnyFlags(Impacts, EWebToUEStyleImpact::Style)) Names.Add(TEXT("Style"));
		if (EnumHasAnyFlags(Impacts, EWebToUEStyleImpact::Measure)) Names.Add(TEXT("Measure"));
		if (EnumHasAnyFlags(Impacts, EWebToUEStyleImpact::Layout)) Names.Add(TEXT("Layout"));
		if (EnumHasAnyFlags(Impacts, EWebToUEStyleImpact::Paint)) Names.Add(TEXT("Paint"));
		if (EnumHasAnyFlags(Impacts, EWebToUEStyleImpact::HitTest)) Names.Add(TEXT("HitTest"));
		if (EnumHasAnyFlags(Impacts, EWebToUEStyleImpact::Resource)) Names.Add(TEXT("Resource"));
		return FString::Join(Names, TEXT("|"));
	}
}

void SWebToUEView::CollectPseudoDependencyTargets(FWebToUENode& ReasonNode,
	EWebToUEPseudoState Flag, TArray<FWebToUEInstanceHandle>& OutTargets) const
{
	const FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument) return;
	const TArray<FWebToUEStyleRule>& Rules = RuntimeDocument->GetRules();
	for (const FWebToUEPseudoInvalidationDependency& Dependency :
		RuntimeDocument->GetPseudoInvalidationDependencies())
	{
		if (Dependency.ReasonState != Flag || !Rules.IsValidIndex(Dependency.RuleIndex))
		{
			continue;
		}
		const FWebToUEStyleRule& Rule = Rules[Dependency.RuleIndex];
		if (!Rule.Selector.IsValidIndex(Dependency.ReasonSegmentIndex)) continue;
		const auto TestTarget = [&](FWebToUEInstanceHandle TargetHandle)
		{
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::PseudoTargetCandidates);
			const FWebToUENode* Target = RuntimeDocument->ResolveNode(TargetHandle);
			if (Target && FWebToUEStyleResolver::MatchesWithReason(
				Rule, *Target, *RuntimeDocument,
				Dependency.ReasonSegmentIndex, ReasonNode))
			{
				OutTargets.AddUnique(TargetHandle);
			}
		};
		if (Dependency.ReasonSegmentIndex == Rule.Selector.Num() - 1)
		{
			TestTarget(ReasonNode.InstanceHandle);
		}
		else
		{
			RuntimeDocument->ForEachPotentialSelectorTarget(Rule.Selector.Last(), TestTarget);
		}
	}
}

void SWebToUEView::UpdatePseudoState(FWebToUENode* OldNode, FWebToUENode* NewNode,
	EWebToUEPseudoState Flag, bool bIncludeAncestors)
{
	using namespace WebToUE::Runtime::PseudoInvalidation::Private;
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument) return;

	struct FTargetReason
	{
		FWebToUEInstanceHandle Target;
		FWebToUEInstanceHandle Source;
		int32 RuleIndex = INDEX_NONE;
	};
	TArray<FWebToUENode*> OldPath;
	TArray<FWebToUENode*> NewPath;
	for (FWebToUENode* Current = OldNode; Current;
		Current = bIncludeAncestors ? Current->Parent : nullptr)
	{
		OldPath.Add(Current);
	}
	for (FWebToUENode* Current = NewNode; Current;
		Current = bIncludeAncestors ? Current->Parent : nullptr)
	{
		NewPath.Add(Current);
	}

	TArray<FTargetReason> DirtyTargets;
	const TArray<FWebToUEStyleRule>& Rules = RuntimeDocument->GetRules();
	const auto CollectReasonTargets = [&](FWebToUENode& ReasonNode)
	{
		for (const FWebToUEPseudoInvalidationDependency& Dependency :
			RuntimeDocument->GetPseudoInvalidationDependencies())
		{
			if (Dependency.ReasonState != Flag ||
				!Rules.IsValidIndex(Dependency.RuleIndex))
			{
				continue;
			}
			const FWebToUEStyleRule& Rule = Rules[Dependency.RuleIndex];
			if (!Rule.Selector.IsValidIndex(Dependency.ReasonSegmentIndex)) continue;
			const auto TestTarget = [&](FWebToUEInstanceHandle TargetHandle)
			{
				FWebToUEPerformanceCapture::RecordCounter(
					EWebToUEPerformanceCounter::PseudoTargetCandidates);
				FWebToUENode* Target = RuntimeDocument->ResolveNode(TargetHandle);
				if (!Target || !FWebToUEStyleResolver::MatchesWithReason(
					Rule, *Target, *RuntimeDocument,
					Dependency.ReasonSegmentIndex, ReasonNode))
				{
					return;
				}
				if (!DirtyTargets.ContainsByPredicate([TargetHandle](const FTargetReason& Existing)
				{
					return Existing.Target == TargetHandle;
				}))
				{
					DirtyTargets.Add({ TargetHandle, ReasonNode.InstanceHandle,
						Dependency.RuleIndex });
				}
			};
			if (Dependency.ReasonSegmentIndex == Rule.Selector.Num() - 1)
			{
				TestTarget(ReasonNode.InstanceHandle);
			}
			else
			{
				RuntimeDocument->ForEachPotentialSelectorTarget(
					Rule.Selector.Last(), TestTarget);
			}
		}
	};

	for (FWebToUENode* Node : OldPath)
	{
		if (NewPath.Contains(Node)) continue;
		CollectReasonTargets(*Node);
		GetRuntimeState(*Node).PseudoStates &= ~Flag;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::PseudoStateNodesChanged);
		CollectReasonTargets(*Node);
	}
	for (FWebToUENode* Node : NewPath)
	{
		if (OldPath.Contains(Node)) continue;
		CollectReasonTargets(*Node);
		GetRuntimeState(*Node).PseudoStates |= Flag;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::PseudoStateNodesChanged);
		CollectReasonTargets(*Node);
	}

	TArray<FWebToUEInstanceHandle> Targets;
	Targets.Reserve(DirtyTargets.Num());
	for (const FTargetReason& DirtyTarget : DirtyTargets) Targets.Add(DirtyTarget.Target);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::StyleDirtyTargets, Targets.Num());
	TArray<FWebToUEStyleUpdate> Updates;
	if (!Targets.IsEmpty())
	{
		FWebToUEStyleResolver::ResolveIncremental(*RuntimeDocument, Targets, Updates);
	}

	EWebToUEStyleImpact CombinedImpacts = EWebToUEStyleImpact::None;
	uint64 PropertyChangeCount = 0;
	TArray<FString> ReportLines;
	for (const FWebToUEStyleUpdate& Update : Updates)
	{
		CombinedImpacts |= Update.Changes.Impacts;
		PropertyChangeCount += Update.Changes.ChangedProperties.Num();
		const FWebToUENode* Target = RuntimeDocument->ResolveNode(Update.Target);
		if (!Target) continue;
		const FTargetReason* Reason = DirtyTargets.FindByPredicate(
			[Target](const FTargetReason& Candidate)
			{
				for (const FWebToUENode* Current = Target; Current; Current = Current->Parent)
				{
					if (Current->InstanceHandle == Candidate.Target) return true;
				}
				return false;
			});
		const FWebToUENode* Source = Reason ? RuntimeDocument->ResolveNode(Reason->Source) : nullptr;
		TArray<FString> Properties;
		for (const EWebToUECssProperty Property : Update.Changes.ChangedProperties)
		{
			Properties.Add(WebToUE::Private::LexToString(Property));
		}
		ReportLines.Add(FString::Printf(TEXT("%s %s -> %s -> %s -> %s -> %s"),
			StateName(Flag), Source ? *FormatNode(*Source) : TEXT("<source>"),
			Reason && Rules.IsValidIndex(Reason->RuleIndex)
				? *FormatSelector(Rules[Reason->RuleIndex]) : TEXT("<inherit>"),
			*FString::Join(Properties, TEXT("|")),
			*FormatImpacts(Update.Changes.Impacts), *FormatNode(*Target)));
	}
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::StylePropertyChanges, PropertyChangeCount);
	LastPseudoInvalidationReport = ReportLines.IsEmpty()
		? FString::Printf(TEXT("%s -> no computed property change"), StateName(Flag))
		: FString::Join(ReportLines, TEXT("\n"));

	if (!Updates.IsEmpty())
	{
		Presentation->ApplyStyleUpdates(Updates);
		Invalidate(EnumHasAnyFlags(CombinedImpacts,
			EWebToUEStyleImpact::Measure | EWebToUEStyleImpact::Layout)
			? EInvalidateWidgetReason::LayoutAndVolatility
			: EInvalidateWidgetReason::Paint);
	}
}

void SWebToUEView::SetHoveredNode(FWebToUENode* Node)
{
	FWebToUENode* OldNode = RuntimeInstance->GetHoveredNode();
	if (OldNode == Node) return;
	UpdatePseudoState(OldNode, Node, EWebToUEPseudoState::Hover, true);
	RuntimeInstance->SetHoveredNode(Node);
}

void SWebToUEView::SetPressedNode(FWebToUENode* Node)
{
	FWebToUENode* OldNode = RuntimeInstance->GetPressedNode();
	if (OldNode == Node) return;
	UpdatePseudoState(OldNode, Node, EWebToUEPseudoState::Active, false);
	RuntimeInstance->SetPressedNode(Node);
}

void SWebToUEView::SetFocusedNode(FWebToUENode* Node)
{
	FWebToUENode* OldNode = RuntimeInstance->GetFocusedNode();
	if (OldNode == Node) return;
	UpdatePseudoState(OldNode, Node, EWebToUEPseudoState::Focus, false);
	RuntimeInstance->SetFocusedNode(Node);
}

FReply SWebToUEView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SetHoveredNode(HitTest(FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()))));
	return FReply::Handled();
}

void SWebToUEView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SetHoveredNode(nullptr);
	SLeafWidget::OnMouseLeave(MouseEvent);
}

FReply SWebToUEView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	FWebToUENode* Hit = HitTest(FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition())));
	if (!Hit) return FReply::Unhandled();
	SetFocusedNode(Hit);
	SetPressedNode(Hit);
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse, true).CaptureMouse(AsShared());
}

FReply SWebToUEView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FWebToUENode* PressedNode = RuntimeInstance->GetPressedNode();
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !PressedNode) return FReply::Unhandled();
	FWebToUENode* Released = PressedNode;
	const bool bActivate = HitTest(FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()))) == Released;
	SetPressedNode(nullptr);
	if (bActivate) DispatchClick(*Released);
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SWebToUEView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f LocalPosition = FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	return ScrollAt(LocalPosition, MouseEvent.GetWheelDelta()) ? FReply::Handled() : FReply::Unhandled();
}

void SWebToUEView::MoveFocus(int32 Direction)
{
	TArray<FWebToUENode*> Nodes;
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument) return;
	RuntimeDocument->ForEachNode([this, &Nodes](FWebToUENode& Node)
	{
		const FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
		if (RuntimeDocument && Node.IsInteractive() && RuntimeDocument->IsDisplayed(Node) &&
			GetComputedStyle(Node).bEnabled)
		{
			Nodes.Add(&Node);
		}
	});
	Nodes.Sort([this](const FWebToUENode& A, const FWebToUENode& B)
	{
		return GetLayoutResult(A).PaintOrder < GetLayoutResult(B).PaintOrder;
	});
	if (Nodes.IsEmpty()) return;
	int32 Index = Nodes.IndexOfByKey(RuntimeInstance->GetFocusedNode());
	Index = Index == INDEX_NONE ? (Direction > 0 ? 0 : Nodes.Num() - 1) : (Index + Direction + Nodes.Num()) % Nodes.Num();
	SetFocusedNode(Nodes[Index]);
}

void SWebToUEView::ActivateFocusedNode()
{
	FWebToUENode* FocusedNode = RuntimeInstance->GetFocusedNode();
	if (FocusedNode && GetComputedStyle(*FocusedNode).bEnabled) DispatchClick(*FocusedNode);
}

FReply SWebToUEView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Tab)
	{
		MoveFocus(KeyEvent.IsShiftDown() ? -1 : 1);
		return FReply::Handled();
	}
	if (KeyEvent.GetKey() == EKeys::Enter || KeyEvent.GetKey() == EKeys::SpaceBar)
	{
		ActivateFocusedNode();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SWebToUEView::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (!RuntimeInstance->GetFocusedNode()) MoveFocus(1);
	return FReply::Handled();
}

void SWebToUEView::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	SetPressedNode(nullptr);
	SLeafWidget::OnFocusLost(InFocusEvent);
}

void SWebToUEView::DispatchClick(FWebToUENode& Node) const
{
	const FString Event = Node.GetAttribute(TEXT("data-ue-on-click"));
	if (!Event.IsEmpty())
	{
		if (UWebToUEView* View = Owner.Get()) View->HandleRuntimeEvent(FName(*Event), FName(*Node.GetAttribute(TEXT("id"))));
	}
}
