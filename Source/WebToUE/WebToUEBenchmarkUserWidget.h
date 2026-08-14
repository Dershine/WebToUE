#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WebToUEBenchmarkUserWidget.generated.h"

class UButton;
class UScrollBox;
class UTextBlock;
class SWidget;

/** Programmatic UMG counterparts for the frozen WebToUE benchmark corpus. */
UCLASS(Transient)
class WEBTOUE_API UWebToUEBenchmarkUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	bool Configure(FName InCorpus);
	bool ApplyTrajectory(int32 Step);
	bool ObserveTrajectoryEffect();
	TSharedPtr<SWidget> GetTrajectoryInputWidget() const;

private:
	FName Corpus;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HealthValue;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WarningText;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> SettingsScroll;

	UPROPERTY(Transient)
	TObjectPtr<UButton> PrimaryButton;

	bool bTrajectoryEffectObserved = false;

	class UOverlay* BuildRoot(bool bOpaqueBackground);
	class UCanvasPanel* FindCanvas(class UOverlay& Root) const;
	void BuildMainMenu(class UCanvasPanel& Canvas);
	void BuildHud(class UCanvasPanel& Canvas);
	void BuildScrollableSettings(class UCanvasPanel& Canvas);
	UTextBlock* MakeText(const FText& Text, float Size, const FLinearColor& Color,
		bool bBold = false) const;
	UButton* MakeButton(const FText& Text, float Height) const;
};
