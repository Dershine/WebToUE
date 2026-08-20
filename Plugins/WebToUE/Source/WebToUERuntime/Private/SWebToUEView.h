#pragma once

#include "CoreMinimal.h"
#include "WebToUEEvents.h"
#include "WebToUERuntimeInstance.h"
#include "WebToUESemantics.h"
#include "WebToUEStyleProperties.h"
#include "Widgets/SLeafWidget.h"

class UWebToUEDocument;
class UWebToUEView;
class FWebToUERuntimePresentation;
struct FWebToUEPaintCommand;
struct FWebToUEDisplayCommandRange;
#if WITH_DEV_AUTOMATION_TESTS
struct FWebToUERuntimeMemoryCensus;
#endif

class SWebToUEView final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWebToUEView) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWebToUEView>, Owner)
	SLATE_END_ARGS()

	SWebToUEView();
	virtual ~SWebToUEView() override;
	void Construct(const FArguments& InArgs);
	void SetDocument(UWebToUEDocument* InDocument);
	void RefreshBindings(UObject* DataContext, FName ChangedField = NAME_None);
	TSet<FName> GetBoundFields() const;
	void GetSemanticNodes(TArray<FWebToUESemanticNode>& OutNodes) const;
	FWebToUEInstanceHandle GetFocusedSemanticNode(uint32 SlateUserIndex = 0) const;
	bool RequestSemanticFocus(FWebToUEInstanceHandle Handle, uint32 SlateUserIndex = 0);
	bool ActivateSemanticNode(
		FWebToUEInstanceHandle Handle,
		uint32 SlateUserIndex = 0,
		EWebToUEInputModality InputModality = EWebToUEInputModality::Unknown);
	FWebToUEEventListenerHandle AddEventListener(
		FWebToUEInstanceHandle Target,
		EWebToUERuntimeEventType Type,
		EWebToUERuntimeEventPhase Phase,
		FWebToUEEventListener&& Listener);
	bool RemoveEventListener(FWebToUEEventListenerHandle Handle);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry,
		const FNavigationEvent& InNavigationEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;

#if WITH_DEV_AUTOMATION_TESTS
	FVector2f MeasureTextForTesting(const FString& Text, float Width, bool bWrap) const;
	FVector2f MeasureRichTextForTesting(const FString& Markup, float Width, bool bWrap) const;
	FText GetDisplayTextForTesting(const FWebToUENode& Node) const;
	void SetRuntimeDocumentForTesting(TSharedRef<FWebToUEDocument> InDocument);
	void LayoutForTesting(const FVector2f& ViewportSize) const;
	FWebToUENode* HitTestForTesting(const FVector2f& LocalPosition) const { return HitTest(LocalPosition); }
	bool ScrollAtForTesting(const FVector2f& LocalPosition, float WheelDelta) { return ScrollAt(LocalPosition, WheelDelta); }
	FVector2f GetVisualPositionForTesting(const FWebToUENode& Node) const;
	TConstArrayView<FWebToUEInstanceHandle> GetPaintOrderForTesting(const FWebToUENode& Parent) const;
	FWebToUEInstanceHandle GetInstanceHandleForTesting(const FWebToUENode& Node) const;
	FWebToUETemplateNodeId GetTemplateNodeIdForTesting(const FWebToUENode& Node) const;
	FWebToUENode* ResolveInstanceHandleForTesting(FWebToUEInstanceHandle Handle) const;
	const void* GetSharedStyleTemplateIdentityForTesting() const;
	FWebToUENode* AddDynamicTextNodeForTesting(FWebToUENode& Parent);
	void SetHoveredNodeForTesting(FWebToUENode* Node) { SetHoveredNode(Node); }
	void SetFocusedNodeForTesting(FWebToUENode* Node) { SetFocusedNode(Node); }
	FWebToUENode* GetHoveredNodeForTesting(FWebToUEInteractionIdentity Interaction) const;
	FWebToUENode* GetPressedNodeForTesting(FWebToUEInteractionIdentity Interaction) const;
	FWebToUENode* GetCapturedNodeForTesting(FWebToUEInteractionIdentity Interaction) const;
	FWebToUENode* GetFocusedNodeForTesting(uint32 SlateUserIndex) const;
	void DispatchClickForTesting(
		FWebToUENode& Node,
		FWebToUEInteractionIdentity Interaction = FWebToUEInteractionIdentity::Pointer(0, 0),
		EWebToUEInputModality InputModality = EWebToUEInputModality::Pointer)
	{
		DispatchClick(Node, Interaction, InputModality);
	}
	void SetDefaultEventObserverForTesting(
		TFunction<void(const FWebToUEEventPathSnapshot&)> Observer)
	{
		DefaultEventObserverForTesting = MoveTemp(Observer);
	}
	EWebToUEEventDispatchResult GetLastEventDispatchResultForTesting() const
	{
		return LastEventDispatchResult;
	}
	void SetBoundTextForTesting(FWebToUENode& Node, const FText& Text);
	bool ApplyBoundTextChangeForTesting(FWebToUENode& Node, const FText& Text, bool bRichText);
	FVector2f PrepareTextLayoutForTesting(const FWebToUENode& Node,
		const FWebToUEComputedStyle& Style, float WrapWidth) const;
	bool IsPresentationMeasureDirtyForTesting(const FWebToUENode& Node) const;
	bool IsPresentationLayoutPathDirtyForTesting(const FWebToUENode& Node) const;
	FString GetPresentationTextCacheCultureForTesting(const FWebToUENode& Node) const;
	const FWebToUERuntimeNodeState& GetRuntimeStateForTesting(const FWebToUENode& Node) const;
	const FWebToUEComputedStyle& GetComputedStyleForTesting(const FWebToUENode& Node) const;
	const FWebToUERuntimeLayoutResult& GetLayoutResultForTesting(const FWebToUENode& Node) const;
	FWebToUENode* FindRuntimeNodeByIdForTesting(const FString& Id) const;
	const void* GetPresentationIdentityForTesting() const { return Presentation.Get(); }
	int32 GetPresentationTextCacheCountForTesting() const;
	int32 GetPresentationBrushCacheCountForTesting() const;
	const void* GetPresentationBrushIdentityForTesting(const FWebToUENode& Node) const;
	uint64 GetPresentationResourceLoadAttemptsForTesting() const;
	uint64 GetPresentationResourceAsyncRequestsForTesting() const;
	uint64 GetPresentationResourceFailuresForTesting() const;
	uint64 GetPresentationResourceCancellationsForTesting() const;
	int32 FindPresentationResourceHandleForTesting(EWebToUEResourceKind Kind,
		const FSoftObjectPath& Path) const;
	const UObject* GetPresentationResourceObjectForTesting(int32 Handle) const;
	bool FinalizePresentationResourcesForTesting() const;
	const void* GetPresentationTextCacheIdentityForTesting(const FWebToUENode& Node) const;
	bool IsPresentationLayoutDirtyForTesting() const;
	const FString& GetLastPseudoInvalidationReportForTesting() const
	{
		return LastPseudoInvalidationReport;
	}
	bool GetRuntimeMemoryCensusForTesting(FWebToUERuntimeMemoryCensus& OutCensus) const;
	int32 GetDisplayCommandCountForTesting() const;
	const FWebToUEPaintCommand* GetDisplayCommandForTesting(const FWebToUENode& Node) const;
	const FWebToUEDisplayCommandRange* GetDisplayCommandRangeForTesting(
		const FWebToUENode& Node) const;
	int32 GetDisplaySpatialCellCountForTesting() const;
	int32 GetDirtyRectCountForTesting() const;
	int32 GetDirtyCommandCountForTesting() const;
	const FSlateRect* GetDirtyRectForTesting(int32 Index) const;
#endif

private:
	TWeakObjectPtr<UWebToUEView> Owner;
	TWeakObjectPtr<UWebToUEDocument> DocumentAsset;
	TUniquePtr<FWebToUERuntimeInstance> RuntimeInstance;
	TUniquePtr<FWebToUERuntimePresentation> Presentation;
	TSet<FString> LoggedBindingErrors;
	FString LastPseudoInvalidationReport;
	TSharedPtr<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> StandaloneUpdateCoordinator;

	struct FRegisteredEventListener
	{
		uint64 Id = 0;
		FWebToUEInstanceHandle Target;
		EWebToUERuntimeEventType Type = EWebToUERuntimeEventType::Click;
		EWebToUERuntimeEventPhase Phase = EWebToUERuntimeEventPhase::Target;
		FWebToUEEventListener Callback;
	};
	TArray<FRegisteredEventListener> EventListeners;
	uint64 NextEventListenerId = 1;
	uint64 NextEventCorrelationId = 1;
	EWebToUEEventDispatchResult LastEventDispatchResult =
		EWebToUEEventDispatchResult::DroppedInvalidPath;
	using FInteractionNodeMap =
		TMap<FWebToUEInteractionIdentity, FWebToUEInstanceHandle>;
	FInteractionNodeMap HoveredNodes;
	FInteractionNodeMap PressedNodes;
	FInteractionNodeMap CapturedNodes;
	FInteractionNodeMap FocusedNodes;
	TMap<FWebToUEInstanceHandle, int32> HoverRefCounts;
	TMap<FWebToUEInstanceHandle, int32> ActiveRefCounts;
	TMap<FWebToUEInstanceHandle, int32> FocusRefCounts;
#if WITH_DEV_AUTOMATION_TESTS
	TFunction<void(const FWebToUEEventPathSnapshot&)> DefaultEventObserverForTesting;
#endif

	FWebToUEDocument* GetRuntimeDocument();
	const FWebToUEDocument* GetRuntimeDocument() const;
	FWebToUERuntimeNodeState& GetRuntimeState(FWebToUENode& Node);
	const FWebToUERuntimeNodeState& GetRuntimeState(const FWebToUENode& Node) const;
	FWebToUEComputedStyle& GetComputedStyle(FWebToUENode& Node);
	const FWebToUEComputedStyle& GetComputedStyle(const FWebToUENode& Node) const;
	FWebToUERuntimeLayoutResult& GetLayoutResult(FWebToUENode& Node);
	const FWebToUERuntimeLayoutResult& GetLayoutResult(const FWebToUENode& Node) const;

	void RebuildStylesAndBrushes(
		EWebToUEStyleImpact Impacts = EWebToUEStyleImpact::Resource);
	FText GetDisplayText(const FWebToUENode& Node) const;
	FWebToUENode* HitTest(const FVector2f& LocalPosition) const;
	bool ScrollAt(const FVector2f& LocalPosition, float WheelDelta);
	FWebToUENode* GetInteractionNode(
		const FInteractionNodeMap& Nodes, FWebToUEInteractionIdentity Interaction) const;
	void SetInteractionNode(
		FInteractionNodeMap& Nodes,
		TMap<FWebToUEInstanceHandle, int32>& RefCounts,
		FWebToUEInteractionIdentity Interaction,
		FWebToUENode* Node,
		EWebToUEPseudoState Flag,
		bool bIncludeAncestors);
	void ResetInteractionState();
	void SetHoveredNode(
		FWebToUENode* Node,
		FWebToUEInteractionIdentity Interaction = FWebToUEInteractionIdentity::Pointer(0, 0));
	void SetPressedNode(
		FWebToUENode* Node,
		FWebToUEInteractionIdentity Interaction = FWebToUEInteractionIdentity::Pointer(0, 0));
	void SetFocusedNode(FWebToUENode* Node, uint32 SlateUserIndex = 0);
	void UpdatePseudoState(FWebToUENode* OldNode, FWebToUENode* NewNode,
		EWebToUEPseudoState Flag, bool bIncludeAncestors);
	void CollectPseudoDependencyTargets(FWebToUENode& ReasonNode,
		EWebToUEPseudoState Flag, TArray<FWebToUEInstanceHandle>& OutTargets) const;
	bool MoveFocusSequential(int32 Direction, bool bWrap, uint32 SlateUserIndex = 0);
	bool MoveFocusSpatial(EUINavigation Direction, uint32 SlateUserIndex = 0);
	void ActivateFocusedNode(
		FWebToUEInteractionIdentity Interaction = FWebToUEInteractionIdentity::NonPointer(0),
		EWebToUEInputModality InputModality = EWebToUEInputModality::Unknown);
	bool IsSemanticFocusable(const FWebToUENode& Node) const;
	FText BuildSemanticLabel(const FWebToUENode& Node) const;
	FWebToUEEventPathSnapshot BuildEventPathSnapshot(
		FWebToUENode& Target,
		EWebToUERuntimeEventType Type,
		FWebToUEInteractionIdentity Interaction,
		EWebToUEInputModality InputModality,
		bool bBubbles,
		bool bCancelable);
	bool IsEventPathCurrent(const FWebToUEEventPathSnapshot& Snapshot) const;
	EWebToUEEventDispatchResult EvaluateEvent(
		const FWebToUEEventPathSnapshot& Snapshot,
		FWebToUEUpdateTransaction& Transaction,
		TUniqueFunction<void()>&& DefaultAction);
	void SubmitRuntimeEvent(
		const FWebToUEEventPathSnapshot& Snapshot,
		TUniqueFunction<void()>&& DefaultAction);
	void DispatchClick(
		FWebToUENode& Node,
		FWebToUEInteractionIdentity Interaction = FWebToUEInteractionIdentity::NonPointer(0),
		EWebToUEInputModality InputModality = EWebToUEInputModality::Unknown);
	void ReportBindingErrorOnce(const FString& Field, const FString& Message);
};
