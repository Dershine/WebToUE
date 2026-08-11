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

	UPROPERTY(FieldNotify)
	FText BenchmarkLabel;
};
