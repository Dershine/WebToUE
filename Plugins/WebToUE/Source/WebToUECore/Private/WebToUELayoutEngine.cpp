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
		case EWebToUEUnit::Auto: if (SetAuto) SetAuto(Node); break;
		default: break;
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
		case EWebToUEUnit::Auto: if (SetAuto) SetAuto(Node, Edge); break;
		default: break;
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

	struct FYogaMeasureContext
	{
		const FWebToUEDocument* Document = nullptr;
		FWebToUEInstanceHandle NodeHandle;
		const FWebToUELayoutEngine::FMeasureNode* MeasureNode = nullptr;
	};

	static FWebToUELayoutEngine::EMeasureMode ToMeasureMode(YGMeasureMode Mode)
	{
		switch (Mode)
		{
		case YGMeasureModeExactly: return FWebToUELayoutEngine::EMeasureMode::Exactly;
		case YGMeasureModeAtMost: return FWebToUELayoutEngine::EMeasureMode::AtMost;
		default: return FWebToUELayoutEngine::EMeasureMode::Undefined;
		}
	}

	static YGSize MeasureYogaNode(YGNodeConstRef Node, float Width, YGMeasureMode WidthMode,
		float Height, YGMeasureMode HeightMode)
	{
		SCOPE_CYCLE_COUNTER(STAT_WebToUE_Measure);
		TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_Measure);
		FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::Measure);
		const FYogaMeasureContext* Context = static_cast<const FYogaMeasureContext*>(YGNodeGetContext(Node));
		if (!Context || !Context->Document || !Context->MeasureNode) return { 0.0f, 0.0f };
		const FWebToUENode* WebNode = Context->Document->ResolveNode(Context->NodeHandle);
		if (!WebNode) return { 0.0f, 0.0f };
		const FWebToUELayoutEngine::FMeasureConstraints Constraints = {
			Width, Height, ToMeasureMode(WidthMode), ToMeasureMode(HeightMode)
		};
		FVector2f Measured = (*Context->MeasureNode)(*WebNode, Constraints);
		if (WidthMode == YGMeasureModeExactly) Measured.X = Width;
		else if (WidthMode == YGMeasureModeAtMost) Measured.X = FMath::Min(Measured.X, Width);
		if (HeightMode == YGMeasureModeExactly) Measured.Y = Height;
		else if (HeightMode == YGMeasureModeAtMost) Measured.Y = FMath::Min(Measured.Y, Height);
		return { FMath::Max(0.0f, Measured.X), FMath::Max(0.0f, Measured.Y) };
	}

	static YGNodeRef BuildYogaTree(FWebToUEDocument& Document, FWebToUENode& WebNode,
		const FWebToUELayoutEngine::FMeasureNode& MeasureNode,
		TArray<TUniquePtr<FYogaMeasureContext>>& MeasureContexts)
	{
		YGNodeRef Node = YGNodeNew();
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::YogaNodesBuilt);
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		const FWebToUEComputedStyle& S = Document.GetComputedStyle(WebNode);
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

		if ((WebNode.Type == EWebToUENodeType::Text || WebNode.Tag == TEXT("img")) &&
			WebNode.Children.IsEmpty())
		{
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
			TUniquePtr<FYogaMeasureContext>& Context =
				MeasureContexts.Add_GetRef(MakeUnique<FYogaMeasureContext>());
			Context->Document = &Document;
			Context->NodeHandle = WebNode.InstanceHandle;
			Context->MeasureNode = &MeasureNode;
			YGNodeSetContext(Node, Context.Get());
			YGNodeSetMeasureFunc(Node, MeasureYogaNode);
		}
		for (int32 Index = 0; Index < WebNode.Children.Num(); ++Index)
		{
			YGNodeInsertChild(Node,
				BuildYogaTree(Document, *WebNode.Children[Index], MeasureNode, MeasureContexts), Index);
		}
		return Node;
	}

	static void CopyYogaLayout(FWebToUEDocument& Document, FWebToUENode& WebNode,
		YGNodeConstRef Node, const FVector2f ParentPosition, int32& PaintOrder)
	{
		FWebToUERuntimeLayoutResult& LayoutResult = Document.GetLayoutResult(WebNode);
		LayoutResult.Position = ParentPosition +
			FVector2f(YGNodeLayoutGetLeft(Node), YGNodeLayoutGetTop(Node));
		LayoutResult.Size = FVector2f(YGNodeLayoutGetWidth(Node), YGNodeLayoutGetHeight(Node));
		LayoutResult.PaintOrder = PaintOrder++;
		for (int32 Index = 0; Index < WebNode.Children.Num(); ++Index)
		{
			CopyYogaLayout(Document, *WebNode.Children[Index],
				YGNodeGetChild(const_cast<YGNodeRef>(Node), Index), LayoutResult.Position, PaintOrder);
		}
	}

	static FVector2f UpdateScrollExtents(FWebToUEDocument& Document, FWebToUENode& Node)
	{
		FWebToUERuntimeNodeState& RuntimeState = Document.GetRuntimeNodeState(Node);
		const FWebToUERuntimeLayoutResult& LayoutResult = Document.GetLayoutResult(Node);
		FVector2f ContentMax = LayoutResult.Position + LayoutResult.Size;
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			const FVector2f ChildContentMax = UpdateScrollExtents(Document, *Child);
			const FWebToUERuntimeLayoutResult& ChildLayout = Document.GetLayoutResult(*Child);
			ContentMax.X = FMath::Max(ContentMax.X,
				Document.ClipsOverflow(*Child)
					? ChildLayout.Position.X + ChildLayout.Size.X
					: ChildContentMax.X);
			ContentMax.Y = FMath::Max(ContentMax.Y,
				Document.ClipsOverflow(*Child)
					? ChildLayout.Position.Y + ChildLayout.Size.Y
					: ChildContentMax.Y);
		}

		if (Document.IsScrollable(Node))
		{
			RuntimeState.MaxScrollOffset = FVector2f(
				FMath::Max(0.0f, ContentMax.X - (LayoutResult.Position.X + LayoutResult.Size.X)),
				FMath::Max(0.0f, ContentMax.Y - (LayoutResult.Position.Y + LayoutResult.Size.Y)));
			RuntimeState.ScrollOffset.X = FMath::Clamp(
				RuntimeState.ScrollOffset.X, 0.0f, RuntimeState.MaxScrollOffset.X);
			RuntimeState.ScrollOffset.Y = FMath::Clamp(
				RuntimeState.ScrollOffset.Y, 0.0f, RuntimeState.MaxScrollOffset.Y);
		}
		else
		{
			RuntimeState.ScrollOffset = FVector2f::ZeroVector;
			RuntimeState.MaxScrollOffset = FVector2f::ZeroVector;
		}

		return Document.ClipsOverflow(Node)
			? LayoutResult.Position + LayoutResult.Size
			: ContentMax;
	}
}

void FWebToUELayoutEngine::Layout(FWebToUEDocument& Document, const FVector2f& ViewportSize,
	const FMeasureNode& MeasureNode)
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_Layout);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_Layout);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::Layout);
	if (!Document.Root) return;
	TArray<TUniquePtr<WebToUE::Private::FYogaMeasureContext>> MeasureContexts;
	YGNodeRef Root = WebToUE::Private::BuildYogaTree(
		Document, *Document.Root, MeasureNode, MeasureContexts);
	YGNodeStyleSetWidth(Root, ViewportSize.X);
	YGNodeStyleSetHeight(Root, ViewportSize.Y);
	YGNodeCalculateLayout(Root, ViewportSize.X, ViewportSize.Y, YGDirectionLTR);
	int32 PaintOrder = 0;
	WebToUE::Private::CopyYogaLayout(
		Document, *Document.Root, Root, FVector2f::ZeroVector, PaintOrder);
	WebToUE::Private::UpdateScrollExtents(Document, *Document.Root);
	YGNodeFreeRecursive(Root);
}
