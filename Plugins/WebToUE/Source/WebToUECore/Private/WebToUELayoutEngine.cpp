#include "WebToUECompiler.h"

#include "WebToUEPerformance.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include <yoga/Yoga.h>

namespace WebToUE::Private
{
	static void SetDimension(YGNodeRef Node, const FWebToUELength& Length,
		void (*SetPoint)(YGNodeRef, float), void (*SetPercent)(YGNodeRef, float),
		void (*SetAuto)(YGNodeRef))
	{
		switch (Length.Unit)
		{
		case EWebToUEUnit::Pixels: SetPoint(Node, Length.Value); break;
		case EWebToUEUnit::Percent: SetPercent(Node, Length.Value); break;
		case EWebToUEUnit::Auto: if (SetAuto) SetAuto(Node); else SetPoint(Node, YGUndefined); break;
		default: if (SetAuto) SetAuto(Node); else SetPoint(Node, YGUndefined); break;
		}
	}

	static void SetEdge(YGNodeRef Node, YGEdge Edge, const FWebToUELength& Length,
		void (*SetPoint)(YGNodeRef, YGEdge, float),
		void (*SetPercent)(YGNodeRef, YGEdge, float),
		void (*SetAuto)(YGNodeRef, YGEdge))
	{
		switch (Length.Unit)
		{
		case EWebToUEUnit::Pixels: SetPoint(Node, Edge, Length.Value); break;
		case EWebToUEUnit::Percent: SetPercent(Node, Edge, Length.Value); break;
		case EWebToUEUnit::Auto: if (SetAuto) SetAuto(Node, Edge); else SetPoint(Node, Edge, YGUndefined); break;
		default: SetPoint(Node, Edge, YGUndefined); break;
		}
	}

	static YGAlign ToAlign(const FString& Value, YGAlign Fallback)
	{
		if (Value == TEXT("flex-start")) return YGAlignFlexStart;
		if (Value == TEXT("center")) return YGAlignCenter;
		if (Value == TEXT("flex-end")) return YGAlignFlexEnd;
		if (Value == TEXT("stretch")) return YGAlignStretch;
		if (Value == TEXT("baseline")) return YGAlignBaseline;
		if (Value == TEXT("auto")) return YGAlignAuto;
		return Fallback;
	}

	static YGJustify ToJustify(const FString& Value)
	{
		if (Value == TEXT("center")) return YGJustifyCenter;
		if (Value == TEXT("flex-end")) return YGJustifyFlexEnd;
		if (Value == TEXT("space-between")) return YGJustifySpaceBetween;
		if (Value == TEXT("space-around")) return YGJustifySpaceAround;
		if (Value == TEXT("space-evenly")) return YGJustifySpaceEvenly;
		return YGJustifyFlexStart;
	}
}

struct FWebToUELayoutEngine::FImpl
{
	struct FMeasureContext
	{
		FImpl* Owner = nullptr;
		FWebToUEInstanceHandle NodeHandle;
	};

	YGNodeRef Root = nullptr;
	FWebToUEDocument* Document = nullptr;
	const FMeasureNode* ActiveMeasureNode = nullptr;
	TMap<FWebToUEInstanceHandle, YGNodeRef> Nodes;
	TArray<TUniquePtr<FMeasureContext>> MeasureContexts;

	~FImpl()
	{
		Reset();
	}

	void Reset()
	{
		if (Root)
		{
			YGNodeFreeRecursive(Root);
		}
		Root = nullptr;
		Document = nullptr;
		ActiveMeasureNode = nullptr;
		Nodes.Reset();
		MeasureContexts.Reset();
	}

	static YGSize MeasureYogaNode(YGNodeConstRef Node, float Width, YGMeasureMode WidthMode,
		float Height, YGMeasureMode HeightMode)
	{
		SCOPE_CYCLE_COUNTER(STAT_WebToUE_Measure);
		TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_Measure);
		FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::Measure);
		const FMeasureContext* Context = static_cast<const FMeasureContext*>(YGNodeGetContext(Node));
		if (!Context || !Context->Owner || !Context->Owner->Document ||
			!Context->Owner->ActiveMeasureNode)
		{
			return { 0.0f, 0.0f };
		}
		const FWebToUENode* WebNode = Context->Owner->Document->ResolveNode(Context->NodeHandle);
		if (!WebNode) return { 0.0f, 0.0f };
		const auto ToMeasureMode = [](YGMeasureMode Mode)
		{
			switch (Mode)
			{
			case YGMeasureModeExactly: return EMeasureMode::Exactly;
			case YGMeasureModeAtMost: return EMeasureMode::AtMost;
			default: return EMeasureMode::Undefined;
			}
		};
		const FMeasureConstraints Constraints = {
			Width, Height, ToMeasureMode(WidthMode), ToMeasureMode(HeightMode)
		};
		FVector2f Measured = (*Context->Owner->ActiveMeasureNode)(*WebNode, Constraints);
		if (WidthMode == YGMeasureModeExactly) Measured.X = Width;
		else if (WidthMode == YGMeasureModeAtMost) Measured.X = FMath::Min(Measured.X, Width);
		if (HeightMode == YGMeasureModeExactly) Measured.Y = Height;
		else if (HeightMode == YGMeasureModeAtMost) Measured.Y = FMath::Min(Measured.Y, Height);
		return { FMath::Max(0.0f, Measured.X), FMath::Max(0.0f, Measured.Y) };
	}

	static void ApplyAllStyle(YGNodeRef Node, const FWebToUEComputedStyle& S)
	{
		using namespace WebToUE::Private;
		YGNodeStyleSetDisplay(Node, S.Display == EWebToUEDisplay::None ? YGDisplayNone : YGDisplayFlex);
		YGNodeStyleSetPositionType(Node,
			S.Position == EWebToUEPosition::Absolute ? YGPositionTypeAbsolute : YGPositionTypeRelative);
		YGNodeStyleSetOverflow(Node,
			S.Overflow == EWebToUEOverflow::Hidden ? YGOverflowHidden :
			S.Overflow == EWebToUEOverflow::Visible ? YGOverflowVisible : YGOverflowScroll);
		YGNodeStyleSetFlexDirection(Node,
			S.FlexDirection == EWebToUEFlexDirection::Row ? YGFlexDirectionRow :
			S.FlexDirection == EWebToUEFlexDirection::RowReverse ? YGFlexDirectionRowReverse :
			S.FlexDirection == EWebToUEFlexDirection::ColumnReverse ? YGFlexDirectionColumnReverse :
			YGFlexDirectionColumn);
		YGNodeStyleSetFlexWrap(Node,
			S.FlexWrap == TEXT("wrap") ? YGWrapWrap :
			S.FlexWrap == TEXT("wrap-reverse") ? YGWrapWrapReverse : YGWrapNoWrap);
		YGNodeStyleSetJustifyContent(Node, ToJustify(S.JustifyContent));
		YGNodeStyleSetAlignItems(Node, ToAlign(S.AlignItems, YGAlignStretch));
		YGNodeStyleSetAlignSelf(Node, ToAlign(S.AlignSelf, YGAlignAuto));
		YGNodeStyleSetFlexGrow(Node, S.FlexGrow);
		YGNodeStyleSetFlexShrink(Node, S.FlexShrink);
		SetDimension(Node, S.FlexBasis, YGNodeStyleSetFlexBasis,
			YGNodeStyleSetFlexBasisPercent, YGNodeStyleSetFlexBasisAuto);
		SetDimension(Node, S.Width, YGNodeStyleSetWidth, YGNodeStyleSetWidthPercent, YGNodeStyleSetWidthAuto);
		SetDimension(Node, S.Height, YGNodeStyleSetHeight, YGNodeStyleSetHeightPercent, YGNodeStyleSetHeightAuto);
		SetDimension(Node, S.MinWidth, YGNodeStyleSetMinWidth, YGNodeStyleSetMinWidthPercent, nullptr);
		SetDimension(Node, S.MinHeight, YGNodeStyleSetMinHeight, YGNodeStyleSetMinHeightPercent, nullptr);
		SetDimension(Node, S.MaxWidth, YGNodeStyleSetMaxWidth, YGNodeStyleSetMaxWidthPercent, nullptr);
		SetDimension(Node, S.MaxHeight, YGNodeStyleSetMaxHeight, YGNodeStyleSetMaxHeightPercent, nullptr);
		SetEdge(Node, YGEdgeLeft, S.Margin.Left, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
		SetEdge(Node, YGEdgeTop, S.Margin.Top, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
		SetEdge(Node, YGEdgeRight, S.Margin.Right, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
		SetEdge(Node, YGEdgeBottom, S.Margin.Bottom, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto);
		SetEdge(Node, YGEdgeLeft, S.Padding.Left, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr);
		SetEdge(Node, YGEdgeTop, S.Padding.Top, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr);
		SetEdge(Node, YGEdgeRight, S.Padding.Right, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr);
		SetEdge(Node, YGEdgeBottom, S.Padding.Bottom, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr);
		SetEdge(Node, YGEdgeLeft, S.Inset.Left, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr);
		SetEdge(Node, YGEdgeTop, S.Inset.Top, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr);
		SetEdge(Node, YGEdgeRight, S.Inset.Right, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr);
		SetEdge(Node, YGEdgeBottom, S.Inset.Bottom, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr);
		YGNodeStyleSetGap(Node, YGGutterRow, S.RowGap);
		YGNodeStyleSetGap(Node, YGGutterColumn, S.ColumnGap);
		YGNodeStyleSetBorder(Node, YGEdgeAll, S.BorderWidth);
	}

	static bool ApplyStyleProperty(YGNodeRef Node, const FWebToUEComputedStyle& S,
		EWebToUECssProperty Property)
	{
		using namespace WebToUE::Private;
		switch (Property)
		{
		case EWebToUECssProperty::Display: YGNodeStyleSetDisplay(Node, S.Display == EWebToUEDisplay::None ? YGDisplayNone : YGDisplayFlex); break;
		case EWebToUECssProperty::Position: YGNodeStyleSetPositionType(Node, S.Position == EWebToUEPosition::Absolute ? YGPositionTypeAbsolute : YGPositionTypeRelative); break;
		case EWebToUECssProperty::Overflow: YGNodeStyleSetOverflow(Node, S.Overflow == EWebToUEOverflow::Hidden ? YGOverflowHidden : S.Overflow == EWebToUEOverflow::Visible ? YGOverflowVisible : YGOverflowScroll); break;
		case EWebToUECssProperty::FlexDirection: YGNodeStyleSetFlexDirection(Node, S.FlexDirection == EWebToUEFlexDirection::Row ? YGFlexDirectionRow : S.FlexDirection == EWebToUEFlexDirection::RowReverse ? YGFlexDirectionRowReverse : S.FlexDirection == EWebToUEFlexDirection::ColumnReverse ? YGFlexDirectionColumnReverse : YGFlexDirectionColumn); break;
		case EWebToUECssProperty::FlexWrap: YGNodeStyleSetFlexWrap(Node, S.FlexWrap == TEXT("wrap") ? YGWrapWrap : S.FlexWrap == TEXT("wrap-reverse") ? YGWrapWrapReverse : YGWrapNoWrap); break;
		case EWebToUECssProperty::JustifyContent: YGNodeStyleSetJustifyContent(Node, ToJustify(S.JustifyContent)); break;
		case EWebToUECssProperty::AlignItems: YGNodeStyleSetAlignItems(Node, ToAlign(S.AlignItems, YGAlignStretch)); break;
		case EWebToUECssProperty::AlignSelf: YGNodeStyleSetAlignSelf(Node, ToAlign(S.AlignSelf, YGAlignAuto)); break;
		case EWebToUECssProperty::FlexGrow: YGNodeStyleSetFlexGrow(Node, S.FlexGrow); break;
		case EWebToUECssProperty::FlexShrink: YGNodeStyleSetFlexShrink(Node, S.FlexShrink); break;
		case EWebToUECssProperty::FlexBasis: SetDimension(Node, S.FlexBasis, YGNodeStyleSetFlexBasis, YGNodeStyleSetFlexBasisPercent, YGNodeStyleSetFlexBasisAuto); break;
		case EWebToUECssProperty::Width: SetDimension(Node, S.Width, YGNodeStyleSetWidth, YGNodeStyleSetWidthPercent, YGNodeStyleSetWidthAuto); break;
		case EWebToUECssProperty::Height: SetDimension(Node, S.Height, YGNodeStyleSetHeight, YGNodeStyleSetHeightPercent, YGNodeStyleSetHeightAuto); break;
		case EWebToUECssProperty::MinWidth: SetDimension(Node, S.MinWidth, YGNodeStyleSetMinWidth, YGNodeStyleSetMinWidthPercent, nullptr); break;
		case EWebToUECssProperty::MinHeight: SetDimension(Node, S.MinHeight, YGNodeStyleSetMinHeight, YGNodeStyleSetMinHeightPercent, nullptr); break;
		case EWebToUECssProperty::MaxWidth: SetDimension(Node, S.MaxWidth, YGNodeStyleSetMaxWidth, YGNodeStyleSetMaxWidthPercent, nullptr); break;
		case EWebToUECssProperty::MaxHeight: SetDimension(Node, S.MaxHeight, YGNodeStyleSetMaxHeight, YGNodeStyleSetMaxHeightPercent, nullptr); break;
		case EWebToUECssProperty::MarginLeft: SetEdge(Node, YGEdgeLeft, S.Margin.Left, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto); break;
		case EWebToUECssProperty::MarginTop: SetEdge(Node, YGEdgeTop, S.Margin.Top, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto); break;
		case EWebToUECssProperty::MarginRight: SetEdge(Node, YGEdgeRight, S.Margin.Right, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto); break;
		case EWebToUECssProperty::MarginBottom: SetEdge(Node, YGEdgeBottom, S.Margin.Bottom, YGNodeStyleSetMargin, YGNodeStyleSetMarginPercent, YGNodeStyleSetMarginAuto); break;
		case EWebToUECssProperty::PaddingLeft: SetEdge(Node, YGEdgeLeft, S.Padding.Left, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr); break;
		case EWebToUECssProperty::PaddingTop: SetEdge(Node, YGEdgeTop, S.Padding.Top, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr); break;
		case EWebToUECssProperty::PaddingRight: SetEdge(Node, YGEdgeRight, S.Padding.Right, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr); break;
		case EWebToUECssProperty::PaddingBottom: SetEdge(Node, YGEdgeBottom, S.Padding.Bottom, YGNodeStyleSetPadding, YGNodeStyleSetPaddingPercent, nullptr); break;
		case EWebToUECssProperty::Left: SetEdge(Node, YGEdgeLeft, S.Inset.Left, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr); break;
		case EWebToUECssProperty::Top: SetEdge(Node, YGEdgeTop, S.Inset.Top, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr); break;
		case EWebToUECssProperty::Right: SetEdge(Node, YGEdgeRight, S.Inset.Right, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr); break;
		case EWebToUECssProperty::Bottom: SetEdge(Node, YGEdgeBottom, S.Inset.Bottom, YGNodeStyleSetPosition, YGNodeStyleSetPositionPercent, nullptr); break;
		case EWebToUECssProperty::RowGap: YGNodeStyleSetGap(Node, YGGutterRow, S.RowGap); break;
		case EWebToUECssProperty::ColumnGap: YGNodeStyleSetGap(Node, YGGutterColumn, S.ColumnGap); break;
		case EWebToUECssProperty::BorderWidth: YGNodeStyleSetBorder(Node, YGEdgeAll, S.BorderWidth); break;
		default: return false;
		}
		return true;
	}

	YGNodeRef BuildTree(FWebToUEDocument& InDocument, FWebToUENode& WebNode)
	{
		YGNodeRef Node = YGNodeNew();
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::YogaNodesBuilt);
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		Nodes.Add(WebNode.InstanceHandle, Node);
		ApplyAllStyle(Node, InDocument.GetComputedStyle(WebNode));
		if ((WebNode.Type == EWebToUENodeType::Text || WebNode.Tag == TEXT("img")) &&
			WebNode.Children.IsEmpty())
		{
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
			TUniquePtr<FMeasureContext>& Context =
				MeasureContexts.Add_GetRef(MakeUnique<FMeasureContext>());
			Context->Owner = this;
			Context->NodeHandle = WebNode.InstanceHandle;
			YGNodeSetContext(Node, Context.Get());
			YGNodeSetMeasureFunc(Node, MeasureYogaNode);
		}
		for (int32 Index = 0; Index < WebNode.Children.Num(); ++Index)
		{
			YGNodeInsertChild(Node, BuildTree(InDocument, *WebNode.Children[Index]), Index);
		}
		return Node;
	}

	void EnsureTree(FWebToUEDocument& InDocument)
	{
		if (Document == &InDocument && Root) return;
		Reset();
		Document = &InDocument;
		if (InDocument.Root) Root = BuildTree(InDocument, *InDocument.Root);
	}

	static void CopyYogaLayout(FWebToUEDocument& InDocument, FWebToUENode& WebNode,
		YGNodeConstRef Node, const FVector2f ParentPosition, int32& PaintOrder)
	{
		FWebToUERuntimeLayoutResult& LayoutResult = InDocument.GetLayoutResult(WebNode);
		const FVector2f NewPosition = ParentPosition +
			FVector2f(YGNodeLayoutGetLeft(Node), YGNodeLayoutGetTop(Node));
		const FVector2f NewSize(YGNodeLayoutGetWidth(Node), YGNodeLayoutGetHeight(Node));
		if (!LayoutResult.Position.Equals(NewPosition) || !LayoutResult.Size.Equals(NewSize))
		{
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::YogaLayoutResultsChanged);
		}
		LayoutResult.Position = NewPosition;
		LayoutResult.Size = NewSize;
		LayoutResult.PaintOrder = PaintOrder++;
		for (int32 Index = 0; Index < WebNode.Children.Num(); ++Index)
		{
			CopyYogaLayout(InDocument, *WebNode.Children[Index],
				YGNodeGetChild(const_cast<YGNodeRef>(Node), Index), LayoutResult.Position, PaintOrder);
		}
	}

	static FVector2f UpdateScrollExtents(FWebToUEDocument& InDocument, FWebToUENode& Node)
	{
		FWebToUERuntimeNodeState& RuntimeState = InDocument.GetRuntimeNodeState(Node);
		const FWebToUERuntimeLayoutResult& LayoutResult = InDocument.GetLayoutResult(Node);
		FVector2f ContentMax = LayoutResult.Position + LayoutResult.Size;
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			const FVector2f ChildContentMax = UpdateScrollExtents(InDocument, *Child);
			const FWebToUERuntimeLayoutResult& ChildLayout = InDocument.GetLayoutResult(*Child);
			ContentMax.X = FMath::Max(ContentMax.X, InDocument.ClipsOverflow(*Child)
				? ChildLayout.Position.X + ChildLayout.Size.X : ChildContentMax.X);
			ContentMax.Y = FMath::Max(ContentMax.Y, InDocument.ClipsOverflow(*Child)
				? ChildLayout.Position.Y + ChildLayout.Size.Y : ChildContentMax.Y);
		}
		if (InDocument.IsScrollable(Node))
		{
			RuntimeState.MaxScrollOffset = FVector2f(
				FMath::Max(0.0f, ContentMax.X - (LayoutResult.Position.X + LayoutResult.Size.X)),
				FMath::Max(0.0f, ContentMax.Y - (LayoutResult.Position.Y + LayoutResult.Size.Y)));
			RuntimeState.ScrollOffset.X = FMath::Clamp(RuntimeState.ScrollOffset.X, 0.0f, RuntimeState.MaxScrollOffset.X);
			RuntimeState.ScrollOffset.Y = FMath::Clamp(RuntimeState.ScrollOffset.Y, 0.0f, RuntimeState.MaxScrollOffset.Y);
		}
		else
		{
			RuntimeState.ScrollOffset = FVector2f::ZeroVector;
			RuntimeState.MaxScrollOffset = FVector2f::ZeroVector;
		}
		return InDocument.ClipsOverflow(Node) ? LayoutResult.Position + LayoutResult.Size : ContentMax;
	}
};

FWebToUELayoutEngine::FWebToUELayoutEngine()
	: Impl(MakeUnique<FImpl>())
{
}

FWebToUELayoutEngine::~FWebToUELayoutEngine() = default;

void FWebToUELayoutEngine::Reset()
{
	Impl->Reset();
}

void FWebToUELayoutEngine::ApplyStyleUpdates(FWebToUEDocument& Document,
	TConstArrayView<FWebToUEStyleUpdate> Updates)
{
	if (Impl->Document != &Document || !Impl->Root) return;
	for (const FWebToUEStyleUpdate& Update : Updates)
	{
		FWebToUENode* WebNode = Document.ResolveNode(Update.Target);
		YGNodeRef* YogaNode = Impl->Nodes.Find(Update.Target);
		if (!WebNode || !YogaNode) continue;
		const FWebToUEComputedStyle& Style = Document.GetComputedStyle(*WebNode);
		bool bMeasureDirty = false;
		for (const EWebToUECssProperty Property : Update.Changes.ChangedProperties)
		{
			if (FImpl::ApplyStyleProperty(*YogaNode, Style, Property))
			{
				FWebToUEPerformanceCapture::RecordCounter(
					EWebToUEPerformanceCounter::YogaStyleWrites);
			}
			bMeasureDirty |= EnumHasAnyFlags(
				WebToUE::Private::GetCssPropertyMetadata(Property).Impacts,
				EWebToUEStyleImpact::Measure);
		}
		if (bMeasureDirty && WebNode->Children.IsEmpty() &&
			(WebNode->Type == EWebToUENodeType::Text || WebNode->Tag == TEXT("img")))
		{
			YGNodeMarkDirty(*YogaNode);
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::YogaNodesDirtied);
		}
	}
}

void FWebToUELayoutEngine::MarkMeasureDirty(FWebToUEDocument& Document,
	FWebToUEInstanceHandle Target)
{
	if (Impl->Document != &Document || !Impl->Root) return;
	FWebToUENode* WebNode = Document.ResolveNode(Target);
	YGNodeRef* YogaNode = Impl->Nodes.Find(Target);
	if (!WebNode || !YogaNode || !WebNode->Children.IsEmpty() ||
		(WebNode->Type != EWebToUENodeType::Text && WebNode->Tag != TEXT("img")))
	{
		return;
	}
	YGNodeMarkDirty(*YogaNode);
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::YogaNodesDirtied);
}

void FWebToUELayoutEngine::LayoutPersistent(FWebToUEDocument& Document,
	const FVector2f& ViewportSize, const FMeasureNode& MeasureNode)
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_Layout);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_Layout);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::Layout);
	if (!Document.Root) return;
	Impl->EnsureTree(Document);
	Impl->ActiveMeasureNode = &MeasureNode;
	YGNodeStyleSetWidth(Impl->Root, ViewportSize.X);
	YGNodeStyleSetHeight(Impl->Root, ViewportSize.Y);
	YGNodeCalculateLayout(Impl->Root, ViewportSize.X, ViewportSize.Y, YGDirectionLTR);
	int32 PaintOrder = 0;
	FImpl::CopyYogaLayout(Document, *Document.Root, Impl->Root, FVector2f::ZeroVector, PaintOrder);
	FImpl::UpdateScrollExtents(Document, *Document.Root);
	Impl->ActiveMeasureNode = nullptr;
}

void FWebToUELayoutEngine::Layout(FWebToUEDocument& Document,
	const FVector2f& ViewportSize, const FMeasureNode& MeasureNode)
{
	FWebToUELayoutEngine TransientEngine;
	TransientEngine.LayoutPersistent(Document, ViewportSize, MeasureNode);
}
