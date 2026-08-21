#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEFeedbackRouter.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundBase.h"
#include "WebToUEClock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEFeedbackRouterPolicyTest,
	"WebToUE.Runtime.FeedbackRouterPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEFeedbackRouterScopeLifecycleTest,
	"WebToUE.Runtime.FeedbackRouterScopeLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEFeedbackRouterLocalPlayerTest,
	"WebToUE.Runtime.FeedbackRouterLocalPlayerIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::FeedbackRouter::Tests
{
	class FManualResourceProvider final : public IWebToUEFeedbackResourceProvider
	{
	public:
		virtual EWebToUEFeedbackResidencyRequest Request(
			TConstArrayView<FSoftObjectPath> Paths,
			TFunction<void()> Completion) override
		{
			++RequestCount;
			Requests.Add(TArray<FSoftObjectPath>(Paths));
			if (bFailRequests)
			{
				return EWebToUEFeedbackResidencyRequest::Failed;
			}
			bool bReady = true;
			for (const FSoftObjectPath& Path : Paths)
			{
				bReady &= Residents.Contains(Path);
			}
			if (bReady)
			{
				return EWebToUEFeedbackResidencyRequest::Ready;
			}
			PendingCompletions.Add(MoveTemp(Completion));
			return EWebToUEFeedbackResidencyRequest::Pending;
		}

		virtual UObject* FindResident(const FSoftObjectPath& Path) const override
		{
			if (UObject* const* Object = Residents.Find(Path)) return *Object;
			return nullptr;
		}

		virtual void CancelAll() override
		{
			++CancelCount;
			PendingCompletions.Reset();
			Residents.Reset();
		}

		void AddResident(const FSoftObjectPath& Path, UObject* Object)
		{
			Residents.Add(Path, Object);
		}
		void RemoveResident(const FSoftObjectPath& Path) { Residents.Remove(Path); }
		void CompleteAll()
		{
			TArray<TFunction<void()>> Completions = MoveTemp(PendingCompletions);
			for (TFunction<void()>& Completion : Completions)
			{
				if (Completion) Completion();
			}
		}

		int32 RequestCount = 0;
		int32 CancelCount = 0;
		int32 SynchronousLoadCount = 0;
		bool bFailRequests = false;
		TArray<TArray<FSoftObjectPath>> Requests;

	private:
		TMap<FSoftObjectPath, UObject*> Residents;
		TArray<TFunction<void()>> PendingCompletions;
	};

	struct FContextObjects
	{
		UWorld* World = NewObject<UWorld>(GetTransientPackage());
		ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
		ULocalPlayer* SecondLocalPlayer = NewObject<ULocalPlayer>(GEngine);

		FContextObjects()
		{
			World->AddToRoot();
			LocalPlayer->AddToRoot();
			SecondLocalPlayer->AddToRoot();
		}
		~FContextObjects()
		{
			SecondLocalPlayer->RemoveFromRoot();
			LocalPlayer->RemoveFromRoot();
			World->RemoveFromRoot();
		}
	};

	class FPerPlayerSettingsProvider final : public IWebToUEFeedbackSettingsProvider
	{
	public:
		explicit FPerPlayerSettingsProvider(ULocalPlayer* InMutedPlayer)
			: MutedPlayer(InMutedPlayer) {}

		virtual bool ResolveSettings(ULocalPlayer* LocalPlayer,
			FWebToUEFeedbackUserSettings& OutSettings) const override
		{
			OutSettings.bMuted = LocalPlayer == MutedPlayer.Get();
			OutSettings.VolumeScale = 0.8f;
			return LocalPlayer != nullptr;
		}

	private:
		TWeakObjectPtr<ULocalPlayer> MutedPlayer;
	};

	static UWebToUEFeedbackProfile* LoadFixture()
	{
		return LoadObject<UWebToUEFeedbackProfile>(nullptr,
			TEXT("/Game/WebToUEExamples/Audio/DA_WTUE_FeedbackProfile.DA_WTUE_FeedbackProfile"));
	}

	static void AddAllResources(UWebToUEFeedbackProfile& Profile,
		FManualResourceProvider& Resources)
	{
		for (const FWebToUEFeedbackCueProfile& Cue : Profile.Cues)
		{
			for (const TSoftObjectPtr<USoundBase>& Variant : Cue.Variants)
			{
				Resources.AddResident(Variant.ToSoftObjectPath(), Variant.LoadSynchronous());
			}
			if (!Cue.Concurrency.IsNull())
			{
				Resources.AddResident(Cue.Concurrency.ToSoftObjectPath(),
					Cue.Concurrency.LoadSynchronous());
			}
		}
	}

	static FWebToUESessionCreateParams MakeSessionParams(
		FContextObjects& Objects,
		const TSharedPtr<IWebToUEFeedbackRouter>& Router,
		const TSharedRef<FWebToUEVirtualClock>& Clock,
		EWebToUESurfaceKind SurfaceKind = EWebToUESurfaceKind::Screen)
	{
		FWebToUESessionCreateParams Params;
		Params.LocalPlayer = Objects.LocalPlayer;
		Params.World = Objects.World;
		Params.Surface.Kind = SurfaceKind;
		Params.Surface.SurfaceId = SurfaceKind == EWebToUESurfaceKind::Screen
			? TEXT("webtoue.tests.screen") : TEXT("webtoue.tests.world");
		Params.Surface.Owner = Objects.LocalPlayer;
		Params.Surface.bHasFeedbackWorldLocation =
			SurfaceKind == EWebToUESurfaceKind::World;
		Params.Surface.FeedbackWorldLocation = FVector(10.0, 20.0, 30.0);
		Params.Environment.DpiScale = 1.0f;
		Params.Clock = Clock;
		Params.FeedbackRouter = Router;
		return Params;
	}
}

bool FWebToUEFeedbackRouterPolicyTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::FeedbackRouter::Tests;
	UWebToUEFeedbackProfile* Profile = LoadFixture();
	if (!TestNotNull(TEXT("The Feedback policy fixture loads"), Profile)) return false;
	Profile = DuplicateObject<UWebToUEFeedbackProfile>(Profile, GetTransientPackage());
	FWebToUEFeedbackCueProfile* Confirm = Profile->Cues.FindByPredicate(
		[](const FWebToUEFeedbackCueProfile& Cue)
		{
			return Cue.CueId == TEXT("webtoue.feedback.confirm");
		});
	FWebToUEFeedbackCueProfile* CancelVariant = Profile->Cues.FindByPredicate(
		[](const FWebToUEFeedbackCueProfile& Cue)
		{
			return Cue.CueId == TEXT("webtoue.feedback.cancel");
		});
	FWebToUEFeedbackCueProfile* Navigate = Profile->Cues.FindByPredicate(
		[](const FWebToUEFeedbackCueProfile& Cue)
		{
			return Cue.CueId == TEXT("webtoue.feedback.navigate");
		});
	if (!TestNotNull(TEXT("The Profile contains Confirm"), Confirm) ||
		!TestNotNull(TEXT("The Profile contains Cancel"), CancelVariant) ||
		!TestNotNull(TEXT("The Profile contains Navigate"), Navigate))
	{
		return false;
	}
	Confirm->Variants.Add(CancelVariant->Variants[0]);
	Navigate->Residency = EWebToUEResidencyClass::Visible;
	TestTrue(TEXT("The transient multi-variant Profile reseals"),
		Profile->RebuildResourceSeal());
	FContextObjects Objects;
	const TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
	FString Error;
	Clock->SetTimeSeconds(EWebToUEClockDomain::Real, 10.0, Error);
	const TSharedRef<FWebToUERecordingFeedbackBackend> Backend =
		MakeShared<FWebToUERecordingFeedbackBackend>();
	FWebToUEFeedbackUserSettings UserSettings;
	UserSettings.VolumeScale = 0.5f;
	const TSharedRef<FWebToUEFixedFeedbackSettingsProvider> Settings =
		MakeShared<FWebToUEFixedFeedbackSettingsProvider>(UserSettings);
	const TSharedRef<FManualResourceProvider> Resources =
		MakeShared<FManualResourceProvider>();
	const TSharedRef<FWebToUEProfileFeedbackRouter> Router =
		FWebToUEProfileFeedbackRouter::Create(Profile, Backend, Settings, Resources);
	TSharedPtr<FWebToUESession> Session = FWebToUESession::Create(
		MakeSessionParams(Objects, Router, Clock), Error);
	if (!TestNotNull(TEXT("The Profile Router activates in a Screen Session"),
		Session.Get()))
	{
		AddError(Error);
		return false;
	}
	TestFalse(TEXT("Critical asynchronous residency gates interaction"),
		Session->IsReadyForInteraction());
	TestFalse(TEXT("Activation does not prefetch a Visible-only Cue"),
		Resources->Requests[0].Contains(Navigate->Variants[0].ToSoftObjectPath()));
	const EWebToUEFeedbackDispatchResult PendingDispatch =
		Session->DispatchCommittedFeedback(Session->MakeFeedbackRequest(
			TEXT("webtoue.feedback.confirm"), TEXT("pending"), 1,
			EWebToUEInputModality::Pointer, EWebToUEFeedbackScope::LocalPlayer));
	TestEqual(TEXT("Critical pending rejects direct dispatch at the Router boundary"),
		PendingDispatch, EWebToUEFeedbackDispatchResult::DroppedByRouter);
	TestEqual(TEXT("Critical pending does not start an on-demand request"),
		Router->GetPendingOnDemandCount(), 0);
	TestTrue(TEXT("Critical pending produces an explicit not-ready trace"),
		Router->GetTrace().ContainsByPredicate(
			[](const FWebToUEFeedbackTrace& Entry)
			{
				return Entry.Outcome == EWebToUEFeedbackTraceOutcome::NotReady;
			}));
	AddAllResources(*Profile, *Resources);
	Resources->CompleteAll();
	TestTrue(TEXT("Critical completion opens the interaction gate"),
		Session->IsReadyForInteraction());
	TestEqual(TEXT("The Router never asks its provider for a synchronous load"),
		Resources->SynchronousLoadCount, 0);

	auto Dispatch = [Session](FName Cue, uint64 Correlation,
		EWebToUEInputModality Modality = EWebToUEInputModality::Pointer)
	{
		return Session->DispatchCommittedFeedback(Session->MakeFeedbackRequest(
			Cue, TEXT("play"), Correlation, Modality,
			EWebToUEFeedbackScope::LocalPlayer));
	};
	const EWebToUEFeedbackDispatchResult FirstConfirm =
		Dispatch(TEXT("webtoue.feedback.confirm"), 2);
	TestEqual(TEXT("A resident Confirm routes through the injected backend"),
		FirstConfirm, EWebToUEFeedbackDispatchResult::Routed);
	if (Backend->GetRecords().IsEmpty())
	{
		for (const FWebToUEFeedbackTrace& Entry : Router->GetTrace())
		{
			AddInfo(FString::Printf(TEXT("Feedback trace outcome=%d cue=%s detail=%s"),
				static_cast<int32>(Entry.Outcome), *Entry.CueId.ToString(),
				*Entry.Detail));
		}
		AddError(TEXT("The first Confirm produced no backend request."));
		return false;
	}
	USoundBase* const EvenVariant = Backend->GetRecords()[0].Sound;
	TestEqual(TEXT("User volume and Profile volume compose without changing the asset"),
		Backend->GetRecords()[0].VolumeMultiplier, 0.175f);
	TestEqual(TEXT("Screen feedback is always a 2D playback request"),
		Backend->GetRecords()[0].Mode, EWebToUEFeedbackPlaybackMode::Screen2D);
	TestNotNull(TEXT("UE Concurrency is passed to the backend"),
		Backend->GetRecords()[0].Concurrency.Get());
	TestEqual(TEXT("Cooldown rejects a second immediate Confirm"),
		Dispatch(TEXT("webtoue.feedback.confirm"), 3),
		EWebToUEFeedbackDispatchResult::DroppedByRouter);
	Clock->Advance(EWebToUEClockDomain::Real, 0.06, Error);
	TestEqual(TEXT("Confirm routes after the cooldown expires"),
		Dispatch(TEXT("webtoue.feedback.confirm"), 3),
		EWebToUEFeedbackDispatchResult::Routed);
	TestNotEqual(TEXT("Adjacent correlations choose distinct sealed variants"),
		Backend->GetRecords()[1].Sound.Get(), EvenVariant);
	Clock->Advance(EWebToUEClockDomain::Real, 0.06, Error);
	TestEqual(TEXT("A later even correlation routes"),
		Dispatch(TEXT("webtoue.feedback.confirm"), 4),
		EWebToUEFeedbackDispatchResult::Routed);
	TestEqual(TEXT("Variant choice is deterministic for the correlation modulo"),
		Backend->GetRecords()[2].Sound.Get(), EvenVariant);

	TestEqual(TEXT("Hover routes once"), Dispatch(TEXT("webtoue.feedback.hover"),
		100, EWebToUEInputModality::Gamepad), EWebToUEFeedbackDispatchResult::Routed);
	TestEqual(TEXT("Focus from the same correlated Gamepad event is deduplicated"),
		Dispatch(TEXT("webtoue.feedback.focus"), 100,
			EWebToUEInputModality::Gamepad),
		EWebToUEFeedbackDispatchResult::DroppedByRouter);
	for (uint64 Correlation = 200; Correlation < 204; ++Correlation)
	{
		TestEqual(TEXT("Navigate stays within the Profile throttle"),
			Dispatch(TEXT("webtoue.feedback.navigate"), Correlation,
				EWebToUEInputModality::Gamepad),
			EWebToUEFeedbackDispatchResult::Routed);
	}
	TestEqual(TEXT("Rapid fifth Navigate is throttled"),
		Dispatch(TEXT("webtoue.feedback.navigate"), 204,
			EWebToUEInputModality::Gamepad),
		EWebToUEFeedbackDispatchResult::DroppedByRouter);

	UserSettings.bMuted = true;
	Settings->SetSettings(UserSettings);
	TestEqual(TEXT("LocalPlayer-aware mute drops without touching the backend"),
		Dispatch(TEXT("webtoue.feedback.cancel"), 300),
		EWebToUEFeedbackDispatchResult::DroppedByRouter);
	UserSettings.bMuted = false;
	Settings->SetSettings(UserSettings);
	TestEqual(TEXT("A missing Profile Cue degrades deterministically"),
		Dispatch(TEXT("webtoue.feedback.missing"), 301),
		EWebToUEFeedbackDispatchResult::DroppedByRouter);

	const FWebToUEFeedbackCueProfile* Cancel =
		Profile->FindCue(TEXT("webtoue.feedback.cancel"));
	Resources->RemoveResident(Cancel->Variants[0].ToSoftObjectPath());
	TestEqual(TEXT("A non-resident resource drops the current transaction effect"),
		Dispatch(TEXT("webtoue.feedback.cancel"), 302),
		EWebToUEFeedbackDispatchResult::DroppedByRouter);
	TestEqual(TEXT("The missing resource starts one bounded on-demand request"),
		Router->GetPendingOnDemandCount(), 1);
	Resources->AddResident(Cancel->Variants[0].ToSoftObjectPath(),
		Cancel->Variants[0].LoadSynchronous());
	Resources->CompleteAll();
	TestEqual(TEXT("On-demand completion clears pending state"),
		Router->GetPendingOnDemandCount(), 0);

	const TConstArrayView<FWebToUEFeedbackTrace> Trace = Router->GetTrace();
	for (const EWebToUEFeedbackTraceOutcome Expected : {
		EWebToUEFeedbackTraceOutcome::Requested,
		EWebToUEFeedbackTraceOutcome::Committed,
		EWebToUEFeedbackTraceOutcome::CriticalPending,
		EWebToUEFeedbackTraceOutcome::CriticalReady,
		EWebToUEFeedbackTraceOutcome::Routed,
		EWebToUEFeedbackTraceOutcome::Deduplicated,
		EWebToUEFeedbackTraceOutcome::Cooldown,
		EWebToUEFeedbackTraceOutcome::Throttled,
		EWebToUEFeedbackTraceOutcome::MissingCue,
		EWebToUEFeedbackTraceOutcome::MissingResource,
		EWebToUEFeedbackTraceOutcome::Muted })
	{
		TestTrue(TEXT("The deterministic trace distinguishes every policy outcome"),
			Trace.ContainsByPredicate([Expected](const FWebToUEFeedbackTrace& Entry)
			{
				return Entry.Outcome == Expected;
			}));
	}
	return true;
}

bool FWebToUEFeedbackRouterScopeLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::FeedbackRouter::Tests;
	UWebToUEFeedbackProfile* Persistent = LoadFixture();
	if (!TestNotNull(TEXT("The Feedback scope fixture loads"), Persistent)) return false;
	UWebToUEFeedbackProfile* Profile = DuplicateObject<UWebToUEFeedbackProfile>(
		Persistent, GetTransientPackage());
	FWebToUEFeedbackCueProfile* Confirm = Profile->Cues.FindByPredicate(
		[](const FWebToUEFeedbackCueProfile& Cue)
		{
			return Cue.CueId == TEXT("webtoue.feedback.confirm");
		});
	FWebToUEFeedbackCueProfile* Cancel = Profile->Cues.FindByPredicate(
		[](const FWebToUEFeedbackCueProfile& Cue)
		{
			return Cue.CueId == TEXT("webtoue.feedback.cancel");
		});
	if (!TestNotNull(TEXT("The transient Profile contains Confirm"), Confirm) ||
		!TestNotNull(TEXT("The transient Profile contains Cancel"), Cancel))
	{
		return false;
	}
	Confirm->WorldPolicy = EWebToUEFeedbackWorldPolicy::OwnerLocation3D;
	Cancel->WorldPolicy = EWebToUEFeedbackWorldPolicy::TwoDimensional;
	TestTrue(TEXT("The modified transient scope Profile reseals"),
		Profile->RebuildResourceSeal());

	FContextObjects Objects;
	const TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
	FString Error;
	const TSharedRef<FWebToUERecordingFeedbackBackend> Backend =
		MakeShared<FWebToUERecordingFeedbackBackend>();
	const TSharedRef<FManualResourceProvider> Resources =
		MakeShared<FManualResourceProvider>();
	AddAllResources(*Profile, *Resources);
	const TSharedRef<FWebToUEProfileFeedbackRouter> Router =
		FWebToUEProfileFeedbackRouter::Create(Profile, Backend, nullptr, Resources);
	TSharedPtr<FWebToUESession> Session = FWebToUESession::Create(
		MakeSessionParams(Objects, Router, Clock, EWebToUESurfaceKind::World), Error);
	if (!TestNotNull(TEXT("A World Surface Session activates with an explicit policy"),
		Session.Get()))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Pre-resident Critical resources make the Session interactive"),
		Session->IsReadyForInteraction());
	auto Dispatch = [Session](FName Cue, uint64 Correlation)
	{
		return Session->DispatchCommittedFeedback(Session->MakeFeedbackRequest(
			Cue, TEXT("world"), Correlation, EWebToUEInputModality::Pointer,
			EWebToUEFeedbackScope::Surface));
	};
	TestEqual(TEXT("World 3D policy routes at the Host-resolved location"),
		Dispatch(TEXT("webtoue.feedback.confirm"), 401),
		EWebToUEFeedbackDispatchResult::Routed);
	TestEqual(TEXT("The 3D playback preserves the Host anchor"),
		Backend->GetRecords().Last().WorldLocation, FVector(10.0, 20.0, 30.0));
	TestEqual(TEXT("World 3D policy is explicit in the backend request"),
		Backend->GetRecords().Last().Mode, EWebToUEFeedbackPlaybackMode::World3D);
	TestEqual(TEXT("World 2D policy remains non-spatial"),
		Dispatch(TEXT("webtoue.feedback.cancel"), 402),
		EWebToUEFeedbackDispatchResult::Routed);
	TestEqual(TEXT("World 2D policy is explicit in the backend request"),
		Backend->GetRecords().Last().Mode, EWebToUEFeedbackPlaybackMode::World2D);
	TestEqual(TEXT("World Drop policy does not invent a position"),
		Dispatch(TEXT("webtoue.feedback.navigate"), 403),
		EWebToUEFeedbackDispatchResult::DroppedByRouter);

	const FWebToUEFeedbackRequest Old = Session->MakeFeedbackRequest(
		TEXT("webtoue.feedback.confirm"), TEXT("old"), 404,
		EWebToUEInputModality::Pointer, EWebToUEFeedbackScope::Surface);
	Session->AdvanceGeneration();
	TestEqual(TEXT("Generation advance rejects a delayed committed Cue"),
		Session->DispatchCommittedFeedback(Old),
		EWebToUEFeedbackDispatchResult::DroppedStaleGeneration);
	TestFalse(TEXT("Generation replacement re-establishes the Critical residency gate"),
		Session->IsReadyForInteraction());
	AddAllResources(*Profile, *Resources);
	Resources->CompleteAll();
	TestTrue(TEXT("Replacement generation becomes interactive after its own completion"),
		Session->IsReadyForInteraction());
	const int32 CancelsBeforeShutdown = Resources->CancelCount;
	Session->Invalidate();
	TestTrue(TEXT("Session shutdown cancels Router residency and clears readiness"),
		Resources->CancelCount == CancelsBeforeShutdown + 1 &&
		!Session->IsReadyForInteraction());
	TestTrue(TEXT("Trace records explicit World drop and Session end"),
		Router->GetTrace().ContainsByPredicate([](const FWebToUEFeedbackTrace& Entry)
		{
			return Entry.Outcome == EWebToUEFeedbackTraceOutcome::ScopeDropped;
		}) && Router->GetTrace().ContainsByPredicate([](const FWebToUEFeedbackTrace& Entry)
		{
			return Entry.Outcome == EWebToUEFeedbackTraceOutcome::SessionEnded;
		}));

	const TSharedRef<FManualResourceProvider> FailedResources =
		MakeShared<FManualResourceProvider>();
	FailedResources->bFailRequests = true;
	const TSharedRef<FWebToUEProfileFeedbackRouter> FailedRouter =
		FWebToUEProfileFeedbackRouter::Create(Profile, Backend, nullptr,
			FailedResources);
	TSharedPtr<FWebToUESession> FailedSession = FWebToUESession::Create(
		MakeSessionParams(Objects, FailedRouter, Clock), Error);
	TestNotNull(TEXT("A failed Critical request degrades without rejecting the Session"),
		FailedSession.Get());
	TestTrue(TEXT("A failed Critical request does not leave the UI permanently gated"),
		FailedSession.IsValid() && FailedSession->IsReadyForInteraction());
	TestTrue(TEXT("Critical load failure has a distinct deterministic trace outcome"),
		FailedRouter->GetTrace().ContainsByPredicate(
			[](const FWebToUEFeedbackTrace& Entry)
			{
				return Entry.Outcome == EWebToUEFeedbackTraceOutcome::CriticalFailed;
			}));
	return true;
}

bool FWebToUEFeedbackRouterLocalPlayerTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::FeedbackRouter::Tests;
	UWebToUEFeedbackProfile* Profile = LoadFixture();
	if (!TestNotNull(TEXT("The LocalPlayer fixture loads"), Profile)) return false;
	FContextObjects Objects;
	const TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
	const TSharedRef<FPerPlayerSettingsProvider> Settings =
		MakeShared<FPerPlayerSettingsProvider>(Objects.SecondLocalPlayer);
	FString Error;

	auto CreatePlayerSession = [&](ULocalPlayer* LocalPlayer,
		const TSharedRef<FWebToUERecordingFeedbackBackend>& Backend)
	{
		const TSharedRef<FManualResourceProvider> Resources =
			MakeShared<FManualResourceProvider>();
		AddAllResources(*Profile, *Resources);
		const TSharedRef<FWebToUEProfileFeedbackRouter> Router =
			FWebToUEProfileFeedbackRouter::Create(Profile, Backend, Settings, Resources);
		FWebToUESessionCreateParams Params = MakeSessionParams(Objects, Router, Clock);
		Params.LocalPlayer = LocalPlayer;
		return FWebToUESession::Create(Params, Error);
	};
	const TSharedRef<FWebToUERecordingFeedbackBackend> FirstBackend =
		MakeShared<FWebToUERecordingFeedbackBackend>();
	const TSharedRef<FWebToUERecordingFeedbackBackend> SecondBackend =
		MakeShared<FWebToUERecordingFeedbackBackend>();
	TSharedPtr<FWebToUESession> First =
		CreatePlayerSession(Objects.LocalPlayer, FirstBackend);
	TSharedPtr<FWebToUESession> Second =
		CreatePlayerSession(Objects.SecondLocalPlayer, SecondBackend);
	if (!TestNotNull(TEXT("The first LocalPlayer Session activates"), First.Get()) ||
		!TestNotNull(TEXT("The second LocalPlayer Session activates"), Second.Get()))
	{
		AddError(Error);
		return false;
	}
	auto Dispatch = [](const TSharedPtr<FWebToUESession>& Session)
	{
		return Session->DispatchCommittedFeedback(Session->MakeFeedbackRequest(
			TEXT("webtoue.feedback.cancel"), TEXT("local-player"), 700,
			EWebToUEInputModality::Gamepad, EWebToUEFeedbackScope::LocalPlayer));
	};
	TestEqual(TEXT("The unmuted LocalPlayer routes through its own backend"),
		Dispatch(First), EWebToUEFeedbackDispatchResult::Routed);
	TestEqual(TEXT("The muted LocalPlayer is isolated from the first player"),
		Dispatch(Second), EWebToUEFeedbackDispatchResult::DroppedByRouter);
	TestEqual(TEXT("The first LocalPlayer produced exactly one backend request"),
		FirstBackend->GetRecords().Num(), 1);
	TestEqual(TEXT("The muted LocalPlayer produced no backend request"),
		SecondBackend->GetRecords().Num(), 0);
	return true;
}

#endif
