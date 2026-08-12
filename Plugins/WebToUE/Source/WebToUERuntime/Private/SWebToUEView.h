#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/SlateTextBlockLayout.h"
#include "WebToUECompiler.h"

struct FSlateBrush;
class FSlateStyleSet;
class UWebToUEDocument;
class UWebToUEView;

struct FWebToUETextLayoutCache
{
	TSharedPtr<FSlateStyleSet> RichTextStyleSet;
	TUniquePtr<FSlateTextBlockLayout> Layout;
	bool bRichText = false;
};

struct FWebToUEPaintOrderRange
{
	int32 StartIndex = 0;
	int32 Num = 0;
};

class SWebToUEView final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWebToUEView) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWebToUEView>, Owner)
	SLATE_END_ARGS()

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
	TConstArrayView<FWebToUENode*> GetPaintOrderForTesting(const FWebToUENode& Parent) const;
	void SetHoveredNodeForTesting(FWebToUENode* Node) { SetHoveredNode(Node); }
#endif

private:
	TWeakObjectPtr<UWebToUEView> Owner;
	TWeakObjectPtr<UWebToUEDocument> DocumentAsset;
	TSharedPtr<FWebToUEDocument> RuntimeDocument;
	mutable FVector2f LastViewportSize = FVector2f(-1.0f, -1.0f);
	mutable bool bLayoutDirty = true;
	mutable TMap<const FWebToUENode*, TSharedPtr<FSlateBrush>> Brushes;
	mutable TMap<const FWebToUENode*, TUniquePtr<FWebToUETextLayoutCache>> TextLayouts;
	mutable TArray<TStrongObjectPtr<UObject>> LoadedResources;
	TArray<FWebToUENode*> PaintOrderNodes;
	TMap<const FWebToUENode*, FWebToUEPaintOrderRange> PaintOrderRanges;
	TSet<FString> LoggedBindingErrors;
	FWebToUENode* HoveredNode = nullptr;
	FWebToUENode* PressedNode = nullptr;
	FWebToUENode* FocusedNode = nullptr;

	void RebuildStylesAndBrushes();
	void RebuildBrushes() const;
	void RebuildPaintOrderCache();
	TConstArrayView<FWebToUENode*> GetPaintOrder(const FWebToUENode& Parent) const;
	FVector2f MeasureNode(const FWebToUENode& Node, const FWebToUELayoutEngine::FMeasureConstraints& Constraints) const;
	FText GetDisplayText(const FWebToUENode& Node) const;
	FSlateTextBlockLayout& PrepareTextLayout(const FWebToUENode& Node, float WrapWidth) const;
	int32 PaintNode(const FWebToUENode& Node, const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
		const FWidgetStyle& WidgetStyle, float ParentOpacity, bool bParentEnabled,
		const FVector2f& InheritedScrollOffset) const;
	FWebToUENode* HitTest(const FVector2f& LocalPosition) const;
	bool ScrollAt(const FVector2f& LocalPosition, float WheelDelta);
	void SetHoveredNode(FWebToUENode* Node);
	void SetPressedNode(FWebToUENode* Node);
	void SetFocusedNode(FWebToUENode* Node);
	void ClearStateFlag(EWebToUEPseudoState Flag);
	void SetStatePath(FWebToUENode* Node, EWebToUEPseudoState Flag);
	void MoveFocus(int32 Direction);
	void ActivateFocusedNode();
	void DispatchClick(FWebToUENode& Node) const;
	void ReportBindingErrorOnce(const FString& Field, const FString& Message);
};
