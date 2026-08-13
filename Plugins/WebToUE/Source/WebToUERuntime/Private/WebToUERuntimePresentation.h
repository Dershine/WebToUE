#pragma once

#include "CoreMinimal.h"
#include "WebToUECompiler.h"

#include "UObject/StrongObjectPtr.h"

class FPaintArgs;
class FSlateStyleSet;
class FSlateTextBlockLayout;
class FSlateWindowElementList;
class FWidgetStyle;
class SWebToUEView;
struct FSlateBrush;
class FWebToUERuntimeInstance;

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

class FWebToUERuntimePresentation
{
public:
	FWebToUERuntimePresentation(SWebToUEView& InOwnerWidget,
		FWebToUERuntimeInstance& InRuntimeInstance);
	~FWebToUERuntimePresentation();

	void Reset();
	void RebuildCaches(bool bReloadResources);
	void Layout(const FVector2f& ViewportSize) const;
	int32 Paint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
		const FWidgetStyle& WidgetStyle, bool bParentEnabled) const;
	FWebToUENode* HitTest(const FVector2f& LocalPosition) const;
	FVector2f GetVisualPosition(const FWebToUENode& Node) const;
	TConstArrayView<FWebToUENode*> GetPaintOrder(const FWebToUENode& Parent) const;
	FText GetDisplayText(const FWebToUENode& Node) const;

#if WITH_DEV_AUTOMATION_TESTS
	FVector2f MeasureTextForTesting(const FString& Text, float Width, bool bWrap) const;
	FVector2f MeasureRichTextForTesting(const FString& Markup, float Width, bool bWrap) const;
	int32 GetTextLayoutCacheCountForTesting() const { return TextLayouts.Num(); }
	int32 GetBrushCacheCountForTesting() const { return Brushes.Num(); }
	const void* GetBrushIdentityForTesting(const FWebToUENode& Node) const;
	uint64 GetResourceLoadAttemptsForTesting() const { return ResourceLoadAttemptsForTesting; }
	const void* GetTextLayoutCacheIdentityForTesting(const FWebToUENode& Node) const;
	bool IsLayoutDirtyForTesting() const { return bLayoutDirty; }
	uint64 GetKnownOwnedBytesForTesting() const;
#endif

private:
	SWebToUEView& OwnerWidget;
	FWebToUERuntimeInstance& RuntimeInstance;
	mutable FVector2f LastViewportSize = FVector2f(-1.0f, -1.0f);
	mutable bool bLayoutDirty = true;
	mutable TMap<const FWebToUENode*, TSharedPtr<FSlateBrush>> Brushes;
	mutable TMap<const FWebToUENode*, TUniquePtr<FWebToUETextLayoutCache>> TextLayouts;
	mutable TArray<TStrongObjectPtr<UObject>> LoadedResources;
	TArray<FWebToUENode*> PaintOrderNodes;
	TMap<const FWebToUENode*, FWebToUEPaintOrderRange> PaintOrderRanges;
#if WITH_DEV_AUTOMATION_TESTS
	mutable uint64 ResourceLoadAttemptsForTesting = 0;
#endif

	FWebToUEDocument* GetDocument();
	const FWebToUEDocument* GetDocument() const;
	FWebToUERuntimeNodeState& GetState(FWebToUENode& Node);
	const FWebToUERuntimeNodeState& GetState(const FWebToUENode& Node) const;
	const FWebToUEComputedStyle& GetStyle(const FWebToUENode& Node) const;
	const FWebToUERuntimeLayoutResult& GetLayout(const FWebToUENode& Node) const;
	const FWebToUERuntimeNodeState* FindState(const FWebToUENode& Node) const;
	bool IsRichText(const FWebToUENode& Node) const;
	FSlateTextBlockLayout& PrepareTextLayout(const FWebToUENode& Node,
		const FWebToUEComputedStyle& Style, float WrapWidth) const;
	FVector2f MeasureNode(const FWebToUENode& Node,
		const FWebToUELayoutEngine::FMeasureConstraints& Constraints) const;
	FVector2f MeasureNodeWithStyle(const FWebToUENode& Node, const FWebToUEComputedStyle& Style,
		const FWebToUELayoutEngine::FMeasureConstraints& Constraints) const;
	int32 PaintNode(const FWebToUEDocument& RuntimeDocument, const FWebToUENode& Node,
		const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& WidgetStyle,
		float ParentOpacity, bool bParentEnabled, const FVector2f& InheritedScrollOffset) const;
	void RebuildBrushes(bool bReloadResources) const;
	void RebuildPaintOrderCache();
};
