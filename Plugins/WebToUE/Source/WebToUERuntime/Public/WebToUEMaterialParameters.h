#pragma once

#include "CoreMinimal.h"
#include "WebToUEIdentity.h"
#include "WebToUEPropertyOwnership.h"

enum class EWebToUEMaterialParameterSubmitResult : uint8
{
	Committed,
	Unchanged,
	Queued,
	RejectedInvalidAddress,
	RejectedInvalidTarget,
	RejectedOwnership,
	RejectedResource,
	RejectedParameter,
	RejectedTransaction,
	RejectedInactive
};

struct WEBTOUERUNTIME_API FWebToUEMaterialParameterValue
{
	EWebToUEMaterialParameterType Type = EWebToUEMaterialParameterType::None;
	float Scalar = 0.0f;
	FLinearColor Vector = FLinearColor::Transparent;

	static FWebToUEMaterialParameterValue MakeScalar(float InValue)
	{
		FWebToUEMaterialParameterValue Result;
		Result.Type = EWebToUEMaterialParameterType::Scalar;
		Result.Scalar = InValue;
		return Result;
	}

	static FWebToUEMaterialParameterValue MakeVector(FLinearColor InValue)
	{
		FWebToUEMaterialParameterValue Result;
		Result.Type = EWebToUEMaterialParameterType::Vector;
		Result.Vector = InValue;
		return Result;
	}

	bool IsFinite() const
	{
		return Type == EWebToUEMaterialParameterType::Scalar
			? FMath::IsFinite(Scalar)
			: Type == EWebToUEMaterialParameterType::Vector &&
				FMath::IsFinite(Vector.R) && FMath::IsFinite(Vector.G) &&
				FMath::IsFinite(Vector.B) && FMath::IsFinite(Vector.A);
	}

	bool operator==(const FWebToUEMaterialParameterValue& Other) const
	{
		return Type == Other.Type &&
			(Type == EWebToUEMaterialParameterType::Scalar
				? Scalar == Other.Scalar : Vector == Other.Vector);
	}
};

struct WEBTOUERUNTIME_API FWebToUEMaterialParameterSubmission
{
	FWebToUEInstanceHandle Target;
	FWebToUEPropertyAddress Address;
	FWebToUEMaterialParameterValue Value;
	EWebToUEPropertyWriter DurableOwner = EWebToUEPropertyWriter::Binding;
};

struct WEBTOUERUNTIME_API FWebToUEMaterialParameterSubmitOutcome
{
	EWebToUEMaterialParameterSubmitResult Result =
		EWebToUEMaterialParameterSubmitResult::RejectedInactive;
	FString Diagnostic;

	bool IsAccepted() const
	{
		return Result == EWebToUEMaterialParameterSubmitResult::Committed ||
			Result == EWebToUEMaterialParameterSubmitResult::Unchanged ||
			Result == EWebToUEMaterialParameterSubmitResult::Queued;
	}
};
