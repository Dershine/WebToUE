#include "SWebToUEView.h"

#include "Algo/StableSort.h"
#include "WebToUECompiler.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUESettings.h"
#include "WebToUEView.h"
#include "WebToUERuntimeInstance.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Text/PlainTextLayoutMarshaller.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Clipping.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/SlateTextBlockLayout.h"

DEFINE_LOG_CATEGORY_STATIC(LogWebToUE, Log, All);

SWebToUEView::~SWebToUEView() = default;

void SWebToUEView::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	RuntimeInstance = MakeUnique<FWebToUERuntimeInstance>();
	SetCanTick(false);
}

FWebToUEDocument* SWebToUEView::GetRuntimeDocument()
{
	return RuntimeInstance ? RuntimeInstance->GetDocument() : nullptr;
}

const FWebToUEDocument* SWebToUEView::GetRuntimeDocument() const
{
	return RuntimeInstance ? RuntimeInstance->GetDocument() : nullptr;
}

FWebToUERuntimeNodeState& SWebToUEView::GetRuntimeState(FWebToUENode& Node)
{
	return RuntimeInstance->GetState(Node);
}

const FWebToUERuntimeNodeState& SWebToUEView::GetRuntimeState(const FWebToUENode& Node) const
{
	return RuntimeInstance->GetState(Node);
}

const FWebToUERuntimeNodeState* SWebToUEView::FindRuntimeState(const FWebToUENode& Node) const
{
	const FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	return RuntimeDocument && RuntimeDocument->RuntimeNodeStates.IsValidIndex(Node.RuntimeStateIndex)
		? &RuntimeDocument->RuntimeNodeStates[Node.RuntimeStateIndex]
		: nullptr;
}

bool SWebToUEView::IsRichText(const FWebToUENode& Node) const
{
	if (const FWebToUERuntimeNodeState* State = FindRuntimeState(Node))
	{
		return State->bHasRichTextOverride ? State->bRichTextOverride : Node.bRichText;
	}
	return Node.bRichText;
}

void SWebToUEView::SetDocument(UWebToUEDocument* InDocument)
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_Hydrate);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_Hydrate);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::Hydrate);
	DocumentAsset = InDocument;
	RuntimeInstance->Reset();
	TextLayouts.Reset();
	PaintOrderNodes.Reset();
	PaintOrderRanges.Reset();
	if (InDocument)
	{
		RuntimeInstance->Hydrate(*InDocument);
	}
	RebuildStylesAndBrushes();
}

FVector2D SWebToUEView::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(320.0, 180.0);
}

static ETextJustify::Type ToTextJustification(const FString& TextAlign)
{
	if (TextAlign == TEXT("center")) return ETextJustify::Center;
	if (TextAlign == TEXT("right")) return ETextJustify::Right;
	return ETextJustify::Left;
}

static FTextBlockStyle MakeTextBlockStyle(const FWebToUENode& Node)
{
	const UWebToUESettings* Settings = GetDefault<UWebToUESettings>();
	FTextBlockStyle Style = FTextBlockStyle::GetDefault();
	Style.SetFont(Settings->ResolveFont(Node.Style.FontFamily, Node.Style.FontSize, Node.Style.FontWeight));
	Style.SetColorAndOpacity(Node.Style.Color);
	return Style;
}

static TSharedRef<FSlateStyleSet> MakeRichTextStyleSet(const FWebToUENode& Node)
{
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
	TSharedRef<FSlateStyleSet> StyleSet = MakeShared<FSlateStyleSet>(TEXT("WebToUERichText"));
	const FTextBlockStyle BaseStyle = MakeTextBlockStyle(Node);
	const UWebToUESettings* Settings = GetDefault<UWebToUESettings>();
	const auto AddStyle = [&](const FName Name, bool bBold, bool bItalic, bool bUnderline)
	{
		FTextBlockStyle Style = BaseStyle;
		if (bBold || bItalic)
		{
			Style.SetFont(Settings->ResolveFont(Node.Style.FontFamily, Node.Style.FontSize,
				bBold ? TEXT("bold") : Node.Style.FontWeight));
			if (bItalic) Style.SetTypefaceFontName(bBold ? TEXT("BoldItalic") : TEXT("Italic"));
		}
		if (bUnderline) Style.SetUnderlineBrush(*FCoreStyle::Get().GetBrush(TEXT("DefaultTextUnderline")));
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

FText SWebToUEView::GetDisplayText(const FWebToUENode& Node) const
{
	if (const FWebToUERuntimeNodeState* State = FindRuntimeState(Node); State && State->bHasBoundText)
	{
		return State->BoundText;
	}
	return Node.bHasLocalizedText ? Node.LocalizedText : FText::FromString(Node.Text);
}

FSlateTextBlockLayout& SWebToUEView::PrepareTextLayout(const FWebToUENode& Node, float WrapWidth) const
{
	TUniquePtr<FWebToUETextLayoutCache>& Cache = TextLayouts.FindOrAdd(&Node);
	const bool bRichText = IsRichText(Node);
	if (!Cache || Cache->bRichText != bRichText)
	{
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TextLayoutBuilds);
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		Cache = MakeUnique<FWebToUETextLayoutCache>();
		Cache->bRichText = bRichText;
		TSharedRef<ITextLayoutMarshaller> Marshaller = FPlainTextLayoutMarshaller::Create();
		if (bRichText)
		{
			Cache->RichTextStyleSet = MakeRichTextStyleSet(Node);
			Marshaller = FRichTextLayoutMarshaller::Create(TArray<TSharedRef<ITextDecorator>>(), Cache->RichTextStyleSet.Get());
		}
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		Cache->Layout = MakeUnique<FSlateTextBlockLayout>(const_cast<SWebToUEView*>(this), FTextBlockStyle::GetDefault(),
			TOptional<ETextShapingMethod>(), TOptional<ETextFlowDirection>(), FCreateSlateTextLayout(),
			Marshaller, nullptr);
	}
	const float EffectiveWrapWidth = Node.Style.WhiteSpace == TEXT("normal") && FMath::IsFinite(WrapWidth) && WrapWidth > 0.0f
		? WrapWidth : 0.0f;
	const FSlateTextBlockLayout::FWidgetDesiredSizeArgs DesiredSizeArgs(
		GetDisplayText(Node), FText::GetEmpty(), EffectiveWrapWidth, false,
		ETextWrappingPolicy::DefaultWrapping, ETextTransformPolicy::None, FMargin(), 1.0f, true,
		ToTextJustification(Node.Style.TextAlign));
	FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TextLayoutComputes);
	Cache->Layout->ComputeDesiredSize(DesiredSizeArgs, 1.0f, MakeTextBlockStyle(Node));
	return *Cache->Layout;
}

FVector2f SWebToUEView::MeasureNode(const FWebToUENode& Node,
	const FWebToUELayoutEngine::FMeasureConstraints& Constraints) const
{
	if (Node.Type == EWebToUENodeType::Text)
	{
		if (FSlateApplication::IsInitialized())
		{
			const float WrapWidth = Constraints.WidthMode == FWebToUELayoutEngine::EMeasureMode::Undefined
				? 0.0f : Constraints.Width;
			return FVector2f(PrepareTextLayout(Node, WrapWidth).GetDesiredSize());
		}
		const float CharacterWidth = Node.Style.FontSize * 0.5f;
		const float UnwrappedWidth = Node.Text.Len() * CharacterWidth;
		const bool bCanWrap = Node.Style.WhiteSpace == TEXT("normal") &&
			Constraints.WidthMode != FWebToUELayoutEngine::EMeasureMode::Undefined && Constraints.Width > 0.0f;
		const int32 LineCount = bCanWrap ? FMath::Max(1, FMath::CeilToInt(UnwrappedWidth / Constraints.Width)) : 1;
		return FVector2f(bCanWrap ? FMath::Min(UnwrappedWidth, Constraints.Width) : UnwrappedWidth,
			LineCount * Node.Style.FontSize * 1.25f);
	}
	if (Node.Tag == TEXT("img"))
	{
		if (const TSharedPtr<FSlateBrush>* Brush = Brushes.Find(&Node))
		{
			return (*Brush)->ImageSize;
		}
	}
	return FVector2f::ZeroVector;
}

#if WITH_DEV_AUTOMATION_TESTS
FVector2f SWebToUEView::MeasureTextForTesting(const FString& Text, float Width, bool bWrap) const
{
	FWebToUENode Node;
	Node.Type = EWebToUENodeType::Text;
	Node.Tag = TEXT("#text");
	Node.Text = Text;
	Node.Style.WhiteSpace = bWrap ? TEXT("normal") : TEXT("nowrap");
	FWebToUELayoutEngine::FMeasureConstraints Constraints;
	Constraints.Width = Width;
	Constraints.WidthMode = FWebToUELayoutEngine::EMeasureMode::AtMost;
	return MeasureNode(Node, Constraints);
}

FVector2f SWebToUEView::MeasureRichTextForTesting(const FString& Markup, float Width, bool bWrap) const
{
	FWebToUENode Node;
	Node.Type = EWebToUENodeType::Text;
	Node.Tag = TEXT("#text");
	Node.Text = Markup;
	Node.bRichText = true;
	Node.Style.WhiteSpace = bWrap ? TEXT("normal") : TEXT("nowrap");
	FWebToUELayoutEngine::FMeasureConstraints Constraints;
	Constraints.Width = Width;
	Constraints.WidthMode = FWebToUELayoutEngine::EMeasureMode::AtMost;
	return MeasureNode(Node, Constraints);
}

FText SWebToUEView::GetDisplayTextForTesting(const FWebToUENode& Node) const
{
	return GetDisplayText(Node);
}

void SWebToUEView::SetRuntimeDocumentForTesting(TSharedRef<FWebToUEDocument> InDocument)
{
	RuntimeInstance->AdoptDocumentForTesting(MoveTemp(InDocument));
	RebuildStylesAndBrushes();
}

void SWebToUEView::LayoutForTesting(const FVector2f& ViewportSize) const
{
	FWebToUEDocument* RuntimeDocument = const_cast<FWebToUEDocument*>(GetRuntimeDocument());
	if (!RuntimeDocument || !RuntimeDocument->Root) return;
	FWebToUELayoutEngine::Layout(*RuntimeDocument, ViewportSize,
		[this](const FWebToUENode& Node, const FWebToUELayoutEngine::FMeasureConstraints& Constraints)
		{
			return MeasureNode(Node, Constraints);
		});
	LastViewportSize = ViewportSize;
	bLayoutDirty = false;
}

FVector2f SWebToUEView::GetVisualPositionForTesting(const FWebToUENode& Node) const
{
	FVector2f ScrollOffset = FVector2f::ZeroVector;
	for (const FWebToUENode* Parent = Node.Parent; Parent; Parent = Parent->Parent)
	{
		ScrollOffset += GetRuntimeState(*Parent).ScrollOffset;
	}
	return Node.Position - ScrollOffset;
}

TConstArrayView<FWebToUENode*> SWebToUEView::GetPaintOrderForTesting(const FWebToUENode& Parent) const
{
	return GetPaintOrder(Parent);
}

const FWebToUERuntimeNodeState& SWebToUEView::GetRuntimeStateForTesting(const FWebToUENode& Node) const
{
	return GetRuntimeState(Node);
}

void SWebToUEView::SetBoundTextForTesting(FWebToUENode& Node, const FText& Text)
{
	FWebToUERuntimeNodeState& State = GetRuntimeState(Node);
	State.BoundText = Text;
	State.bHasBoundText = true;
}

FWebToUENode* SWebToUEView::FindRuntimeNodeByIdForTesting(const FString& Id) const
{
	FWebToUENode* Result = nullptr;
	if (const FWebToUEDocument* RuntimeDocument = GetRuntimeDocument())
	{
		RuntimeDocument->ForEachNode([&Result, &Id](FWebToUENode& Node)
		{
			if (!Result && Node.GetAttribute(TEXT("id")) == Id)
			{
				Result = &Node;
			}
		});
	}
	return Result;
}
#endif

int32 SWebToUEView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_PaintBuild);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_PaintBuild);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::PaintBuild);
	FWebToUEDocument* RuntimeDocument = const_cast<FWebToUEDocument*>(GetRuntimeDocument());
	if (!RuntimeDocument || !RuntimeDocument->Root)
	{
		return LayerId;
	}
	const FVector2f ViewportSize = FVector2f(AllottedGeometry.GetLocalSize());
	if (bLayoutDirty || !ViewportSize.Equals(LastViewportSize, 0.1f))
	{
		FWebToUELayoutEngine::Layout(*RuntimeDocument, ViewportSize,
			[this](const FWebToUENode& Node, const FWebToUELayoutEngine::FMeasureConstraints& Constraints)
			{
				return MeasureNode(Node, Constraints);
			});
		LastViewportSize = ViewportSize;
		bLayoutDirty = false;
	}
	return PaintNode(*RuntimeDocument->Root, Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
		InWidgetStyle, 1.0f, bParentEnabled, FVector2f::ZeroVector);
}

int32 SWebToUEView::PaintNode(const FWebToUENode& Node, const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& WidgetStyle, float ParentOpacity, bool bParentEnabled,
	const FVector2f& InheritedScrollOffset) const
{
	if (!Node.IsDisplayed(GetRuntimeState(Node))) return LayerId;
	const float Opacity = ParentOpacity * Node.Style.Opacity;
	const float DrawOpacity = Opacity * WidgetStyle.GetColorAndOpacityTint().A;
	const FVector2f Position = Node.Position - InheritedScrollOffset;
	const FVector2f Size = Node.Size;
	const FPaintGeometry PaintGeometry = Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position));

	if (Node.Type == EWebToUENodeType::Element)
	{
		if (const TSharedPtr<FSlateBrush>* Brush = Brushes.Find(&Node))
		{
			if (Node.Tag == TEXT("img"))
			{
				FVector2f ImagePosition = Position;
				FVector2f ImageSize = Size;
				bool bClipImage = false;
				const FVector2f IntrinsicSize = (*Brush)->ImageSize;
				if (IntrinsicSize.X > 0.0f && IntrinsicSize.Y > 0.0f &&
					(Node.Style.ObjectFit == TEXT("contain") || Node.Style.ObjectFit == TEXT("cover")))
				{
					const float ScaleX = Size.X / IntrinsicSize.X;
					const float ScaleY = Size.Y / IntrinsicSize.Y;
					const float Scale = Node.Style.ObjectFit == TEXT("cover") ? FMath::Max(ScaleX, ScaleY) : FMath::Min(ScaleX, ScaleY);
					ImageSize = IntrinsicSize * Scale;
					ImagePosition += (Size - ImageSize) * 0.5f;
					bClipImage = Node.Style.ObjectFit == TEXT("cover");
				}
				if (bClipImage) Out.PushClip(FSlateClippingZone(Geometry.MakeChild(Size, FSlateLayoutTransform(Position))));
				FSlateDrawElement::MakeBox(Out, LayerId++, Geometry.ToPaintGeometry(ImageSize, FSlateLayoutTransform(ImagePosition)), Brush->Get(),
					bParentEnabled && Node.Style.bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
					FLinearColor(1.0f, 1.0f, 1.0f, DrawOpacity));
				if (bClipImage) Out.PopClip();
			}
			else if (Node.Style.BackgroundColor.A > 0.0f || Node.Style.BorderWidth > 0.0f)
			{
				FSlateDrawElement::MakeBox(Out, LayerId++, PaintGeometry, Brush->Get(),
					bParentEnabled && Node.Style.bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
					FLinearColor(1.0f, 1.0f, 1.0f, DrawOpacity));
			}
		}
	}
	else
	{
		FSlateTextBlockLayout& TextLayout = PrepareTextLayout(Node, Size.X);
		const FGeometry TextGeometry = Geometry.MakeChild(Size, FSlateLayoutTransform(Position));
		FWidgetStyle TextWidgetStyle = WidgetStyle;
		TextWidgetStyle.BlendColorAndOpacityTint(FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
		LayerId = TextLayout.OnPaint(Args, TextGeometry, CullingRect, Out, LayerId, TextWidgetStyle,
			bParentEnabled && Node.Style.bEnabled) + 1;
	}

	bool bPushedClip = false;
	if (Node.ClipsOverflow())
	{
		const FGeometry ClipGeometry = Geometry.MakeChild(Size, FSlateLayoutTransform(Position));
		Out.PushClip(FSlateClippingZone(ClipGeometry));
		bPushedClip = true;
	}

	for (const FWebToUENode* Child : GetPaintOrder(Node))
	{
		const FVector2f ChildScrollOffset = InheritedScrollOffset +
			(Node.IsScrollable() ? GetRuntimeState(Node).ScrollOffset : FVector2f::ZeroVector);
		LayerId = PaintNode(*Child, Args, Geometry, CullingRect, Out, LayerId, WidgetStyle,
			Opacity, bParentEnabled && Node.Style.bEnabled, ChildScrollOffset);
	}
	if (bPushedClip) Out.PopClip();
	return LayerId;
}

void SWebToUEView::RebuildBrushes() const
{
	Brushes.Reset();
	LoadedResources.Reset();
	const FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument) return;
	RuntimeDocument->ForEachNode([this](FWebToUENode& Node)
	{
		if (Node.Tag == TEXT("img"))
		{
			const FString Source = Node.GetAttribute(TEXT("src"));
			if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Source))
			{
				LoadedResources.Emplace(Texture);
				FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::BrushBuilds);
				FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
				TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
				Brush->DrawAs = ESlateBrushDrawType::Image;
				Brush->SetResourceObject(Texture);
				Brush->ImageSize = FVector2f(Texture->GetSizeX(), Texture->GetSizeY());
				Brushes.Add(&Node, MoveTemp(Brush));
			}
		}
		else if (Node.Type == EWebToUENodeType::Element)
		{
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::BrushBuilds);
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
			Brushes.Add(&Node, MakeShared<FSlateRoundedBoxBrush>(Node.Style.BackgroundColor,
				Node.Style.BorderRadius, Node.Style.BorderColor, Node.Style.BorderWidth, FVector2f(32.0f, 32.0f)));
		}
	});
}

void SWebToUEView::RebuildStylesAndBrushes()
{
	TextLayouts.Reset();
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (RuntimeDocument)
	{
		FWebToUEStyleResolver::Resolve(*RuntimeDocument);
		RebuildPaintOrderCache();
		RebuildBrushes();
	}
	else
	{
		PaintOrderNodes.Reset();
		PaintOrderRanges.Reset();
	}
	bLayoutDirty = true;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void SWebToUEView::RebuildPaintOrderCache()
{
	PaintOrderNodes.Reset();
	PaintOrderRanges.Reset();
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument)
	{
		return;
	}

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
		if (Node.Children.IsEmpty())
		{
			return;
		}

		const int32 StartIndex = PaintOrderNodes.Num();
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			PaintOrderNodes.Add(Child.Get());
		}
		const int32 Num = PaintOrderNodes.Num() - StartIndex;
		TArrayView<FWebToUENode*> Children(PaintOrderNodes.GetData() + StartIndex, Num);
		Algo::StableSort(Children, [](const FWebToUENode* A, const FWebToUENode* B)
		{
			return A->Style.ZIndex < B->Style.ZIndex;
		});
		PaintOrderRanges.Add(&Node, { StartIndex, Num });
	});
}

TConstArrayView<FWebToUENode*> SWebToUEView::GetPaintOrder(const FWebToUENode& Parent) const
{
	if (const FWebToUEPaintOrderRange* Range = PaintOrderRanges.Find(&Parent))
	{
		return TConstArrayView<FWebToUENode*>(PaintOrderNodes.GetData() + Range->StartIndex, Range->Num);
	}
	return {};
}

static bool ReadPropertyAsText(UObject* Context, const FString& Field, FText& Out)
{
	if (!Context) return false;
	FProperty* Property = FindFProperty<FProperty>(Context->GetClass(), FName(*Field));
	if (!Property) return false;
	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		Out = TextProperty->GetPropertyValue_InContainer(Context);
		return true;
	}
	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		Out = FText::FromString(StringProperty->GetPropertyValue_InContainer(Context));
		return true;
	}
	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		Out = FText::FromName(NameProperty->GetPropertyValue_InContainer(Context));
		return true;
	}
	FString ExportedValue;
	Property->ExportText_InContainer(0, ExportedValue, Context, Context, Context, PPF_None);
	Out = FText::FromString(MoveTemp(ExportedValue));
	return true;
}

static bool ReadPropertyAsBool(UObject* Context, const FString& Field, bool& Out)
{
	if (!Context) return false;
	if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(Context->GetClass(), FName(*Field)))
	{
		Out = Property->GetPropertyValue_InContainer(Context);
		return true;
	}
	return false;
}

void SWebToUEView::RefreshBindings(UObject* DataContext)
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_Binding);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_Binding);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::Binding);
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument || !DataContext) return;
	RuntimeDocument->ForEachNode([this, RuntimeDocument, DataContext](FWebToUENode& Node)
	{
		if (Node.Type != EWebToUENodeType::Element) return;
		const FString TextField = Node.GetAttribute(TEXT("data-ue-bind-text"));
		if (!TextField.IsEmpty())
		{
			FText Value;
			if (ReadPropertyAsText(DataContext, TextField, Value))
			{
				TSharedPtr<FWebToUENode> TextNode;
				if (TSharedPtr<FWebToUENode>* Existing = Node.Children.FindByPredicate([](const TSharedPtr<FWebToUENode>& Child)
				{
					return Child->Type == EWebToUENodeType::Text;
				}))
				{
					TextNode = *Existing;
				}
				if (!TextNode)
				{
					FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
					TextNode = MakeShared<FWebToUENode>();
					TextNode->Type = EWebToUENodeType::Text;
					TextNode->Tag = TEXT("#text");
					TextNode->Parent = &Node;
					RuntimeDocument->AddRuntimeNodeState(*TextNode);
					Node.Children.Insert(TextNode, 0);
				}
				FWebToUERuntimeNodeState& TextState = GetRuntimeState(*TextNode);
				TextState.BoundText = MoveTemp(Value);
				TextState.bHasBoundText = true;
				TextState.bHasRichTextOverride = true;
				TextState.bRichTextOverride = Node.GetAttribute(TEXT("data-ue-rich-text")).Equals(
					TEXT("true"), ESearchCase::IgnoreCase);
			}
			else ReportBindingErrorOnce(TextField, TEXT("Text binding property was not found."));
		}
		const FString VisibleField = Node.GetAttribute(TEXT("data-ue-bind-visible"));
		if (!VisibleField.IsEmpty())
		{
			bool bValue = true;
			if (ReadPropertyAsBool(DataContext, VisibleField, bValue)) GetRuntimeState(Node).bRuntimeVisible = bValue;
			else ReportBindingErrorOnce(VisibleField, TEXT("Visible binding requires a bool UPROPERTY."));
		}
		const FString EnabledField = Node.GetAttribute(TEXT("data-ue-bind-enabled"));
		if (!EnabledField.IsEmpty())
		{
			bool bValue = true;
			if (ReadPropertyAsBool(DataContext, EnabledField, bValue)) GetRuntimeState(Node).bRuntimeEnabled = bValue;
			else ReportBindingErrorOnce(EnabledField, TEXT("Enabled binding requires a bool UPROPERTY."));
		}
	});
	RebuildStylesAndBrushes();
}

TSet<FName> SWebToUEView::GetBoundFields() const
{
	TSet<FName> Result;
	const FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument) return Result;
	RuntimeDocument->ForEachNode([&Result](FWebToUENode& Node)
	{
		for (const TCHAR* Attribute : { TEXT("data-ue-bind-text"), TEXT("data-ue-bind-visible"), TEXT("data-ue-bind-enabled") })
		{
			const FString Field = Node.GetAttribute(Attribute);
			if (!Field.IsEmpty()) Result.Add(FName(*Field));
		}
	});
	return Result;
}

void SWebToUEView::ReportBindingErrorOnce(const FString& Field, const FString& Message)
{
	if (!LoggedBindingErrors.Contains(Field))
	{
		LoggedBindingErrors.Add(Field);
		UE_LOG(LogWebToUE, Warning, TEXT("Binding '%s': %s"), *Field, *Message);
	}
}

FWebToUENode* SWebToUEView::HitTest(const FVector2f& LocalPosition) const
{
	SCOPE_CYCLE_COUNTER(STAT_WebToUE_HitTest);
	TRACE_CPUPROFILER_EVENT_SCOPE(WebToUE_HitTest);
	FWebToUEPerformanceScope PerformanceScope(EWebToUEPerformancePhase::HitTest);
	FWebToUENode* Best = nullptr;
	const FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument || !RuntimeDocument->Root) return nullptr;

	TFunction<void(FWebToUENode&, const FVector2f&, const FVector2f&, const FVector2f&, bool)> Visit;
	Visit = [&](FWebToUENode& Node, const FVector2f& InheritedScrollOffset,
		const FVector2f& ClipMin, const FVector2f& ClipMax, bool bHasClip)
	{
		const FWebToUERuntimeNodeState& State = GetRuntimeState(Node);
		if (!Node.IsDisplayed(State)) return;
		if (bHasClip && (LocalPosition.X < ClipMin.X || LocalPosition.Y < ClipMin.Y ||
			LocalPosition.X > ClipMax.X || LocalPosition.Y > ClipMax.Y)) return;

		const FVector2f Position = Node.Position - InheritedScrollOffset;
		const FVector2f NodeMax = Position + Node.Size;
		const bool bInsideNode = LocalPosition.X >= Position.X && LocalPosition.Y >= Position.Y &&
			LocalPosition.X <= NodeMax.X && LocalPosition.Y <= NodeMax.Y;
		if (bInsideNode && Node.IsInteractive() && Node.Style.bEnabled)
		{
			if (!Best || Node.Style.ZIndex > Best->Style.ZIndex ||
				(Node.Style.ZIndex == Best->Style.ZIndex && Node.PaintOrder > Best->PaintOrder)) Best = &Node;
		}

		FVector2f ChildClipMin = ClipMin;
		FVector2f ChildClipMax = ClipMax;
		bool bChildHasClip = bHasClip;
		if (Node.ClipsOverflow())
		{
			ChildClipMin = bHasClip ? FVector2f(FMath::Max(ClipMin.X, Position.X), FMath::Max(ClipMin.Y, Position.Y)) : Position;
			ChildClipMax = bHasClip ? FVector2f(FMath::Min(ClipMax.X, NodeMax.X), FMath::Min(ClipMax.Y, NodeMax.Y)) : NodeMax;
			bChildHasClip = true;
			if (ChildClipMin.X > ChildClipMax.X || ChildClipMin.Y > ChildClipMax.Y) return;
		}

		const FVector2f ChildScrollOffset = InheritedScrollOffset +
			(Node.IsScrollable() ? State.ScrollOffset : FVector2f::ZeroVector);
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			Visit(*Child, ChildScrollOffset, ChildClipMin, ChildClipMax, bChildHasClip);
		}
	};
	Visit(*RuntimeDocument->Root, FVector2f::ZeroVector, FVector2f::ZeroVector, FVector2f::ZeroVector, false);
	return Best;
}

bool SWebToUEView::ScrollAt(const FVector2f& LocalPosition, float WheelDelta)
{
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument || !RuntimeDocument->Root || FMath::IsNearlyZero(WheelDelta)) return false;

	struct FScrollCandidate
	{
		FWebToUENode* Node = nullptr;
		int32 Depth = 0;
	};
	TArray<FScrollCandidate> Candidates;
	TFunction<void(FWebToUENode&, const FVector2f&, const FVector2f&, const FVector2f&, bool, int32)> Visit;
	Visit = [&](FWebToUENode& Node, const FVector2f& InheritedScrollOffset,
		const FVector2f& ClipMin, const FVector2f& ClipMax, bool bHasClip, int32 Depth)
	{
		const FWebToUERuntimeNodeState& State = GetRuntimeState(Node);
		if (!Node.IsDisplayed(State)) return;
		if (bHasClip && (LocalPosition.X < ClipMin.X || LocalPosition.Y < ClipMin.Y ||
			LocalPosition.X > ClipMax.X || LocalPosition.Y > ClipMax.Y)) return;

		const FVector2f Position = Node.Position - InheritedScrollOffset;
		const FVector2f NodeMax = Position + Node.Size;
		const bool bInsideNode = LocalPosition.X >= Position.X && LocalPosition.Y >= Position.Y &&
			LocalPosition.X <= NodeMax.X && LocalPosition.Y <= NodeMax.Y;
		if (bInsideNode && Node.IsScrollable() && State.MaxScrollOffset.Y > 0.0f)
		{
			Candidates.Add({ &Node, Depth });
		}

		FVector2f ChildClipMin = ClipMin;
		FVector2f ChildClipMax = ClipMax;
		bool bChildHasClip = bHasClip;
		if (Node.ClipsOverflow())
		{
			ChildClipMin = bHasClip ? FVector2f(FMath::Max(ClipMin.X, Position.X), FMath::Max(ClipMin.Y, Position.Y)) : Position;
			ChildClipMax = bHasClip ? FVector2f(FMath::Min(ClipMax.X, NodeMax.X), FMath::Min(ClipMax.Y, NodeMax.Y)) : NodeMax;
			bChildHasClip = true;
			if (ChildClipMin.X > ChildClipMax.X || ChildClipMin.Y > ChildClipMax.Y) return;
		}

		const FVector2f ChildScrollOffset = InheritedScrollOffset +
			(Node.IsScrollable() ? State.ScrollOffset : FVector2f::ZeroVector);
		for (const TSharedPtr<FWebToUENode>& Child : Node.Children)
		{
			Visit(*Child, ChildScrollOffset, ChildClipMin, ChildClipMax, bChildHasClip, Depth + 1);
		}
	};
	Visit(*RuntimeDocument->Root, FVector2f::ZeroVector, FVector2f::ZeroVector, FVector2f::ZeroVector, false, 0);
	Candidates.Sort([](const FScrollCandidate& A, const FScrollCandidate& B)
	{
		if (A.Depth != B.Depth) return A.Depth > B.Depth;
		if (A.Node->Style.ZIndex != B.Node->Style.ZIndex) return A.Node->Style.ZIndex > B.Node->Style.ZIndex;
		return A.Node->PaintOrder > B.Node->PaintOrder;
	});

	constexpr float WheelScrollAmount = 48.0f;
	for (const FScrollCandidate& Candidate : Candidates)
	{
		FWebToUERuntimeNodeState& State = GetRuntimeState(*Candidate.Node);
		const float PreviousOffset = State.ScrollOffset.Y;
		State.ScrollOffset.Y = FMath::Clamp(
			PreviousOffset - WheelDelta * WheelScrollAmount, 0.0f, State.MaxScrollOffset.Y);
		if (!FMath::IsNearlyEqual(PreviousOffset, State.ScrollOffset.Y))
		{
			Invalidate(EInvalidateWidgetReason::Paint);
			SetHoveredNode(HitTest(LocalPosition));
			return true;
		}
	}
	return false;
}

void SWebToUEView::ClearStateFlag(EWebToUEPseudoState Flag)
{
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument) return;
	RuntimeDocument->ForEachNode([this, Flag](FWebToUENode& Node)
	{
		GetRuntimeState(Node).PseudoStates &= ~Flag;
	});
}

void SWebToUEView::SetStatePath(FWebToUENode* Node, EWebToUEPseudoState Flag)
{
	for (FWebToUENode* Current = Node; Current; Current = Current->Parent)
	{
		GetRuntimeState(*Current).PseudoStates |= Flag;
	}
}

void SWebToUEView::SetHoveredNode(FWebToUENode* Node)
{
	if (RuntimeInstance->GetHoveredNode() == Node) return;
	RuntimeInstance->SetHoveredNode(Node);
	ClearStateFlag(EWebToUEPseudoState::Hover);
	SetStatePath(Node, EWebToUEPseudoState::Hover);
	RebuildStylesAndBrushes();
}

void SWebToUEView::SetPressedNode(FWebToUENode* Node)
{
	if (RuntimeInstance->GetPressedNode() == Node) return;
	RuntimeInstance->SetPressedNode(Node);
	ClearStateFlag(EWebToUEPseudoState::Active);
	if (Node) GetRuntimeState(*Node).PseudoStates |= EWebToUEPseudoState::Active;
	RebuildStylesAndBrushes();
}

void SWebToUEView::SetFocusedNode(FWebToUENode* Node)
{
	if (RuntimeInstance->GetFocusedNode() == Node) return;
	RuntimeInstance->SetFocusedNode(Node);
	ClearStateFlag(EWebToUEPseudoState::Focus);
	if (Node) GetRuntimeState(*Node).PseudoStates |= EWebToUEPseudoState::Focus;
	RebuildStylesAndBrushes();
}

FReply SWebToUEView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SetHoveredNode(HitTest(FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()))));
	return FReply::Handled();
}

void SWebToUEView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SetHoveredNode(nullptr);
	SLeafWidget::OnMouseLeave(MouseEvent);
}

FReply SWebToUEView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	FWebToUENode* Hit = HitTest(FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition())));
	if (!Hit) return FReply::Unhandled();
	SetFocusedNode(Hit);
	SetPressedNode(Hit);
	return FReply::Handled().SetUserFocus(AsShared(), EFocusCause::Mouse, true).CaptureMouse(AsShared());
}

FReply SWebToUEView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FWebToUENode* PressedNode = RuntimeInstance->GetPressedNode();
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !PressedNode) return FReply::Unhandled();
	FWebToUENode* Released = PressedNode;
	const bool bActivate = HitTest(FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()))) == Released;
	SetPressedNode(nullptr);
	if (bActivate) DispatchClick(*Released);
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SWebToUEView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f LocalPosition = FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	return ScrollAt(LocalPosition, MouseEvent.GetWheelDelta()) ? FReply::Handled() : FReply::Unhandled();
}

void SWebToUEView::MoveFocus(int32 Direction)
{
	TArray<FWebToUENode*> Nodes;
	FWebToUEDocument* RuntimeDocument = GetRuntimeDocument();
	if (!RuntimeDocument) return;
	RuntimeDocument->ForEachNode([this, &Nodes](FWebToUENode& Node)
	{
		if (Node.IsInteractive() && Node.IsDisplayed(GetRuntimeState(Node)) && Node.Style.bEnabled) Nodes.Add(&Node);
	});
	Nodes.Sort([](const FWebToUENode& A, const FWebToUENode& B) { return A.PaintOrder < B.PaintOrder; });
	if (Nodes.IsEmpty()) return;
	int32 Index = Nodes.IndexOfByKey(RuntimeInstance->GetFocusedNode());
	Index = Index == INDEX_NONE ? (Direction > 0 ? 0 : Nodes.Num() - 1) : (Index + Direction + Nodes.Num()) % Nodes.Num();
	SetFocusedNode(Nodes[Index]);
}

void SWebToUEView::ActivateFocusedNode()
{
	FWebToUENode* FocusedNode = RuntimeInstance->GetFocusedNode();
	if (FocusedNode && FocusedNode->Style.bEnabled) DispatchClick(*FocusedNode);
}

FReply SWebToUEView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Tab)
	{
		MoveFocus(KeyEvent.IsShiftDown() ? -1 : 1);
		return FReply::Handled();
	}
	if (KeyEvent.GetKey() == EKeys::Enter || KeyEvent.GetKey() == EKeys::SpaceBar)
	{
		ActivateFocusedNode();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SWebToUEView::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (!RuntimeInstance->GetFocusedNode()) MoveFocus(1);
	return FReply::Handled();
}

void SWebToUEView::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	SetPressedNode(nullptr);
	SLeafWidget::OnFocusLost(InFocusEvent);
}

void SWebToUEView::DispatchClick(FWebToUENode& Node) const
{
	const FString Event = Node.GetAttribute(TEXT("data-ue-on-click"));
	if (!Event.IsEmpty())
	{
		if (UWebToUEView* View = Owner.Get()) View->HandleRuntimeEvent(FName(*Event), FName(*Node.GetAttribute(TEXT("id"))));
	}
}
