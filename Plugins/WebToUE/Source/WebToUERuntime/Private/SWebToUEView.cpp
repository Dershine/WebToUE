#include "SWebToUEView.h"

#include "WebToUECompiler.h"
#include "WebToUEDocument.h"
#include "WebToUESettings.h"
#include "WebToUEView.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogWebToUE, Log, All);

void SWebToUEView::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	SetCanTick(false);
}

void SWebToUEView::SetDocument(UWebToUEDocument* InDocument)
{
	DocumentAsset = InDocument;
	RuntimeDocument.Reset();
	HoveredNode = PressedNode = FocusedNode = nullptr;
	if (InDocument && InDocument->CompiledNodes.IsValidIndex(InDocument->RootNodeIndex))
	{
		RuntimeDocument = MakeShared<FWebToUEDocument>();
		TArray<TSharedPtr<FWebToUENode>> Nodes;
		Nodes.Reserve(InDocument->CompiledNodes.Num());
		for (const FWebToUECompiledNode& Source : InDocument->CompiledNodes)
		{
			TSharedPtr<FWebToUENode> Node = MakeShared<FWebToUENode>();
			Node->Type = static_cast<EWebToUENodeType>(Source.Type);
			Node->Tag = Source.Tag;
			Node->Text = Source.Text;
			for (const FWebToUECompiledAttribute& Attribute : Source.Attributes) Node->Attributes.Add(Attribute.Name, Attribute.Value);
			Nodes.Add(MoveTemp(Node));
		}
		for (int32 Index = 0; Index < InDocument->CompiledNodes.Num(); ++Index)
		{
			const int32 ParentIndex = InDocument->CompiledNodes[Index].ParentIndex;
			if (Nodes.IsValidIndex(ParentIndex))
			{
				Nodes[Index]->Parent = Nodes[ParentIndex].Get();
				Nodes[ParentIndex]->Children.Add(Nodes[Index]);
			}
		}
		RuntimeDocument->Root = Nodes[InDocument->RootNodeIndex];
		for (const FWebToUECompiledRule& SourceRule : InDocument->CompiledRules)
		{
			FWebToUEStyleRule Rule;
			Rule.Specificity = SourceRule.Specificity;
			Rule.SourceOrder = SourceRule.SourceOrder;
			for (const FWebToUECompiledSelectorSegment& SourceSegment : SourceRule.Selector)
			{
				FWebToUESelectorSegment Segment;
				Segment.Type = SourceSegment.Type;
				Segment.Id = SourceSegment.Id;
				Segment.Classes = SourceSegment.Classes;
				Segment.RequiredState = static_cast<EWebToUEPseudoState>(SourceSegment.RequiredState);
				Segment.RelationToPrevious = static_cast<EWebToUECombinator>(SourceSegment.RelationToPrevious);
				Rule.Selector.Add(MoveTemp(Segment));
			}
			for (const FWebToUECompiledDeclaration& Declaration : SourceRule.Declarations) Rule.Declarations.Add(Declaration.Name, Declaration.Value);
			RuntimeDocument->Rules.Add(MoveTemp(Rule));
		}
		FWebToUEStyleResolver::Resolve(*RuntimeDocument);
	}
	RebuildStylesAndBrushes();
}

FVector2D SWebToUEView::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(320.0, 180.0);
}

FVector2f SWebToUEView::MeasureNode(const FWebToUENode& Node) const
{
	if (Node.Type == EWebToUENodeType::Text)
	{
		const UWebToUESettings* Settings = GetDefault<UWebToUESettings>();
		const FSlateFontInfo Font = Settings->ResolveFont(Node.Style.FontFamily, Node.Style.FontSize, Node.Style.FontWeight);
		if (FSlateApplication::IsInitialized())
		{
			return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Node.Text, Font));
		}
		return FVector2f(Node.Text.Len() * Node.Style.FontSize * 0.5f, Node.Style.FontSize * 1.25f);
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

int32 SWebToUEView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!RuntimeDocument || !RuntimeDocument->Root)
	{
		return LayerId;
	}
	const FVector2f ViewportSize = FVector2f(AllottedGeometry.GetLocalSize());
	if (bLayoutDirty || !ViewportSize.Equals(LastViewportSize, 0.1f))
	{
		FWebToUELayoutEngine::Layout(*RuntimeDocument, ViewportSize,
			[this](const FWebToUENode& Node) { return MeasureNode(Node); });
		LastViewportSize = ViewportSize;
		bLayoutDirty = false;
	}
	return PaintNode(*RuntimeDocument->Root, AllottedGeometry, OutDrawElements, LayerId,
		InWidgetStyle.GetColorAndOpacityTint().A, bParentEnabled);
}

int32 SWebToUEView::PaintNode(const FWebToUENode& Node, const FGeometry& Geometry,
	FSlateWindowElementList& Out, int32 LayerId, float ParentOpacity, bool bParentEnabled) const
{
	if (!Node.IsDisplayed()) return LayerId;
	const float Opacity = ParentOpacity * Node.Style.Opacity;
	const FVector2f Position = Node.Position;
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
					FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
				if (bClipImage) Out.PopClip();
			}
			else if (Node.Style.BackgroundColor.A > 0.0f || Node.Style.BorderWidth > 0.0f)
			{
				FSlateDrawElement::MakeBox(Out, LayerId++, PaintGeometry, Brush->Get(),
					bParentEnabled && Node.Style.bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
					FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
			}
		}
	}
	else
	{
		const UWebToUESettings* Settings = GetDefault<UWebToUESettings>();
		const FSlateFontInfo Font = Settings->ResolveFont(Node.Style.FontFamily, Node.Style.FontSize, Node.Style.FontWeight);
		FLinearColor Tint = Node.Style.Color;
		Tint.A *= Opacity;
		FVector2f TextPosition = Position;
		const FVector2f TextSize = MeasureNode(Node);
		if (Node.Style.TextAlign == TEXT("center")) TextPosition.X += FMath::Max(0.0f, (Size.X - TextSize.X) * 0.5f);
		else if (Node.Style.TextAlign == TEXT("right")) TextPosition.X += FMath::Max(0.0f, Size.X - TextSize.X);
		FSlateDrawElement::MakeText(Out, LayerId++, Geometry.ToOffsetPaintGeometry(FVector2D(TextPosition)),
			Node.Text, Font, bParentEnabled && Node.Style.bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect, Tint);
	}

	bool bPushedClip = false;
	if (Node.Style.Overflow == EWebToUEOverflow::Hidden)
	{
		const FGeometry ClipGeometry = Geometry.MakeChild(Size, FSlateLayoutTransform(Position));
		Out.PushClip(FSlateClippingZone(ClipGeometry));
		bPushedClip = true;
	}

	TArray<TSharedPtr<FWebToUENode>> SortedChildren = Node.Children;
	SortedChildren.Sort([](const TSharedPtr<FWebToUENode>& A, const TSharedPtr<FWebToUENode>& B)
	{
		return A->Style.ZIndex == B->Style.ZIndex ? A->PaintOrder < B->PaintOrder : A->Style.ZIndex < B->Style.ZIndex;
	});
	for (const TSharedPtr<FWebToUENode>& Child : SortedChildren)
	{
		LayerId = PaintNode(*Child, Geometry, Out, LayerId, Opacity, bParentEnabled && Node.Style.bEnabled);
	}
	if (bPushedClip) Out.PopClip();
	return LayerId;
}

void SWebToUEView::RebuildBrushes() const
{
	Brushes.Reset();
	LoadedResources.Reset();
	if (!RuntimeDocument) return;
	RuntimeDocument->ForEachNode([this](FWebToUENode& Node)
	{
		if (Node.Tag == TEXT("img"))
		{
			const FString Source = Node.GetAttribute(TEXT("src"));
			if (UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Source))
			{
				LoadedResources.Emplace(Texture);
				TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
				Brush->DrawAs = ESlateBrushDrawType::Image;
				Brush->SetResourceObject(Texture);
				Brush->ImageSize = FVector2f(Texture->GetSizeX(), Texture->GetSizeY());
				Brushes.Add(&Node, MoveTemp(Brush));
			}
		}
		else if (Node.Type == EWebToUENodeType::Element)
		{
			Brushes.Add(&Node, MakeShared<FSlateRoundedBoxBrush>(Node.Style.BackgroundColor,
				Node.Style.BorderRadius, Node.Style.BorderColor, Node.Style.BorderWidth, FVector2f(32.0f, 32.0f)));
		}
	});
}

void SWebToUEView::RebuildStylesAndBrushes()
{
	if (RuntimeDocument)
	{
		FWebToUEStyleResolver::Resolve(*RuntimeDocument);
		RebuildBrushes();
	}
	bLayoutDirty = true;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

static bool ReadPropertyAsText(UObject* Context, const FString& Field, FString& Out)
{
	if (!Context) return false;
	FProperty* Property = FindFProperty<FProperty>(Context->GetClass(), FName(*Field));
	if (!Property) return false;
	if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		Out = TextProperty->GetPropertyValue_InContainer(Context).ToString();
		return true;
	}
	if (const FStrProperty* StringProperty = CastField<FStrProperty>(Property))
	{
		Out = StringProperty->GetPropertyValue_InContainer(Context);
		return true;
	}
	if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
	{
		Out = NameProperty->GetPropertyValue_InContainer(Context).ToString();
		return true;
	}
	Property->ExportText_InContainer(0, Out, Context, Context, Context, PPF_None);
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
	if (!RuntimeDocument || !DataContext) return;
	RuntimeDocument->ForEachNode([this, DataContext](FWebToUENode& Node)
	{
		if (Node.Type != EWebToUENodeType::Element) return;
		const FString TextField = Node.GetAttribute(TEXT("data-ue-bind-text"));
		if (!TextField.IsEmpty())
		{
			FString Value;
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
					TextNode = MakeShared<FWebToUENode>();
					TextNode->Type = EWebToUENodeType::Text;
					TextNode->Tag = TEXT("#text");
					TextNode->Parent = &Node;
					Node.Children.Insert(TextNode, 0);
				}
				TextNode->Text = MoveTemp(Value);
			}
			else ReportBindingErrorOnce(TextField, TEXT("Text binding property was not found."));
		}
		const FString VisibleField = Node.GetAttribute(TEXT("data-ue-bind-visible"));
		if (!VisibleField.IsEmpty())
		{
			bool bValue = true;
			if (ReadPropertyAsBool(DataContext, VisibleField, bValue)) Node.bRuntimeVisible = bValue;
			else ReportBindingErrorOnce(VisibleField, TEXT("Visible binding requires a bool UPROPERTY."));
		}
		const FString EnabledField = Node.GetAttribute(TEXT("data-ue-bind-enabled"));
		if (!EnabledField.IsEmpty())
		{
			bool bValue = true;
			if (ReadPropertyAsBool(DataContext, EnabledField, bValue)) Node.bRuntimeEnabled = bValue;
			else ReportBindingErrorOnce(EnabledField, TEXT("Enabled binding requires a bool UPROPERTY."));
		}
	});
	RebuildStylesAndBrushes();
}

TSet<FName> SWebToUEView::GetBoundFields() const
{
	TSet<FName> Result;
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
	FWebToUENode* Best = nullptr;
	if (!RuntimeDocument) return nullptr;
	RuntimeDocument->ForEachNode([&](FWebToUENode& Node)
	{
		if (!Node.IsInteractive() || !Node.IsDisplayed() || !Node.Style.bEnabled) return;
		if (LocalPosition.X >= Node.Position.X && LocalPosition.Y >= Node.Position.Y &&
			LocalPosition.X <= Node.Position.X + Node.Size.X && LocalPosition.Y <= Node.Position.Y + Node.Size.Y)
		{
			if (!Best || Node.Style.ZIndex > Best->Style.ZIndex ||
				(Node.Style.ZIndex == Best->Style.ZIndex && Node.PaintOrder > Best->PaintOrder)) Best = &Node;
		}
	});
	return Best;
}

void SWebToUEView::ClearStateFlag(EWebToUEPseudoState Flag)
{
	if (!RuntimeDocument) return;
	RuntimeDocument->ForEachNode([Flag](FWebToUENode& Node) { Node.StateFlags &= ~Flag; });
}

void SWebToUEView::SetStatePath(FWebToUENode* Node, EWebToUEPseudoState Flag)
{
	for (FWebToUENode* Current = Node; Current; Current = Current->Parent) Current->StateFlags |= Flag;
}

void SWebToUEView::SetHoveredNode(FWebToUENode* Node)
{
	if (HoveredNode == Node) return;
	HoveredNode = Node;
	ClearStateFlag(EWebToUEPseudoState::Hover);
	SetStatePath(Node, EWebToUEPseudoState::Hover);
	RebuildStylesAndBrushes();
}

void SWebToUEView::SetPressedNode(FWebToUENode* Node)
{
	if (PressedNode == Node) return;
	PressedNode = Node;
	ClearStateFlag(EWebToUEPseudoState::Active);
	if (Node) Node->StateFlags |= EWebToUEPseudoState::Active;
	RebuildStylesAndBrushes();
}

void SWebToUEView::SetFocusedNode(FWebToUENode* Node)
{
	if (FocusedNode == Node) return;
	FocusedNode = Node;
	ClearStateFlag(EWebToUEPseudoState::Focus);
	if (Node) Node->StateFlags |= EWebToUEPseudoState::Focus;
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
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !PressedNode) return FReply::Unhandled();
	FWebToUENode* Released = PressedNode;
	const bool bActivate = HitTest(FVector2f(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()))) == Released;
	SetPressedNode(nullptr);
	if (bActivate) DispatchClick(*Released);
	return FReply::Handled().ReleaseMouseCapture();
}

void SWebToUEView::MoveFocus(int32 Direction)
{
	TArray<FWebToUENode*> Nodes;
	if (!RuntimeDocument) return;
	RuntimeDocument->ForEachNode([&Nodes](FWebToUENode& Node)
	{
		if (Node.IsInteractive() && Node.IsDisplayed() && Node.Style.bEnabled) Nodes.Add(&Node);
	});
	Nodes.Sort([](const FWebToUENode& A, const FWebToUENode& B) { return A.PaintOrder < B.PaintOrder; });
	if (Nodes.IsEmpty()) return;
	int32 Index = Nodes.IndexOfByKey(FocusedNode);
	Index = Index == INDEX_NONE ? (Direction > 0 ? 0 : Nodes.Num() - 1) : (Index + Direction + Nodes.Num()) % Nodes.Num();
	SetFocusedNode(Nodes[Index]);
}

void SWebToUEView::ActivateFocusedNode()
{
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
	if (!FocusedNode) MoveFocus(1);
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
