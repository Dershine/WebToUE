#include "WebToUERuntimePresentation.h"

#include "SWebToUEView.h"
#include "WebToUEPerformance.h"
#include "WebToUERuntimeInstance.h"
#include "WebToUESettings.h"

#include "Algo/StableSort.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "SlateMaterialBrush.h"
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
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
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
		Style.SetColorAndOpacity(FLinearColor::White);
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

	static float ResolveTransformLength(const FWebToUELength& Length, float Extent)
	{
		return Length.Unit == EWebToUEUnit::Percent
			? Extent * Length.Value * 0.01f : Length.Value;
	}

	static FTransform2D ResolveLocalTransform(
		const FWebToUEVisualTransformValue& Value,
		const FWebToUETransformOriginValue& OriginValue,
		const FVector2f& Size)
	{
		const FVector2f Translation = Value.TranslationPixels +
			Value.TranslationByWidth * Size.X + Value.TranslationByHeight * Size.Y;
		const FVector2f Origin(
			ResolveTransformLength(OriginValue.X, Size.X),
			ResolveTransformLength(OriginValue.Y, Size.Y));
		const FTransform2D Affine(
			FMatrix2x2(Value.M00, Value.M01, Value.M10, Value.M11), Translation);
		return FTransform2D(-Origin).Concatenate(Affine).Concatenate(FTransform2D(Origin));
	}

	static bool TryInvertTransform(const FTransform2D& Transform,
		FTransform2D& OutInverse)
	{
		float A = 0.0f;
		float B = 0.0f;
		float C = 0.0f;
		float D = 0.0f;
		Transform.GetMatrix().GetMatrix(A, B, C, D);
		const FVector2D Translation = Transform.GetTranslation();
		const float Determinant = A * D - B * C;
		if (!FMath::IsFinite(A) || !FMath::IsFinite(B) || !FMath::IsFinite(C) ||
			!FMath::IsFinite(D) || !FMath::IsFinite(Translation.X) ||
			!FMath::IsFinite(Translation.Y) || FMath::IsNearlyZero(Determinant))
		{
			return false;
		}
		OutInverse = Transform.Inverse();
		return !OutInverse.ContainsNaN();
	}

	static FSlateRect TransformRectBounds(
		const FTransform2D& Transform, const FVector2f& Size)
	{
		const FVector2f Points[] = {
			Transform.TransformPoint(FVector2f::ZeroVector),
			Transform.TransformPoint(FVector2f(Size.X, 0.0f)),
			Transform.TransformPoint(FVector2f(0.0f, Size.Y)),
			Transform.TransformPoint(Size)
		};
		FSlateRect Bounds(Points[0].X, Points[0].Y, Points[0].X, Points[0].Y);
		for (int32 Index = 1; Index < UE_ARRAY_COUNT(Points); ++Index)
		{
			Bounds.Left = FMath::Min(Bounds.Left, Points[Index].X);
			Bounds.Top = FMath::Min(Bounds.Top, Points[Index].Y);
			Bounds.Right = FMath::Max(Bounds.Right, Points[Index].X);
			Bounds.Bottom = FMath::Max(Bounds.Bottom, Points[Index].Y);
		}
		return Bounds;
	}

	static FWebToUEClipZone MakeClipZone(
		const FTransform2D& LocalToView, const FVector2f& Size)
	{
		FWebToUEClipZone Zone;
		Zone.LocalSize = Size;
		Zone.LocalToView = LocalToView;
		Zone.TopLeft = LocalToView.TransformPoint(FVector2f::ZeroVector);
		Zone.TopRight = LocalToView.TransformPoint(FVector2f(Size.X, 0.0f));
		Zone.BottomLeft = LocalToView.TransformPoint(FVector2f(0.0f, Size.Y));
		Zone.BottomRight = LocalToView.TransformPoint(Size);
		Zone.Bounds = TransformRectBounds(LocalToView, Size);
		return Zone;
	}

	static bool IsPointInsideClipZone(
		const FWebToUEClipZone& Zone, const FVector2f& Point)
	{
		return FSlateClippingZone(Zone.TopLeft, Zone.TopRight,
			Zone.BottomLeft, Zone.BottomRight).IsPointInside(Point);
	}

	static uint32 HashClipChain(TConstArrayView<FWebToUEClipZone> ClipChain)
	{
		uint32 Hash = 0;
		for (const FWebToUEClipZone& Zone : ClipChain)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Zone.TopLeft));
			Hash = HashCombineFast(Hash, GetTypeHash(Zone.TopRight));
			Hash = HashCombineFast(Hash, GetTypeHash(Zone.BottomLeft));
			Hash = HashCombineFast(Hash, GetTypeHash(Zone.BottomRight));
		}
		return Hash;
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
		if (!Node.ResourceId.IsEmpty())
		{
			return GetTypeHash(Node.ResourceId);
		}
		// Rounded boxes use the shared white resource. Fill and outline colors are
		// vertex data and must not split otherwise compatible Slate batches.
		return 0;
	}

	static uint32 MakeBatchShaderKey(const FWebToUENode& Node,
		const FWebToUEComputedStyle& Style, const FVector2f& Size)
	{
		if (Node.Type == EWebToUENodeType::Text)
		{
			return 0;
		}
		if (!Node.ResourceId.IsEmpty())
		{
			uint32 Hash = GetTypeHash(Size.X);
			return HashCombineFast(Hash, GetTypeHash(Size.Y));
		}
		uint32 Hash = GetTypeHash(Size.X);
		Hash = HashCombineFast(Hash, GetTypeHash(Size.Y));
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
	if (CompositingCache) CompositingCache->Shutdown();
	CancelResourceRequests();
	ResetDynamicMaterials();
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
	if (CompositingCache) CompositingCache->Shutdown();
	CompositingCache.Reset();
	CompositingCacheOwnerId = 0;
	CompositingCacheGeneration = 0;
	CompositingPlan = {};
	LastCompositingDiagnostic.Reset();
	++CompositingProjectionRevision;
	LastViewportSize = FVector2f(-1.0f, -1.0f);
	bLayoutDirty = true;
	Brushes.Reset();
	ResetDynamicMaterials();
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
	CancelResourceRequests();
	ResolvedResources.Reset();
	ResourceLoadStates.Reset();
	PendingResourceRequests.Reset();
	bCriticalResourcesReady = true;
#if WITH_DEV_AUTOMATION_TESTS
	ResourceLoadAttemptsForTesting = 0;
	ResourceAsyncRequestsForTesting = 0;
	ResourceFailuresForTesting = 0;
	ResourceCancellationsForTesting = 0;
#endif
}

void FWebToUERuntimePresentation::HandleSurfaceChanged(FName SurfaceId)
{
	if (CompositingSurfaceId == SurfaceId) return;
	if (CompositingCache) CompositingCache->DetachSurface();
	CompositingCache.Reset();
	CompositingCacheOwnerId = 0;
	CompositingCacheGeneration = 0;
	CompositingSurfaceId = SurfaceId;
	LastCompositingDiagnostic.Reset();
	++CompositingProjectionRevision;
	bDisplayListDirty = true;
}

#if WITH_DEV_AUTOMATION_TESTS
uint64 FWebToUERuntimePresentation::GetKnownOwnedBytesForTesting() const
{
	uint64 Bytes = sizeof(*this) + Brushes.GetAllocatedSize() + TextLayouts.GetAllocatedSize() +
		MeasureDirtyNodes.GetAllocatedSize() + LayoutDirtyNodes.GetAllocatedSize() +
		ResolvedResources.GetAllocatedSize() + ResourceLoadStates.GetAllocatedSize() +
		PendingResourceRequests.GetAllocatedSize() + PaintOrderNodes.GetAllocatedSize() +
		PaintOrderRanges.GetAllocatedSize() + DisplayCommands.GetAllocatedSize() +
		DisplayCommandIndices.GetAllocatedSize() + DisplayCommandRanges.GetAllocatedSize() +
		DisplaySpatialCells.GetAllocatedSize() + LargeDisplayCommands.GetAllocatedSize() +
		DisplayQueryMarks.GetAllocatedSize() + DisplayQueryScratch.GetAllocatedSize() +
		DirtyRects.GetAllocatedSize() + DirtyCommandIndices.GetAllocatedSize() +
		DynamicMaterials.GetAllocatedSize();
	for (const TPair<uint64, TArray<int32>>& Pair : DisplaySpatialCells)
	{
		Bytes += Pair.Value.GetAllocatedSize();
	}
	for (const FWebToUEPaintCommand& Command : DisplayCommands)
	{
		Bytes += Command.ClipChain.GetAllocatedSize();
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

void FWebToUERuntimePresentation::ResetDynamicMaterials() const
{
	if (!DynamicMaterials.IsEmpty())
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::MaterialInstancesReleased,
			DynamicMaterials.Num());
	}
	DynamicMaterials.Reset();
}

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
		CancelResourceRequests();
		ResolvedResources.Reset();
		ResourceLoadStates.Reset();
		PendingResourceRequests.Reset();
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
		case EWebToUEResourceKind::Material:
			return Object->IsA<UMaterialInterface>();
		case EWebToUEResourceKind::Font:
			return Object->IsA<UFont>() || Object->IsA<UFontFace>();
		case EWebToUEResourceKind::StringTable:
			return Object->IsA<UStringTable>();
		default:
			return false;
		}
	}

	static bool DoesResourceMatchContract(
		const FWebToUECompiledResource& Resource, UObject* Object)
	{
		if (!IsExpectedResourceType(Resource.Kind, Object))
		{
			return false;
		}
		if (Resource.Kind == EWebToUEResourceKind::Material)
		{
			return Resource.BrushImageSize.X > 0.0f &&
				Resource.BrushImageSize.Y > 0.0f;
		}
		if (Resource.Kind != EWebToUEResourceKind::Texture ||
			Resource.IntrinsicSize.X <= 0.0f || Resource.IntrinsicSize.Y <= 0.0f)
		{
			return true;
		}
		const UTexture2D* Texture = CastChecked<UTexture2D>(Object);
		const FIntPoint ImportedSize = Texture->GetImportedSize();
		return Resource.IntrinsicSize == FVector2f(ImportedSize.X, ImportedSize.Y);
	}
}

void FWebToUERuntimePresentation::CancelResourceRequests() const
{
	for (int32 Index = 0; Index < PendingResourceRequests.Num(); ++Index)
	{
		TSharedPtr<FStreamableHandle>& Request = PendingResourceRequests[Index];
		if (!Request.IsValid() || Request->HasLoadCompleted())
		{
			continue;
		}
		Request->CancelHandle();
		if (ResourceLoadStates.IsValidIndex(Index))
		{
			ResourceLoadStates[Index] = EResourceLoadState::NotRequested;
		}
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::ResourceCancellations);
#if WITH_DEV_AUTOMATION_TESTS
		++ResourceCancellationsForTesting;
#endif
	}
	PendingResourceRequests.Reset();
}

void FWebToUERuntimePresentation::InitializeResourceResidency() const
{
	CancelResourceRequests();
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	ResolvedResources.Reset();
	ResolvedResources.SetNum(Manifest.Num());
	ResourceLoadStates.Reset();
	ResourceLoadStates.Init(EResourceLoadState::NotRequested, Manifest.Num());
	PendingResourceRequests.Reset();
	PendingResourceRequests.SetNum(Manifest.Num());
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::ResourceManifestEntries, Manifest.Num());
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::ResourceKnownOwnedBytes,
		ResolvedResources.GetAllocatedSize());

	for (int32 Index = 0; Index < Manifest.Num(); ++Index)
	{
		const FWebToUECompiledResource& Resource = Manifest[Index];
		const bool bResidencyManaged =
			Resource.Kind == EWebToUEResourceKind::Texture ||
			Resource.Kind == EWebToUEResourceKind::Material;
		if (!bResidencyManaged ||
			Resource.Residency == EWebToUEResidencyClass::Critical)
		{
			RequestResource(Index);
		}
	}
	RefreshCriticalResourceReadiness();
}

bool FWebToUERuntimePresentation::RequestResource(int32 Handle) const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	if (!Manifest.IsValidIndex(Handle) || !ResourceLoadStates.IsValidIndex(Handle) ||
		ResourceLoadStates[Handle] != EResourceLoadState::NotRequested)
	{
		return false;
	}
	const FWebToUECompiledResource& Resource = Manifest[Handle];
	if (UObject* Object = Resource.Path.ResolveObject())
	{
		if (DoesResourceMatchContract(Resource, Object))
		{
			ResolvedResources[Handle].Reset(Object);
			ResourceLoadStates[Handle] = EResourceLoadState::Resolved;
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::ResourceCacheHits);
		}
		else
		{
			ResourceLoadStates[Handle] = EResourceLoadState::Failed;
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::ResourceFailures);
#if WITH_DEV_AUTOMATION_TESTS
			++ResourceFailuresForTesting;
#endif
		}
		return true;
	}

	const TWeakPtr<SWidget> WeakOwnerWidget = OwnerWidget.AsShared();
	PendingResourceRequests[Handle] =
		UAssetManager::GetStreamableManager().RequestAsyncLoad(
			Resource.Path, FStreamableDelegate::CreateLambda([WeakOwnerWidget]()
			{
				if (const TSharedPtr<SWidget> Widget = WeakOwnerWidget.Pin())
				{
					Widget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
				}
			}), FStreamableManager::AsyncLoadHighPriority,
			false, false, *FString::Printf(TEXT("WebToUEResource:%s"), *Resource.ResourceId));
	if (PendingResourceRequests[Handle].IsValid())
	{
		ResourceLoadStates[Handle] = EResourceLoadState::Pending;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::ResourceAsyncRequests);
#if WITH_DEV_AUTOMATION_TESTS
		++ResourceAsyncRequestsForTesting;
#endif
	}
	else
	{
		ResourceLoadStates[Handle] = EResourceLoadState::Failed;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::ResourceFailures);
#if WITH_DEV_AUTOMATION_TESTS
		++ResourceFailuresForTesting;
#endif
	}
	return true;
}

bool FWebToUERuntimePresentation::RequestVisibleResources() const
{
	const FWebToUEDocument* RuntimeDocument = GetDocument();
	if (!RuntimeDocument)
	{
		return false;
	}
	bool bRequested = false;
	RuntimeDocument->ForEachNode([this, RuntimeDocument, &bRequested](FWebToUENode& Node)
	{
		if (Node.ResourceId.IsEmpty() || !RuntimeDocument->IsDisplayed(Node))
		{
			return;
		}
		const int32 Handle = FindResourceHandleById(Node.ResourceId);
		const TConstArrayView<FWebToUECompiledResource> Manifest =
			RuntimeInstance.GetResourceManifest();
		if (Manifest.IsValidIndex(Handle) &&
			Manifest[Handle].Residency == EWebToUEResidencyClass::Visible)
		{
			bRequested |= RequestResource(Handle);
		}
	});
	return bRequested;
}

bool FWebToUERuntimePresentation::FinalizeResourcePreload() const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	bool bChanged = false;
	for (int32 Index = 0; Index < Manifest.Num(); ++Index)
	{
		if (!PendingResourceRequests.IsValidIndex(Index) ||
			!PendingResourceRequests[Index].IsValid() ||
			!PendingResourceRequests[Index]->HasLoadCompleted())
		{
			continue;
		}
		UObject* Object = Manifest[Index].Path.ResolveObject();
		if (DoesResourceMatchContract(Manifest[Index], Object))
		{
			ResolvedResources[Index].Reset(Object);
			ResourceLoadStates[Index] = EResourceLoadState::Resolved;
		}
		else
		{
			ResourceLoadStates[Index] = EResourceLoadState::Failed;
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::ResourceFailures);
#if WITH_DEV_AUTOMATION_TESTS
			++ResourceFailuresForTesting;
#endif
		}
		PendingResourceRequests[Index].Reset();
		bChanged = true;
	}
	if (bChanged)
	{
		RefreshCriticalResourceReadiness();
	}
	return bChanged;
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

int32 FWebToUERuntimePresentation::FindResourceHandleById(
	const FString& ResourceId) const
{
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	return Manifest.IndexOfByPredicate([&ResourceId](const FWebToUECompiledResource& Resource)
	{
		return Resource.ResourceId == ResourceId;
	});
}

UObject* FWebToUERuntimePresentation::GetResolvedResource(EWebToUEResourceKind Kind,
	const FSoftObjectPath& Path) const
{
	const int32 Handle = FindResourceHandle(Kind, Path);
	return ResolvedResources.IsValidIndex(Handle) ? ResolvedResources[Handle].Get() : nullptr;
}

UObject* FWebToUERuntimePresentation::GetResolvedResourceById(
	const FString& ResourceId) const
{
	const int32 Handle = FindResourceHandleById(ResourceId);
	return ResolvedResources.IsValidIndex(Handle) ? ResolvedResources[Handle].Get() : nullptr;
}

bool FWebToUERuntimePresentation::AreCriticalResourcesReady() const
{
	return bCriticalResourcesReady;
}

void FWebToUERuntimePresentation::RefreshCriticalResourceReadiness() const
{
	bCriticalResourcesReady = true;
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	for (int32 Index = 0; Index < Manifest.Num(); ++Index)
	{
		if ((Manifest[Index].Kind == EWebToUEResourceKind::Texture ||
			 Manifest[Index].Kind == EWebToUEResourceKind::Material) &&
			Manifest[Index].Residency == EWebToUEResidencyClass::Critical &&
			(!ResourceLoadStates.IsValidIndex(Index) ||
				ResourceLoadStates[Index] != EResourceLoadState::Resolved))
		{
			bCriticalResourcesReady = false;
			return;
		}
	}
}

bool FWebToUERuntimePresentation::RequestLazyResource(
	const FString& ResourceId) const
{
	const int32 Handle = FindResourceHandleById(ResourceId);
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	if (!Manifest.IsValidIndex(Handle) ||
		Manifest[Handle].Residency != EWebToUEResidencyClass::Lazy)
	{
		return false;
	}
	const bool bRequested = RequestResource(Handle);
	if (bRequested && ResourceLoadStates[Handle] == EResourceLoadState::Resolved)
	{
		if (const FWebToUEDocument* RuntimeDocument = GetDocument())
		{
			RuntimeDocument->ForEachNode([this, &ResourceId](FWebToUENode& Node)
			{
				if (Node.ResourceId == ResourceId)
				{
					RebuildBrush(Node);
				}
			});
		}
		bLayoutDirty = true;
		bDisplayListDirty = true;
	}
	return bRequested;
}

bool FWebToUERuntimePresentation::ValidateMaterialParameter(
	FWebToUEInstanceHandle Target,
	const FWebToUEPropertyAddress& Address,
	FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::MaterialParameterLookups);
	const FWebToUENode* Node = RuntimeInstance.ResolveNode(Target);
	if (!Node || Node->ResourceId.IsEmpty())
	{
		OutDiagnostic = TEXT("WTUE-MID-002: target is stale or has no Material resource.");
		return false;
	}
	const int32 ResourceHandle = FindResourceHandleById(Node->ResourceId);
	const TConstArrayView<FWebToUECompiledResource> Manifest =
		RuntimeInstance.GetResourceManifest();
	if (!Manifest.IsValidIndex(ResourceHandle) ||
		Manifest[ResourceHandle].Kind != EWebToUEResourceKind::Material)
	{
		OutDiagnostic = TEXT("WTUE-MID-002: target does not resolve to a Material resource.");
		return false;
	}
	UMaterialInterface* Material = Cast<UMaterialInterface>(
		GetResolvedResourceById(Node->ResourceId));
	if (!Material)
	{
		OutDiagnostic = TEXT("WTUE-MID-002: Material resource is not resident.");
		return false;
	}

	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Ids;
	if (Address.MaterialParameterType == EWebToUEMaterialParameterType::Scalar)
	{
		Material->GetAllScalarParameterInfo(Infos, Ids);
	}
	else if (Address.MaterialParameterType == EWebToUEMaterialParameterType::Vector)
	{
		Material->GetAllVectorParameterInfo(Infos, Ids);
	}
	else
	{
		OutDiagnostic = TEXT("WTUE-MID-001: only typed Scalar and Vector parameters are supported.");
		return false;
	}
	const bool bFound = Infos.ContainsByPredicate([&Address](
		const FMaterialParameterInfo& Info)
	{
		return Info.Name == Address.MaterialParameter &&
			Info.Association == EMaterialParameterAssociation::GlobalParameter;
	});
	if (!bFound)
	{
		OutDiagnostic = FString::Printf(
			TEXT("WTUE-MID-003: Material has no global %s parameter '%s'."),
			Address.MaterialParameterType == EWebToUEMaterialParameterType::Scalar
				? TEXT("Scalar") : TEXT("Vector"),
			*Address.MaterialParameter.ToString());
	}
	return bFound;
}

bool FWebToUERuntimePresentation::ApplyMaterialParameterChange(
	FWebToUEInstanceHandle Target,
	const FWebToUEPropertyAddress& Address) const
{
	(void)Address;
	FWebToUENode* Node = RuntimeInstance.ResolveNode(Target);
	if (!Node)
	{
		return false;
	}
	UMaterialInterface* BaseMaterial = Cast<UMaterialInterface>(
		GetResolvedResourceById(Node->ResourceId));
	const TMap<FWebToUEPropertyAddress, FWebToUEMaterialParameterRuntimeState>* States =
		RuntimeInstance.FindMaterialParameterStates(Target);
	if (!BaseMaterial || !States || States->IsEmpty())
	{
		return false;
	}

	UMaterialInstanceDynamic* DynamicMaterial = nullptr;
	if (TStrongObjectPtr<UMaterialInstanceDynamic>* Existing =
		DynamicMaterials.Find(Target))
	{
		DynamicMaterial = Existing->Get();
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::MaterialInstancesReused);
	}
	else
	{
		DynamicMaterial = UMaterialInstanceDynamic::Create(
			BaseMaterial, GetTransientPackage());
		if (!DynamicMaterial)
		{
			return false;
		}
		DynamicMaterials.Emplace(Target,
			TStrongObjectPtr<UMaterialInstanceDynamic>(DynamicMaterial));
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::MaterialInstancesCreated);
	}

	for (const TPair<FWebToUEPropertyAddress,
		FWebToUEMaterialParameterRuntimeState>& Pair : *States)
	{
		if (Pair.Key.MaterialParameterType == EWebToUEMaterialParameterType::Scalar)
		{
			DynamicMaterial->SetScalarParameterValue(
				Pair.Key.MaterialParameter, Pair.Value.Value.Scalar);
		}
		else if (Pair.Key.MaterialParameterType == EWebToUEMaterialParameterType::Vector)
		{
			DynamicMaterial->SetVectorParameterValue(
				Pair.Key.MaterialParameter, Pair.Value.Value.Vector);
		}
	}

	RebuildBrush(*Node);
	if (!bDisplayListDirty)
	{
		const int32 PatchedCommandCount = PatchDisplaySubtree(*Node, false);
		if (PatchedCommandCount > 0)
		{
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::DisplayCommandsPatched,
				PatchedCommandCount);
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::DisplayCommandsReused,
				FMath::Max(0, DisplayCommands.Num() - PatchedCommandCount));
		}
	}
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::MaterialBrushPatches);
	return true;
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
		const auto HasAnimationOverlay = [this, Target = Update.Target](
			EWebToUECssProperty Property)
		{
			return OwnerWidget.FindAnimationOverlay(
				Target, FWebToUEPropertyAddress::Css(Property)) != nullptr;
		};
		const bool bBrushChanged =
			Update.Changes.ChangedProperties.Contains(EWebToUECssProperty::BorderWidth) ||
			Update.Changes.ChangedProperties.Contains(EWebToUECssProperty::BorderRadius) ||
			(Update.Changes.ChangedProperties.Contains(
				EWebToUECssProperty::BackgroundColor) &&
				!HasAnimationOverlay(EWebToUECssProperty::BackgroundColor)) ||
			(Update.Changes.ChangedProperties.Contains(
				EWebToUECssProperty::BorderColor) &&
				!HasAnimationOverlay(EWebToUECssProperty::BorderColor));
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

bool FWebToUERuntimePresentation::ApplyAnimationOverlayChange(
	FWebToUEInstanceHandle Target,
	const FWebToUEPropertyAddress& Address) const
{
	const bool bOpacity =
		Address == FWebToUEPropertyAddress::Css(EWebToUECssProperty::Opacity);
	const bool bColor =
		Address == FWebToUEPropertyAddress::Css(EWebToUECssProperty::Color);
	const bool bBackground =
		Address == FWebToUEPropertyAddress::Css(EWebToUECssProperty::BackgroundColor);
	const bool bBorder =
		Address == FWebToUEPropertyAddress::Css(EWebToUECssProperty::BorderColor);
	const bool bTransform =
		Address == FWebToUEPropertyAddress::VisualTransform();
	if (!bOpacity && !bColor && !bBackground && !bBorder && !bTransform)
	{
		return false;
	}
	const FWebToUENode* Node = RuntimeInstance.ResolveNode(Target);
	if (!Node)
	{
		return false;
	}
	if (bDisplayListDirty)
	{
		return true;
	}
	const int32 PatchedCommandCount = PatchDisplaySubtree(
		*Node, bOpacity || bColor || bTransform);
	if (PatchedCommandCount > 0)
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::DisplayCommandsPatched,
			PatchedCommandCount);
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::DisplayCommandsReused,
			FMath::Max(0, DisplayCommands.Num() - PatchedCommandCount));
	}
	return true;
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
	Key.FontSize = Style.FontSize;
	Key.WrapWidth = EffectiveWrapWidth;
	Key.bRichText = bRichText;
	const bool bKeyChanged = !Cache || !Cache->bHasKey || !(Cache->Key == Key);
	const bool bRichLayoutChanged = !Cache || !Cache->bHasKey ||
		Cache->Key.bRichText != bRichText ||
		(bRichText && (Cache->Key.FontFamily != Key.FontFamily ||
			Cache->Key.FontWeight != Key.FontWeight ||
			Cache->Key.FontSize != Key.FontSize));
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
		const int32 Handle = FindResourceHandleById(Node.ResourceId);
		const TConstArrayView<FWebToUECompiledResource> Manifest =
			RuntimeInstance.GetResourceManifest();
		if (Manifest.IsValidIndex(Handle))
		{
			return Manifest[Handle].IntrinsicSize;
		}
	}
	return FVector2f::ZeroVector;
}

void FWebToUERuntimePresentation::Layout(const FVector2f& ViewportSize) const
{
	FWebToUEDocument* RuntimeDocument = const_cast<FWebToUEDocument*>(GetDocument());
	if (!RuntimeDocument || !RuntimeDocument->Root) return;
	const bool bRequestedVisibleResources = RequestVisibleResources();
	if (FinalizeResourcePreload() || bRequestedVisibleResources)
	{
		TextLayouts.Reset();
		RuntimeDocument->ForEachNode([this](FWebToUENode& Node)
		{
			if (!Node.ResourceId.IsEmpty()) RebuildBrush(Node);
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
			if (!Node.ResourceId.IsEmpty()) RebuildBrush(Node);
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
		EWebToUEPerformanceCounter::CompositingRedraws);
	if (!DisplayQueryScratch.IsEmpty())
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::CompositingPasses);
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::CompositingCommands,
			DisplayQueryScratch.Num());
	}
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::PaintCommandsCulled,
		FMath::Max(0, DisplayCommands.Num() - DisplayQueryScratch.Num()));
	const FWebToUEPaintCommand* PreviousBatchableCommand = nullptr;
	int32 PreviousBatchLayer = INDEX_NONE;
	for (const int32 CommandIndex : DisplayQueryScratch)
	{
		if (!DisplayCommands.IsValidIndex(CommandIndex)) continue;
		const FWebToUEPaintCommand& Command = DisplayCommands[CommandIndex];
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::PaintCommandsVisited);
		const bool bCanReusePreviousLayer =
			PreviousBatchableCommand && Command.Type == EWebToUEPaintCommandType::Box &&
			PreviousBatchableCommand->BatchKey == Command.BatchKey;
		const int32 CommandLayer = bCanReusePreviousLayer ? PreviousBatchLayer : LayerId;
		const int32 NextLayer = PaintCommand(Command, Args, Geometry, CullingRect,
			Out, CommandLayer, WidgetStyle, bParentEnabled);
		if (bCanReusePreviousLayer)
		{
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::PaintCommandsLayerMerged);
		}
		else
		{
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::PaintBatchRuns);
		}
		LayerId = FMath::Max(LayerId, NextLayer);
		if (Command.Type == EWebToUEPaintCommandType::Box)
		{
			PreviousBatchableCommand = &Command;
			PreviousBatchLayer = CommandLayer;
		}
		else
		{
			PreviousBatchableCommand = nullptr;
			PreviousBatchLayer = INDEX_NONE;
		}
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
		0, FVector2f::ZeroVector, FTransform2D(), {});
	RebuildCompositingPlan();
	DisplayQueryMarks.SetNumZeroed(DisplayCommands.Num());
	DisplayQueryScratch.Reserve(DisplayCommands.Num());
	DirtyRects.Reserve(DisplayCommands.Num());
	DirtyCommandIndices.Reserve(DisplayCommands.Num());
	RebuildDisplaySpatialIndex();
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplayCommandsBuilt, DisplayCommands.Num());
	bDisplayListDirty = false;
}

bool FWebToUERuntimePresentation::RebuildCompositingPlan() const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::CompositingPlanBuilds);
	LastCompositingDiagnostic.Reset();
	TArray<FWebToUECompositingNodeRequest> Requests;
	Requests.Reserve(DisplayCommands.Num());
	for (int32 CommandIndex = 0; CommandIndex < DisplayCommands.Num(); ++CommandIndex)
	{
		const FWebToUEPaintCommand& Command = DisplayCommands[CommandIndex];
		const FWebToUENode* Node = RuntimeInstance.ResolveNode(Command.Owner);
		if (!Node) continue;
		FWebToUECompositingNodeRequest& Planned = Requests.AddDefaulted_GetRef();
		Planned.Owner = Command.Owner;
		Planned.PaintSequence = CommandIndex;

		if (!Node->ResourceId.IsEmpty())
		{
			const int32 ResourceHandle = FindResourceHandleById(Node->ResourceId);
			const TConstArrayView<FWebToUECompiledResource> Manifest =
				RuntimeInstance.GetResourceManifest();
			if (Manifest.IsValidIndex(ResourceHandle) &&
				Manifest[ResourceHandle].Kind == EWebToUEResourceKind::Material)
			{
				Planned.Request.Requirements |=
					EWebToUECompositingRequirement::MaterialBrush;
			}
		}

		const FWebToUEComputedStyle& Style = GetStyle(*Node);
		if (Style.Opacity > 0.0f && Style.Opacity < 1.0f)
		{
			const TConstArrayView<FWebToUEInstanceHandle> Children = GetPaintOrder(*Node);
			bool bHasOverlappingChildren = false;
			for (int32 LeftIndex = 0;
				LeftIndex < Children.Num() && !bHasOverlappingChildren; ++LeftIndex)
			{
				const int32* LeftCommandIndex = DisplayCommandIndices.Find(Children[LeftIndex]);
				if (!LeftCommandIndex || !DisplayCommands.IsValidIndex(*LeftCommandIndex)) continue;
				const FSlateRect& Left = DisplayCommands[*LeftCommandIndex].SubtreeBounds;
				if (!IsUsableRect(Left)) continue;
				for (int32 RightIndex = LeftIndex + 1; RightIndex < Children.Num(); ++RightIndex)
				{
					const int32* RightCommandIndex =
						DisplayCommandIndices.Find(Children[RightIndex]);
					if (!RightCommandIndex ||
						!DisplayCommands.IsValidIndex(*RightCommandIndex)) continue;
					const FSlateRect& Right =
						DisplayCommands[*RightCommandIndex].SubtreeBounds;
					bHasOverlappingChildren = IsUsableRect(Right) &&
						Left.Left < Right.Right && Left.Right > Right.Left &&
						Left.Top < Right.Bottom && Left.Bottom > Right.Top;
					if (bHasOverlappingChildren) break;
				}
			}
			if (bHasOverlappingChildren)
			{
				Planned.Request.Requirements |=
					EWebToUECompositingRequirement::IsolatedSubtree;
				Planned.Request.PixelExtent = FIntPoint(
					FMath::Max(1, FMath::CeilToInt(Command.SubtreeBounds.Right -
						Command.SubtreeBounds.Left)),
					FMath::Max(1, FMath::CeilToInt(Command.SubtreeBounds.Bottom -
						Command.SubtreeBounds.Top)));
			}
		}
	}

	FWebToUECompositingBackend Backend;
	Backend.bMaterialBrushAvailable = true;
	Backend.bSubtreeLayerAvailable = false;
	Backend.bRenderTargetAvailable = false;
	FWebToUECompositingBudget Budget;
	Budget.MaxActiveLayers = Requests.Num();
	Budget.MaxActiveSurfaces = Requests.Num();
	Budget.MaxAllocatedPixels = MAX_uint64;
	Budget.MaxAllocatedBytes = MAX_uint64;
	const bool bPlanAccepted = CompositingPlan.Build(Requests, Backend, Budget);
	for (const FWebToUECompositingPlanEntry& Entry : CompositingPlan.GetEntries())
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::CompositingTierDecisions);
		const EWebToUEPerformanceCounter TierCounter =
			Entry.Decision.Tier == EWebToUECompositingTier::DirectPaint
			? EWebToUEPerformanceCounter::CompositingTier0Decisions
			: Entry.Decision.Tier == EWebToUECompositingTier::MaterialBrush
			? EWebToUEPerformanceCounter::CompositingTier1Decisions
			: Entry.Decision.Tier == EWebToUECompositingTier::SubtreeLayer
			? EWebToUEPerformanceCounter::CompositingTier2Decisions
			: EWebToUEPerformanceCounter::CompositingTier3Decisions;
		FWebToUEPerformanceCapture::RecordCounter(TierCounter);
	}

	const auto RecordCacheDelta = [](const FWebToUECompositingCacheStats& Before,
		const FWebToUECompositingCacheStats& After)
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::CompositingCacheAllocated,
			After.Allocated - Before.Allocated);
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::CompositingCacheReused,
			After.Reused - Before.Reused);
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::CompositingCacheReleased,
			After.Released - Before.Released);
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::CompositingCacheEvicted,
			After.Evicted - Before.Evicted);
	};
	if (!bPlanAccepted || CompositingSurfaceId.IsNone() || Requests.IsEmpty())
	{
		LastCompositingDiagnostic = bPlanAccepted
			? TEXT("WTUE-COMP-005: cache is detached or has no plan owner")
			: CompositingPlan.GetDiagnostic();
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::CompositingPlanRejections);
		if (CompositingCache)
		{
			const FWebToUECompositingCacheStats Before = CompositingCache->GetStats();
			CompositingCache->Shutdown();
			RecordCacheDelta(Before, CompositingCache->GetStats());
			CompositingCache.Reset();
			CompositingCacheOwnerId = 0;
			CompositingCacheGeneration = 0;
		}
		for (FWebToUEPaintCommand& Command : DisplayCommands) Command.bDisplayed = false;
		return false;
	}

	const FWebToUEInstanceHandle PlanOwner = Requests[0].Owner;
	if (CompositingCache &&
		(PlanOwner.GetOwnerId() != CompositingCacheOwnerId ||
		 PlanOwner.GetGeneration() != CompositingCacheGeneration))
	{
		CompositingCache->Shutdown();
		CompositingCache.Reset();
		CompositingCacheOwnerId = 0;
		CompositingCacheGeneration = 0;
	}
	if (!CompositingCache)
	{
		CompositingCache = MakeUnique<FWebToUECompositingCache>(
			PlanOwner.GetOwnerId(), PlanOwner.GetGeneration(), CompositingSurfaceId);
		CompositingCacheOwnerId = PlanOwner.GetOwnerId();
		CompositingCacheGeneration = PlanOwner.GetGeneration();
	}
	const FWebToUECompositingCacheStats Before = CompositingCache->GetStats();
	if (!CompositingCache->ApplyPlan(
		CompositingPlan, CompositingProjectionRevision, LastCompositingDiagnostic))
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::CompositingPlanRejections);
		for (FWebToUEPaintCommand& Command : DisplayCommands) Command.bDisplayed = false;
		return false;
	}
	const FWebToUECompositingCacheStats& After = CompositingCache->GetStats();
	RecordCacheDelta(Before, After);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::CompositingActiveLayers,
		After.Usage.ActiveLayers);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::CompositingActiveSurfaces,
		After.Usage.ActiveSurfaces);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::CompositingAllocatedPixels,
		After.Usage.AllocatedPixels);
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::CompositingAllocatedBytes,
		After.Usage.AllocatedBytes);
	return true;
}

void FWebToUERuntimePresentation::UpdateDisplayCommand(
	const FWebToUEDocument& RuntimeDocument, const FWebToUENode& Node,
	FWebToUEPaintCommand& Command, float ParentOpacity, bool bParentDisplayed,
	bool bParentEnabled, int32 Depth, const FVector2f& InheritedScrollOffset,
	const FTransform2D& InheritedVisualTransform,
	TConstArrayView<FWebToUEClipZone> InheritedClipChain) const
{
	using namespace WebToUE::Runtime::Presentation::Private;
	const FWebToUEComputedStyle& Style = GetStyle(Node);
	const FWebToUERuntimeLayoutResult& LayoutResult = GetLayout(Node);
	const FWebToUEInstanceHandle NodeHandle = RuntimeInstance.GetHandle(&Node);
	const auto FindOwnOverlay = [this, NodeHandle](
		const FWebToUEPropertyAddress& Address)
	{
		return OwnerWidget.FindAnimationOverlay(NodeHandle, Address);
	};
	const FWebToUEAnimationValue* TransformOverlay = FindOwnOverlay(
		FWebToUEPropertyAddress::VisualTransform());
	const FWebToUEVisualTransformValue& EffectiveTransform = TransformOverlay &&
		TransformOverlay->Type == EWebToUEAnimationValueType::Transform
		? TransformOverlay->Transform : Style.Transform;
	const FVector2f Position = LayoutResult.Position - InheritedScrollOffset;
	const FVector2f Size = LayoutResult.Size;
	const FTransform2D LocalTransform = ResolveLocalTransform(
		EffectiveTransform, Style.TransformOrigin, Size);
	const FTransform2D NodeSpaceTransform = FTransform2D(-Position)
		.Concatenate(LocalTransform)
		.Concatenate(FTransform2D(Position))
		.Concatenate(InheritedVisualTransform);
	Command.Owner = NodeHandle;
	Command.Type = Node.Type == EWebToUENodeType::Text
		? EWebToUEPaintCommandType::Text : EWebToUEPaintCommandType::Box;
	Command.LocalSize = Size;
	Command.InheritedScrollOffset = InheritedScrollOffset;
	Command.InheritedVisualTransform = InheritedVisualTransform;
	Command.DescendantVisualTransform = NodeSpaceTransform;
	Command.LocalToView = FTransform2D(Position).Concatenate(NodeSpaceTransform);
	Command.bInvertibleTransform = TryInvertTransform(
		Command.LocalToView, Command.ViewToLocal);
	Command.Bounds = TransformRectBounds(Command.LocalToView, Size);
	Command.Depth = Depth;
	const FWebToUEAnimationValue* OpacityOverlay = OwnerWidget.FindAnimationOverlay(
		NodeHandle,
		FWebToUEPropertyAddress::Css(EWebToUECssProperty::Opacity));
	const float LocalOpacity = OpacityOverlay &&
		OpacityOverlay->Type == EWebToUEAnimationValueType::Scalar
		? FMath::Clamp(OpacityOverlay->Scalar, 0.0f, 1.0f) : Style.Opacity;
	Command.Opacity = ParentOpacity * LocalOpacity;
	Command.Color = Style.Color;
	for (const FWebToUENode* ColorNode = &Node; ColorNode;
		ColorNode = ColorNode->Parent)
	{
		const FWebToUEAnimationValue* ColorOverlay =
			OwnerWidget.FindAnimationOverlay(
				RuntimeInstance.GetHandle(ColorNode),
				FWebToUEPropertyAddress::Css(EWebToUECssProperty::Color));
		if (ColorOverlay && ColorOverlay->Type == EWebToUEAnimationValueType::Color)
		{
			Command.Color = ColorOverlay->Color;
			break;
		}
	}
	const FWebToUEAnimationValue* BackgroundOverlay = FindOwnOverlay(
		FWebToUEPropertyAddress::Css(EWebToUECssProperty::BackgroundColor));
	Command.BackgroundColor = BackgroundOverlay &&
		BackgroundOverlay->Type == EWebToUEAnimationValueType::Color
		? BackgroundOverlay->Color : Style.BackgroundColor;
	const FWebToUEAnimationValue* BorderOverlay = FindOwnOverlay(
		FWebToUEPropertyAddress::Css(EWebToUECssProperty::BorderColor));
	Command.BorderColor = BorderOverlay &&
		BorderOverlay->Type == EWebToUEAnimationValueType::Color
		? BorderOverlay->Color : Style.BorderColor;
	Command.bDisplayed = bParentDisplayed && RuntimeDocument.IsDisplayed(Node);
	Command.ClipChain.Reset(InheritedClipChain.Num());
	Command.ClipChain.Append(InheritedClipChain);
	if (!EffectiveTransform.IsIdentity())
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::VisualTransformCommandsResolved);
	}
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::ClipChainZonesResolved,
		Command.ClipChain.Num());
	Command.bEnabled = bParentEnabled && Style.bEnabled;
	Command.bHasClip = !Command.ClipChain.IsEmpty();
	Command.ClipBounds = FSlateRect();
	Command.VisibleBounds = Command.bDisplayed ? Command.Bounds : FSlateRect();
	bool bHasCombinedClipBounds = false;
	for (const FWebToUEClipZone& Clip : Command.ClipChain)
	{
		Command.ClipBounds = bHasCombinedClipBounds
			? Command.ClipBounds.IntersectionWith(Clip.Bounds) : Clip.Bounds;
		bHasCombinedClipBounds = true;
		if (Command.bDisplayed)
		{
			Command.VisibleBounds = Command.VisibleBounds.IntersectionWith(Clip.Bounds);
			if (!IsUsableRect(Command.VisibleBounds)) Command.bDisplayed = false;
		}
	}
	Command.SubtreeBounds = Command.VisibleBounds;
	Command.bDrawable = Node.Type == EWebToUENodeType::Text;
	Command.bInteractive = Node.IsInteractive() && AreCriticalResourcesReady();
	Command.bScrollable = RuntimeDocument.IsScrollable(Node) &&
		GetState(Node).MaxScrollOffset.Y > 0.0f;
	Command.bSpatiallyIndexed = false;
	Command.bLargeSpatialEntry = false;
	Command.SpatialCells = FWebToUESpatialCellRange();
	if (Node.Type == EWebToUENodeType::Element)
	{
		const TSharedPtr<FSlateBrush>* Brush = Brushes.Find(Command.Owner);
		if (Brush && Brush->IsValid() && Node.ResourceId.IsEmpty())
		{
			(*Brush)->TintColor = Command.BackgroundColor;
			(*Brush)->OutlineSettings.Color = Command.BorderColor;
		}
		Command.bDrawable = Brush && Brush->IsValid() &&
			(!Node.ResourceId.IsEmpty() || Command.BackgroundColor.A > 0.0f ||
				Style.BorderWidth > 0.0f);
	}
	Command.BatchKey.Type = Command.Type;
	Command.BatchKey.ResourceKey =
		WebToUE::Runtime::Presentation::Private::MakeBatchResourceKey(Node, Style);
	Command.BatchKey.ShaderKey =
		WebToUE::Runtime::Presentation::Private::MakeBatchShaderKey(Node, Style, Size);
	Command.BatchKey.ClipKey = Command.bHasClip ? HashClipChain(Command.ClipChain) : 0;
	Command.BatchKey.bClipped = Command.bHasClip;
	Command.BatchKey.DrawEffects = static_cast<uint8>(Command.bEnabled
		? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect);
}

void FWebToUERuntimePresentation::BuildDisplaySubtree(
	const FWebToUEDocument& RuntimeDocument, const FWebToUENode& Node,
	float ParentOpacity, bool bParentDisplayed, bool bParentEnabled, int32 Depth,
	const FVector2f& InheritedScrollOffset,
	const FTransform2D& InheritedVisualTransform,
	TConstArrayView<FWebToUEClipZone> InheritedClipChain) const
{
	const int32 RangeStart = DisplayCommands.Num();
	const int32 CommandIndex = DisplayCommands.AddDefaulted();
	FWebToUEPaintCommand& Command = DisplayCommands[CommandIndex];
	UpdateDisplayCommand(RuntimeDocument, Node, Command, ParentOpacity, bParentDisplayed,
		bParentEnabled, Depth, InheritedScrollOffset, InheritedVisualTransform,
		InheritedClipChain);
	const FWebToUEInstanceHandle OwnerHandle = Command.Owner;
	const float ChildOpacity = Command.Opacity;
	const bool bChildDisplayed = Command.bDisplayed;
	const bool bChildEnabled = Command.bEnabled;
	DisplayCommandIndices.Add(OwnerHandle, CommandIndex);

	TArray<FWebToUEClipZone, TInlineAllocator<4>> ChildClipChain = Command.ClipChain;
	if (RuntimeDocument.ClipsOverflow(Node))
	{
		ChildClipChain.Add(WebToUE::Runtime::Presentation::Private::MakeClipZone(
			Command.LocalToView, Command.LocalSize));
	}
	const FVector2f ChildScrollOffset = InheritedScrollOffset +
		(RuntimeDocument.IsScrollable(Node)
			? GetState(Node).ScrollOffset : FVector2f::ZeroVector);
	for (const FWebToUEInstanceHandle ChildHandle : GetPaintOrder(Node))
	{
		const FWebToUENode* Child = RuntimeInstance.ResolveNode(ChildHandle);
		if (!Child) continue;
		BuildDisplaySubtree(RuntimeDocument, *Child, ChildOpacity, bChildDisplayed,
			bChildEnabled, Depth + 1, ChildScrollOffset,
			Command.DescendantVisualTransform, ChildClipChain);
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
	if (!Command.bDisplayed)
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::DisplayCommandsRejectedHidden);
		return;
	}
	if (!IsUsableRect(Command.VisibleBounds))
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::DisplayCommandsRejectedUnusableBounds);
		return;
	}
	if (!Command.bDrawable && !Command.bInteractive && !Command.bScrollable)
	{
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::DisplayCommandsRejectedInert);
		return;
	}
	Command.SpatialCells.MinX = FMath::FloorToInt(Command.VisibleBounds.Left / SpatialCellSize);
	Command.SpatialCells.MinY = FMath::FloorToInt(Command.VisibleBounds.Top / SpatialCellSize);
	Command.SpatialCells.MaxX = FMath::FloorToInt(Command.VisibleBounds.Right / SpatialCellSize);
	Command.SpatialCells.MaxY = FMath::FloorToInt(Command.VisibleBounds.Bottom / SpatialCellSize);
	Command.bSpatiallyIndexed = true;
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::DisplayCommandsSpatiallyIndexed);
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
	const FVector2f InheritedScrollOffset = RootCommand.InheritedScrollOffset;
	const FTransform2D InheritedVisualTransform =
		RootCommand.InheritedVisualTransform;
	TArray<FWebToUEClipZone, TInlineAllocator<4>> InheritedClipChain =
		RootCommand.ClipChain;
	const int32 RootDepth = RootCommand.Depth;

	int32 PatchedCount = 0;
	const auto Patch = [&](const auto& Self, const FWebToUENode& Current,
		float InParentOpacity,
		bool bInParentDisplayed, bool bInParentEnabled, int32 Depth,
		const FVector2f& InScrollOffset,
		const FTransform2D& InVisualTransform,
		TConstArrayView<FWebToUEClipZone> InClipChain)
	{
		const int32* CommandIndex =
			DisplayCommandIndices.Find(RuntimeInstance.GetHandle(&Current));
		if (!CommandIndex || !DisplayCommands.IsValidIndex(*CommandIndex)) return;
		FWebToUEPaintCommand& Command = DisplayCommands[*CommandIndex];
		const FSlateRect PreviousBounds = Command.VisibleBounds;
		RemoveDisplayCommandFromSpatialIndex(*CommandIndex);
		UpdateDisplayCommand(*RuntimeDocument, Current, Command, InParentOpacity,
			bInParentDisplayed, bInParentEnabled, Depth, InScrollOffset,
			InVisualTransform, InClipChain);
		AddDisplayCommandToSpatialIndex(*CommandIndex);
		AddDirtyRegion(PreviousBounds, Command.VisibleBounds, *CommandIndex);
		++PatchedCount;
		TArray<FWebToUEClipZone, TInlineAllocator<4>> ChildClipChain = Command.ClipChain;
		if (RuntimeDocument->ClipsOverflow(Current))
		{
			ChildClipChain.Add(WebToUE::Runtime::Presentation::Private::MakeClipZone(
				Command.LocalToView, Command.LocalSize));
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
						ChildScrollOffset, Command.DescendantVisualTransform,
						ChildClipChain);
				}
			}
		}
		UpdateDisplaySubtreeBounds(Current);
	};
	Patch(Patch, Node, ParentOpacity, bParentDisplayed, bParentEnabled,
		RootDepth, InheritedScrollOffset, InheritedVisualTransform,
		InheritedClipChain);
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
	const FVector2f Size = Command.LocalSize;
	int32 PushedClipCount = 0;
	for (const FWebToUEClipZone& Clip : Command.ClipChain)
	{
		Out.PushClip(FSlateClippingZone(Geometry.MakeChild(
			Clip.LocalSize, FSlateLayoutTransform(), Clip.LocalToView,
			FVector2f::ZeroVector)));
		++PushedClipCount;
	}

	const float DrawOpacity =
		Command.Opacity * WidgetStyle.GetColorAndOpacityTint().A;
	if (Command.Type == EWebToUEPaintCommandType::Box)
	{
		if (const TSharedPtr<FSlateBrush>* Brush = Brushes.Find(Command.Owner))
		{
			FVector2f DrawPosition = FVector2f::ZeroVector;
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
				Out.PushClip(FSlateClippingZone(Geometry.MakeChild(
					Size, FSlateLayoutTransform(), Command.LocalToView,
					FVector2f::ZeroVector)));
			}
			FLinearColor DrawTint = (*Brush)->GetTint(WidgetStyle);
			DrawTint.A *= DrawOpacity;
			const FTransform2D DrawTransform = FTransform2D(DrawPosition)
				.Concatenate(Command.LocalToView);
			FSlateDrawElement::MakeBox(Out, LayerId++,
				Geometry.ToPaintGeometry(DrawSize, FSlateLayoutTransform(),
					DrawTransform, FVector2f::ZeroVector),
				Brush->Get(), bParentEnabled && Command.bEnabled
					? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
				DrawTint);
			if (bClipImage) Out.PopClip();
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::PaintDrawElements);
		}
	}
	else
	{
		FSlateTextBlockLayout& TextLayout = PrepareTextLayout(*Node, Style, Size.X);
		const FGeometry TextGeometry = Geometry.MakeChild(
			Size, FSlateLayoutTransform(), Command.LocalToView,
			FVector2f::ZeroVector);
		FWidgetStyle TextWidgetStyle = WidgetStyle;
		FLinearColor TextTint = Command.Color;
		TextTint.A *= Command.Opacity;
		TextWidgetStyle.BlendColorAndOpacityTint(
			TextTint);
		LayerId = TextLayout.OnPaint(Args, TextGeometry, CullingRect, Out, LayerId,
			TextWidgetStyle, bParentEnabled && Command.bEnabled) + 1;
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::PaintDrawElements);
	}
	while (PushedClipCount-- > 0) Out.PopClip();
	return LayerId;
}

void FWebToUERuntimePresentation::RebuildBrushes(bool bReloadResources) const
{
	if (bReloadResources)
	{
		Brushes.Reset();
		InitializeResourceResidency();
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
	if (!Node.ResourceId.IsEmpty())
	{
		const FWebToUEInstanceHandle NodeHandle = RuntimeInstance.GetHandle(&Node);
		Brushes.Remove(NodeHandle);
		const int32 Handle = FindResourceHandleById(Node.ResourceId);
		const TConstArrayView<FWebToUECompiledResource> Manifest =
			RuntimeInstance.GetResourceManifest();
		if (!Manifest.IsValidIndex(Handle))
		{
			return;
		}
		const FWebToUECompiledResource& Resource = Manifest[Handle];
		if (Resource.Kind == EWebToUEResourceKind::Texture)
		{
			UTexture2D* Texture = Cast<UTexture2D>(
				GetResolvedResourceById(Node.ResourceId));
			if (!Texture)
			{
				return;
			}
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::BrushBuilds);
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::TrackedAllocations);
			TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
			Brush->DrawAs = ESlateBrushDrawType::Image;
			Brush->SetResourceObject(Texture);
			Brush->ImageSize = Resource.IntrinsicSize.X > 0.0f &&
				Resource.IntrinsicSize.Y > 0.0f
				? Resource.IntrinsicSize
				: FVector2f(Texture->GetImportedSize().X, Texture->GetImportedSize().Y);
			Brushes.Add(NodeHandle, MoveTemp(Brush));
			return;
		}
		if (Resource.Kind == EWebToUEResourceKind::Material)
		{
			UMaterialInterface* Material = nullptr;
			if (const TStrongObjectPtr<UMaterialInstanceDynamic>* Dynamic =
				DynamicMaterials.Find(NodeHandle))
			{
				Material = Dynamic->Get();
			}
			if (!Material)
			{
				Material = Cast<UMaterialInterface>(
					GetResolvedResourceById(Node.ResourceId));
			}
			if (!Material || Resource.BrushImageSize.X <= 0.0f ||
				Resource.BrushImageSize.Y <= 0.0f)
			{
				return;
			}
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::BrushBuilds);
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::TrackedAllocations);
			Brushes.Add(NodeHandle,
				MakeShared<FSlateMaterialBrush>(*Material, Resource.BrushImageSize));
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

UMaterialInstanceDynamic*
FWebToUERuntimePresentation::GetDynamicMaterialForTesting(
	FWebToUEInstanceHandle Target) const
{
	const TStrongObjectPtr<UMaterialInstanceDynamic>* Dynamic =
		DynamicMaterials.Find(Target);
	return Dynamic ? Dynamic->Get() : nullptr;
}

int32 FWebToUERuntimePresentation::FindResourceHandleForTesting(
	EWebToUEResourceKind Kind, const FSoftObjectPath& Path) const
{
	return FindResourceHandle(Kind, Path);
}

int32 FWebToUERuntimePresentation::FindResourceHandleByIdForTesting(
	const FString& ResourceId) const
{
	return FindResourceHandleById(ResourceId);
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
	if (bDisplayListDirty && !bLayoutDirty) RebuildDisplayList();
	const int32* CommandIndex = DisplayCommandIndices.Find(RuntimeInstance.GetHandle(&Node));
	if (CommandIndex && DisplayCommands.IsValidIndex(*CommandIndex) &&
		WebToUE::Runtime::Presentation::Private::IsUsableRect(
			DisplayCommands[*CommandIndex].Bounds))
	{
		return DisplayCommands[*CommandIndex].Bounds.GetTopLeft2f();
	}
	FVector2f ScrollOffset = FVector2f::ZeroVector;
	for (const FWebToUENode* Parent = Node.Parent; Parent; Parent = Parent->Parent)
	{
		ScrollOffset += GetState(*Parent).ScrollOffset;
	}
	return GetLayout(Node).Position - ScrollOffset;
}

FSlateRect FWebToUERuntimePresentation::GetVisualBounds(const FWebToUENode& Node) const
{
	if (bDisplayListDirty && !bLayoutDirty) RebuildDisplayList();
	const int32* CommandIndex = DisplayCommandIndices.Find(RuntimeInstance.GetHandle(&Node));
	return CommandIndex && DisplayCommands.IsValidIndex(*CommandIndex)
		? DisplayCommands[*CommandIndex].Bounds : FSlateRect();
}

FWebToUENode* FWebToUERuntimePresentation::HitTest(const FVector2f& LocalPosition) const
{
	if (!AreCriticalResourcesReady()) return nullptr;
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
			!Command.bInvertibleTransform ||
			!Command.VisibleBounds.ContainsPoint(FVector2D(LocalPosition)))
		{
			continue;
		}
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::InverseHitTests);
		bool bInsideClipChain = true;
		for (const FWebToUEClipZone& Clip : Command.ClipChain)
		{
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::ExactClipTests);
			if (!WebToUE::Runtime::Presentation::Private::IsPointInsideClipZone(
				Clip, LocalPosition))
			{
				bInsideClipChain = false;
				break;
			}
		}
		if (!bInsideClipChain) continue;
		const FVector2f NodeLocal = Command.ViewToLocal.TransformPoint(LocalPosition);
		if (NodeLocal.X < 0.0f || NodeLocal.Y < 0.0f ||
			NodeLocal.X > Command.LocalSize.X || NodeLocal.Y > Command.LocalSize.Y)
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
		bool bInsideClipChain = true;
		for (const FWebToUEClipZone& Clip : Command.ClipChain)
		{
			bInsideClipChain &=
				WebToUE::Runtime::Presentation::Private::IsPointInsideClipZone(
					Clip, LocalPosition);
		}
		if (!bInsideClipChain || !Command.bInvertibleTransform) continue;
		const FVector2f NodeLocal = Command.ViewToLocal.TransformPoint(LocalPosition);
		if (NodeLocal.X < 0.0f || NodeLocal.Y < 0.0f ||
			NodeLocal.X > Command.LocalSize.X || NodeLocal.Y > Command.LocalSize.Y)
		{
			continue;
		}
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

bool FWebToUERuntimePresentation::ScrollIntoView(const FWebToUENode& Node)
{
	const FWebToUEDocument* RuntimeDocument = GetDocument();
	if (!RuntimeDocument) return false;
	bool bChanged = false;
	constexpr float NavigationPadding = 4.0f;
	for (FWebToUENode* Ancestor = Node.Parent; Ancestor; Ancestor = Ancestor->Parent)
	{
		if (!RuntimeDocument->IsScrollable(*Ancestor)) continue;
		FWebToUERuntimeNodeState& State = GetState(*Ancestor);
		const FVector2f TargetPosition = GetVisualPosition(Node);
		const FVector2f TargetSize = GetLayout(Node).Size;
		const FVector2f ViewPosition = GetVisualPosition(*Ancestor);
		const FVector2f ViewSize = GetLayout(*Ancestor).Size;
		FVector2f Delta = FVector2f::ZeroVector;
		if (TargetPosition.X < ViewPosition.X + NavigationPadding)
		{
			Delta.X = TargetPosition.X - (ViewPosition.X + NavigationPadding);
		}
		else if (TargetPosition.X + TargetSize.X >
			ViewPosition.X + ViewSize.X - NavigationPadding)
		{
			Delta.X = TargetPosition.X + TargetSize.X -
				(ViewPosition.X + ViewSize.X - NavigationPadding);
		}
		if (TargetPosition.Y < ViewPosition.Y + NavigationPadding)
		{
			Delta.Y = TargetPosition.Y - (ViewPosition.Y + NavigationPadding);
		}
		else if (TargetPosition.Y + TargetSize.Y >
			ViewPosition.Y + ViewSize.Y - NavigationPadding)
		{
			Delta.Y = TargetPosition.Y + TargetSize.Y -
				(ViewPosition.Y + ViewSize.Y - NavigationPadding);
		}
		const FVector2f PreviousOffset = State.ScrollOffset;
		State.ScrollOffset.X = FMath::Clamp(
			State.ScrollOffset.X + Delta.X, 0.0f, State.MaxScrollOffset.X);
		State.ScrollOffset.Y = FMath::Clamp(
			State.ScrollOffset.Y + Delta.Y, 0.0f, State.MaxScrollOffset.Y);
		if (!PreviousOffset.Equals(State.ScrollOffset, 0.1f))
		{
			ApplyScrollOffsetChange(*Ancestor);
			bChanged = true;
		}
	}
	return bChanged;
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
