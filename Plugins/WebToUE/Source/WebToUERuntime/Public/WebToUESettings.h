#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Fonts/SlateFontInfo.h"
#include "WebToUESettings.generated.h"

USTRUCT(BlueprintType)
struct WEBTOUERUNTIME_API FWebToUEFontFamily
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category="Font")
	FName CssFamily = TEXT("Default");

	UPROPERTY(EditAnywhere, Config, Category="Font")
	TSoftObjectPtr<UObject> FontObject;

	UPROPERTY(EditAnywhere, Config, Category="Font")
	FName Typeface = TEXT("Regular");
};

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="WebToUE"))
class WEBTOUERUNTIME_API UWebToUESettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category="Fonts")
	TArray<FWebToUEFontFamily> FontFamilies;

	FSoftObjectPath FindFontObjectPath(const FString& Family) const;
	FSlateFontInfo ResolveFont(const FString& Family, float Size, const FString& Weight) const;
};
