#include "WebToUEBenchmarkUserWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace WebToUE::Benchmark::UMG::Private
{
	static const FLinearColor PageBackground = FLinearColor::FromSRGBColor(FColor(9, 17, 31));
	static const FLinearColor ShellBackground = FLinearColor::FromSRGBColor(FColor(20, 35, 58));
	static const FLinearColor ShellBorder = FLinearColor::FromSRGBColor(FColor(67, 101, 142));
	static const FLinearColor Foreground = FLinearColor::FromSRGBColor(FColor(238, 244, 255));
	static const FLinearColor Muted = FLinearColor::FromSRGBColor(FColor(181, 200, 228));
	static const FLinearColor Accent = FLinearColor::FromSRGBColor(FColor(100, 216, 255));
	static const FLinearColor ButtonNormal = FLinearColor::FromSRGBColor(FColor(32, 59, 97));
	static const FLinearColor ButtonHover = FLinearColor::FromSRGBColor(FColor(47, 86, 136));
	static const FLinearColor ButtonPressed = FLinearColor::FromSRGBColor(FColor(24, 48, 79));
	static const FLinearColor ButtonBorder = FLinearColor::FromSRGBColor(FColor(82, 119, 166));

	static FSlateBrush MakeRoundedBrush(const FLinearColor& Fill, float Radius,
		const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.0f)
	{
		return FSlateRoundedBoxBrush(Fill, Radius, Outline, OutlineWidth,
			FVector2f(32.0f, 32.0f));
	}
}

bool UWebToUEBenchmarkUserWidget::Configure(FName InCorpus)
{
	Corpus = InCorpus;
	HealthValue = nullptr;
	WarningText = nullptr;
	SettingsScroll = nullptr;
	PrimaryButton = nullptr;
	bTrajectoryEffectObserved = false;
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("BenchmarkWidgetTree"));
	}
	if (!WidgetTree) return false;

	const bool bOpaqueBackground = Corpus != TEXT("HUD");
	UOverlay* Root = BuildRoot(bOpaqueBackground);
	UCanvasPanel* Canvas = Root ? FindCanvas(*Root) : nullptr;
	if (!Root || !Canvas) return false;
	WidgetTree->RootWidget = Root;

	if (Corpus == TEXT("MainMenu"))
	{
		BuildMainMenu(*Canvas);
	}
	else if (Corpus == TEXT("HUD"))
	{
		BuildHud(*Canvas);
	}
	else if (Corpus == TEXT("ScrollableSettings"))
	{
		BuildScrollableSettings(*Canvas);
	}
	else
	{
		return false;
	}
	return true;
}

UOverlay* UWebToUEBenchmarkUserWidget::BuildRoot(bool bOpaqueBackground)
{
	using namespace WebToUE::Benchmark::UMG::Private;
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	if (bOpaqueBackground)
	{
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
			TEXT("PageBackground"));
		Background->SetBrush(MakeRoundedBrush(PageBackground, 0.0f));
		UOverlaySlot* BackgroundSlot = Root->AddChildToOverlay(Background);
		BackgroundSlot->SetHorizontalAlignment(HAlign_Fill);
		BackgroundSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas"));
	UOverlaySlot* CanvasSlot = Root->AddChildToOverlay(Canvas);
	CanvasSlot->SetHorizontalAlignment(HAlign_Fill);
	CanvasSlot->SetVerticalAlignment(VAlign_Fill);
	return Root;
}

UCanvasPanel* UWebToUEBenchmarkUserWidget::FindCanvas(UOverlay& Root) const
{
	for (UWidget* Child : Root.GetAllChildren())
	{
		if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(Child)) return Canvas;
	}
	return nullptr;
}

UTextBlock* UWebToUEBenchmarkUserWidget::MakeText(const FText& Text, float Size,
	const FLinearColor& Color, bool bBold) const
{
	UTextBlock* Result = WidgetTree->ConstructWidget<UTextBlock>();
	Result->SetText(Text);
	Result->SetColorAndOpacity(FSlateColor(Color));
	Result->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"),
		FMath::RoundToInt(Size)));
	return Result;
}

UButton* UWebToUEBenchmarkUserWidget::MakeButton(const FText& Text, float Height) const
{
	using namespace WebToUE::Benchmark::UMG::Private;
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	FButtonStyle Style;
	Style.SetNormal(MakeRoundedBrush(ButtonNormal, 10.0f, ButtonBorder, 1.0f));
	Style.SetHovered(MakeRoundedBrush(ButtonHover, 10.0f, Accent, 1.0f));
	Style.SetPressed(MakeRoundedBrush(ButtonPressed, 10.0f, Accent, 1.0f));
	Style.SetDisabled(MakeRoundedBrush(ButtonNormal.CopyWithNewOpacity(0.4f),
		10.0f, ButtonBorder.CopyWithNewOpacity(0.4f), 1.0f));
	Style.SetNormalPadding(FMargin(18.0f, 12.0f));
	Style.SetPressedPadding(FMargin(18.0f, 13.0f, 18.0f, 11.0f));
	Button->SetStyle(Style);
	Button->SetColorAndOpacity(FLinearColor::White);
	USizeBox* TextHeight = WidgetTree->ConstructWidget<USizeBox>();
	// The corpus height is the outer CSS box. Slate padding is part of that height,
	// so keep only the content-box remainder here.
	TextHeight->SetHeightOverride(FMath::Max(0.0f, Height - 24.0f));
	UTextBlock* Label = MakeText(Text, 18.0f, Foreground);
	Label->SetJustification(ETextJustify::Center);
	TextHeight->AddChild(Label);
	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->AddChild(TextHeight)))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Center);
	}
	return Button;
}

void UWebToUEBenchmarkUserWidget::BuildMainMenu(UCanvasPanel& Canvas)
{
	using namespace WebToUE::Benchmark::UMG::Private;
	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
	Width->SetWidthOverride(620.0f);
	UBorder* Shell = WidgetTree->ConstructWidget<UBorder>();
	Shell->SetBrush(MakeRoundedBrush(ShellBackground.CopyWithNewOpacity(0.933f),
		18.0f, ShellBorder, 1.0f));
	Shell->SetPadding(FMargin(42.0f));
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	Content->AddChildToVerticalBox(MakeText(
		NSLOCTEXT("WebToUEBenchmark", "MenuEyebrow", "WEBTOUE DEVELOPER PREVIEW"),
		13.0f, Accent))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	UTextBlock* Title = MakeText(
		NSLOCTEXT("WebToUEBenchmark", "MenuTitle", "Native UI, frontend workflow."),
		36.0f, Foreground, true);
	Title->SetAutoWrapText(true);
	Title->SetWrapTextAt(536.0f);
	Content->AddChildToVerticalBox(Title)->SetPadding(
		FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	UTextBlock* Subtitle = MakeText(
		NSLOCTEXT("WebToUEBenchmark", "MenuSubtitle",
			"HTML and CSS authored outside Unreal, rendered by Slate."),
		18.0f, Muted);
	Subtitle->SetAutoWrapText(true);
	Subtitle->SetWrapTextAt(536.0f);
	Content->AddChildToVerticalBox(Subtitle)->SetPadding(
		FMargin(0.0f, 0.0f, 0.0f, 36.0f));
	const FText Labels[] = {
		NSLOCTEXT("WebToUEBenchmark", "Start", "Start Game"),
		NSLOCTEXT("WebToUEBenchmark", "Settings", "Settings"),
		NSLOCTEXT("WebToUEBenchmark", "Quit", "Quit")
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Labels); ++Index)
	{
		UButton* Button = MakeButton(Labels[Index], 52.0f);
		if (Index == 0) PrimaryButton = Button;
		UVerticalBoxSlot* ButtonSlot = Content->AddChildToVerticalBox(Button);
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		ButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f,
			Index + 1 < UE_ARRAY_COUNT(Labels) ? 12.0f : 0.0f));
	}
	Shell->AddChild(Content);
	Width->AddChild(Shell);
	UCanvasPanelSlot* CanvasSlot = Canvas.AddChildToCanvas(Width);
	CanvasSlot->SetAnchors(FAnchors(0.0f, 0.5f));
	CanvasSlot->SetAlignment(FVector2D(0.0, 0.5));
	CanvasSlot->SetPosition(FVector2D(72.0, 0.0));
	CanvasSlot->SetAutoSize(true);
}

void UWebToUEBenchmarkUserWidget::BuildHud(UCanvasPanel& Canvas)
{
	using namespace WebToUE::Benchmark::UMG::Private;
	USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>();
	Width->SetWidthOverride(300.0f);
	UBorder* Shell = WidgetTree->ConstructWidget<UBorder>();
	Shell->SetBrush(MakeRoundedBrush(FLinearColor::FromSRGBColor(FColor(11, 18, 32, 221)),
		12.0f, FLinearColor::FromSRGBColor(FColor(61, 85, 119)), 1.0f));
	Shell->SetPadding(FMargin(20.0f));
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	Content->AddChildToVerticalBox(MakeText(
		NSLOCTEXT("WebToUEBenchmark", "Status", "PLAYER STATUS"), 12.0f, Accent))
		->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	Content->AddChildToVerticalBox(MakeText(
		NSLOCTEXT("WebToUEBenchmark", "Player", "Player One"), 24.0f, Foreground, true))
		->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	UHorizontalBox* HealthRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	UHorizontalBoxSlot* HealthLabelSlot = HealthRow->AddChildToHorizontalBox(MakeText(
		NSLOCTEXT("WebToUEBenchmark", "Health", "Health"), 18.0f, Foreground));
	HealthLabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	HealthValue = MakeText(FText::FromString(TEXT("100 / 100")), 18.0f,
		FLinearColor::FromSRGBColor(FColor(142, 240, 176)));
	HealthRow->AddChildToHorizontalBox(HealthValue)->SetHorizontalAlignment(HAlign_Right);
	Content->AddChildToVerticalBox(HealthRow)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	WarningText = MakeText(NSLOCTEXT("WebToUEBenchmark", "LowHealth", "LOW HEALTH"),
		18.0f, FLinearColor::FromSRGBColor(FColor(255, 109, 109)), true);
	WarningText->SetVisibility(ESlateVisibility::Collapsed);
	Content->AddChildToVerticalBox(WarningText)->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	Shell->AddChild(Content);
	Width->AddChild(Shell);
	UCanvasPanelSlot* CanvasSlot = Canvas.AddChildToCanvas(Width);
	CanvasSlot->SetPosition(FVector2D(36.0, 36.0));
	CanvasSlot->SetAutoSize(true);
}

void UWebToUEBenchmarkUserWidget::BuildScrollableSettings(UCanvasPanel& Canvas)
{
	using namespace WebToUE::Benchmark::UMG::Private;
	USizeBox* ShellSize = WidgetTree->ConstructWidget<USizeBox>();
	ShellSize->SetWidthOverride(680.0f);
	ShellSize->SetHeightOverride(600.0f);
	UBorder* Shell = WidgetTree->ConstructWidget<UBorder>();
	Shell->SetBrush(MakeRoundedBrush(ShellBackground, 18.0f, ShellBorder, 1.0f));
	Shell->SetPadding(FMargin(32.0f));
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	Content->AddChildToVerticalBox(MakeText(
		NSLOCTEXT("WebToUEBenchmark", "SettingsTitle", "Settings"), 34.0f,
		Foreground, true))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	UTextBlock* Subtitle = MakeText(
		NSLOCTEXT("WebToUEBenchmark", "SettingsSubtitle",
			"Move the mouse over the list and use the wheel to scroll."),
		18.0f, Muted);
	Subtitle->SetAutoWrapText(true);
	Subtitle->SetWrapTextAt(616.0f);
	Content->AddChildToVerticalBox(Subtitle)->SetPadding(
		FMargin(0.0f, 0.0f, 0.0f, 20.0f));
	USizeBox* ScrollHeight = WidgetTree->ConstructWidget<USizeBox>();
	ScrollHeight->SetHeightOverride(440.0f);
	SettingsScroll = WidgetTree->ConstructWidget<UScrollBox>();
	SettingsScroll->SetScrollBarVisibility(ESlateVisibility::Hidden);
	SettingsScroll->SetAnimateWheelScrolling(false);
	// Match WebToUE's fixed 48 px wheel quantum (Slate defaults to 32 px).
	SettingsScroll->SetWheelScrollMultiplier(1.5f);
	const FText Labels[] = {
		NSLOCTEXT("WebToUEBenchmark", "Display", "Display"),
		NSLOCTEXT("WebToUEBenchmark", "Graphics", "Graphics"),
		NSLOCTEXT("WebToUEBenchmark", "Audio", "Audio"),
		NSLOCTEXT("WebToUEBenchmark", "Controls", "Controls"),
		NSLOCTEXT("WebToUEBenchmark", "Gameplay", "Gameplay"),
		NSLOCTEXT("WebToUEBenchmark", "Accessibility", "Accessibility"),
		NSLOCTEXT("WebToUEBenchmark", "Language", "Language")
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Labels); ++Index)
	{
		UButton* Button = MakeButton(Labels[Index], 76.0f);
		SettingsScroll->AddChild(Button);
		if (UScrollBoxSlot* ScrollSlot = Cast<UScrollBoxSlot>(Button->Slot))
		{
			ScrollSlot->SetPadding(FMargin(4.0f, 0.0f, 4.0f,
				Index + 1 < UE_ARRAY_COUNT(Labels) ? 12.0f : 0.0f));
		}
	}
	ScrollHeight->AddChild(SettingsScroll);
	Content->AddChildToVerticalBox(ScrollHeight)->SetHorizontalAlignment(HAlign_Fill);
	Shell->AddChild(Content);
	ShellSize->AddChild(Shell);
	UCanvasPanelSlot* CanvasSlot = Canvas.AddChildToCanvas(ShellSize);
	CanvasSlot->SetAnchors(FAnchors(0.5f));
	CanvasSlot->SetAlignment(FVector2D(0.5, 0.5));
	CanvasSlot->SetAutoSize(true);
}

bool UWebToUEBenchmarkUserWidget::ApplyTrajectory(int32 Step)
{
	if (Corpus == TEXT("HUD"))
	{
		const bool bLow = (Step & 1) != 0;
		if (HealthValue)
		{
			HealthValue->SetText(FText::FromString(bLow ? TEXT("20 / 100") : TEXT("85 / 100")));
		}
		if (WarningText)
		{
			WarningText->SetVisibility(bLow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
		bTrajectoryEffectObserved |= HealthValue && WarningText &&
			HealthValue->GetText().ToString() ==
				(bLow ? TEXT("20 / 100") : TEXT("85 / 100")) &&
			WarningText->GetVisibility() ==
				(bLow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	return bTrajectoryEffectObserved;
}

bool UWebToUEBenchmarkUserWidget::ObserveTrajectoryEffect()
{
	if (!bTrajectoryEffectObserved && Corpus == TEXT("MainMenu"))
	{
		bTrajectoryEffectObserved = PrimaryButton && PrimaryButton->IsHovered();
	}
	else if (!bTrajectoryEffectObserved && Corpus == TEXT("ScrollableSettings"))
	{
		bTrajectoryEffectObserved = SettingsScroll &&
			!FMath::IsNearlyZero(SettingsScroll->GetScrollOffset());
	}
	return bTrajectoryEffectObserved;
}

TSharedPtr<SWidget> UWebToUEBenchmarkUserWidget::GetTrajectoryInputWidget() const
{
	if (Corpus == TEXT("MainMenu") && PrimaryButton)
	{
		return PrimaryButton->GetCachedWidget();
	}
	if (Corpus == TEXT("ScrollableSettings") && SettingsScroll)
	{
		return SettingsScroll->GetCachedWidget();
	}
	return nullptr;
}
