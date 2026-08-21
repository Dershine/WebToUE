#include "WebToUEFeedbackRouter.h"

#include "Engine/AssetManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"

namespace WebToUE::FeedbackRouter::Private
{
	static constexpr int32 MaximumTraceEntries = 256;

	class FStreamableFeedbackResources final
		: public IWebToUEFeedbackResourceProvider,
		  public TSharedFromThis<FStreamableFeedbackResources>
	{
	public:
		virtual EWebToUEFeedbackResidencyRequest Request(
			TConstArrayView<FSoftObjectPath> Paths,
			TFunction<void()> Completion) override
		{
			check(IsInGameThread());
			TArray<FSoftObjectPath> Pending;
			for (const FSoftObjectPath& Path : Paths)
			{
				if (ResidentIndices.Contains(Path)) continue;
				if (UObject* Object = Path.ResolveObject())
				{
					AddResident(Path, Object);
				}
				else if (!Path.IsNull())
				{
					Pending.AddUnique(Path);
				}
			}
			if (Pending.IsEmpty())
			{
				return EWebToUEFeedbackResidencyRequest::Ready;
			}

			const TWeakPtr<FStreamableFeedbackResources> Weak = AsShared();
			TSharedPtr<FStreamableHandle> Handle =
				UAssetManager::GetStreamableManager().RequestAsyncLoad(
					Pending, FStreamableDelegate::CreateLambda(
						[Weak, Pending, Completion = MoveTemp(Completion)]() mutable
						{
							const TSharedPtr<FStreamableFeedbackResources> Self = Weak.Pin();
							if (!Self) return;
							for (const FSoftObjectPath& Path : Pending)
							{
								if (UObject* Object = Path.ResolveObject())
								{
									Self->AddResident(Path, Object);
								}
							}
							if (Completion) Completion();
						}));
			if (!Handle)
			{
				return EWebToUEFeedbackResidencyRequest::Failed;
			}
			Handles.Add(MoveTemp(Handle));
			return EWebToUEFeedbackResidencyRequest::Pending;
		}

		virtual UObject* FindResident(const FSoftObjectPath& Path) const override
		{
			check(IsInGameThread());
			const int32* Index = ResidentIndices.Find(Path);
			return Index && ResidentObjects.IsValidIndex(*Index)
				? ResidentObjects[*Index].Get() : nullptr;
		}

		virtual void CancelAll() override
		{
			check(IsInGameThread());
			for (const TSharedPtr<FStreamableHandle>& Handle : Handles)
			{
				if (Handle) Handle->CancelHandle();
			}
			Handles.Reset();
			ResidentIndices.Reset();
			ResidentObjects.Reset();
		}

	private:
		void AddResident(const FSoftObjectPath& Path, UObject* Object)
		{
			if (!Object || ResidentIndices.Contains(Path)) return;
			const int32 Index = ResidentObjects.Add(TStrongObjectPtr<UObject>(Object));
			ResidentIndices.Add(Path, Index);
		}

		TArray<TSharedPtr<FStreamableHandle>> Handles;
		TArray<TStrongObjectPtr<UObject>> ResidentObjects;
		TMap<FSoftObjectPath, int32> ResidentIndices;
	};

	static FString JoinKey(const FString& Scope, FName Policy)
	{
		return Scope + TEXT("|") + Policy.ToString();
	}
}

bool FWebToUEFixedFeedbackSettingsProvider::ResolveSettings(
	ULocalPlayer* LocalPlayer, FWebToUEFeedbackUserSettings& OutSettings) const
{
	OutSettings = Settings;
	return LocalPlayer != nullptr;
}

bool FWebToUEEngineFeedbackBackend::Play(
	const FWebToUEFeedbackPlaybackRequest& Request)
{
	check(IsInGameThread());
	UWorld* World = Request.World.Get();
	if (!World || !Request.Sound || !FMath::IsFinite(Request.VolumeMultiplier) ||
		!FMath::IsFinite(Request.PitchMultiplier))
	{
		return false;
	}
	if (Request.Mode == EWebToUEFeedbackPlaybackMode::World3D)
	{
		UGameplayStatics::PlaySoundAtLocation(World, Request.Sound,
			Request.WorldLocation, FRotator::ZeroRotator,
			Request.VolumeMultiplier, Request.PitchMultiplier, 0.0f,
			nullptr, Request.Concurrency, Request.OwningActor.Get());
		return true;
	}
	UGameplayStatics::PlaySound2D(World, Request.Sound,
		Request.VolumeMultiplier, Request.PitchMultiplier, 0.0f,
		Request.Concurrency, Request.OwningActor.Get(), true);
	return true;
}

bool FWebToUERecordingFeedbackBackend::Play(
	const FWebToUEFeedbackPlaybackRequest& Request)
{
	check(IsInGameThread());
	Records.Add(Request);
	return true;
}

TSharedRef<FWebToUEProfileFeedbackRouter> FWebToUEProfileFeedbackRouter::Create(
	UWebToUEFeedbackProfile* Profile,
	TSharedPtr<IWebToUEFeedbackBackend> Backend,
	TSharedPtr<IWebToUEFeedbackSettingsProvider> SettingsProvider,
	TSharedPtr<IWebToUEFeedbackResourceProvider> ResourceProvider)
{
	if (!Backend)
	{
		Backend = MakeShared<FWebToUEEngineFeedbackBackend>();
	}
	if (!SettingsProvider)
	{
		SettingsProvider = MakeShared<FWebToUEFixedFeedbackSettingsProvider>();
	}
	if (!ResourceProvider)
	{
		ResourceProvider =
			MakeShared<WebToUE::FeedbackRouter::Private::FStreamableFeedbackResources>();
	}
	return MakeShareable(new FWebToUEProfileFeedbackRouter(
		Profile, Backend.ToSharedRef(), SettingsProvider.ToSharedRef(),
		ResourceProvider.ToSharedRef()));
}

FWebToUEProfileFeedbackRouter::FWebToUEProfileFeedbackRouter(
	UWebToUEFeedbackProfile* InProfile,
	TSharedRef<IWebToUEFeedbackBackend> InBackend,
	TSharedRef<IWebToUEFeedbackSettingsProvider> InSettingsProvider,
	TSharedRef<IWebToUEFeedbackResourceProvider> InResourceProvider)
	: Profile(InProfile)
	, Backend(MoveTemp(InBackend))
	, SettingsProvider(MoveTemp(InSettingsProvider))
	, ResourceProvider(MoveTemp(InResourceProvider))
{
}

bool FWebToUEProfileFeedbackRouter::ActivateSession(
	const FWebToUEFeedbackRoutingContext& Context, FString& OutError)
{
	check(IsInGameThread());
	OutError.Reset();
	if (!Profile.IsValid() || !Context.Session.IsValid())
	{
		if (bActive)
		{
			ResourceProvider->CancelAll();
		}
		bActive = false;
		bCriticalReady = false;
		ResetPolicyState();
		OutError = TEXT("Feedback Router requires a Profile and active Session generation.");
		return false;
	}
	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	if (!Profile->ValidateResourceContract(Diagnostics))
	{
		OutError = Diagnostics.IsEmpty()
			? TEXT("Feedback Profile Resource Contract is invalid.")
			: FString::Printf(TEXT("%s %s: %s"), *Diagnostics[0].Code,
				*Diagnostics[0].Path, *Diagnostics[0].Detail);
		return false;
	}

	if (bActive)
	{
		ResourceProvider->CancelAll();
	}
	ResetPolicyState();
	ActiveContext = Context;
	bActive = true;
	bCriticalReady = false;

	TArray<FSoftObjectPath> Critical;
	for (const FWebToUEFeedbackCueProfile& Cue : Profile->Cues)
	{
		if (Cue.Residency != EWebToUEResidencyClass::Critical) continue;
		for (const TSoftObjectPtr<USoundBase>& Variant : Cue.Variants)
		{
			Critical.AddUnique(Variant.ToSoftObjectPath());
		}
		if (!Cue.Concurrency.IsNull())
		{
			Critical.AddUnique(Cue.Concurrency.ToSoftObjectPath());
		}
	}
	if (Critical.IsEmpty())
	{
		bCriticalReady = true;
		AddTrace(EWebToUEFeedbackTraceOutcome::CriticalReady,
			nullptr, Context.RealTimeSeconds, TEXT("no-critical-resources"));
		return true;
	}

	const FWebToUESessionHandle Expected = Context.Session;
	const TWeakPtr<FWebToUEProfileFeedbackRouter> Weak = AsShared();
	const EWebToUEFeedbackResidencyRequest Result = ResourceProvider->Request(
		Critical, [Weak, Expected]()
		{
			if (const TSharedPtr<FWebToUEProfileFeedbackRouter> Self = Weak.Pin())
			{
				Self->HandleCriticalCompletion(Expected);
			}
		});
	if (Result == EWebToUEFeedbackResidencyRequest::Ready)
	{
		bCriticalReady = true;
		AddTrace(EWebToUEFeedbackTraceOutcome::CriticalReady,
			nullptr, Context.RealTimeSeconds, TEXT("resident"));
	}
	else if (Result == EWebToUEFeedbackResidencyRequest::Pending)
	{
		AddTrace(EWebToUEFeedbackTraceOutcome::CriticalPending,
			nullptr, Context.RealTimeSeconds,
			FString::Printf(TEXT("resources=%d"), Critical.Num()));
	}
	else
	{
		bCriticalReady = true;
		AddTrace(EWebToUEFeedbackTraceOutcome::CriticalFailed,
			nullptr, Context.RealTimeSeconds, TEXT("request-failed-degraded"));
	}
	return true;
}

void FWebToUEProfileFeedbackRouter::ObserveRequestedFeedback(
	const FWebToUEFeedbackRequest& Request)
{
	check(IsInGameThread());
	AddTrace(EWebToUEFeedbackTraceOutcome::Requested, &Request,
		ActiveContext.RealTimeSeconds);
}

void FWebToUEProfileFeedbackRouter::OnSessionGenerationAdvanced(
	const FWebToUEFeedbackRoutingContext& Context)
{
	check(IsInGameThread());
	FString Error;
	ActivateSession(Context, Error);
}

void FWebToUEProfileFeedbackRouter::DeactivateSession(
	const FWebToUESessionHandle& Session)
{
	check(IsInGameThread());
	if (!bActive || Session.GetSessionId() != ActiveContext.Session.GetSessionId()) return;
	AddTrace(EWebToUEFeedbackTraceOutcome::SessionEnded,
		nullptr, ActiveContext.RealTimeSeconds);
	bActive = false;
	bCriticalReady = false;
	ResourceProvider->CancelAll();
	ResetPolicyState();
}

bool FWebToUEProfileFeedbackRouter::IsReadyForInteraction(
	const FWebToUESessionHandle& Session) const
{
	return IsInGameThread() && bActive && bCriticalReady &&
		Session == ActiveContext.Session;
}

bool FWebToUEProfileFeedbackRouter::RouteCommittedFeedback(
	const FWebToUEFeedbackRequest& Request,
	const FWebToUEFeedbackRoutingContext& Context)
{
	using namespace WebToUE::FeedbackRouter::Private;
	check(IsInGameThread());
	AddTrace(EWebToUEFeedbackTraceOutcome::Committed, &Request,
		Context.RealTimeSeconds);
	if (!bActive || !bCriticalReady || Context.Session != ActiveContext.Session)
	{
		AddTrace(EWebToUEFeedbackTraceOutcome::NotReady, &Request,
			Context.RealTimeSeconds);
		return false;
	}
	const FWebToUEFeedbackCueProfile* Cue = Profile.IsValid()
		? Profile->FindCue(Request.CueId) : nullptr;
	if (!Cue)
	{
		AddTrace(EWebToUEFeedbackTraceOutcome::MissingCue, &Request,
			Context.RealTimeSeconds);
		return false;
	}

	FWebToUEFeedbackUserSettings Settings;
	if (!SettingsProvider->ResolveSettings(Context.LocalPlayer.Get(), Settings) ||
		!FMath::IsFinite(Settings.VolumeScale) || Settings.VolumeScale < 0.0f)
	{
		AddTrace(EWebToUEFeedbackTraceOutcome::InvalidSettings, &Request,
			Context.RealTimeSeconds);
		return false;
	}
	if (Settings.bMuted || Settings.VolumeScale == 0.0f)
	{
		AddTrace(EWebToUEFeedbackTraceOutcome::Muted, &Request,
			Context.RealTimeSeconds);
		return false;
	}

	const FString ScopeKey = MakeScopeKey(Request, Context);
	const FString DedupeKey = JoinKey(ScopeKey, Cue->DeduplicationGroup);
	if (!Cue->DeduplicationGroup.IsNone())
	{
		if (const FRecentDeduplication* Recent = RecentDeduplication.Find(DedupeKey);
			Recent && Recent->CorrelationId == Request.EventCorrelationId &&
			Recent->Modality == Request.InputModality &&
			Context.RealTimeSeconds - Recent->TimeSeconds <= Cue->DeduplicationWindowSeconds)
		{
			AddTrace(EWebToUEFeedbackTraceOutcome::Deduplicated, &Request,
				Context.RealTimeSeconds, Cue->DeduplicationGroup.ToString());
			return false;
		}
	}
	const FString CueKey = JoinKey(ScopeKey, Cue->CueId);
	if (const double* LastTime = LastCueTimes.Find(CueKey);
		Cue->CooldownSeconds > 0.0 && LastTime &&
		Context.RealTimeSeconds - *LastTime < Cue->CooldownSeconds)
	{
		AddTrace(EWebToUEFeedbackTraceOutcome::Cooldown, &Request,
			Context.RealTimeSeconds);
		return false;
	}
	TArray<double>* ExistingThrottle = nullptr;
	FString ThrottleKey;
	if (!Cue->ThrottleGroup.IsNone())
	{
		ThrottleKey = JoinKey(ScopeKey, Cue->ThrottleGroup);
		ExistingThrottle = &ThrottleTimes.FindOrAdd(ThrottleKey);
		ExistingThrottle->RemoveAll([&](double Time)
		{
			return Context.RealTimeSeconds - Time >= Cue->ThrottleWindowSeconds;
		});
		if (ExistingThrottle->Num() >= Cue->ThrottleMaximum)
		{
			AddTrace(EWebToUEFeedbackTraceOutcome::Throttled, &Request,
				Context.RealTimeSeconds, Cue->ThrottleGroup.ToString());
			return false;
		}
	}

	const int32 VariantIndex = static_cast<int32>(
		Request.EventCorrelationId % static_cast<uint64>(Cue->Variants.Num()));
	const FSoftObjectPath SoundPath = Cue->Variants[VariantIndex].ToSoftObjectPath();
	USoundBase* Sound = Cast<USoundBase>(ResourceProvider->FindResident(SoundPath));
	USoundConcurrency* Concurrency = nullptr;
	TArray<FSoftObjectPath> Missing;
	if (!Sound) Missing.Add(SoundPath);
	if (!Cue->Concurrency.IsNull())
	{
		const FSoftObjectPath ConcurrencyPath = Cue->Concurrency.ToSoftObjectPath();
		Concurrency = Cast<USoundConcurrency>(ResourceProvider->FindResident(ConcurrencyPath));
		if (!Concurrency) Missing.Add(ConcurrencyPath);
	}
	if (!Missing.IsEmpty())
	{
		RequestOnDemand(MoveTemp(Missing));
		AddTrace(EWebToUEFeedbackTraceOutcome::MissingResource, &Request,
			Context.RealTimeSeconds, SoundPath.ToString());
		return false;
	}

	FWebToUEFeedbackPlaybackRequest Playback;
	Playback.Session = Request.Session;
	Playback.CueId = Request.CueId;
	Playback.ProjectRouteId = Cue->ProjectRouteId;
	Playback.Scope = Request.Scope;
	Playback.Sound = Sound;
	Playback.Concurrency = Concurrency;
	Playback.World = Context.World;
	Playback.OwningActor = Cast<AActor>(Context.Surface.Owner.Get());
	Playback.VolumeMultiplier = Cue->VolumeMultiplier * Settings.VolumeScale;
	Playback.PitchMultiplier = Cue->PitchMultiplier;
	if (Context.Surface.Kind == EWebToUESurfaceKind::Screen)
	{
		Playback.Mode = EWebToUEFeedbackPlaybackMode::Screen2D;
	}
	else if (Cue->WorldPolicy == EWebToUEFeedbackWorldPolicy::TwoDimensional)
	{
		Playback.Mode = EWebToUEFeedbackPlaybackMode::World2D;
	}
	else if (Cue->WorldPolicy == EWebToUEFeedbackWorldPolicy::OwnerLocation3D &&
		Context.Surface.bHasFeedbackWorldLocation)
	{
		Playback.Mode = EWebToUEFeedbackPlaybackMode::World3D;
		Playback.WorldLocation = Context.Surface.FeedbackWorldLocation;
	}
	else
	{
		AddTrace(EWebToUEFeedbackTraceOutcome::ScopeDropped, &Request,
			Context.RealTimeSeconds);
		return false;
	}

	if (!Backend->Play(Playback))
	{
		AddTrace(EWebToUEFeedbackTraceOutcome::BackendRejected, &Request,
			Context.RealTimeSeconds);
		return false;
	}
	if (!Cue->DeduplicationGroup.IsNone())
	{
		RecentDeduplication.Add(DedupeKey, {
			Request.EventCorrelationId, Request.InputModality, Context.RealTimeSeconds });
	}
	LastCueTimes.Add(CueKey, Context.RealTimeSeconds);
	if (ExistingThrottle)
	{
		ExistingThrottle->Add(Context.RealTimeSeconds);
	}
	AddTrace(EWebToUEFeedbackTraceOutcome::Routed, &Request,
		Context.RealTimeSeconds, SoundPath.ToString());
	return true;
}

void FWebToUEProfileFeedbackRouter::AddTrace(
	EWebToUEFeedbackTraceOutcome Outcome,
	const FWebToUEFeedbackRequest* Request,
	double TimeSeconds,
	FString Detail)
{
	if (Trace.Num() == WebToUE::FeedbackRouter::Private::MaximumTraceEntries)
	{
		Trace.RemoveAt(0, 1, EAllowShrinking::No);
	}
	FWebToUEFeedbackTrace& Entry = Trace.AddDefaulted_GetRef();
	Entry.Outcome = Outcome;
	Entry.Session = Request ? Request->Session : ActiveContext.Session;
	Entry.CueId = Request ? Request->CueId : NAME_None;
	Entry.EventCorrelationId = Request ? Request->EventCorrelationId : 0;
	Entry.TimeSeconds = TimeSeconds;
	Entry.Detail = MoveTemp(Detail);
}

void FWebToUEProfileFeedbackRouter::ResetPolicyState()
{
	PendingOnDemand.Reset();
	RecentDeduplication.Reset();
	LastCueTimes.Reset();
	ThrottleTimes.Reset();
}

void FWebToUEProfileFeedbackRouter::HandleCriticalCompletion(
	FWebToUESessionHandle ExpectedSession)
{
	check(IsInGameThread());
	if (!bActive || ExpectedSession != ActiveContext.Session) return;
	bCriticalReady = true;
	AddTrace(EWebToUEFeedbackTraceOutcome::CriticalReady,
		nullptr, ActiveContext.RealTimeSeconds, TEXT("async-complete"));
}

void FWebToUEProfileFeedbackRouter::HandleOnDemandCompletion(
	FWebToUESessionHandle ExpectedSession, TArray<FSoftObjectPath> Paths)
{
	check(IsInGameThread());
	if (!bActive || ExpectedSession != ActiveContext.Session) return;
	for (const FSoftObjectPath& Path : Paths)
	{
		PendingOnDemand.Remove(Path);
	}
}

void FWebToUEProfileFeedbackRouter::RequestOnDemand(TArray<FSoftObjectPath> Paths)
{
	Paths.RemoveAll([this](const FSoftObjectPath& Path)
	{
		return Path.IsNull() || PendingOnDemand.Contains(Path) ||
			ResourceProvider->FindResident(Path) != nullptr;
	});
	if (Paths.IsEmpty()) return;
	for (const FSoftObjectPath& Path : Paths) PendingOnDemand.Add(Path);
	const FWebToUESessionHandle Expected = ActiveContext.Session;
	const TWeakPtr<FWebToUEProfileFeedbackRouter> Weak = AsShared();
	const EWebToUEFeedbackResidencyRequest Result = ResourceProvider->Request(
		Paths, [Weak, Expected, Paths]() mutable
		{
			if (const TSharedPtr<FWebToUEProfileFeedbackRouter> Self = Weak.Pin())
			{
				Self->HandleOnDemandCompletion(Expected, MoveTemp(Paths));
			}
		});
	if (Result != EWebToUEFeedbackResidencyRequest::Pending)
	{
		for (const FSoftObjectPath& Path : Paths) PendingOnDemand.Remove(Path);
	}
}

FString FWebToUEProfileFeedbackRouter::MakeScopeKey(
	const FWebToUEFeedbackRequest& Request,
	const FWebToUEFeedbackRoutingContext& Context) const
{
	switch (Request.Scope)
	{
	case EWebToUEFeedbackScope::Session:
		return FString::Printf(TEXT("session/%llu"),
			static_cast<unsigned long long>(Request.Session.GetSessionId()));
	case EWebToUEFeedbackScope::LocalPlayer:
		return FString::Printf(TEXT("local-player/%llu/%u"),
			static_cast<unsigned long long>(Request.Session.GetSessionId()),
			Context.LocalPlayer.IsValid() ? Context.LocalPlayer->GetUniqueID() : 0);
	case EWebToUEFeedbackScope::Viewport:
		return TEXT("viewport/global");
	case EWebToUEFeedbackScope::Surface:
		return TEXT("surface/") + Context.Surface.SurfaceId.ToString();
	default:
		return TEXT("invalid");
	}
}
