#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "WebToUEDemoViewModel.generated.h"

UCLASS(BlueprintType)
class WEBTOUE_API UWebToUEDemoViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()

public:
	UWebToUEDemoViewModel();

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="WebToUE Demo")
	FText PlayerName;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="WebToUE Demo")
	FText HealthText;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="WebToUE Demo")
	bool bShowWarning = false;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category="WebToUE Demo")
	bool bCanStart = true;

	UFUNCTION(BlueprintCallable, Category="WebToUE Demo")
	void SetPlayerName(const FText& NewName);

	UFUNCTION(BlueprintCallable, Category="WebToUE Demo")
	void SetHealth(int32 CurrentHealth, int32 MaxHealth = 100);

	UFUNCTION(BlueprintCallable, Category="WebToUE Demo")
	void SetCanStart(bool bNewCanStart);
};
