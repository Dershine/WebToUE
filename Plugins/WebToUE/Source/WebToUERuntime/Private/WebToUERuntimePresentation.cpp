#include "WebToUERuntimePresentation.h"

#include "SWebToUEView.h"
#include "WebToUEPerformance.h"
#include "WebToUERuntimeInstance.h"
#include "WebToUESettings.h"

#include "Algo/StableSort.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/AssetManager.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/StringTable.h"
#include "HAL/IConsoleManager.h"
#include "Layout/Clipping.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Text/SlateTextBlockLayout.h"

namespace WebToUE::Runtime::Presentation::Private
{
	static TAutoConsoleVariable<int32> CVarDisplayListDebug(
		TEXT("WebToUE.Debug.DisplayList"), 0,
		TEXT("Display List diagnostics: 0=off, 1=dirty rects, 2=dirty commands, "
			"3=all command bounds plus dirty overlays."),
		ECVF_Default);

	static constexpr float SpatialCellSize = 128.0f;
	static constexpr int64 MaxCellsPerCommand = 256;

	static uint64 MakeSpatialCellKey(int32 X, int32 Y)
	{
		return (uint64(static_cast<uint32>(X)) << 32) |
			uint64(static_cast<uint32>(Y));
	}

	static bool IsUsableRect(const FSlateRect& Rect)
	{
		return Rect.IsValid() && !Rect.IsEmpty();
	}

	static FSlateRect UnionRects(const FSlateRect& A, const FSlateRect& B)
	{
		if (!IsUsableRect(A)) return B;
		if (!IsUsableRect(B)) return A;
		return FSlateRect(FMath::Min(A.Left, B.Left), FMath::Min(A.Top, B.Top),
			FMath::Max(A.Right, B.Right), FMath::Max(A.Bottom, B.Bottom));
	}

	static ETextJustify::Type ToTextJustification(const FString& TextAlign)
	{
		if (TextAlign == TEXT("center")) return ETextJustify::Center;
		if (TextAlign == TEXT("right")) return ETextJustify::Right;
		return ETextJustify::Left;
	}

	static FTextBlockStyle MakeTextBlockStyle(const FWebToUEComputedStyle& ComputedStyle,
		UObject* ResolvedFontObject)
	{
		const UWebToUESettings* Settings = GetDefault<UWebToUESettings>();
		FTextBlockStyle Style = FTextBlockStyle::GetDefault();
		Style.SetFont(Settings->ResolveFont(
			ComputedStyle.FontFamily, ComputedStyle.FontSize, ComputedStyle.FontWeight,
			ResolvedFontObject));
		Style.SetColorAndOpacity(ComputedStyle.Color);
		return Style;
	}

	static TSharedRef<FSlateStyleSet> MakeRichTextStyleSet(
		const FWebToUEComputedStyle& ComputedStyle, UObject* ResolvedFontObject)
	{
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		TSharedRef<FSlateStyleSet> StyleSet = MakeShared<FSlateStyleSet>(TEXT("WebToUERichText"));
		const FTextBlockStyle BaseStyle = MakeTextBlockStyle(ComputedStyle, ResolvedFontObject);
		const UWebToUESettings* Settings = GetDefault<UWebToUESettings>();
		const auto AddStyle = [&](const FName Name, bool bBold, bool bItalic, bool bUnderline)
		{
			FTextBlockStyle Style = BaseStyle;
			if (bBold || bItalic)
			{
				Style.SetFont(Settings->ResolveFont(ComputedStyle.FontFamily, ComputedStyle.FontSize,
					bBold ? TEXT("bold") : ComputedStyle.FontWeight, ResolvedFontObject));
				if (bItalic) Style.SetTypefaceFontName(bBold ? TEXT("BoldItalic") : TEXT("Italic"));
			}
			if (bUnderline)
			{
				Style.SetUnderlineBrush(*FCoreStyle::Get().GetBrush(TEXT("DefaultTextUnderline")));
			}
			StyleSet->Set(Name, Style);
		};
		AddStyle(TEXT("strong"), true, false, false);
		AddStyle(TEXT("em"), false, true, false);
		AddStyle(TEXT("underline"), false, false, true);
		AddStyle(TEXT("strong_em"), true, true, false);
		AddStyle(TEXT("strong_underline"), true, false, true);
		AddStyle(TEXT("em_underline"), false, true, true);
		AddStyle(TEXT("strong_em_underline"), true, true, true);
		return StyleSet;
	}

	static uint32 HashRect(const FSlateRect& Rect)
	{
		uint32 Hash = GetTypeHash(Rect.Left);
		Hash = HashCombineFast(Hash, GetTypeHash(Rect.Top));
		Hash = HashCombineFast(Hash, GetTypeHash(Rect.Right));
		return HashCombineFast(Hash, GetTypeHash(Rect.Bottom));
	}

	static uint32 MakeBatchResourceKey(const FWebToUENode& Node,
		const FWebToUEComputedStyle& Style)
	{
		if (Node.Type == EWebToUENodeType::Text)
		{
			uint32 Hash = GetTypeHash(Style.FontFamily);
			Hash = HashCombineFast(Hash, GetTypeHash(Style.FontWeight));
			return HashCombineFast(Hash, GetTypeHash(Style.FontSize));
		}
		if (Node.Tag == TEXT("img"))
		{
			return GetTypeHash(FSoftObjectPath(Node.GetAttribute(TEXT("src"))));
		}
		uint32 Hash = GetTypeHash(Style.BackgroundColor);
		Hash = HashCombineFast(Hash, GetTypeHash(Style.BorderColor));
		Hash = HashCombineFast(Hash, GetTypeHash(Style.BorderWidth));
		return HashCombineFast(Hash, GetTypeHash(Style.BorderRadius));
	}
}

FWebToUERuntimePresentation::FWebToUERuntimePresentation(SWebToUEView& InOwnerWidget,
	FWebToUERuntimeInstance& InRuntimeInstance)
	: OwnerWidget(InOwnerWidget)
	, RuntimeInstance(InRuntimeInstance)
{
}

FWebToUERuntimePresentation::~FWebToUERuntimePresentation()
{
	CancelResourcePreload();
}

FWebToUEDocument* FWebToUERuntimePresentation::GetDocument()
{
	return RuntimeInstance.GetDocument();
}

const FWebToUEDocument* FWebToUERuntimePresentation::GetDocument() const
{
	return RuntimeInstance.GetDocument();
}

FWebToUERuntimeNodeState& FWebToUERuntimePresentation::GetState(FWebToUENode& Node)
{
	return RuntimeInstance.GetState(Node);
}

const FWebToUERuntimeNodeState& FWebToUERuntimePresentation::GetState(
	const FWebToUENode& Node) const
{
	return RuntimeInstance.GetState(Node);
}

const FWebToUEComputedStyle& FWebToUERuntimePresentation::GetStyle(
	const FWebToUENode& Node) const
{
	return RuntimeInstance.GetStyle(Node);
}

const FWebToUERuntimeLayoutResult& FWebToUERuntimePresentation::GetLayout(
	const FWebToUENode& Node) const
{
	return RuntimeInstance.GetLayout(Node);
}

const FWebToUERuntimeNodeState* FWebToUERuntimePresentation::FindState(
	const FWebToUENode& Node) const
{
	const FWebToUEDocument* RuntimeDocument = GetDocument();
	return RuntimeDocument && RuntimeDocument->IsValidRuntimeNodeStateIndex(Node)
		? &RuntimeDocument->GetRuntimeNodeState(Node)
		: nullptr;
}

bool FWebToUERuntimePresentation::IsRichText(const FWebToUENode& Node) const
{
	if (const FWebToUERuntimeNodeState* State = FindState(Node))
	{
		return State->bHasRichTextOverride ? State->bRichTextOverride : Node.bRichText;
	}
	return Node.bRichText;
}

void FWebToUERuntimePresentation::Reset()
{
	LastViewportSize = FVector2f(-1.0f, -1.0f);
	bLayoutDirty = true;
	Brushes.Reset();
	TextLayouts.Reset();
	MeasureDirtyNodes.Reset();
	LayoutDirtyNodes.Reset();
	PaintOrderNodes.Reset();
	PaintOrderRanges.Reset();
	DisplayCommands.Reset();
	DisplayCommandIndices.Reset();
	DisplayCommandRanges.Reset();
	DisplaySpatialCells.Reset();
	LargeDisplayCommands.Reset();
	DisplayQueryMarks.Reset();
	DisplayQueryScratch.Reset();
	DisplayQueryGeneration = 0;
	DirtyRects.Reset();
	DirtyCommandIndices.Reset();
	bDisplayListDirty = true;
#if WITH_DEV_AUTOMATION_TESTS
	ResourceLoadAttemptsForTesting = 0;
	ResourceAsyncRequestsForTesting = 0;
	ResourceFailuresForTesting = 0;
	ResourceCancellationsForTesting = 0;
#endif
	CancelResourcePreload();
	ResolvedResources.Reset();
}

#if WITH_DEV_AUTOMATION_TESTS
uint64 FWebToUERuntimePresentation::GetKnownOwnedBytesForTesting() const
{
	uint64 Bytes = sizeof(*this) + Brushes.GetAllocatedSize() + TextLayouts.GetAllocatedSize() +
		MeasureDirtyNodes.GetAllocatedSize() + LayoutDirtyNodes.GetAllocatedSize() +
		ResolvedResources.GetAllocatedSize() + PaintOrderNodes.GetAllocatedSize() +
		PaintOrderRanges.GetAllocatedSize() + DisplayCommands.GetAllocatedSize() +
		DisplayCommandIndices.GetAllocatedSize() + DisplayCommandRanges.GetAllocatedSize() +
		DisplaySpatialCells.GetAllocatedSize() + LargeDisplayCommands.GetAllocatedSize() +
		DisplayQueryMarks.GetAllocatedSize() + DisplayQueryScratch.GetAllocatedSize() +
		DirtyRects.GetAllocatedSize() + DirtyCommandIndices.GetAllocatedSize();
	for (const TPair<uint64, TArray<int32>>& Pair : DisplaySpatialCells)
	{
		Bytes += Pair.Value.GetAllocatedSize();
	}
	for (const TPair<FWebToUEInstanceHandle, TSharedPtr<FSlateBrush>>& Pair : Brushes)
	{
		if (Pair.Value)
		{
			Bytes += sizeof(FSlateBrush);
		}
	}
	for (const TPair<FWebToUEInstanceHandle, TUniquePtr<FWebToUETextLayoutCache>>& Pair : TextLayouts)
	{
		if (Pair.Value)
		{
			Bytes += sizeof(FWebToUETextLayoutCache);
		}
	}
	return Bytes;
}
#endif

void FWebToUERuntimePresentation::RebuildCaches(bool bReloadResources)
{
	TextLayouts.Reset();
	MeasureDirtyNodes.Reset();
	LayoutDirtyNodes.Reset();
	if (GetDocument())
	{
		RebuildPaintOrderCache();
		RebuildBrushes(bReloadResources);
		bDisplayListDirty = true;
	}
	else
	{
		Brushes.Reset();
		CancelResourcePreload();
		ResolvedResources.Reset();
		PaintOrderNodes.Reset();
		PaintOrderRanges.Reset();
		DisplayCommands.Reset();
		DisplayCommandIndices.Reset();
		DisplayCommandRanges.Reset();
		DisplaySpatialCells.Reset();
		LargeDisplayCommands.Reset();
		DisplayQueryMarks.Reset();
		DisplayQueryScratch.Reset();
		DirtyRects.Reset();
		DirtyCommandIndices.Reset();
		bDisplayListDirty = true;
	}
	bLayoutDirty = true;
}

namespace WebToUE::Runtime::Presentation::Private
{
	static bool IsExpectedResourceType(EWebToUEResourceKind Kind, UObject* Object)
	{
		if (!Object) return false;
		switch (Kind)
		{
		case EWebToUEResourceKind::Texture:
			return Object->IsA<UTexture2D>();
		case EWebToUEResourceKind::Font:
			return Object->IsA<UFont>() || Object->IsA<UFontFace>();
		case EWebToUEResourceKind::StringTable:
			return Object->IsA<UStringTable>();
		default:
			return false;
		}
	}
}

void FWebToUERuntimePresentation::CancelResourcePreload() const
{
	if (PendingResourceRequest.IsValid() && !PendingResourceRequest->HasLoadCompleted())
	{
		PendingResourceRequest->CancelHandle();
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::ResourceCancellations);
#if WITH_DEV_AUTOMATION_TESTS
		++ResourceCancellationsForTesting;
#endif
	}
	PendingResourceRequest.Reset();
}

void FWebToUERuntimePresentation::BeginResourcePreload() const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	CancelResourcePreload();
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	ResolvedResources.Reset();
	ResolvedResources.SetNum(Manifest.Num());
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::ResourceManifestEntries, Manifest.Num());
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::ResourceKnownOwnedBytes,
		ResolvedResources.GetAllocatedSize());

	TArray<FSoftObjectPath> PendingPaths;
	PendingPaths.Reserve(Manifest.Num());
	for (int32 Index = 0; Index < Manifest.Num(); ++Index)
	{
		const FWebToUECompiledResource& Resource = Manifest[Index];
		if (UObject* Object = Resource.Path.ResolveObject())
		{
			if (IsExpectedResourceType(Resource.Kind, Object))
			{
				ResolvedResources[Index].Reset(Object);
				FWebToUEPerformanceCapture::RecordCounter(
					EWebToUEPerformanceCounter::ResourceCacheHits);
			}
			else
			{
				FWebToUEPerformanceCapture::RecordCounter(
					EWebToUEPerformanceCounter::ResourceFailures);
#if WITH_DEV_AUTOMATION_TESTS
				++ResourceFailuresForTesting;
#endif
			}
		}
		else
		{
			PendingPaths.AddUnique(Resource.Path);
		}
	}
	if (!PendingPaths.IsEmpty())
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::ResourceAsyncRequests, PendingPaths.Num());
#if WITH_DEV_AUTOMATION_TESTS
		ResourceAsyncRequestsForTesting += PendingPaths.Num();
#endif
		const TWeakPtr<SWidget> WeakOwnerWidget = OwnerWidget.AsShared();
		PendingResourceRequest = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			PendingPaths, FStreamableDelegate::CreateLambda([WeakOwnerWidget]()
			{
				if (const TSharedPtr<SWidget> Widget = WeakOwnerWidget.Pin())
				{
					Widget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
				}
			}), FStreamableManager::AsyncLoadHighPriority,
			false, false, TEXT("WebToUEViewResources"));
		FinalizeResourcePreload();
	}
}

bool FWebToUERuntimePresentation::FinalizeResourcePreload() const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	if (!PendingResourceRequest.IsValid() || !PendingResourceRequest->HasLoadCompleted())
	{
		return false;
	}
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	for (int32 Index = 0; Index < Manifest.Num(); ++Index)
	{
		if (ResolvedResources.IsValidIndex(Index) && ResolvedResources[Index].IsValid())
		{
			continue;
		}
		UObject* Object = Manifest[Index].Path.ResolveObject();
		if (IsExpectedResourceType(Manifest[Index].Kind, Object))
		{
			ResolvedResources[Index].Reset(Object);
		}
		else
		{
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::ResourceFailures);
#if WITH_DEV_AUTOMATION_TESTS
			++ResourceFailuresForTesting;
#endif
		}
	}
	PendingResourceRequest.Reset();
	return true;
}

int32 FWebToUERuntimePresentation::FindResourceHandle(EWebToUEResourceKind Kind,
	const FSoftObjectPath& Path) const
{
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	return Manifest.IndexOfByPredicate([Kind, &Path](const FWebToUECompiledResource& Resource)
	{
		return Resource.Kind == Kind && Resource.Path == Path;
	});
}

UObject* FWebToUERuntimePresentation::GetResolvedResource(EWebToUEResourceKind Kind,
	const FSoftObjectPath& Path) const
{
	const int32 Handle = FindResourceHandle(Kind, Path);
	return ResolvedResources.IsValidIndex(Handle) ? ResolvedResources[Handle].Get() : nullptr;
}

UObject* FWebToUERuntimePresentation::GetResolvedFont(const FString& Family) const
{
	const FSoftObjectPath Path = GetDefault<UWebToUESettings>()->FindFontObjectPath(Family);
	return Path.IsValid() ? GetResolvedResource(EWebToUEResourceKind::Font, Path) : nullptr;
}

void FWebToUERuntimePresentation::MarkTextLayoutDependencyPath(const FWebToUENode& Node)
{
	MeasureDirtyNodes.Add(RuntimeInstance.GetHandle(&Node));
	if (FWebToUEDocument* RuntimeDocument = GetDocument())
	{
		RuntimeInstance.GetLayoutEngine().MarkMeasureDirty(
			*RuntimeDocument, RuntimeInstance.GetHandle(&Node));
	}
	for (const FWebToUENode* Current = &Node; Current; Current = Current->Parent)
	{
		LayoutDirtyNodes.Add(RuntimeInstance.GetHandle(Current));
	}
	bLayoutDirty = true;
}

bool FWebToUERuntimePresentation::ApplyBoundTextChange(FWebToUENode& Node)
{
	TUniquePtr<FWebToUETextLayoutCache>* CachePtr =
		TextLayouts.Find(RuntimeInstance.GetHandle(&Node));
	if (!CachePtr || !CachePtr->IsValid() || !(*CachePtr)->bHasKey)
	{
		MarkTextLayoutDependencyPath(Node);
		return true;
	}

	const FVector2f PreviousDesiredSize = (*CachePtr)->DesiredSize;
	const float PreviousWrapWidth = (*CachePtr)->Key.WrapWidth;
	PrepareTextLayoutInCache(Node, GetStyle(Node), PreviousWrapWidth, *CachePtr);
	MarkDisplayCommandDirty(Node);
	if (PreviousDesiredSize.Equals((*CachePtr)->DesiredSize, 0.01f))
	{
		return false;
	}
	MarkTextLayoutDependencyPath(Node);
	return true;
}

void FWebToUERuntimePresentation::ApplyStyleUpdates(
	TConstArrayView<FWebToUEStyleUpdate> Updates)
{
	if (FWebToUEDocument* RuntimeDocument = GetDocument())
	{
		RuntimeInstance.GetLayoutEngine().ApplyStyleUpdates(*RuntimeDocument, Updates);
	}
	bool bRebuildPaintOrder = false;
	int32 PatchedCommandCount = 0;
	for (const FWebToUEStyleUpdate& Update : Updates)
	{
		FWebToUENode* Node = RuntimeInstance.ResolveNode(Update.Target);
		if (!Node) continue;
		if (EnumHasAnyFlags(Update.Changes.Impacts, EWebToUEStyleImpact::Resource))
		{
			RebuildCaches(true);
			return;
		}
		if (EnumHasAnyFlags(Update.Changes.Impacts,
			EWebToUEStyleImpact::Measure | EWebToUEStyleImpact::Layout))
		{
			bLayoutDirty = true;
			bDisplayListDirty = true;
		}
		if (Update.Changes.ChangedProperties.Contains(EWebToUECssProperty::ZIndex))
		{
			bRebuildPaintOrder = true;
		}
		const bool bBrushChanged = Update.Changes.ChangedProperties.ContainsByPredicate(
			[](EWebToUECssProperty Property)
			{
				return Property == EWebToUECssProperty::BackgroundColor ||
					Property == EWebToUECssProperty::BorderColor ||
					Property == EWebToUECssProperty::BorderWidth ||
					Property == EWebToUECssProperty::BorderRadius;
			});
		if (bBrushChanged && Node->Type == EWebToUENodeType::Element)
		{
			RebuildBrush(*Node);
		}
		if (!bDisplayListDirty && EnumHasAnyFlags(Update.Changes.Impacts,
			EWebToUEStyleImpact::Paint | EWebToUEStyleImpact::HitTest))
		{
			const bool bAffectsDescendants = EnumHasAnyFlags(
				Update.Changes.Impacts, EWebToUEStyleImpact::HitTest) ||
				Update.Changes.ChangedProperties.Contains(EWebToUECssProperty::Display) ||
				Update.Changes.ChangedProperties.Contains(EWebToUECssProperty::Visibility) ||
				Update.Changes.ChangedProperties.Contains(EWebToUECssProperty::Overflow) ||
				Update.Changes.ChangedProperties.Contains(EWebToUECssProperty::Opacity);
			PatchedCommandCount += PatchDisplaySubtree(*Node, bAffectsDescendants);
		}
	}
	if (bRebuildPaintOrder)
	{
		RebuildPaintOrderCache();
		bDisplayListDirty = true;
	}
	else if (PatchedCommandCount > 0)
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::DisplayCommandsPatched, PatchedCommandCount);
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::DisplayCommandsReused,
			FMath::Max(0, DisplayCommands.Num() - PatchedCommandCount));
	}
}

void FWebToUERuntimePresentation::ApplyRuntimeStateChanges(
	TConstArrayView<FWebToUEInstanceHandle> ChangedNodes)
{
	if (bDisplayListDirty) return;
	int32 PatchedCommandCount = 0;
	for (const FWebToUEInstanceHandle Handle : ChangedNodes)
	{
		if (const FWebToUENode* Node = RuntimeInstance.ResolveNode(Handle))
		{
			PatchedCommandCount += PatchDisplaySubtree(*Node, true);
		}
	}
	if (PatchedCommandCount <= 0) return;
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplayCommandsPatched, PatchedCommandCount);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplayCommandsReused,
		FMath::Max(0, DisplayCommands.Num() - PatchedCommandCount));
}

FText FWebToUERuntimePresentation::GetDisplayText(const FWebToUENode& Node) const
{
	if (const FWebToUERuntimeNodeState* State = FindState(Node); State && State->bHasBoundText)
	{
		return State->BoundText;
	}
	return Node.bHasLocalizedText ? Node.LocalizedText : FText::FromString(Node.Text);
}

FSlateTextBlockLayout& FWebToUERuntimePresentation::PrepareTextLayout(
	const FWebToUENode& Node, const FWebToUEComputedStyle& Style, float WrapWidth) const
{
	const FWebToUEInstanceHandle Handle = RuntimeInstance.GetHandle(&Node);
	check(Handle.IsValid());
	TUniquePtr<FWebToUETextLayoutCache>& Cache = TextLayouts.FindOrAdd(Handle);
	return PrepareTextLayoutInCache(Node, Style, WrapWidth, Cache);
}

FSlateTextBlockLayout& FWebToUERuntimePresentation::PrepareTextLayoutInCache(
	const FWebToUENode& Node, const FWebToUEComputedStyle& Style, float WrapWidth,
	TUniquePtr<FWebToUETextLayoutCache>& Cache) const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	const bool bRichText = IsRichText(Node);
	const float EffectiveWrapWidth = Style.WhiteSpace == TEXT("normal") &&
		FMath::IsFinite(WrapWidth) && WrapWidth > 0.0f ? WrapWidth : 0.0f;
	FWebToUETextLayoutCache::FKey Key;
	Key.Text = GetDisplayText(Node).ToString();
	Key.FontFamily = Style.FontFamily;
	Key.FontWeight = Style.FontWeight;
	Key.TextAlign = Style.TextAlign;
	Key.WhiteSpace = Style.WhiteSpace;
	Key.CultureName = FInternationalization::Get().GetCurrentCulture()->GetName();
	Key.Color = Style.Color;
	Key.FontSize = Style.FontSize;
	Key.WrapWidth = EffectiveWrapWidth;
	Key.bRichText = bRichText;
	const bool bKeyChanged = !Cache || !Cache->bHasKey || !(Cache->Key == Key);
	const bool bRichLayoutChanged = !Cache || !Cache->bHasKey ||
		Cache->Key.bRichText != bRichText ||
		(bRichText && (Cache->Key.FontFamily != Key.FontFamily ||
			Cache->Key.FontWeight != Key.FontWeight ||
			Cache->Key.FontSize != Key.FontSize || Cache->Key.Color != Key.Color));
	if (!Cache || bRichLayoutChanged)
	{
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TextLayoutBuilds);
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		const FVector2f PreviousDesiredSize = Cache ? Cache->DesiredSize : FVector2f::ZeroVector;
		Cache = MakeUnique<FWebToUETextLayoutCache>();
		Cache->DesiredSize = PreviousDesiredSize;
		TSharedRef<ITextLayoutMarshaller> Marshaller = FPlainTextLayoutMarshaller::Create();
		if (bRichText)
		{
			Cache->RichTextStyleSet = MakeRichTextStyleSet(
				Style, GetResolvedFont(Style.FontFamily));
			Marshaller = FRichTextLayoutMarshaller::Create(
				TArray<TSharedRef<ITextDecorator>>(), Cache->RichTextStyleSet.Get());
		}
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		Cache->Layout = MakeUnique<FSlateTextBlockLayout>(&OwnerWidget,
			FTextBlockStyle::GetDefault(), TOptional<ETextShapingMethod>(),
			TOptional<ETextFlowDirection>(), FCreateSlateTextLayout(), Marshaller, nullptr);
	}
	if (!bKeyChanged && Cache->bHasKey)
	{
		return *Cache->Layout;
	}
	const FSlateTextBlockLayout::FWidgetDesiredSizeArgs DesiredSizeArgs(
		GetDisplayText(Node), FText::GetEmpty(), EffectiveWrapWidth, false,
		ETextWrappingPolicy::DefaultWrapping, ETextTransformPolicy::None, FMargin(), 1.0f, true,
		ToTextJustification(Style.TextAlign));
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TextLayoutComputes);
	Cache->Layout->ComputeDesiredSize(DesiredSizeArgs, 1.0f,
		MakeTextBlockStyle(Style, GetResolvedFont(Style.FontFamily)));
	Cache->Key = MoveTemp(Key);
	Cache->DesiredSize = FVector2f(Cache->Layout->GetDesiredSize());
	Cache->bHasKey = true;
	return *Cache->Layout;
}

FVector2f FWebToUERuntimePresentation::MeasureNode(const FWebToUENode& Node,
	const FWebToUELayoutEngine::FMeasureConstraints& Constraints) const
{
	return MeasureNodeWithStyle(Node, GetStyle(Node), Constraints);
}

FVector2f FWebToUERuntimePresentation::MeasureNodeWithStyle(const FWebToUENode& Node,
	const FWebToUEComputedStyle& Style,
	const FWebToUELayoutEngine::FMeasureConstraints& Constraints) const
{
	if (Node.Type == EWebToUENodeType::Text)
	{
		if (FSlateApplication::IsInitialized())
		{
			const float WrapWidth =
				Constraints.WidthMode == FWebToUELayoutEngine::EMeasureMode::Undefined
					? 0.0f : Constraints.Width;
			return FVector2f(PrepareTextLayout(Node, Style, WrapWidth).GetDesiredSize());
		}
		const float CharacterWidth = Style.FontSize * 0.5f;
		const float UnwrappedWidth = Node.Text.Len() * CharacterWidth;
		const bool bCanWrap = Style.WhiteSpace == TEXT("normal") &&
			Constraints.WidthMode != FWebToUELayoutEngine::EMeasureMode::Undefined &&
			Constraints.Width > 0.0f;
		const int32 LineCount = bCanWrap
			? FMath::Max(1, FMath::CeilToInt(UnwrappedWidth / Constraints.Width)) : 1;
		return FVector2f(
			bCanWrap ? FMath::Min(UnwrappedWidth, Constraints.Width) : UnwrappedWidth,
			LineCount * Style.FontSize * 1.25f);
	}
	if (Node.Tag == TEXT("img"))
	{
		if (const TSharedPtr<FSlateBrush>* Brush = Brushes.Find(RuntimeInstance.GetHandle(&Node)))
		{
			return (*Brush)->ImageSize;
		}
	}
	return FVector2f::ZeroVector;
}

void FWebToUERuntimePresentation::Layout(const FVector2f& ViewportSize) const
{
	FWebToUEDocument* RuntimeDocument = const_cast<FWebToUEDocument*>(GetDocument());
	if (!RuntimeDocument || !RuntimeDocument->Root) return;
	if (FinalizeResourcePreload())
	{
		TextLayouts.Reset();
		RuntimeDocument->ForEachNode([this](FWebToUENode& Node)
		{
			if (Node.Tag == TEXT("img")) RebuildBrush(Node);
		});
		bDisplayListDirty = true;
	}
	const bool bRebuildCommands = bDisplayListDirty ||
		!ViewportSize.Equals(LastViewportSize, 0.1f);
	RuntimeInstance.GetLayoutEngine().LayoutPersistent(*RuntimeDocument, ViewportSize,
		[this](const FWebToUENode& Node,
			const FWebToUELayoutEngine::FMeasureConstraints& Constraints)
		{
			return MeasureNode(Node, Constraints);
		});
	LastViewportSize = ViewportSize;
	bLayoutDirty = false;
	MeasureDirtyNodes.Reset();
	LayoutDirtyNodes.Reset();
	if (bRebuildCommands)
	{
		RebuildDisplayList();
	}
}

int32 FWebToUERuntimePresentation::Paint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& WidgetStyle, bool bParentEnabled) const
{
	FWebToUEDocument* RuntimeDocument = const_cast<FWebToUEDocument*>(GetDocument());
	if (!RuntimeDocument || !RuntimeDocument->Root) return LayerId;
	if (FinalizeResourcePreload())
	{
		TextLayouts.Reset();
		RuntimeDocument->ForEachNode([this](FWebToUENode& Node)
		{
			if (Node.Tag == TEXT("img")) RebuildBrush(Node);
		});
		bLayoutDirty = true;
		bDisplayListDirty = true;
	}
	const FVector2f ViewportSize = FVector2f(Geometry.GetLocalSize());
	if (bLayoutDirty || !ViewportSize.Equals(LastViewportSize, 0.1f))
	{
		Layout(ViewportSize);
	}
	else if (bDisplayListDirty)
	{
		RebuildDisplayList();
	}
	const FVector2f AbsoluteTopLeft(CullingRect.Left, CullingRect.Top);
	const FVector2f AbsoluteBottomRight(CullingRect.Right, CullingRect.Bottom);
	const FVector2f LocalTopLeft(
		Geometry.AbsoluteToLocal(FVector2D(AbsoluteTopLeft)));
	const FVector2f LocalBottomRight(
		Geometry.AbsoluteToLocal(FVector2D(AbsoluteBottomRight)));
	const FSlateRect LocalCullingRect(
		FMath::Min(LocalTopLeft.X, LocalBottomRight.X),
		FMath::Min(LocalTopLeft.Y, LocalBottomRight.Y),
		FMath::Max(LocalTopLeft.X, LocalBottomRight.X),
		FMath::Max(LocalTopLeft.Y, LocalBottomRight.Y));
	QueryDisplayCommands(LocalCullingRect, true, DisplayQueryScratch);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::PaintCommandsCulled,
		FMath::Max(0, DisplayCommands.Num() - DisplayQueryScratch.Num()));
	for (const int32 CommandIndex : DisplayQueryScratch)
	{
		if (!DisplayCommands.IsValidIndex(CommandIndex)) continue;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::PaintCommandsVisited);
		LayerId = PaintCommand(DisplayCommands[CommandIndex], Args, Geometry, CullingRect,
			Out, LayerId, WidgetStyle, bParentEnabled);
	}
	PaintDebugOverlay(Geometry, Out, LayerId);
	DirtyRects.Reset();
	DirtyCommandIndices.Reset();
	return LayerId;
}

void FWebToUERuntimePresentation::RebuildDisplayList() const
{
	DisplayCommands.Reset();
	DisplayCommandIndices.Reset();
	DisplayCommandRanges.Reset();
	DisplaySpatialCells.Reset();
	LargeDisplayCommands.Reset();
	DisplayQueryMarks.Reset();
	DisplayQueryScratch.Reset();
	DirtyRects.Reset();
	DirtyCommandIndices.Reset();
	const FWebToUEDocument* RuntimeDocument = GetDocument();
	if (!RuntimeDocument || !RuntimeDocument->Root)
	{
		bDisplayListDirty = false;
		return;
	}
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplayListBuilds);
	int32 RuntimeNodeCount = 0;
	RuntimeDocument->ForEachNode([&RuntimeNodeCount](FWebToUENode&) { ++RuntimeNodeCount; });
	DisplayCommands.Reserve(RuntimeNodeCount);
	DisplayCommandIndices.Reserve(RuntimeNodeCount);
	DisplayCommandRanges.Reserve(RuntimeNodeCount);
	BuildDisplaySubtree(*RuntimeDocument, *RuntimeDocument->Root, 1.0f, true, true,
		0, FVector2f::ZeroVector, FSlateRect(), false);
	DisplayQueryMarks.SetNumZeroed(DisplayCommands.Num());
	DisplayQueryScratch.Reserve(DisplayCommands.Num());
	DirtyRects.Reserve(DisplayCommands.Num());
	DirtyCommandIndices.Reserve(DisplayCommands.Num());
	RebuildDisplaySpatialIndex();
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplayCommandsBuilt, DisplayCommands.Num());
	bDisplayListDirty = false;
}

void FWebToUERuntimePresentation::UpdateDisplayCommand(
	const FWebToUEDocument& RuntimeDocument, const FWebToUENode& Node,
	FWebToUEPaintCommand& Command, float ParentOpacity, bool bParentDisplayed,
	bool bParentEnabled, int32 Depth, const FVector2f& InheritedScrollOffset,
	const FSlateRect& InheritedClip, bool bHasInheritedClip) const
{
	const FWebToUEComputedStyle& Style = GetStyle(Node);
	const FWebToUERuntimeLayoutResult& LayoutResult = GetLayout(Node);
	const FVector2f Position = LayoutResult.Position - InheritedScrollOffset;
	const FVector2f Size = LayoutResult.Size;
	Command.Owner = RuntimeInstance.GetHandle(&Node);
	Command.Type = Node.Type == EWebToUENodeType::Text
		? EWebToUEPaintCommandType::Text : EWebToUEPaintCommandType::Box;
	Command.Bounds = FSlateRect(Position.X, Position.Y,
		Position.X + Size.X, Position.Y + Size.Y);
	Command.Depth = Depth;
	Command.Opacity = ParentOpacity * Style.Opacity;
	Command.bDisplayed = bParentDisplayed && RuntimeDocument.IsDisplayed(Node);
	if (bHasInheritedClip && (!InheritedClip.IsValid() || InheritedClip.IsEmpty()))
	{
		Command.bDisplayed = false;
	}
	Command.bEnabled = bParentEnabled && Style.bEnabled;
	Command.bHasClip = bHasInheritedClip;
	Command.ClipBounds = InheritedClip;
	Command.VisibleBounds = Command.bDisplayed ? Command.Bounds : FSlateRect();
	if (Command.bDisplayed && Command.bHasClip)
	{
		Command.VisibleBounds = Command.Bounds.IntersectionWith(Command.ClipBounds);
	}
	Command.SubtreeBounds = Command.VisibleBounds;
	Command.bDrawable = Node.Type == EWebToUENodeType::Text;
	Command.bInteractive = Node.IsInteractive();
	Command.bScrollable = RuntimeDocument.IsScrollable(Node) &&
		GetState(Node).MaxScrollOffset.Y > 0.0f;
	Command.bSpatiallyIndexed = false;
	Command.bLargeSpatialEntry = false;
	Command.SpatialCells = FWebToUESpatialCellRange();
	if (Node.Type == EWebToUENodeType::Element)
	{
		const TSharedPtr<FSlateBrush>* Brush = Brushes.Find(Command.Owner);
		Command.bDrawable = Brush && Brush->IsValid() &&
			(Node.Tag == TEXT("img") || Style.BackgroundColor.A > 0.0f ||
				Style.BorderWidth > 0.0f);
	}
	Command.BatchKey.Type = Command.Type;
	Command.BatchKey.ResourceKey =
		WebToUE::Runtime::Presentation::Private::MakeBatchResourceKey(Node, Style);
	Command.BatchKey.ClipKey = Command.bHasClip
		? WebToUE::Runtime::Presentation::Private::HashRect(Command.ClipBounds) : 0;
	Command.BatchKey.DrawEffects = static_cast<uint8>(Command.bEnabled
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect);
}

void FWebToUERuntimePresentation::BuildDisplaySubtree(
	const FWebToUEDocument& RuntimeDocument, const FWebToUENode& Node,
	float ParentOpacity, bool bParentDisplayed, bool bParentEnabled, int32 Depth,
	const FVector2f& InheritedScrollOffset, const FSlateRect& InheritedClip,
	bool bHasInheritedClip) const
{
	const int32 RangeStart = DisplayCommands.Num();
	const int32 CommandIndex = DisplayCommands.AddDefaulted();
	FWebToUEPaintCommand& Command = DisplayCommands[CommandIndex];
	UpdateDisplayCommand(RuntimeDocument, Node, Command, ParentOpacity, bParentDisplayed,
		bParentEnabled, Depth, InheritedScrollOffset, InheritedClip, bHasInheritedClip);
	const FWebToUEInstanceHandle OwnerHandle = Command.Owner;
	const float ChildOpacity = Command.Opacity;
	const bool bChildDisplayed = Command.bDisplayed;
	const bool bChildEnabled = Command.bEnabled;
	DisplayCommandIndices.Add(OwnerHandle, CommandIndex);

	FSlateRect ChildClip = InheritedClip;
	bool bHasChildClip = bHasInheritedClip;
	if (RuntimeDocument.ClipsOverflow(Node))
	{
		ChildClip = bHasInheritedClip
			? InheritedClip.IntersectionWith(Command.Bounds) : Command.Bounds;
		bHasChildClip = true;
	}
	const FVector2f ChildScrollOffset = InheritedScrollOffset +
		(RuntimeDocument.IsScrollable(Node)
			? GetState(Node).ScrollOffset : FVector2f::ZeroVector);
	for (const FWebToUEInstanceHandle ChildHandle : GetPaintOrder(Node))
	{
		const FWebToUENode* Child = RuntimeInstance.ResolveNode(ChildHandle);
		if (!Child) continue;
		BuildDisplaySubtree(RuntimeDocument, *Child, ChildOpacity, bChildDisplayed,
			bChildEnabled, Depth + 1, ChildScrollOffset, ChildClip, bHasChildClip);
	}
	DisplayCommandRanges.Add(OwnerHandle,
		{ RangeStart, DisplayCommands.Num() - RangeStart });
	UpdateDisplaySubtreeBounds(Node);
}

void FWebToUERuntimePresentation::UpdateDisplaySubtreeBounds(
	const FWebToUENode& Node) const
{
	const int32* CommandIndex =
		DisplayCommandIndices.Find(RuntimeInstance.GetHandle(&Node));
	if (!CommandIndex || !DisplayCommands.IsValidIndex(*CommandIndex)) return;
	FSlateRect SubtreeBounds = DisplayCommands[*CommandIndex].VisibleBounds;
	for (const FWebToUEInstanceHandle ChildHandle : GetPaintOrder(Node))
	{
		const int32* ChildIndex = DisplayCommandIndices.Find(ChildHandle);
		if (ChildIndex && DisplayCommands.IsValidIndex(*ChildIndex))
		{
			SubtreeBounds = WebToUE::Runtime::Presentation::Private::UnionRects(
				SubtreeBounds, DisplayCommands[*ChildIndex].SubtreeBounds);
		}
	}
	DisplayCommands[*CommandIndex].SubtreeBounds = SubtreeBounds;
}

void FWebToUERuntimePresentation::RebuildDisplaySpatialIndex() const
{
	DisplaySpatialCells.Reset();
	LargeDisplayCommands.Reset();
	for (int32 CommandIndex = 0; CommandIndex < DisplayCommands.Num(); ++CommandIndex)
	{
		AddDisplayCommandToSpatialIndex(CommandIndex);
	}
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplaySpatialIndexBuilds);
}

void FWebToUERuntimePresentation::AddDisplayCommandToSpatialIndex(
	int32 CommandIndex) const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	if (!DisplayCommands.IsValidIndex(CommandIndex)) return;
	FWebToUEPaintCommand& Command = DisplayCommands[CommandIndex];
	Command.bSpatiallyIndexed = false;
	Command.bLargeSpatialEntry = false;
	Command.SpatialCells = FWebToUESpatialCellRange();
	if (!Command.bDisplayed || !IsUsableRect(Command.VisibleBounds) ||
		(!Command.bDrawable && !Command.bInteractive && !Command.bScrollable))
	{
		return;
	}
	Command.SpatialCells.MinX = FMath::FloorToInt(Command.VisibleBounds.Left / SpatialCellSize);
	Command.SpatialCells.MinY = FMath::FloorToInt(Command.VisibleBounds.Top / SpatialCellSize);
	Command.SpatialCells.MaxX = FMath::FloorToInt(Command.VisibleBounds.Right / SpatialCellSize);
	Command.SpatialCells.MaxY = FMath::FloorToInt(Command.VisibleBounds.Bottom / SpatialCellSize);
	Command.bSpatiallyIndexed = true;
	if (Command.SpatialCells.GetCellCount() > MaxCellsPerCommand)
	{
		Command.bLargeSpatialEntry = true;
		LargeDisplayCommands.Add(CommandIndex);
		return;
	}
	for (int32 Y = Command.SpatialCells.MinY; Y <= Command.SpatialCells.MaxY; ++Y)
	{
		for (int32 X = Command.SpatialCells.MinX; X <= Command.SpatialCells.MaxX; ++X)
		{
			DisplaySpatialCells.FindOrAdd(MakeSpatialCellKey(X, Y)).Add(CommandIndex);
		}
	}
}

void FWebToUERuntimePresentation::RemoveDisplayCommandFromSpatialIndex(
	int32 CommandIndex) const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	if (!DisplayCommands.IsValidIndex(CommandIndex)) return;
	FWebToUEPaintCommand& Command = DisplayCommands[CommandIndex];
	if (!Command.bSpatiallyIndexed) return;
	if (Command.bLargeSpatialEntry)
	{
		LargeDisplayCommands.RemoveSingle(CommandIndex);
	}
	else
	{
		const FWebToUESpatialCellRange PreviousCells = Command.SpatialCells;
		for (int32 Y = PreviousCells.MinY; Y <= PreviousCells.MaxY; ++Y)
		{
			for (int32 X = PreviousCells.MinX; X <= PreviousCells.MaxX; ++X)
			{
				const uint64 CellKey = MakeSpatialCellKey(X, Y);
				if (TArray<int32>* Cell = DisplaySpatialCells.Find(CellKey))
				{
					Cell->RemoveSingle(CommandIndex);
				}
			}
		}
	}
	Command.bSpatiallyIndexed = false;
	Command.bLargeSpatialEntry = false;
	Command.SpatialCells = FWebToUESpatialCellRange();
}

void FWebToUERuntimePresentation::QueryDisplayCommands(const FSlateRect& LocalBounds,
	bool bRequireDrawable, TArray<int32>& OutCommandIndices) const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	OutCommandIndices.Reset();
	if (!IsUsableRect(LocalBounds) || DisplayCommands.IsEmpty()) return;
	if (DisplayQueryMarks.Num() != DisplayCommands.Num())
	{
		DisplayQueryMarks.SetNumZeroed(DisplayCommands.Num());
	}
	if (++DisplayQueryGeneration == 0)
	{
		DisplayQueryMarks.Init(0, DisplayCommands.Num());
		DisplayQueryGeneration = 1;
	}
	const auto AddCandidate = [this, &LocalBounds, bRequireDrawable,
		&OutCommandIndices](int32 CommandIndex)
	{
		if (!DisplayCommands.IsValidIndex(CommandIndex) ||
			DisplayQueryMarks[CommandIndex] == DisplayQueryGeneration)
		{
			return;
		}
		DisplayQueryMarks[CommandIndex] = DisplayQueryGeneration;
		const FWebToUEPaintCommand& Command = DisplayCommands[CommandIndex];
		const bool bRelevant = bRequireDrawable ? Command.bDrawable
			: (Command.bInteractive || Command.bScrollable);
		if (Command.bDisplayed && bRelevant &&
			FSlateRect::DoRectanglesIntersect(Command.VisibleBounds, LocalBounds))
		{
			OutCommandIndices.Add(CommandIndex);
		}
	};
	for (const int32 CommandIndex : LargeDisplayCommands)
	{
		AddCandidate(CommandIndex);
	}
	const FWebToUESpatialCellRange QueryCells = {
		FMath::FloorToInt(LocalBounds.Left / SpatialCellSize),
		FMath::FloorToInt(LocalBounds.Top / SpatialCellSize),
		FMath::FloorToInt(LocalBounds.Right / SpatialCellSize),
		FMath::FloorToInt(LocalBounds.Bottom / SpatialCellSize)
	};
	if (QueryCells.GetCellCount() <= 4096)
	{
		for (int32 Y = QueryCells.MinY; Y <= QueryCells.MaxY; ++Y)
		{
			for (int32 X = QueryCells.MinX; X <= QueryCells.MaxX; ++X)
			{
				if (const TArray<int32>* Cell =
					DisplaySpatialCells.Find(MakeSpatialCellKey(X, Y)))
				{
					for (const int32 CommandIndex : *Cell) AddCandidate(CommandIndex);
				}
			}
		}
	}
	else
	{
		for (const TPair<uint64, TArray<int32>>& Pair : DisplaySpatialCells)
		{
			for (const int32 CommandIndex : Pair.Value) AddCandidate(CommandIndex);
		}
	}
	OutCommandIndices.Sort();
}

void FWebToUERuntimePresentation::AddDirtyRegion(const FSlateRect& PreviousBounds,
	const FSlateRect& CurrentBounds, int32 CommandIndex) const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	const FSlateRect Region = UnionRects(PreviousBounds, CurrentBounds);
	if (!IsUsableRect(Region)) return;
	DirtyRects.Add(Region);
	DirtyCommandIndices.AddUnique(CommandIndex);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DirtyRectsAdded);
}

void FWebToUERuntimePresentation::MarkDisplayCommandDirty(
	const FWebToUENode& Node) const
{
	const int32* CommandIndex =
		DisplayCommandIndices.Find(RuntimeInstance.GetHandle(&Node));
	if (CommandIndex && DisplayCommands.IsValidIndex(*CommandIndex))
	{
		const FSlateRect Bounds = DisplayCommands[*CommandIndex].VisibleBounds;
		AddDirtyRegion(Bounds, Bounds, *CommandIndex);
	}
}

int32 FWebToUERuntimePresentation::PatchDisplaySubtree(
	const FWebToUENode& Node, bool bIncludeDescendants) const
{
	const FWebToUEDocument* RuntimeDocument = GetDocument();
	const FWebToUEDisplayCommandRange* Range =
		DisplayCommandRanges.Find(RuntimeInstance.GetHandle(&Node));
	if (!RuntimeDocument || !Range || Range->Num <= 0)
	{
		bDisplayListDirty = true;
		return 0;
	}

	const int32* RootCommandIndex =
		DisplayCommandIndices.Find(RuntimeInstance.GetHandle(&Node));
	if (!RootCommandIndex || !DisplayCommands.IsValidIndex(*RootCommandIndex))
	{
		bDisplayListDirty = true;
		return 0;
	}
	const FWebToUEPaintCommand& RootCommand = DisplayCommands[*RootCommandIndex];
	float ParentOpacity = 1.0f;
	bool bParentDisplayed = true;
	bool bParentEnabled = true;
	if (Node.Parent)
	{
		const int32* ParentCommandIndex =
			DisplayCommandIndices.Find(RuntimeInstance.GetHandle(Node.Parent));
		if (ParentCommandIndex && DisplayCommands.IsValidIndex(*ParentCommandIndex))
		{
			const FWebToUEPaintCommand& ParentCommand =
				DisplayCommands[*ParentCommandIndex];
			ParentOpacity = ParentCommand.Opacity;
			bParentDisplayed = ParentCommand.bDisplayed;
			bParentEnabled = ParentCommand.bEnabled;
		}
	}
	const FVector2f InheritedScrollOffset = GetLayout(Node).Position -
		RootCommand.Bounds.GetTopLeft2f();
	const FSlateRect InheritedClip = RootCommand.ClipBounds;
	const bool bHasInheritedClip = RootCommand.bHasClip;
	const int32 RootDepth = RootCommand.Depth;

	int32 PatchedCount = 0;
	const auto Patch = [&](const auto& Self, const FWebToUENode& Current,
		float InParentOpacity,
		bool bInParentDisplayed, bool bInParentEnabled, int32 Depth,
		const FVector2f& InScrollOffset, const FSlateRect& InClip, bool bInHasClip)
	{
		const int32* CommandIndex =
			DisplayCommandIndices.Find(RuntimeInstance.GetHandle(&Current));
		if (!CommandIndex || !DisplayCommands.IsValidIndex(*CommandIndex)) return;
		FWebToUEPaintCommand& Command = DisplayCommands[*CommandIndex];
		const FSlateRect PreviousBounds = Command.VisibleBounds;
		RemoveDisplayCommandFromSpatialIndex(*CommandIndex);
		UpdateDisplayCommand(*RuntimeDocument, Current, Command, InParentOpacity,
			bInParentDisplayed, bInParentEnabled, Depth, InScrollOffset, InClip, bInHasClip);
		AddDisplayCommandToSpatialIndex(*CommandIndex);
		AddDirtyRegion(PreviousBounds, Command.VisibleBounds, *CommandIndex);
		++PatchedCount;
		FSlateRect ChildClip = InClip;
		bool bHasChildClip = bInHasClip;
		if (RuntimeDocument->ClipsOverflow(Current))
		{
			ChildClip = bInHasClip ? InClip.IntersectionWith(Command.Bounds) : Command.Bounds;
			bHasChildClip = true;
		}
		const FVector2f ChildScrollOffset = InScrollOffset +
			(RuntimeDocument->IsScrollable(Current)
				? GetState(Current).ScrollOffset : FVector2f::ZeroVector);
		if (bIncludeDescendants)
		{
			for (const FWebToUEInstanceHandle ChildHandle : GetPaintOrder(Current))
			{
				if (const FWebToUENode* Child = RuntimeInstance.ResolveNode(ChildHandle))
				{
					Self(Self, *Child, Command.Opacity, Command.bDisplayed,
						Command.bEnabled, Depth + 1,
						ChildScrollOffset, ChildClip, bHasChildClip);
				}
			}
		}
		UpdateDisplaySubtreeBounds(Current);
	};
	Patch(Patch, Node, ParentOpacity, bParentDisplayed, bParentEnabled,
		RootDepth, InheritedScrollOffset, InheritedClip, bHasInheritedClip);
	for (const FWebToUENode* Ancestor = Node.Parent; Ancestor; Ancestor = Ancestor->Parent)
	{
		UpdateDisplaySubtreeBounds(*Ancestor);
	}
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplaySpatialIndexPatches, PatchedCount);
	return PatchedCount;
}

void FWebToUERuntimePresentation::PaintDebugOverlay(const FGeometry& Geometry,
	FSlateWindowElementList& Out, int32& LayerId) const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	const int32 DebugMode = CVarDisplayListDebug.GetValueOnGameThread();
	if (DebugMode <= 0) return;
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const auto DrawOutline = [&Geometry, &Out, &LayerId, WhiteBrush](
		const FSlateRect& Rect, const FLinearColor& Color)
	{
		if (!IsUsableRect(Rect)) return;
		constexpr float Thickness = 1.5f;
		const FVector2f Position = Rect.GetTopLeft2f();
		const FVector2f Size = Rect.GetSize2f();
		FSlateDrawElement::MakeBox(Out, LayerId,
			Geometry.ToPaintGeometry(FVector2f(Size.X, Thickness),
				FSlateLayoutTransform(Position)), WhiteBrush, ESlateDrawEffect::None, Color);
		FSlateDrawElement::MakeBox(Out, LayerId,
			Geometry.ToPaintGeometry(FVector2f(Size.X, Thickness),
				FSlateLayoutTransform(Position + FVector2f(0.0f, Size.Y - Thickness))),
			WhiteBrush, ESlateDrawEffect::None, Color);
		FSlateDrawElement::MakeBox(Out, LayerId,
			Geometry.ToPaintGeometry(FVector2f(Thickness, Size.Y),
				FSlateLayoutTransform(Position)), WhiteBrush, ESlateDrawEffect::None, Color);
		FSlateDrawElement::MakeBox(Out, LayerId,
			Geometry.ToPaintGeometry(FVector2f(Thickness, Size.Y),
				FSlateLayoutTransform(Position + FVector2f(Size.X - Thickness, 0.0f))),
			WhiteBrush, ESlateDrawEffect::None, Color);
		++LayerId;
	};
	if (DebugMode >= 3)
	{
		for (const FWebToUEPaintCommand& Command : DisplayCommands)
		{
			if (Command.bDisplayed)
			{
				DrawOutline(Command.VisibleBounds, FLinearColor(0.0f, 0.65f, 1.0f, 0.65f));
			}
		}
	}
	if (DebugMode == 1 || DebugMode >= 3)
	{
		for (const FSlateRect& DirtyRect : DirtyRects)
		{
			constexpr float DirtyRectPadding = 2.0f;
			DrawOutline(FSlateRect(
				DirtyRect.Left - DirtyRectPadding,
				DirtyRect.Top - DirtyRectPadding,
				DirtyRect.Right + DirtyRectPadding,
				DirtyRect.Bottom + DirtyRectPadding),
				FLinearColor(1.0f, 0.1f, 0.1f, 0.9f));
		}
	}
	if (DebugMode == 2 || DebugMode >= 3)
	{
		for (const int32 CommandIndex : DirtyCommandIndices)
		{
			if (DisplayCommands.IsValidIndex(CommandIndex))
			{
				DrawOutline(DisplayCommands[CommandIndex].VisibleBounds,
					FLinearColor(1.0f, 0.55f, 0.0f, 0.9f));
			}
		}
	}
}

int32 FWebToUERuntimePresentation::PaintCommand(
	const FWebToUEPaintCommand& Command, const FPaintArgs& Args,
	const FGeometry& Geometry, const FSlateRect& CullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& WidgetStyle,
	bool bParentEnabled) const
{
	if (!Command.bDisplayed || !Command.bDrawable) return LayerId;
	const FWebToUENode* Node = RuntimeInstance.ResolveNode(Command.Owner);
	if (!Node) return LayerId;
	const FWebToUEComputedStyle& Style = GetStyle(*Node);
	const FVector2f Position(Command.Bounds.Left, Command.Bounds.Top);
	const FVector2f Size = Command.Bounds.GetSize2f();
	bool bPushedClip = false;
	if (Command.bHasClip)
	{
		Out.PushClip(FSlateClippingZone(Geometry.MakeChild(
			Command.ClipBounds.GetSize2f(),
			FSlateLayoutTransform(Command.ClipBounds.GetTopLeft2f()))));
		bPushedClip = true;
	}

	const float DrawOpacity =
		Command.Opacity * WidgetStyle.GetColorAndOpacityTint().A;
	if (Command.Type == EWebToUEPaintCommandType::Box)
	{
		if (const TSharedPtr<FSlateBrush>* Brush = Brushes.Find(Command.Owner))
		{
			FVector2f DrawPosition = Position;
			FVector2f DrawSize = Size;
			bool bClipImage = false;
			if (Node->Tag == TEXT("img"))
			{
				const FVector2f IntrinsicSize = (*Brush)->ImageSize;
				if (IntrinsicSize.X > 0.0f && IntrinsicSize.Y > 0.0f &&
					(Style.ObjectFit == TEXT("contain") || Style.ObjectFit == TEXT("cover")))
				{
					const float ScaleX = Size.X / IntrinsicSize.X;
					const float ScaleY = Size.Y / IntrinsicSize.Y;
					const float Scale = Style.ObjectFit == TEXT("cover")
						? FMath::Max(ScaleX, ScaleY) : FMath::Min(ScaleX, ScaleY);
					DrawSize = IntrinsicSize * Scale;
					DrawPosition += (Size - DrawSize) * 0.5f;
					bClipImage = Style.ObjectFit == TEXT("cover");
				}
			}
			if (bClipImage)
			{
				Out.PushClip(FSlateClippingZone(
					Geometry.MakeChild(Size, FSlateLayoutTransform(Position))));
			}
			FSlateDrawElement::MakeBox(Out, LayerId++,
				Geometry.ToPaintGeometry(DrawSize, FSlateLayoutTransform(DrawPosition)),
				Brush->Get(), bParentEnabled && Command.bEnabled
					? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
				FLinearColor(1.0f, 1.0f, 1.0f, DrawOpacity));
			if (bClipImage) Out.PopClip();
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::PaintDrawElements);
		}
	}
	else
	{
		FSlateTextBlockLayout& TextLayout = PrepareTextLayout(*Node, Style, Size.X);
		const FGeometry TextGeometry =
			Geometry.MakeChild(Size, FSlateLayoutTransform(Position));
		FWidgetStyle TextWidgetStyle = WidgetStyle;
		TextWidgetStyle.BlendColorAndOpacityTint(
			FLinearColor(1.0f, 1.0f, 1.0f, Command.Opacity));
		LayerId = TextLayout.OnPaint(Args, TextGeometry, CullingRect, Out, LayerId,
			TextWidgetStyle, bParentEnabled && Command.bEnabled) + 1;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::PaintDrawElements);
	}
	if (bPushedClip) Out.PopClip();
	return LayerId;
}

void FWebToUERuntimePresentation::RebuildBrushes(bool bReloadResources) const
{
	if (bReloadResources)
	{
		Brushes.Reset();
		BeginResourcePreload();
	}
	const FWebToUEDocument* RuntimeDocument = GetDocument();
	if (!RuntimeDocument) return;
	RuntimeDocument->ForEachNode([this](FWebToUENode& Node)
	{
		RebuildBrush(Node);
	});
}

void FWebToUERuntimePresentation::RebuildBrush(FWebToUENode& Node) const
{
	const FWebToUEComputedStyle& Style = GetStyle(Node);
	if (Node.Tag == TEXT("img"))
	{
		const FWebToUEInstanceHandle NodeHandle = RuntimeInstance.GetHandle(&Node);
		Brushes.Remove(NodeHandle);
		UTexture2D* Texture = Cast<UTexture2D>(GetResolvedResource(
			EWebToUEResourceKind::Texture, FSoftObjectPath(Node.GetAttribute(TEXT("src")))));
		if (Texture)
		{
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::BrushBuilds);
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::TrackedAllocations);
			TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
			Brush->DrawAs = ESlateBrushDrawType::Image;
			Brush->SetResourceObject(Texture);
			Brush->ImageSize = FVector2f(Texture->GetSizeX(), Texture->GetSizeY());
			Brushes.Add(NodeHandle, MoveTemp(Brush));
		}
	}
	else if (Node.Type == EWebToUENodeType::Element)
	{
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::BrushBuilds);
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::TrackedAllocations);
		Brushes.Add(RuntimeInstance.GetHandle(&Node), MakeShared<FSlateRoundedBoxBrush>(Style.BackgroundColor,
			Style.BorderRadius, Style.BorderColor, Style.BorderWidth, FVector2f(32.0f, 32.0f)));
	}
}

#if WITH_DEV_AUTOMATION_TESTS
const void* FWebToUERuntimePresentation::GetBrushIdentityForTesting(
	const FWebToUENode& Node) const
{
	const TSharedPtr<FSlateBrush>* Brush = Brushes.Find(RuntimeInstance.GetHandle(&Node));
	return Brush ? Brush->Get() : nullptr;
}

int32 FWebToUERuntimePresentation::FindResourceHandleForTesting(
	EWebToUEResourceKind Kind, const FSoftObjectPath& Path) const
{
	return FindResourceHandle(Kind, Path);
}

const UObject* FWebToUERuntimePresentation::GetResourceObjectForTesting(int32 Handle) const
{
	return ResolvedResources.IsValidIndex(Handle) ? ResolvedResources[Handle].Get() : nullptr;
}
#endif

void FWebToUERuntimePresentation::RebuildPaintOrderCache()
{
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::PaintOrderCacheBuilds);
	PaintOrderNodes.Reset();
	PaintOrderRanges.Reset();
	bDisplayListDirty = true;
	FWebToUEDocument* RuntimeDocument = GetDocument();
	if (!RuntimeDocument) return;

	int32 ChildCount = 0;
	int32 ParentCount = 0;
	RuntimeDocument->ForEachNode([&ChildCount, &ParentCount](FWebToUENode& Node)
	{
		ChildCount += Node.Children.Num();
		ParentCount += Node.Children.IsEmpty() ? 0 : 1;
	});
	PaintOrderNodes.Reserve(ChildCount);
	PaintOrderRanges.Reserve(ParentCount);

	RuntimeDocument->ForEachNode([this](FWebToUENode& Node)
	{
		if (Node.Children.IsEmpty()) return;
		const int32 StartIndex = PaintOrderNodes.Num();
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			PaintOrderNodes.Add(RuntimeInstance.GetHandle(Child.Get()));
		}
		const int32 Num = PaintOrderNodes.Num() - StartIndex;
		TArrayView<FWebToUEInstanceHandle> Children(PaintOrderNodes.GetData() + StartIndex, Num);
		Algo::StableSort(Children, [this](FWebToUEInstanceHandle A, FWebToUEInstanceHandle B)
		{
			const FWebToUENode* NodeA = RuntimeInstance.ResolveNode(A);
			const FWebToUENode* NodeB = RuntimeInstance.ResolveNode(B);
			check(NodeA && NodeB);
			return GetStyle(*NodeA).ZIndex < GetStyle(*NodeB).ZIndex;
		});
		PaintOrderRanges.Add(RuntimeInstance.GetHandle(&Node), { StartIndex, Num });
	});
}

TConstArrayView<FWebToUEInstanceHandle> FWebToUERuntimePresentation::GetPaintOrder(
	const FWebToUENode& Parent) const
{
	if (const FWebToUEPaintOrderRange* Range = PaintOrderRanges.Find(RuntimeInstance.GetHandle(&Parent)))
	{
		return TConstArrayView<FWebToUEInstanceHandle>(
			PaintOrderNodes.GetData() + Range->StartIndex, Range->Num);
	}
	return {};
}

FVector2f FWebToUERuntimePresentation::GetVisualPosition(const FWebToUENode& Node) const
{
	FVector2f ScrollOffset = FVector2f::ZeroVector;
	for (const FWebToUENode* Parent = Node.Parent; Parent; Parent = Parent->Parent)
	{
		ScrollOffset += GetState(*Parent).ScrollOffset;
	}
	return GetLayout(Node).Position - ScrollOffset;
}

FWebToUENode* FWebToUERuntimePresentation::HitTest(const FVector2f& LocalPosition) const
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_HitTest);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_HitTest);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::HitTest);
	if (bDisplayListDirty && !bLayoutDirty) RebuildDisplayList();
	constexpr float PointExtent = 0.01f;
	QueryDisplayCommands(FSlateRect(LocalPosition.X, LocalPosition.Y,
		LocalPosition.X + PointExtent, LocalPosition.Y + PointExtent),
		false, DisplayQueryScratch);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::HitTestCandidates, DisplayQueryScratch.Num());
	for (int32 QueryIndex = DisplayQueryScratch.Num() - 1; QueryIndex >= 0; --QueryIndex)
	{
		const int32 CommandIndex = DisplayQueryScratch[QueryIndex];
		if (!DisplayCommands.IsValidIndex(CommandIndex)) continue;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::HitTestCommandsVisited);
		const FWebToUEPaintCommand& Command = DisplayCommands[CommandIndex];
		if (!Command.bInteractive || !Command.bEnabled ||
			!Command.VisibleBounds.ContainsPoint(FVector2D(LocalPosition)))
		{
			continue;
		}
		if (FWebToUENode* Node = RuntimeInstance.ResolveNode(Command.Owner)) return Node;
	}
	return nullptr;
}

bool FWebToUERuntimePresentation::ScrollAt(
	const FVector2f& LocalPosition, float WheelDelta)
{
	if (FMath::IsNearlyZero(WheelDelta)) return false;
	if (bDisplayListDirty && !bLayoutDirty) RebuildDisplayList();
	constexpr float PointExtent = 0.01f;
	QueryDisplayCommands(FSlateRect(LocalPosition.X, LocalPosition.Y,
		LocalPosition.X + PointExtent, LocalPosition.Y + PointExtent),
		false, DisplayQueryScratch);
	DisplayQueryScratch.Sort([this](int32 A, int32 B)
	{
		if (!DisplayCommands.IsValidIndex(A)) return false;
		if (!DisplayCommands.IsValidIndex(B)) return true;
		if (DisplayCommands[A].Depth != DisplayCommands[B].Depth)
		{
			return DisplayCommands[A].Depth > DisplayCommands[B].Depth;
		}
		return A > B;
	});
	constexpr float WheelScrollAmount = 48.0f;
	for (const int32 CommandIndex : DisplayQueryScratch)
	{
		if (!DisplayCommands.IsValidIndex(CommandIndex)) continue;
		const FWebToUEPaintCommand& Command = DisplayCommands[CommandIndex];
		if (!Command.bScrollable ||
			!Command.VisibleBounds.ContainsPoint(FVector2D(LocalPosition))) continue;
		FWebToUENode* Node = RuntimeInstance.ResolveNode(Command.Owner);
		if (!Node) continue;
		FWebToUERuntimeNodeState& State = GetState(*Node);
		const float PreviousOffset = State.ScrollOffset.Y;
		State.ScrollOffset.Y = FMath::Clamp(
			PreviousOffset - WheelDelta * WheelScrollAmount,
			0.0f, State.MaxScrollOffset.Y);
		if (!FMath::IsNearlyEqual(PreviousOffset, State.ScrollOffset.Y))
		{
			ApplyScrollOffsetChange(*Node);
			return true;
		}
	}
	return false;
}

void FWebToUERuntimePresentation::ApplyScrollOffsetChange(const FWebToUENode& Node)
{
	if (bDisplayListDirty) return;
	const int32 PatchedCommandCount = PatchDisplaySubtree(Node);
	if (PatchedCommandCount <= 0) return;
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplayCommandsPatched, PatchedCommandCount);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplayCommandsReused,
		FMath::Max(0, DisplayCommands.Num() - PatchedCommandCount));
}

#if WITH_DEV_AUTOMATION_TESTS
FVector2f FWebToUERuntimePresentation::MeasureTextForTesting(
	const FString& Text, float Width, bool bWrap) const
{
	FWebToUENode Node;
	Node.Type = EWebToUENodeType::Text;
	Node.Tag = TEXT("#text");
	Node.Text = Text;
	FWebToUEComputedStyle Style;
	Style.WhiteSpace = bWrap ? TEXT("normal") : TEXT("nowrap");
	TUniquePtr<FWebToUETextLayoutCache> Cache;
	return FVector2f(PrepareTextLayoutInCache(Node, Style, Width, Cache).GetDesiredSize());
}

FVector2f FWebToUERuntimePresentation::MeasureRichTextForTesting(
	const FString& Markup, float Width, bool bWrap) const
{
	FWebToUENode Node;
	Node.Type = EWebToUENodeType::Text;
	Node.Tag = TEXT("#text");
	Node.Text = Markup;
	Node.bRichText = true;
	FWebToUEComputedStyle Style;
	Style.WhiteSpace = bWrap ? TEXT("normal") : TEXT("nowrap");
	TUniquePtr<FWebToUETextLayoutCache> Cache;
	return FVector2f(PrepareTextLayoutInCache(Node, Style, Width, Cache).GetDesiredSize());
}

FVector2f FWebToUERuntimePresentation::PrepareTextLayoutForTesting(
	const FWebToUENode& Node, const FWebToUEComputedStyle& Style, float WrapWidth) const
{
	return FVector2f(PrepareTextLayout(Node, Style, WrapWidth).GetDesiredSize());
}

bool FWebToUERuntimePresentation::IsMeasureDirtyForTesting(
	const FWebToUENode& Node) const
{
	return MeasureDirtyNodes.Contains(RuntimeInstance.GetHandle(&Node));
}

bool FWebToUERuntimePresentation::IsLayoutPathDirtyForTesting(
	const FWebToUENode& Node) const
{
	return LayoutDirtyNodes.Contains(RuntimeInstance.GetHandle(&Node));
}

FString FWebToUERuntimePresentation::GetTextCacheCultureForTesting(
	const FWebToUENode& Node) const
{
	if (const TUniquePtr<FWebToUETextLayoutCache>* Cache =
		TextLayouts.Find(RuntimeInstance.GetHandle(&Node));
		Cache && Cache->IsValid() && (*Cache)->bHasKey)
	{
		return (*Cache)->Key.CultureName;
	}
	return FString();
}

const void* FWebToUERuntimePresentation::GetTextLayoutCacheIdentityForTesting(
	const FWebToUENode& Node) const
{
	if (const TUniquePtr<FWebToUETextLayoutCache>* Cache =
		TextLayouts.Find(RuntimeInstance.GetHandle(&Node)))
	{
		return Cache->Get();
	}
	return nullptr;
}

const FWebToUEPaintCommand* FWebToUERuntimePresentation::GetDisplayCommandForTesting(
	const FWebToUENode& Node) const
{
	const int32* CommandIndex =
		DisplayCommandIndices.Find(RuntimeInstance.GetHandle(&Node));
	return CommandIndex && DisplayCommands.IsValidIndex(*CommandIndex)
		? &DisplayCommands[*CommandIndex] : nullptr;
}

const FWebToUEDisplayCommandRange*
FWebToUERuntimePresentation::GetDisplayCommandRangeForTesting(
	const FWebToUENode& Node) const
{
	return DisplayCommandRanges.Find(RuntimeInstance.GetHandle(&Node));
}
#endif
