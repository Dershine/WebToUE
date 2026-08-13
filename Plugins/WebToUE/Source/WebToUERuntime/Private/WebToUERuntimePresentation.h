#pragma once

#include "CoreMinimal.h"
#include "WebToUECompiler.h"
#include "WebToUEDocument.h"

#include "UObject/StrongObjectPtr.h"

class FPaintArgs;
class FSlateStyleSet;
class FSlateTextBlockLayout;
class FSlateWindowElementList;
struct FStreamableHandle;
class FWidgetStyle;
class SWebToUEView;
struct FSlateBrush;
class FWebToUERuntimeInstance;

struct FWebToUETextLayoutCache
{
	struct FKey
	{
		FString Text;
		FString FontFamily;
		FString FontWeight;
		FString TextAlign;
		FString WhiteSpace;
		FString CultureName;
		FLinearColor Color = FLinearColor::White;
		float FontSize = 0.0f;
		float WrapWidth = 0.0f;
		bool bRichText = false;

		bool operator==(const FKey& Other) const
		{
			return Text == Other.Text && FontFamily == Other.FontFamily &&
				FontWeight == Other.FontWeight && TextAlign == Other.TextAlign &&
				WhiteSpace == Other.WhiteSpace && CultureName == Other.CultureName &&
				Color == Other.Color && FontSize == Other.FontSize &&
				WrapWidth == Other.WrapWidth && bRichText == Other.bRichText;
		}
	};

	TSharedPtr<FSlateStyleSet> RichTextStyleSet;
	TUniquePtr<FSlateTextBlockLayout> Layout;
	FKey Key;
	FVector2f DesiredSize = FVector2f::ZeroVector;
	bool bHasKey = false;
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
	bool ApplyBoundTextChange(FWebToUENode& Node);
	void ApplyStyleUpdates(TConstArrayView<FWebToUEStyleUpdate> Updates);
	void Layout(const FVector2f& ViewportSize) const;
	int32 Paint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
		const FWidgetStyle& WidgetStyle, bool bParentEnabled) const;
	FWebToUENode* HitTest(const FVector2f& LocalPosition) const;
	FVector2f GetVisualPosition(const FWebToUENode& Node) const;
	TConstArrayView<FWebToUEInstanceHandle> GetPaintOrder(const FWebToUENode& Parent) const;
	FText GetDisplayText(const FWebToUENode& Node) const;

#if WITH_DEV_AUTOMATION_TESTS
	FVector2f MeasureTextForTesting(const FString& Text, float Width, bool bWrap) const;
	FVector2f MeasureRichTextForTesting(const FString& Markup, float Width, bool bWrap) const;
	int32 GetTextLayoutCacheCountForTesting() const { return TextLayouts.Num(); }
	int32 GetBrushCacheCountForTesting() const { return Brushes.Num(); }
	const void* GetBrushIdentityForTesting(const FWebToUENode& Node) const;
	uint64 GetResourceLoadAttemptsForTesting() const { return ResourceLoadAttemptsForTesting; }
	uint64 GetResourceAsyncRequestsForTesting() const { return ResourceAsyncRequestsForTesting; }
	uint64 GetResourceFailuresForTesting() const { return ResourceFailuresForTesting; }
	uint64 GetResourceCancellationsForTesting() const { return ResourceCancellationsForTesting; }
	int32 FindResourceHandleForTesting(EWebToUEResourceKind Kind,
		const FSoftObjectPath& Path) const;
	const UObject* GetResourceObjectForTesting(int32 Handle) const;
	bool FinalizeResourcesForTesting() const { return FinalizeResourcePreload(); }
	const void* GetTextLayoutCacheIdentityForTesting(const FWebToUENode& Node) const;
	bool IsLayoutDirtyForTesting() const { return bLayoutDirty; }
	bool IsMeasureDirtyForTesting(const FWebToUENode& Node) const;
	bool IsLayoutPathDirtyForTesting(const FWebToUENode& Node) const;
	FVector2f PrepareTextLayoutForTesting(const FWebToUENode& Node,
		const FWebToUEComputedStyle& Style, float WrapWidth) const;
	FString GetTextCacheCultureForTesting(const FWebToUENode& Node) const;
	uint64 GetKnownOwnedBytesForTesting() const;
#endif

private:
	SWebToUEView& OwnerWidget;
	FWebToUERuntimeInstance& RuntimeInstance;
	mutable FVector2f LastViewportSize = FVector2f(-1.0f, -1.0f);
	mutable bool bLayoutDirty = true;
	mutable TMap<FWebToUEInstanceHandle, TSharedPtr<FSlateBrush>> Brushes;
	mutable TMap<FWebToUEInstanceHandle, TUniquePtr<FWebToUETextLayoutCache>> TextLayouts;
	mutable TSet<FWebToUEInstanceHandle> MeasureDirtyNodes;
	mutable TSet<FWebToUEInstanceHandle> LayoutDirtyNodes;
	mutable TArray<TStrongObjectPtr<UObject>> ResolvedResources;
	mutable TSharedPtr<FStreamableHandle> PendingResourceRequest;
	TArray<FWebToUEInstanceHandle> PaintOrderNodes;
	TMap<FWebToUEInstanceHandle, FWebToUEPaintOrderRange> PaintOrderRanges;
#if WITH_DEV_AUTOMATION_TESTS
	mutable uint64 ResourceLoadAttemptsForTesting = 0;
	mutable uint64 ResourceAsyncRequestsForTesting = 0;
	mutable uint64 ResourceFailuresForTesting = 0;
	mutable uint64 ResourceCancellationsForTesting = 0;
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
	FSlateTextBlockLayout& PrepareTextLayoutInCache(const FWebToUENode& Node,
		const FWebToUEComputedStyle& Style, float WrapWidth,
		TUniquePtr<FWebToUETextLayoutCache>& Cache) const;
	void MarkTextLayoutDependencyPath(const FWebToUENode& Node);
	FVector2f MeasureNode(const FWebToUENode& Node,
		const FWebToUELayoutEngine::FMeasureConstraints& Constraints) const;
	FVector2f MeasureNodeWithStyle(const FWebToUENode& Node, const FWebToUEComputedStyle& Style,
		const FWebToUELayoutEngine::FMeasureConstraints& Constraints) const;
	void BeginResourcePreload() const;
	void CancelResourcePreload() const;
	bool FinalizeResourcePreload() const;
	int32 FindResourceHandle(EWebToUEResourceKind Kind, const FSoftObjectPath& Path) const;
	UObject* GetResolvedResource(EWebToUEResourceKind Kind, const FSoftObjectPath& Path) const;
	UObject* GetResolvedFont(const FString& Family) const;
	int32 PaintNode(const FWebToUEDocument& RuntimeDocument, const FWebToUENode& Node,
		const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& WidgetStyle,
		float ParentOpacity, bool bParentEnabled, const FVector2f& InheritedScrollOffset) const;
	void RebuildBrushes(bool bReloadResources) const;
	void RebuildBrush(FWebToUENode& Node) const;
	void RebuildPaintOrderCache();
};
