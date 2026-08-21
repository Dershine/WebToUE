#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WebToUEResourceContract.h"
#include "WebToUEFeedbackProfile.generated.h"

class USoundBase;
class USoundConcurrency;
class UWebToUEFeedbackProfile;

DECLARE_DELEGATE_RetVal_TwoParams(bool, FWebToUEFeedbackCookFreshnessValidator,
	const UWebToUEFeedbackProfile&, TArray<FWebToUEResourceContractDiagnostic>&);

UENUM(BlueprintType)
enum class EWebToUEFeedbackWorldPolicy : uint8
{
	Drop,
	TwoDimensional,
	OwnerLocation3D
};

USTRUCT(BlueprintType)
struct WEBTOUERUNTIME_API FWebToUEFeedbackCueProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	FName CueId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	EWebToUEResidencyClass Residency = EWebToUEResidencyClass::Critical;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	TArray<TSoftObjectPtr<USoundBase>> Variants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	TSoftObjectPtr<USoundConcurrency> Concurrency;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	float PitchMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback", meta=(ClampMin="0.0"))
	double CooldownSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	FName DeduplicationGroup;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback", meta=(ClampMin="0.0"))
	double DeduplicationWindowSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	FName ThrottleGroup;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback", meta=(ClampMin="0"))
	int32 ThrottleMaximum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback", meta=(ClampMin="0.0"))
	double ThrottleWindowSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	EWebToUEFeedbackWorldPolicy WorldPolicy = EWebToUEFeedbackWorldPolicy::Drop;

	/** Optional project adapter route; it is never interpreted as a UObject or function path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	FName ProjectRouteId;
};

/**
 * Versioned Cue-to-presentation mapping. The asset owns policy and sealed soft dependencies;
 * Sessions/Routers own residency handles and rate-limit state.
 */
UCLASS(BlueprintType)
class WEBTOUERUNTIME_API UWebToUEFeedbackProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static constexpr uint16 SupportedSchemaMajor = 1;
	static constexpr uint16 SupportedSchemaMinor = 0;
	static constexpr uint16 ResourceIrMinor = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	FName ProfileId = TEXT("webtoue.feedback.default");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	int32 SchemaMajor = SupportedSchemaMajor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	int32 SchemaMinor = SupportedSchemaMinor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="WebToUE|Feedback")
	TArray<FWebToUEFeedbackCueProfile> Cues;

	const FWebToUEFeedbackCueProfile* FindCue(FName CueId) const;
	const TArray<FWebToUEResourceDependency>& GetSealedResourceDependencies() const
	{
		return SealedResourceDependencies;
	}
	const FWebToUECookFreshnessStamp& GetResourceFreshness() const
	{
		return ResourceFreshness;
	}

	bool ValidateProfile(TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics) const;
	bool ValidateResourceContract(
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics) const;

#if WITH_EDITOR
	/** Rebuild the current profile/resource/package seal before saving a release candidate. */
	UFUNCTION(CallInEditor, Category="WebToUE|Feedback")
	bool RebuildResourceSeal();
	static bool ValidateCurrentCookFreshness(
		const UWebToUEFeedbackProfile& Profile,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics);
#endif

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;
	static FWebToUEFeedbackCookFreshnessValidator& CookFreshnessValidator();

private:
	UPROPERTY()
	TArray<FWebToUEResourceDependency> SealedResourceDependencies;

	UPROPERTY()
	FWebToUECookFreshnessStamp ResourceFreshness;
};
