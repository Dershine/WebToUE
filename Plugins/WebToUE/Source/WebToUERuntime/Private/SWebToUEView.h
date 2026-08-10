#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "WebToUECoreTypes.h"

struct FSlateBrush;
class UWebToUEDocument;
class UWebToUEView;

class SWebToUEView final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWebToUEView) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWebToUEView>, Owner)
	SLATE_END_ARGS()

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
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;

private:
	TWeakObjectPtr<UWebToUEView> Owner;
	TWeakObjectPtr<UWebToUEDocument> DocumentAsset;
	TSharedPtr<FWebToUEDocument> RuntimeDocument;
	mutable FVector2f LastViewportSize = FVector2f(-1.0f, -1.0f);
	mutable bool bLayoutDirty = true;
	mutable TMap<const FWebToUENode*, TSharedPtr<FSlateBrush>> Brushes;
	mutable TArray<TStrongObjectPtr<UObject>> LoadedResources;
	TSet<FString> LoggedBindingErrors;
	FWebToUENode* HoveredNode = nullptr;
	FWebToUENode* PressedNode = nullptr;
	FWebToUENode* FocusedNode = nullptr;

	void RebuildStylesAndBrushes();
	void RebuildBrushes() const;
	FVector2f MeasureNode(const FWebToUENode& Node) const;
	int32 PaintNode(const FWebToUENode& Node, const FGeometry& Geometry, FSlateWindowElementList& Out,
		int32 LayerId, float ParentOpacity, bool bParentEnabled) const;
	FWebToUENode* HitTest(const FVector2f& LocalPosition) const;
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
