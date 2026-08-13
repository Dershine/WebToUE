#include "WebToUERuntimePresentation.h"

#include "SWebToUEView.h"
#include "WebToUEPerformance.h"
#include "WebToUERuntimeInstance.h"
#include "WebToUESettings.h"

#include "Algo/StableSort.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Layout/Clipping.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Text/SlateTextBlockLayout.h"

namespace WebToUE::Runtime::Presentation::Private
{
	static ETextJustify::Type ToTextJustification(const FString& TextAlign)
	{
		if (TextAlign == TEXT("center")) return ETextJustify::Center;
		if (TextAlign == TEXT("right")) return ETextJustify::Right;
		return ETextJustify::Left;
	}

	static FTextBlockStyle MakeTextBlockStyle(const FWebToUEComputedStyle& ComputedStyle)
	{
		const UWebToUESettings* Settings = GetDefault<UWebToUESettings>();
		FTextBlockStyle Style = FTextBlockStyle::GetDefault();
		Style.SetFont(Settings->ResolveFont(
			ComputedStyle.FontFamily, ComputedStyle.FontSize, ComputedStyle.FontWeight));
		Style.SetColorAndOpacity(ComputedStyle.Color);
		return Style;
	}

	static TSharedRef<FSlateStyleSet> MakeRichTextStyleSet(
		const FWebToUEComputedStyle& ComputedStyle)
	{
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		TSharedRef<FSlateStyleSet> StyleSet = MakeShared<FSlateStyleSet>(TEXT("WebToUERichText"));
		const FTextBlockStyle BaseStyle = MakeTextBlockStyle(ComputedStyle);
		const UWebToUESettings* Settings = GetDefault<UWebToUESettings>();
		const auto AddStyle = [&](const FName Name, bool bBold, bool bItalic, bool bUnderline)
		{
			FTextBlockStyle Style = BaseStyle;
			if (bBold || bItalic)
			{
				Style.SetFont(Settings->ResolveFont(ComputedStyle.FontFamily, ComputedStyle.FontSize,
					bBold ? TEXT("bold") : ComputedStyle.FontWeight));
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
}

FWebToUERuntimePresentation::FWebToUERuntimePresentation(SWebToUEView& InOwnerWidget,
	FWebToUERuntimeInstance& InRuntimeInstance)
	: OwnerWidget(InOwnerWidget)
	, RuntimeInstance(InRuntimeInstance)
{
}

FWebToUERuntimePresentation::~FWebToUERuntimePresentation() = default;

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
	LoadedResources.Reset();
	PaintOrderNodes.Reset();
	PaintOrderRanges.Reset();
#if WITH_DEV_AUTOMATION_TESTS
	ResourceLoadAttemptsForTesting = 0;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
uint64 FWebToUERuntimePresentation::GetKnownOwnedBytesForTesting() const
{
	uint64 Bytes = sizeof(*this) + Brushes.GetAllocatedSize() + TextLayouts.GetAllocatedSize() +
		MeasureDirtyNodes.GetAllocatedSize() + LayoutDirtyNodes.GetAllocatedSize() +
		LoadedResources.GetAllocatedSize() + PaintOrderNodes.GetAllocatedSize() +
		PaintOrderRanges.GetAllocatedSize();
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
	}
	else
	{
		Brushes.Reset();
		LoadedResources.Reset();
		PaintOrderNodes.Reset();
		PaintOrderRanges.Reset();
	}
	bLayoutDirty = true;
}

void FWebToUERuntimePresentation::MarkTextLayoutDependencyPath(const FWebToUENode& Node)
{
	MeasureDirtyNodes.Add(RuntimeInstance.GetHandle(&Node));
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
	bool bRebuildPaintOrder = false;
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
			RebuildBrush(*Node, false);
		}
	}
	if (bRebuildPaintOrder)
	{
		RebuildPaintOrderCache();
	}
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
			Cache->RichTextStyleSet = MakeRichTextStyleSet(Style);
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
	Cache->Layout->ComputeDesiredSize(DesiredSizeArgs, 1.0f, MakeTextBlockStyle(Style));
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
	FWebToUELayoutEngine::Layout(*RuntimeDocument, ViewportSize,
		[this](const FWebToUENode& Node,
			const FWebToUELayoutEngine::FMeasureConstraints& Constraints)
		{
			return MeasureNode(Node, Constraints);
		});
	LastViewportSize = ViewportSize;
	bLayoutDirty = false;
	MeasureDirtyNodes.Reset();
	LayoutDirtyNodes.Reset();
}

int32 FWebToUERuntimePresentation::Paint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& WidgetStyle, bool bParentEnabled) const
{
	FWebToUEDocument* RuntimeDocument = const_cast<FWebToUEDocument*>(GetDocument());
	if (!RuntimeDocument || !RuntimeDocument->Root) return LayerId;
	const FVector2f ViewportSize = FVector2f(Geometry.GetLocalSize());
	if (bLayoutDirty || !ViewportSize.Equals(LastViewportSize, 0.1f))
	{
		Layout(ViewportSize);
	}
	return PaintNode(*RuntimeDocument, *RuntimeDocument->Root, Args, Geometry, CullingRect,
		Out, LayerId, WidgetStyle, 1.0f, bParentEnabled, FVector2f::ZeroVector);
}

int32 FWebToUERuntimePresentation::PaintNode(const FWebToUEDocument& RuntimeDocument,
	const FWebToUENode& Node, const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& WidgetStyle, float ParentOpacity, bool bParentEnabled,
	const FVector2f& InheritedScrollOffset) const
{
	if (!RuntimeDocument.IsDisplayed(Node)) return LayerId;
	const FWebToUEComputedStyle& Style = RuntimeDocument.GetComputedStyle(Node);
	const FWebToUERuntimeLayoutResult& LayoutResult = RuntimeDocument.GetLayoutResult(Node);
	const float Opacity = ParentOpacity * Style.Opacity;
	const float DrawOpacity = Opacity * WidgetStyle.GetColorAndOpacityTint().A;
	const FVector2f Position = LayoutResult.Position - InheritedScrollOffset;
	const FVector2f Size = LayoutResult.Size;
	const FPaintGeometry PaintGeometry =
		Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position));

	if (Node.Type == EWebToUENodeType::Element)
	{
		if (const TSharedPtr<FSlateBrush>* Brush = Brushes.Find(RuntimeInstance.GetHandle(&Node)))
		{
			if (Node.Tag == TEXT("img"))
			{
				FVector2f ImagePosition = Position;
				FVector2f ImageSize = Size;
				bool bClipImage = false;
				const FVector2f IntrinsicSize = (*Brush)->ImageSize;
				if (IntrinsicSize.X > 0.0f && IntrinsicSize.Y > 0.0f &&
					(Style.ObjectFit == TEXT("contain") || Style.ObjectFit == TEXT("cover")))
				{
					const float ScaleX = Size.X / IntrinsicSize.X;
					const float ScaleY = Size.Y / IntrinsicSize.Y;
					const float Scale = Style.ObjectFit == TEXT("cover")
						? FMath::Max(ScaleX, ScaleY) : FMath::Min(ScaleX, ScaleY);
					ImageSize = IntrinsicSize * Scale;
					ImagePosition += (Size - ImageSize) * 0.5f;
					bClipImage = Style.ObjectFit == TEXT("cover");
				}
				if (bClipImage)
				{
					Out.PushClip(FSlateClippingZone(
						Geometry.MakeChild(Size, FSlateLayoutTransform(Position))));
				}
				FSlateDrawElement::MakeBox(Out, LayerId++,
					Geometry.ToPaintGeometry(ImageSize, FSlateLayoutTransform(ImagePosition)),
					Brush->Get(), bParentEnabled && Style.bEnabled
						? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
					FLinearColor(1.0f, 1.0f, 1.0f, DrawOpacity));
				if (bClipImage) Out.PopClip();
			}
			else if (Style.BackgroundColor.A > 0.0f || Style.BorderWidth > 0.0f)
			{
				FSlateDrawElement::MakeBox(Out, LayerId++, PaintGeometry, Brush->Get(),
					bParentEnabled && Style.bEnabled
						? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
					FLinearColor(1.0f, 1.0f, 1.0f, DrawOpacity));
			}
		}
	}
	else
	{
		FSlateTextBlockLayout& TextLayout = PrepareTextLayout(Node, Style, Size.X);
		const FGeometry TextGeometry =
			Geometry.MakeChild(Size, FSlateLayoutTransform(Position));
		FWidgetStyle TextWidgetStyle = WidgetStyle;
		TextWidgetStyle.BlendColorAndOpacityTint(
			FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
		LayerId = TextLayout.OnPaint(Args, TextGeometry, CullingRect, Out, LayerId,
			TextWidgetStyle, bParentEnabled && Style.bEnabled) + 1;
	}

	bool bPushedClip = false;
	if (RuntimeDocument.ClipsOverflow(Node))
	{
		const FGeometry ClipGeometry =
			Geometry.MakeChild(Size, FSlateLayoutTransform(Position));
		Out.PushClip(FSlateClippingZone(ClipGeometry));
		bPushedClip = true;
	}
	for (const FWebToUEInstanceHandle ChildHandle : GetPaintOrder(Node))
	{
		const FWebToUENode* Child = RuntimeInstance.ResolveNode(ChildHandle);
		if (!Child) continue;
		const FVector2f ChildScrollOffset = InheritedScrollOffset +
			(RuntimeDocument.IsScrollable(Node)
				? RuntimeDocument.GetRuntimeNodeState(Node).ScrollOffset
				: FVector2f::ZeroVector);
		LayerId = PaintNode(RuntimeDocument, *Child, Args, Geometry, CullingRect, Out,
			LayerId, WidgetStyle, Opacity, bParentEnabled && Style.bEnabled, ChildScrollOffset);
	}
	if (bPushedClip) Out.PopClip();
	return LayerId;
}

void FWebToUERuntimePresentation::RebuildBrushes(bool bReloadResources) const
{
	if (bReloadResources)
	{
		Brushes.Reset();
		LoadedResources.Reset();
	}
	const FWebToUEDocument* RuntimeDocument = GetDocument();
	if (!RuntimeDocument) return;
	RuntimeDocument->ForEachNode([this, bReloadResources](FWebToUENode& Node)
	{
		RebuildBrush(Node, bReloadResources);
	});
}

void FWebToUERuntimePresentation::RebuildBrush(FWebToUENode& Node,
	bool bReloadResource) const
{
	const FWebToUEComputedStyle& Style = GetStyle(Node);
	if (Node.Tag == TEXT("img"))
	{
		if (!bReloadResource) return;
		const FString Source = Node.GetAttribute(TEXT("src"));
		FWebToUEPerformanceCapture::RecordCounter(
			EWebToUEPerformanceCounter::ResourceLoadAttempts);
#if WITH_DEV_AUTOMATION_TESTS
		++ResourceLoadAttemptsForTesting;
#endif
		if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Source))
		{
			LoadedResources.Emplace(Texture);
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::BrushBuilds);
			FWebToUEPerformanceCapture::RecordCounter(
				EWebToUEPerformanceCounter::TrackedAllocations);
			TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
			Brush->DrawAs = ESlateBrushDrawType::Image;
			Brush->SetResourceObject(Texture);
			Brush->ImageSize = FVector2f(Texture->GetSizeX(), Texture->GetSizeY());
			Brushes.Add(RuntimeInstance.GetHandle(&Node), MoveTemp(Brush));
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
#endif

void FWebToUERuntimePresentation::RebuildPaintOrderCache()
{
	FWebToUEPerformanceCapture::RecordCounter(
		EWebToUEPerformanceCounter::PaintOrderCacheBuilds);
	PaintOrderNodes.Reset();
	PaintOrderRanges.Reset();
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
	FWebToUENode* Best = nullptr;
	const FWebToUEDocument* RuntimeDocument = GetDocument();
	if (!RuntimeDocument || !RuntimeDocument->Root) return nullptr;

	TFunction<void(FWebToUENode&, const FVector2f&, const FVector2f&, const FVector2f&, bool)> Visit;
	Visit = [&](FWebToUENode& Node, const FVector2f& InheritedScrollOffset,
		const FVector2f& ClipMin, const FVector2f& ClipMax, bool bHasClip)
	{
		if (!RuntimeDocument->IsDisplayed(Node)) return;
		if (bHasClip && (LocalPosition.X < ClipMin.X || LocalPosition.Y < ClipMin.Y ||
			LocalPosition.X > ClipMax.X || LocalPosition.Y > ClipMax.Y)) return;

		const FWebToUEComputedStyle& Style = GetStyle(Node);
		const FWebToUERuntimeLayoutResult& LayoutResult = GetLayout(Node);
		const FVector2f Position = LayoutResult.Position - InheritedScrollOffset;
		const FVector2f NodeMax = Position + LayoutResult.Size;
		const bool bInsideNode = LocalPosition.X >= Position.X &&
			LocalPosition.Y >= Position.Y && LocalPosition.X <= NodeMax.X &&
			LocalPosition.Y <= NodeMax.Y;
		if (bInsideNode && Node.IsInteractive() && Style.bEnabled)
		{
			if (!Best)
			{
				Best = &Node;
			}
			else
			{
				const FWebToUEComputedStyle& BestStyle = GetStyle(*Best);
				const FWebToUERuntimeLayoutResult& BestLayout = GetLayout(*Best);
				if (Style.ZIndex > BestStyle.ZIndex ||
					(Style.ZIndex == BestStyle.ZIndex &&
						LayoutResult.PaintOrder > BestLayout.PaintOrder))
				{
					Best = &Node;
				}
			}
		}

		FVector2f ChildClipMin = ClipMin;
		FVector2f ChildClipMax = ClipMax;
		bool bChildHasClip = bHasClip;
		if (RuntimeDocument->ClipsOverflow(Node))
		{
			ChildClipMin = bHasClip
				? FVector2f(FMath::Max(ClipMin.X, Position.X), FMath::Max(ClipMin.Y, Position.Y))
				: Position;
			ChildClipMax = bHasClip
				? FVector2f(FMath::Min(ClipMax.X, NodeMax.X), FMath::Min(ClipMax.Y, NodeMax.Y))
				: NodeMax;
			bChildHasClip = true;
			if (ChildClipMin.X > ChildClipMax.X || ChildClipMin.Y > ChildClipMax.Y) return;
		}

		const FVector2f ChildScrollOffset = InheritedScrollOffset +
			(RuntimeDocument->IsScrollable(Node) ? GetState(Node).ScrollOffset : FVector2f::ZeroVector);
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			Visit(*Child, ChildScrollOffset, ChildClipMin, ChildClipMax, bChildHasClip);
		}
	};
	Visit(*RuntimeDocument->Root, FVector2f::ZeroVector, FVector2f::ZeroVector,
		FVector2f::ZeroVector, false);
	return Best;
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
#endif
