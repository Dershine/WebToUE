#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "WebToUEFeedbackProfile.h"
#include "WebToUESession.h"

class AActor;
struct FStreamableHandle;
class USoundBase;
class USoundConcurrency;

struct WEBTOUERUNTIME_API FWebToUEFeedbackUserSettings
{
	bool bMuted = false;
	float VolumeScale = 1.0f;
};

class WEBTOUERUNTIME_API IWebToUEFeedbackSettingsProvider
{
public:
	virtual ~IWebToUEFeedbackSettingsProvider() = default;
	virtual bool ResolveSettings(
		ULocalPlayer* LocalPlayer, FWebToUEFeedbackUserSettings& OutSettings) const = 0;
};

class WEBTOUERUNTIME_API FWebToUEFixedFeedbackSettingsProvider final
	: public IWebToUEFeedbackSettingsProvider
{
public:
	explicit FWebToUEFixedFeedbackSettingsProvider(
		FWebToUEFeedbackUserSettings InSettings = {}) : Settings(InSettings) {}
	virtual bool ResolveSettings(
		ULocalPlayer* LocalPlayer, FWebToUEFeedbackUserSettings& OutSettings) const override;
	void SetSettings(FWebToUEFeedbackUserSettings InSettings) { Settings = InSettings; }

private:
	FWebToUEFeedbackUserSettings Settings;
};

enum class EWebToUEFeedbackResidencyRequest : uint8
{
	Ready,
	Pending,
	Failed
};

/** Injectable async-only residency boundary. Production uses FStreamableManager. */
class WEBTOUERUNTIME_API IWebToUEFeedbackResourceProvider
{
public:
	virtual ~IWebToUEFeedbackResourceProvider() = default;
	virtual EWebToUEFeedbackResidencyRequest Request(
		TConstArrayView<FSoftObjectPath> Paths,
		TFunction<void()> Completion) = 0;
	virtual UObject* FindResident(const FSoftObjectPath& Path) const = 0;
	virtual void CancelAll() = 0;
};

enum class EWebToUEFeedbackPlaybackMode : uint8
{
	Screen2D,
	World2D,
	World3D
};

struct WEBTOUERUNTIME_API FWebToUEFeedbackPlaybackRequest
{
	FWebToUESessionHandle Session;
	FName CueId;
	FName ProjectRouteId;
	EWebToUEFeedbackScope Scope = EWebToUEFeedbackScope::Session;
	EWebToUEFeedbackPlaybackMode Mode = EWebToUEFeedbackPlaybackMode::Screen2D;
	TObjectPtr<USoundBase> Sound = nullptr;
	TObjectPtr<USoundConcurrency> Concurrency = nullptr;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AActor> OwningActor;
	FVector WorldLocation = FVector::ZeroVector;
	float VolumeMultiplier = 1.0f;
	float PitchMultiplier = 1.0f;
};

class WEBTOUERUNTIME_API IWebToUEFeedbackBackend
{
public:
	virtual ~IWebToUEFeedbackBackend() = default;
	virtual bool Play(const FWebToUEFeedbackPlaybackRequest& Request) = 0;
};

/** Default UE native backend. Sound assets retain their SoundClass/Submix policy. */
class WEBTOUERUNTIME_API FWebToUEEngineFeedbackBackend final
	: public IWebToUEFeedbackBackend
{
public:
	virtual bool Play(const FWebToUEFeedbackPlaybackRequest& Request) override;
	int32 GetAttemptCount() const { return AttemptCount; }
	int32 GetSuccessfulPlayCount() const { return SuccessfulPlayCount; }
	const TOptional<FWebToUEFeedbackPlaybackRequest>& GetLastSuccessfulRequest() const
	{
		return LastSuccessfulRequest;
	}

private:
	int32 AttemptCount = 0;
	int32 SuccessfulPlayCount = 0;
	TOptional<FWebToUEFeedbackPlaybackRequest> LastSuccessfulRequest;
};

class WEBTOUERUNTIME_API FWebToUERecordingFeedbackBackend final
	: public IWebToUEFeedbackBackend
{
public:
	virtual bool Play(const FWebToUEFeedbackPlaybackRequest& Request) override;
	TConstArrayView<FWebToUEFeedbackPlaybackRequest> GetRecords() const { return Records; }
	void Reset() { Records.Reset(); }

private:
	TArray<FWebToUEFeedbackPlaybackRequest> Records;
};

enum class EWebToUEFeedbackTraceOutcome : uint8
{
	Requested,
	Committed,
	CriticalPending,
	CriticalReady,
	CriticalFailed,
	NotReady,
	Routed,
	Deduplicated,
	Cooldown,
	Throttled,
	MissingCue,
	MissingResource,
	Muted,
	InvalidSettings,
	ScopeDropped,
	BackendRejected,
	SessionEnded
};

struct WEBTOUERUNTIME_API FWebToUEFeedbackTrace
{
	EWebToUEFeedbackTraceOutcome Outcome = EWebToUEFeedbackTraceOutcome::Requested;
	FWebToUESessionHandle Session;
	FName CueId;
	uint64 EventCorrelationId = 0;
	double TimeSeconds = 0.0;
	FString Detail;
};

/**
 * Session-scoped Profile policy executor. It never performs a synchronous load and never stores
 * Sound paths in Behavior/UI Command data.
 */
class WEBTOUERUNTIME_API FWebToUEProfileFeedbackRouter final
	: public IWebToUEFeedbackRouter,
	  public TSharedFromThis<FWebToUEProfileFeedbackRouter>
{
public:
	static TSharedRef<FWebToUEProfileFeedbackRouter> Create(
		UWebToUEFeedbackProfile* Profile,
		TSharedPtr<IWebToUEFeedbackBackend> Backend = nullptr,
		TSharedPtr<IWebToUEFeedbackSettingsProvider> SettingsProvider = nullptr,
		TSharedPtr<IWebToUEFeedbackResourceProvider> ResourceProvider = nullptr);

	virtual bool ActivateSession(
		const FWebToUEFeedbackRoutingContext& Context, FString& OutError) override;
	virtual void ObserveRequestedFeedback(
		const FWebToUEFeedbackRequest& Request) override;
	virtual void OnSessionGenerationAdvanced(
		const FWebToUEFeedbackRoutingContext& Context) override;
	virtual void DeactivateSession(const FWebToUESessionHandle& Session) override;
	virtual bool IsReadyForInteraction(
		const FWebToUESessionHandle& Session) const override;
	virtual bool RouteCommittedFeedback(
		const FWebToUEFeedbackRequest& Request,
		const FWebToUEFeedbackRoutingContext& Context) override;

	TConstArrayView<FWebToUEFeedbackTrace> GetTrace() const { return Trace; }
	int32 GetPendingOnDemandCount() const { return PendingOnDemand.Num(); }

private:
	FWebToUEProfileFeedbackRouter(
		UWebToUEFeedbackProfile* InProfile,
		TSharedRef<IWebToUEFeedbackBackend> InBackend,
		TSharedRef<IWebToUEFeedbackSettingsProvider> InSettingsProvider,
		TSharedRef<IWebToUEFeedbackResourceProvider> InResourceProvider);

	struct FRecentDeduplication
	{
		uint64 CorrelationId = 0;
		EWebToUEInputModality Modality = EWebToUEInputModality::Unknown;
		double TimeSeconds = 0.0;
	};

	void AddTrace(EWebToUEFeedbackTraceOutcome Outcome,
		const FWebToUEFeedbackRequest* Request, double TimeSeconds, FString Detail = {});
	void ResetPolicyState();
	void HandleCriticalCompletion(FWebToUESessionHandle ExpectedSession);
	void HandleOnDemandCompletion(
		FWebToUESessionHandle ExpectedSession, TArray<FSoftObjectPath> Paths);
	void RequestOnDemand(TArray<FSoftObjectPath> Paths);
	FString MakeScopeKey(const FWebToUEFeedbackRequest& Request,
		const FWebToUEFeedbackRoutingContext& Context) const;

	TStrongObjectPtr<UWebToUEFeedbackProfile> Profile;
	TSharedRef<IWebToUEFeedbackBackend> Backend;
	TSharedRef<IWebToUEFeedbackSettingsProvider> SettingsProvider;
	TSharedRef<IWebToUEFeedbackResourceProvider> ResourceProvider;
	FWebToUEFeedbackRoutingContext ActiveContext;
	bool bActive = false;
	bool bCriticalReady = false;
	TSet<FSoftObjectPath> PendingOnDemand;
	TMap<FString, FRecentDeduplication> RecentDeduplication;
	TMap<FString, double> LastCueTimes;
	TMap<FString, TArray<double>> ThrottleTimes;
	TArray<FWebToUEFeedbackTrace> Trace;
};
