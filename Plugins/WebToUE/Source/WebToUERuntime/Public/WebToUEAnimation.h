#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "WebToUEClock.h"
#include "WebToUEIdentity.h"
#include "WebToUEPropertyOwnership.h"
#include "WebToUEResourceContract.h"
#include "WebToUEUpdateTransaction.h"
#include "WebToUEAnimation.generated.h"

UENUM()
enum class EWebToUEAnimationValueType : uint8
{
	Invalid,
	Scalar,
	Color,
	Transform
};

USTRUCT()
struct WEBTOUERUNTIME_API FWebToUEAnimationValue
{
	GENERATED_BODY()
	UPROPERTY() EWebToUEAnimationValueType Type = EWebToUEAnimationValueType::Invalid;
	UPROPERTY() float Scalar = 0.0f;
	UPROPERTY() FLinearColor Color = FLinearColor::Transparent;
	UPROPERTY() FWebToUEVisualTransformValue Transform;

	static FWebToUEAnimationValue MakeScalar(float InValue);
	static FWebToUEAnimationValue MakeColor(const FLinearColor& InValue);
	static FWebToUEAnimationValue MakeTransform(const FWebToUEVisualTransformValue& InValue);
	static FWebToUEAnimationValue Interpolate(
		const FWebToUEAnimationValue& From,
		const FWebToUEAnimationValue& To,
		float Alpha);
	bool IsFinite() const;
	bool IsCompatibleWith(const FWebToUEPropertyAddress& Address) const;
};

UENUM()
enum class EWebToUECompiledAnimationTargetKind : uint8
{
	Invalid,
	Opacity,
	Color,
	BackgroundColor,
	BorderColor,
	VisualTransform,
	MaterialScalar,
	MaterialVector
};

USTRUCT()
struct WEBTOUERUNTIME_API FWebToUECompiledAnimationTarget
{
	GENERATED_BODY()
	UPROPERTY() int32 TargetNodeIndex = INDEX_NONE;
	UPROPERTY() EWebToUECompiledAnimationTargetKind Kind =
		EWebToUECompiledAnimationTargetKind::Invalid;
	UPROPERTY() FName MaterialParameter;

	FWebToUEPropertyAddress ToPropertyAddress() const;
	EWebToUEAnimationValueType GetValueType() const;
};

USTRUCT()
struct WEBTOUERUNTIME_API FWebToUECompiledAnimationTrack
{
	GENERATED_BODY()
	UPROPERTY() FName TrackId;
	UPROPERTY() FWebToUECompiledAnimationTarget Target;
	UPROPERTY() FWebToUEAnimationValue From;
	UPROPERTY() FWebToUEAnimationValue To;
	UPROPERTY() double DurationSeconds = 0.0;
	UPROPERTY() EWebToUEClockDomain ClockDomain = EWebToUEClockDomain::Game;
};

UENUM()
enum class EWebToUETransitionReverseMode : uint8
{
	RetargetFromCurrent
};

UENUM()
enum class EWebToUETransitionFillMode : uint8
{
	UnderlyingAfterCompletion
};

USTRUCT()
struct WEBTOUERUNTIME_API FWebToUECompiledTransition
{
	GENERATED_BODY()
	UPROPERTY() FName TransitionId;
	UPROPERTY() FWebToUECompiledAnimationTarget Target;
	UPROPERTY() double DurationSeconds = 0.0;
	UPROPERTY() double DelaySeconds = 0.0;
	UPROPERTY() EWebToUETransitionEasing Easing = EWebToUETransitionEasing::Ease;
	UPROPERTY() EWebToUETransitionReverseMode ReverseMode =
		EWebToUETransitionReverseMode::RetargetFromCurrent;
	UPROPERTY() EWebToUETransitionFillMode FillMode =
		EWebToUETransitionFillMode::UnderlyingAfterCompletion;
	UPROPERTY() EWebToUEClockDomain ClockDomain = EWebToUEClockDomain::Game;
};

struct WEBTOUERUNTIME_API FWebToUEAnimationDiagnostic
{
	FString Code;
	FString Path;
	FString Detail;
};

USTRUCT()
struct WEBTOUERUNTIME_API FWebToUECompiledAnimationIR
{
	GENERATED_BODY()
	static constexpr uint16 CurrentMajor = 1;
	static constexpr uint16 CurrentMinor = 1;
	UPROPERTY() FWebToUEArtifactLayerVersion Version{ CurrentMajor, CurrentMinor };
	UPROPERTY() TArray<FWebToUECompiledAnimationTrack> Tracks;
	UPROPERTY() TArray<FWebToUECompiledTransition> Transitions;

	bool Validate(int32 CompiledNodeCount,
		TArray<FWebToUEAnimationDiagnostic>& OutDiagnostics) const;
};

struct WEBTOUERUNTIME_API FWebToUEAnimationOwnerHandle
{
	uint64 SessionId = 0;
	uint32 Generation = 0;
	bool IsValid() const { return SessionId != 0 && Generation != 0; }
	friend bool operator==(const FWebToUEAnimationOwnerHandle& A,
		const FWebToUEAnimationOwnerHandle& B) = default;
};

struct WEBTOUERUNTIME_API FWebToUEAnimationTrackHandle
{
	FWebToUEAnimationOwnerHandle Owner;
	uint64 TrackId = 0;
	bool IsValid() const { return Owner.IsValid() && TrackId != 0; }
	friend bool operator==(const FWebToUEAnimationTrackHandle& A,
		const FWebToUEAnimationTrackHandle& B) = default;
};

class WEBTOUERUNTIME_API IWebToUEAnimationTarget
{
public:
	virtual ~IWebToUEAnimationTarget() = default;
	virtual bool ValidateAnimationTarget(
		FWebToUEInstanceHandle Target,
		const FWebToUEPropertyAddress& Address,
		EWebToUEAnimationValueType ValueType,
		FString& OutError) const = 0;
	virtual void ApplyAnimationOverlay(
		FWebToUEInstanceHandle Target,
		const FWebToUEPropertyAddress& Address,
		const FWebToUEAnimationValue& Value) = 0;
	virtual void ReleaseAnimationOverlay(
		FWebToUEInstanceHandle Target,
		const FWebToUEPropertyAddress& Address) = 0;
};

enum class EWebToUEAnimationConflictPolicy : uint8
{
	Reject,
	Retarget,
	Replace
};

struct WEBTOUERUNTIME_API FWebToUEAnimationTrackRequest
{
	FName DebugName;
	FWebToUEInstanceHandle Target;
	FWebToUEPropertyAddress Address;
	FWebToUEAnimationValue From;
	FWebToUEAnimationValue To;
	double DurationSeconds = 0.0;
	EWebToUEClockDomain ClockDomain = EWebToUEClockDomain::Game;
	TWeakPtr<IWebToUEAnimationTarget> TargetAdapter;
};

enum class EWebToUEAnimationStartResult : uint8
{
	Started,
	Retargeted,
	Replaced,
	Queued,
	RejectedInvalidRequest,
	RejectedConflict,
	RejectedTarget,
	RejectedBudget,
	RejectedInactive
};

struct WEBTOUERUNTIME_API FWebToUEAnimationStartOutcome
{
	EWebToUEAnimationStartResult Result = EWebToUEAnimationStartResult::RejectedInactive;
	FWebToUEAnimationTrackHandle Handle;
	FString Diagnostic;
	bool IsAccepted() const
	{
		return Result == EWebToUEAnimationStartResult::Started ||
			Result == EWebToUEAnimationStartResult::Retargeted ||
			Result == EWebToUEAnimationStartResult::Replaced ||
			Result == EWebToUEAnimationStartResult::Queued;
	}
};

enum class EWebToUEAnimationCancelResult : uint8
{
	Cancelled,
	Queued,
	DroppedSuperseded,
	DroppedWrongSession,
	DroppedStaleGeneration,
	RejectedInvalidHandle,
	RejectedInactive
};

enum class EWebToUEAnimationTraceOutcome : uint8
{
	Started,
	Retargeted,
	Replaced,
	Sampled,
	Completed,
	CancelledExplicit,
	CancelledGeneration,
	CancelledSession,
	DroppedTarget,
	Rejected
};

struct WEBTOUERUNTIME_API FWebToUEAnimationTrace
{
	FWebToUEAnimationTrackHandle Handle;
	FName DebugName;
	FString Address;
	EWebToUEAnimationTraceOutcome Outcome = EWebToUEAnimationTraceOutcome::Rejected;
	double TimeSeconds = 0.0;
	float Alpha = 0.0f;
	FString Diagnostic;
};

struct WEBTOUERUNTIME_API FWebToUEAnimationBudget
{
	int32 MaxActiveTracks = 256;
	int32 MaxSamplesPerPump = 256;
	int32 MaxTraceEntries = 256;
	bool Validate(FString& OutError) const;
};

class WEBTOUERUNTIME_API FWebToUEAnimationCoordinator final
	: public TSharedFromThis<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe>
{
public:
	static TSharedRef<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe> Create(
		FWebToUEAnimationOwnerHandle Owner,
		TSharedRef<IWebToUEClock> Clock,
		TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Updates,
		const FWebToUEAnimationBudget& Budget = FWebToUEAnimationBudget());
	~FWebToUEAnimationCoordinator();

	FWebToUEAnimationStartOutcome StartTrack(
		const FWebToUEAnimationTrackRequest& Request,
		EWebToUEAnimationConflictPolicy ConflictPolicy);
	EWebToUEAnimationCancelResult Cancel(FWebToUEAnimationTrackHandle Handle);
	int32 Pump();
	void AdvanceGeneration(FWebToUEAnimationOwnerHandle NewOwner);
	void Shutdown();

	bool IsActive() const { return bActive; }
	int32 GetActiveTrackCount() const { return ActiveTracks.Num(); }
	bool IsTickerRegistered() const;
	uint64 GetTickerInvocationCount() const { return TickerInvocationCount; }
	TConstArrayView<FWebToUEAnimationTrace> GetTrace() const { return Trace; }

private:
	struct FTargetKey
	{
		FWebToUEInstanceHandle Target;
		FWebToUEPropertyAddress Address;
		friend bool operator==(const FTargetKey& A, const FTargetKey& B) = default;
		friend uint32 GetTypeHash(const FTargetKey& Key)
		{
			return HashCombineFast(GetTypeHash(Key.Target), GetTypeHash(Key.Address));
		}
	};

	struct FActiveTrack
	{
		FWebToUEAnimationTrackHandle Handle;
		FName DebugName;
		FTargetKey Key;
		FWebToUEAnimationValue From;
		FWebToUEAnimationValue To;
		double StartTimeSeconds = 0.0;
		double DurationSeconds = 0.0;
		EWebToUEClockDomain ClockDomain = EWebToUEClockDomain::Game;
		TWeakPtr<IWebToUEAnimationTarget> TargetAdapter;
	};

	FWebToUEAnimationCoordinator(
		FWebToUEAnimationOwnerHandle InOwner,
		TSharedRef<IWebToUEClock> InClock,
		TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> InUpdates,
		const FWebToUEAnimationBudget& InBudget);
	bool Tick(float DeltaSeconds);
	void EnsureTicker();
	void RemoveTicker();
	void CancelAll(EWebToUEAnimationTraceOutcome Outcome);
	void AddTrace(const FActiveTrack& Track,
		EWebToUEAnimationTraceOutcome Outcome,
		double TimeSeconds,
		float Alpha,
		FString Diagnostic = {});
	static FWebToUEAnimationValue Sample(
		const FActiveTrack& Track, double TimeSeconds, float& OutAlpha);

	FWebToUEAnimationOwnerHandle Owner;
	TSharedRef<IWebToUEClock> Clock;
	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Updates;
	FWebToUEAnimationBudget Budget;
	TMap<FTargetKey, FActiveTrack> ActiveTracks;
	TArray<FWebToUEAnimationTrace> Trace;
	FTSTicker::FDelegateHandle TickerHandle;
	uint64 NextTrackId = 1;
	uint64 TickerInvocationCount = 0;
	bool bActive = true;
};
