#pragma once

#include "CoreMinimal.h"
#include "WebToUERuntimeInstance.h"
#include "WebToUEStyleProperties.h"
#include "Widgets/SLeafWidget.h"

class UWebToUEDocument;
class UWebToUEView;
class FWebToUERuntimePresentation;
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
	void RefreshBindings(UObject* DataContext);
	TSet<FName> GetBoundFields() const;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
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
	void SetBoundTextForTesting(FWebToUENode& Node, const FText& Text);
	const FWebToUERuntimeNodeState& GetRuntimeStateForTesting(const FWebToUENode& Node) const;
	const FWebToUEComputedStyle& GetComputedStyleForTesting(const FWebToUENode& Node) const;
	const FWebToUERuntimeLayoutResult& GetLayoutResultForTesting(const FWebToUENode& Node) const;
	FWebToUENode* FindRuntimeNodeByIdForTesting(const FString& Id) const;
	const void* GetPresentationIdentityForTesting() const { return Presentation.Get(); }
	int32 GetPresentationTextCacheCountForTesting() const;
	int32 GetPresentationBrushCacheCountForTesting() const;
	const void* GetPresentationBrushIdentityForTesting(const FWebToUENode& Node) const;
	uint64 GetPresentationResourceLoadAttemptsForTesting() const;
	const void* GetPresentationTextCacheIdentityForTesting(const FWebToUENode& Node) const;
	bool IsPresentationLayoutDirtyForTesting() const;
	const FString& GetLastPseudoInvalidationReportForTesting() const
	{
		return LastPseudoInvalidationReport;
	}
	bool GetRuntimeMemoryCensusForTesting(FWebToUERuntimeMemoryCensus& OutCensus) const;
#endif

private:
	TWeakObjectPtr<UWebToUEView> Owner;
	TWeakObjectPtr<UWebToUEDocument> DocumentAsset;
	TUniquePtr<FWebToUERuntimeInstance> RuntimeInstance;
	TUniquePtr<FWebToUERuntimePresentation> Presentation;
	TSet<FString> LoggedBindingErrors;
	FString LastPseudoInvalidationReport;

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
	void SetHoveredNode(FWebToUENode* Node);
	void SetPressedNode(FWebToUENode* Node);
	void SetFocusedNode(FWebToUENode* Node);
	void UpdatePseudoState(FWebToUENode* OldNode, FWebToUENode* NewNode,
		EWebToUEPseudoState Flag, bool bIncludeAncestors);
	void MoveFocus(int32 Direction);
	void ActivateFocusedNode();
	void DispatchClick(FWebToUENode& Node) const;
	void ReportBindingErrorOnce(const FString& Field, const FString& Message);
};
