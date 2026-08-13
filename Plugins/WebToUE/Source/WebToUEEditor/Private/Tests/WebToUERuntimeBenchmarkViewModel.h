#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"

#include "WebToUERuntimeBenchmarkViewModel.generated.h"

UCLASS(Transient)
class UWebToUERuntimeBenchmarkViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()

public:
	bool SetBenchmarkLabel(const FText& NewValue)
	{
		return UE_MVVM_SET_PROPERTY_VALUE(BenchmarkLabel, NewValue);
	}

	bool SetBenchmarkVisible(bool bNewValue)
	{
		return UE_MVVM_SET_PROPERTY_VALUE(BenchmarkVisible, bNewValue);
	}

	bool SetUnrelatedLabel(const FText& NewValue)
	{
		return UE_MVVM_SET_PROPERTY_VALUE(UnrelatedLabel, NewValue);
	}

	UPROPERTY(FieldNotify)
	FText BenchmarkLabel;

	UPROPERTY(FieldNotify)
	bool BenchmarkVisible = true;

	UPROPERTY(FieldNotify)
	FText UnrelatedLabel;
};
