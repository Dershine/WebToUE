#pragma once

#include "CoreMinimal.h"
#include "WebToUECompiler.h"
#include "WebToUEDocument.h"

#include "Layout/SlateRect.h"
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

enum class EWebToUEPaintCommandType : uint8
{
	Box,
	Text
};

struct FWebToUEPaintBatchKey
{
	EWebToUEPaintCommandType Type = EWebToUEPaintCommandType::Box;
	uint32 ResourceKey = 0;
	uint32 ClipKey = 0;
	uint8 DrawEffects = 0;

	bool operator==(const FWebToUEPaintBatchKey& Other) const
	{
		return Type == Other.Type && ResourceKey == Other.ResourceKey &&
			ClipKey == Other.ClipKey && DrawEffects == Other.DrawEffects;
	}
};

struct FWebToUESpatialCellRange
{
	int32 MinX = 0;
	int32 MinY = 0;
	int32 MaxX = -1;
	int32 MaxY = -1;

	bool IsValid() const { return MinX <= MaxX && MinY <= MaxY; }
	int64 GetCellCount() const
	{
		return IsValid()
			? int64(MaxX - MinX + 1) * int64(MaxY - MinY + 1) : 0;
	}
};

struct FWebToUEPaintCommand
{
	FWebToUEInstanceHandle Owner;
	EWebToUEPaintCommandType Type = EWebToUEPaintCommandType::Box;
	FSlateRect Bounds = FSlateRect(0.0f, 0.0f, 0.0f, 0.0f);
	FSlateRect ClipBounds = FSlateRect(0.0f, 0.0f, 0.0f, 0.0f);
	FSlateRect VisibleBounds = FSlateRect(0.0f, 0.0f, 0.0f, 0.0f);
	FSlateRect SubtreeBounds = FSlateRect(0.0f, 0.0f, 0.0f, 0.0f);
	FWebToUEPaintBatchKey BatchKey;
	FWebToUESpatialCellRange SpatialCells;
	float Opacity = 1.0f;
	int32 Depth = 0;
	bool bDisplayed = false;
	bool bDrawable = false;
	bool bInteractive = false;
	bool bScrollable = false;
	bool bEnabled = true;
	bool bHasClip = false;
	bool bSpatiallyIndexed = false;
	bool bLargeSpatialEntry = false;
};

struct FWebToUEDisplayCommandRange
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
	void ApplyRuntimeStateChanges(
		TConstArrayView<FWebToUEInstanceHandle> ChangedNodes);
	void Layout(const FVector2f& ViewportSize) const;
	int32 Paint(const FPaintArgs& Args, const FGeometry& Geometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
		const FWidgetStyle& WidgetStyle, bool bParentEnabled) const;
	FWebToUENode* HitTest(const FVector2f& LocalPosition) const;
	bool ScrollAt(const FVector2f& LocalPosition, float WheelDelta);
	void ApplyScrollOffsetChange(const FWebToUENode& Node);
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
	int32 GetDisplayCommandCountForTesting() const { return DisplayCommands.Num(); }
	const FWebToUEPaintCommand* GetDisplayCommandForTesting(
		const FWebToUENode& Node) const;
	const FWebToUEDisplayCommandRange* GetDisplayCommandRangeForTesting(
		const FWebToUENode& Node) const;
	int32 GetDisplaySpatialCellCountForTesting() const { return DisplaySpatialCells.Num(); }
	int32 GetDirtyRectCountForTesting() const { return DirtyRects.Num(); }
	int32 GetDirtyCommandCountForTesting() const { return DirtyCommandIndices.Num(); }
	const FSlateRect* GetDirtyRectForTesting(int32 Index) const
	{
		return DirtyRects.IsValidIndex(Index) ? &DirtyRects[Index] : nullptr;
	}
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
	mutable TArray<FWebToUEPaintCommand> DisplayCommands;
	mutable TMap<FWebToUEInstanceHandle, int32> DisplayCommandIndices;
	mutable TMap<FWebToUEInstanceHandle, FWebToUEDisplayCommandRange> DisplayCommandRanges;
	mutable TMap<uint64, TArray<int32>> DisplaySpatialCells;
	mutable TArray<int32> LargeDisplayCommands;
	mutable TArray<uint32> DisplayQueryMarks;
	mutable TArray<int32> DisplayQueryScratch;
	mutable uint32 DisplayQueryGeneration = 0;
	mutable TArray<FSlateRect> DirtyRects;
	mutable TArray<int32> DirtyCommandIndices;
	mutable bool bDisplayListDirty = true;
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
	void RebuildDisplayList() const;
	void BuildDisplaySubtree(const FWebToUEDocument& RuntimeDocument,
		const FWebToUENode& Node, float ParentOpacity, bool bParentDisplayed,
		bool bParentEnabled, int32 Depth,
		const FVector2f& InheritedScrollOffset, const FSlateRect& InheritedClip,
		bool bHasInheritedClip) const;
	int32 PatchDisplaySubtree(const FWebToUENode& Node,
		bool bIncludeDescendants = true) const;
	void UpdateDisplayCommand(const FWebToUEDocument& RuntimeDocument,
		const FWebToUENode& Node, FWebToUEPaintCommand& Command,
		float ParentOpacity, bool bParentDisplayed, bool bParentEnabled, int32 Depth,
		const FVector2f& InheritedScrollOffset,
		const FSlateRect& InheritedClip, bool bHasInheritedClip) const;
	void UpdateDisplaySubtreeBounds(const FWebToUENode& Node) const;
	void RebuildDisplaySpatialIndex() const;
	void AddDisplayCommandToSpatialIndex(int32 CommandIndex) const;
	void RemoveDisplayCommandFromSpatialIndex(int32 CommandIndex) const;
	void QueryDisplayCommands(const FSlateRect& LocalBounds,
		bool bRequireDrawable, TArray<int32>& OutCommandIndices) const;
	void AddDirtyRegion(const FSlateRect& PreviousBounds,
		const FSlateRect& CurrentBounds, int32 CommandIndex) const;
	void MarkDisplayCommandDirty(const FWebToUENode& Node) const;
	void PaintDebugOverlay(const FGeometry& Geometry, FSlateWindowElementList& Out,
		int32& LayerId) const;
	int32 PaintCommand(const FWebToUEPaintCommand& Command, const FPaintArgs& Args,
		const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& WidgetStyle,
		bool bParentEnabled) const;
	void RebuildBrushes(bool bReloadResources) const;
	void RebuildBrush(FWebToUENode& Node) const;
	void RebuildPaintOrderCache();
};
