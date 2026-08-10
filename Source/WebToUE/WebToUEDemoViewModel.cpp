#include "WebToUEDemoViewModel.h"

UWebToUEDemoViewModel::UWebToUEDemoViewModel()
{
	PlayerName = NSLOCTEXT("WebToUEDemo", "DefaultPlayer", "Player One");
	HealthText = NSLOCTEXT("WebToUEDemo", "DefaultHealth", "100 / 100");
}

void UWebToUEDemoViewModel::SetPlayerName(const FText& NewName)
{
	UE_MVVM_SET_PROPERTY_VALUE(PlayerName, NewName);
}

void UWebToUEDemoViewModel::SetHealth(int32 CurrentHealth, int32 MaxHealth)
{
	const int32 SafeMax = FMath::Max(1, MaxHealth);
	const int32 SafeCurrent = FMath::Clamp(CurrentHealth, 0, SafeMax);
	UE_MVVM_SET_PROPERTY_VALUE(HealthText,
		FText::Format(NSLOCTEXT("WebToUEDemo", "HealthFormat", "{0} / {1}"), SafeCurrent, SafeMax));
	UE_MVVM_SET_PROPERTY_VALUE(bShowWarning, SafeCurrent <= FMath::CeilToInt(SafeMax * 0.25f));
}

void UWebToUEDemoViewModel::SetCanStart(bool bNewCanStart)
{
	UE_MVVM_SET_PROPERTY_VALUE(bCanStart, bNewCanStart);
}
