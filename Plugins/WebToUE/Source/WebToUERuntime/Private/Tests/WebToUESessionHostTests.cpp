#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEScreenHost.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "WebToUEAsyncWork.h"
#include "WebToUEDocument.h"
#include "WebToUEFeedbackRouter.h"
#include "WebToUEView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUESessionFeedbackTest,
	"WebToUE.Runtime.SessionFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEScreenHostTest,
	"WebToUE.Runtime.ScreenHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEScreenHostFeedbackProfileTest,
	"WebToUE.Runtime.ScreenHostFeedbackProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEAsyncLifecycleTest,
	"WebToUE.Runtime.AsyncLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::SessionHost::Tests
{
	class FRecordingScreenLayer final : public IWebToUEScreenLayer
	{
	public:
		FRecordingScreenLayer(ULocalPlayer* InLocalPlayer, UWorld* InWorld)
			: LocalPlayer(InLocalPlayer), World(InWorld)
		{
		}

		virtual ULocalPlayer* GetLocalPlayer() const override { return LocalPlayer; }
		virtual UWorld* GetWorld() const override { return World; }

		virtual bool AddWidget(
			TSharedRef<SWidget> Widget, int32 ZOrder, FString& OutError) override
		{
			OutError.Reset();
			++AddCount;
			LastZOrder = ZOrder;
			AttachedWidget = Widget;
			return true;
		}

		virtual void RemoveWidget(TSharedRef<SWidget> Widget) override
		{
			++RemoveCount;
			if (AttachedWidget == Widget)
			{
				AttachedWidget.Reset();
			}
		}

		ULocalPlayer* LocalPlayer = nullptr;
		UWorld* World = nullptr;
		TSharedPtr<SWidget> AttachedWidget;
		int32 AddCount = 0;
		int32 RemoveCount = 0;
		int32 LastZOrder = 0;
	};

	struct FContextObjects
	{
		UWorld* World = NewObject<UWorld>(GetTransientPackage());
		ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
		UWebToUEDocument* Data = NewObject<UWebToUEDocument>(GetTransientPackage());
		UWebToUEDocument* Commands = NewObject<UWebToUEDocument>(GetTransientPackage());

		FContextObjects()
		{
			World->AddToRoot();
			LocalPlayer->AddToRoot();
			Data->AddToRoot();
			Commands->AddToRoot();
		}

		~FContextObjects()
		{
			Commands->RemoveFromRoot();
			Data->RemoveFromRoot();
			LocalPlayer->RemoveFromRoot();
			World->RemoveFromRoot();
		}
	};

	class FManualFeedbackResources final : public IWebToUEFeedbackResourceProvider
	{
	public:
		virtual EWebToUEFeedbackResidencyRequest Request(
			TConstArrayView<FSoftObjectPath> Paths,
			TFunction<void()> Completion) override
		{
			TArray<FSoftObjectPath> Missing;
			for (const FSoftObjectPath& Path : Paths)
			{
				if (!Residents.Contains(Path)) Missing.Add(Path);
			}
			if (Missing.IsEmpty()) return EWebToUEFeedbackResidencyRequest::Ready;
			Pending.Add(MoveTemp(Completion));
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
			Pending.Reset();
			Residents.Reset();
		}

		void MakeResident(UWebToUEFeedbackProfile& Profile)
		{
			for (const FWebToUEFeedbackCueProfile& Cue : Profile.Cues)
			{
				for (const TSoftObjectPtr<USoundBase>& Variant : Cue.Variants)
				{
					Residents.Add(Variant.ToSoftObjectPath(), Variant.LoadSynchronous());
				}
				if (!Cue.Concurrency.IsNull())
				{
					Residents.Add(Cue.Concurrency.ToSoftObjectPath(),
						Cue.Concurrency.LoadSynchronous());
				}
			}
			TArray<TFunction<void()>> Completions = MoveTemp(Pending);
			for (TFunction<void()>& Completion : Completions)
			{
				if (Completion) Completion();
			}
		}

		int32 CancelCount = 0;

	private:
		TMap<FSoftObjectPath, UObject*> Residents;
		TArray<TFunction<void()>> Pending;
	};

	static FWebToUESessionCreateParams MakeSessionParams(
		FContextObjects& Objects,
		const TSharedPtr<IWebToUEFeedbackRouter>& Router)
	{
		FWebToUESessionCreateParams Params;
		Params.LocalPlayer = Objects.LocalPlayer;
		Params.World = Objects.World;
		Params.Surface.Kind = EWebToUESurfaceKind::Screen;
		Params.Surface.SurfaceId = TEXT("webtoue.tests.screen");
		Params.Surface.Owner = Objects.LocalPlayer;
		Params.DataContext = Objects.Data;
		Params.CommandContext = Objects.Commands;
		Params.Environment.CultureName = TEXT("en-US");
		Params.Environment.ViewportSize = FVector2f(1280.0f, 720.0f);
		Params.Environment.DpiScale = 2.0f;
		const TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
		FString ClockError;
		Clock->SetTimeSeconds(EWebToUEClockDomain::Test, 12.5, ClockError);
		Params.Clock = Clock;
		Params.FeedbackRouter = Router;
		return Params;
	}
}

bool FWebToUESessionFeedbackTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::SessionHost::Tests;
	FContextObjects Objects;
	const TSharedRef<FWebToUERecordingFeedbackRouter> Recording =
		MakeShared<FWebToUERecordingFeedbackRouter>();
	FString Error;
	TSharedPtr<FWebToUESession> Session = FWebToUESession::Create(
		MakeSessionParams(Objects, Recording), Error);
	TestTrue(TEXT("A complete LocalPlayer Screen context creates an active Session"),
		Session.IsValid() && Session->IsActive());
	if (!Session)
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Session binds Data and Command contexts without owning gameplay authority"),
		Session->GetDataContext() == Objects.Data &&
		Session->GetCommandContext() == Objects.Commands);
	TestTrue(TEXT("Session binds the injected deterministic Clock"),
		FMath::IsNearlyEqual(
			Session->GetClock()->GetTimeSeconds(EWebToUEClockDomain::Test), 12.5));

	FWebToUEFeedbackRequest Request = Session->MakeFeedbackRequest(
		TEXT("webtoue.tests.confirm"), TEXT("play"), 41,
		EWebToUEInputModality::Gamepad, EWebToUEFeedbackScope::LocalPlayer);
	TestTrue(TEXT("A committed current-generation Feedback request reaches its injected Router"),
		Session->DispatchCommittedFeedback(Request) ==
		EWebToUEFeedbackDispatchResult::Routed);
	TestEqual(TEXT("Recording Router captures exactly one deterministic dispatch"),
		Recording->GetRecords().Num(), 1);
	if (Recording->GetRecords().Num() == 1)
	{
		const FWebToUERecordedFeedback& Record = Recording->GetRecords()[0];
		TestTrue(TEXT("Routing preserves scope, modality and correlation"),
			Record.Request.Scope == EWebToUEFeedbackScope::LocalPlayer &&
			Record.Request.InputModality == EWebToUEInputModality::Gamepad &&
			Record.Request.EventCorrelationId == 41);
		TestTrue(TEXT("Routing context remains bound to one player and Screen Surface"),
			Record.Context.LocalPlayer.Get() == Objects.LocalPlayer &&
			Record.Context.Surface.Kind == EWebToUESurfaceKind::Screen);
	}

	Recording->Reset();
	bool bStateCommitted = false;
	bool bEffectObservedCommittedState = false;
	EWebToUEFeedbackDispatchResult TransactionDispatch =
		EWebToUEFeedbackDispatchResult::DroppedInvalidRequest;
	Session->GetUpdateCoordinator()->Submit(
		[Session, Request, &bStateCommitted, &bEffectObservedCommittedState,
			&TransactionDispatch](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation([&bStateCommitted]()
			{
				bStateCommitted = true;
			});
			Transaction.AddPostCommitEffect(
				[Session, Request, &bStateCommitted, &bEffectObservedCommittedState,
					&TransactionDispatch]()
				{
					bEffectObservedCommittedState = bStateCommitted;
					TransactionDispatch = Session->DispatchCommittedFeedback(Request);
				});
		});
	TestTrue(TEXT("Session-owned Feedback dispatch observes committed UI state"),
		bStateCommitted && bEffectObservedCommittedState &&
		TransactionDispatch == EWebToUEFeedbackDispatchResult::Routed &&
		Recording->GetRecords().Num() == 1);
	Session->GetUpdateCoordinator()->Submit(
		[Session, Request](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddPostCommitEffect([Session, Request]()
			{
				Session->DispatchCommittedFeedback(Request);
			});
			Transaction.Reject(TEXT("hostile event evaluation failure"));
		});
	TestEqual(TEXT("Rejected UI transactions never dispatch collected Feedback"),
		Recording->GetRecords().Num(), 1);

	const FWebToUESessionHandle FirstGeneration = Session->GetHandle();
	Session->AdvanceGeneration();
	TestTrue(TEXT("Generation advance invalidates a delayed Feedback request"),
		Session->DispatchCommittedFeedback(Request) ==
		EWebToUEFeedbackDispatchResult::DroppedStaleGeneration);
	TestTrue(TEXT("Generation advance preserves Session identity while changing generation"),
		Session->GetHandle().GetSessionId() == FirstGeneration.GetSessionId() &&
		Session->GetHandle().GetGeneration() != FirstGeneration.GetGeneration());

	TSharedPtr<FWebToUESession> OtherSession = FWebToUESession::Create(
		MakeSessionParams(Objects, Recording), Error);
	FWebToUEFeedbackRequest OtherRequest = OtherSession->MakeFeedbackRequest(
		TEXT("webtoue.tests.cancel"), NAME_None, 42,
		EWebToUEInputModality::Pointer, EWebToUEFeedbackScope::Session);
	TestTrue(TEXT("A Session rejects a request owned by another Session"),
		Session->DispatchCommittedFeedback(OtherRequest) ==
		EWebToUEFeedbackDispatchResult::DroppedWrongSession);

	FWebToUESessionCreateParams NullParams = MakeSessionParams(Objects, nullptr);
	TSharedPtr<FWebToUESession> NullSession = FWebToUESession::Create(NullParams, Error);
	FWebToUEFeedbackRequest NullRequest = NullSession->MakeFeedbackRequest(
		TEXT("webtoue.tests.navigate"), NAME_None, 43,
		EWebToUEInputModality::Keyboard, EWebToUEFeedbackScope::Surface);
	TestTrue(TEXT("Default Null Router deterministically drops without media side effects"),
		NullSession->DispatchCommittedFeedback(NullRequest) ==
		EWebToUEFeedbackDispatchResult::DroppedByRouter);

	FWebToUEFeedbackRequest Current = Session->MakeFeedbackRequest(
		TEXT("webtoue.tests.accepted"), NAME_None, 44,
		EWebToUEInputModality::Gamepad, EWebToUEFeedbackScope::Session);
	Session->Invalidate();
	TestTrue(TEXT("Session shutdown rejects feedback even when its former request was current"),
		Session->DispatchCommittedFeedback(Current) ==
		EWebToUEFeedbackDispatchResult::DroppedInactiveSession);
	TestTrue(TEXT("Session shutdown rejects new Runtime update evaluation"),
		Session->GetUpdateCoordinator()->Submit([](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation([]() {});
		}) == EWebToUEUpdateSubmitResult::RejectedInactive);
	return true;
}

bool FWebToUEScreenHostTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::SessionHost::Tests;
	FContextObjects Objects;
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;
	FWebToUECompiledNode& Root = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Root.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Root.Tag = TEXT("body");
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
	Document->AddToRoot();
	ON_SCOPE_EXIT { Document->RemoveFromRoot(); };
	const TSharedRef<FRecordingScreenLayer> Layer =
		MakeShared<FRecordingScreenLayer>(Objects.LocalPlayer, Objects.World);
	const TSharedRef<FWebToUERecordingFeedbackRouter> Recording =
		MakeShared<FWebToUERecordingFeedbackRouter>();

	FWebToUEScreenHostCreateParams Params;
	Params.Document = Document;
	Params.DataContext = Objects.Data;
	Params.CommandContext = Objects.Commands;
	Params.SurfaceId = TEXT("webtoue.tests.player-screen");
	Params.Clock = MakeShared<FWebToUEVirtualClock>();
	Params.FeedbackRouter = Recording;
	Params.ZOrder = 37;
	FString Error;
	TUniquePtr<FWebToUEScreenHost> Host =
		FWebToUEScreenHost::CreateWithLayer(Layer, Params, Error);
	TestTrue(TEXT("Code Screen Host creates exactly one View and active Session"),
		Host.IsValid() && Host->GetView() && Host->GetSession()->IsActive());
	if (!Host)
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("The hosted View is explicitly bound to the Host Session"),
		Host->GetView()->GetSession() == Host->GetSession());
	TestFalse(TEXT("Creation does not attach before the caller reaches its safe boundary"),
		Host->IsAttached());
	TestTrue(TEXT("Attach routes the one View through the injected per-player Screen layer"),
		Host->Attach(Error));
	TestTrue(TEXT("Screen layer observes one attach at the requested Z order"),
		Layer->AddCount == 1 && Layer->LastZOrder == 37 && Layer->AttachedWidget.IsValid());
	TestFalse(TEXT("A Screen Host cannot attach the same Session twice"), Host->Attach(Error));

	TSharedPtr<FWebToUESession> Session = Host->GetSession();
	FWebToUEFeedbackRequest Request = Session->MakeFeedbackRequest(
		TEXT("webtoue.tests.hosted"), NAME_None, 99,
		EWebToUEInputModality::Pointer, EWebToUEFeedbackScope::Surface);
	TestTrue(TEXT("Hosted Session dispatches through its injected Router"),
		Session->DispatchCommittedFeedback(Request) == EWebToUEFeedbackDispatchResult::Routed);

	Host->Shutdown();
	TestTrue(TEXT("Shutdown invalidates Session before releasing the View"),
		!Session->IsActive() && Host->GetView() == nullptr);
	TestTrue(TEXT("Shutdown removes exactly the widget it attached"),
		Layer->RemoveCount == 1 && !Layer->AttachedWidget.IsValid());
	TestTrue(TEXT("Delayed post-shutdown Feedback is rejected"),
		Session->DispatchCommittedFeedback(Request) ==
		EWebToUEFeedbackDispatchResult::DroppedInactiveSession);
	Host->Shutdown();
	TestEqual(TEXT("Repeated shutdown is idempotent"), Layer->RemoveCount, 1);

	FWebToUEScreenHostCreateParams InvalidParams = Params;
	InvalidParams.Document = nullptr;
	TUniquePtr<FWebToUEScreenHost> InvalidHost =
		FWebToUEScreenHost::CreateWithLayer(Layer, InvalidParams, Error);
	TestFalse(TEXT("Screen Host rejects a missing compiled Document"), InvalidHost.IsValid());
	TestTrue(TEXT("Rejected creation returns an actionable diagnostic"), !Error.IsEmpty());
	return true;
}

bool FWebToUEScreenHostFeedbackProfileTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::SessionHost::Tests;
	FContextObjects Objects;
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;
	FWebToUECompiledNode& Root = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Root.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Root.Tag = TEXT("body");
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
	Document->AddToRoot();
	ON_SCOPE_EXIT { Document->RemoveFromRoot(); };
	UWebToUEFeedbackProfile* Profile = LoadObject<UWebToUEFeedbackProfile>(nullptr,
		TEXT("/Game/WebToUEExamples/Audio/DA_WTUE_FeedbackProfile."
			"DA_WTUE_FeedbackProfile"));
	if (!TestNotNull(TEXT("The Screen Host Feedback Profile fixture loads"), Profile))
	{
		return false;
	}
	const TSharedRef<FRecordingScreenLayer> Layer =
		MakeShared<FRecordingScreenLayer>(Objects.LocalPlayer, Objects.World);
	const TSharedRef<FWebToUERecordingFeedbackBackend> Backend =
		MakeShared<FWebToUERecordingFeedbackBackend>();
	const TSharedRef<FManualFeedbackResources> Resources =
		MakeShared<FManualFeedbackResources>();
	FWebToUEScreenHostCreateParams Params;
	Params.Document = Document;
	Params.SurfaceId = TEXT("webtoue.tests.feedback-profile-screen");
	Params.Clock = MakeShared<FWebToUEVirtualClock>();
	Params.FeedbackProfile = Profile;
	Params.FeedbackBackend = Backend;
	Params.FeedbackResourceProvider = Resources;
	FString Error;
	TUniquePtr<FWebToUEScreenHost> Host =
		FWebToUEScreenHost::CreateWithLayer(Layer, Params, Error);
	if (!TestNotNull(TEXT("A Profile creates the default Profile Router in Screen Host"),
		Host.Get()))
	{
		AddError(Error);
		return false;
	}
	TestFalse(TEXT("Critical Feedback residency initially gates the Host Session"),
		Host->GetSession()->IsReadyForInteraction());
	TestFalse(TEXT("Attach fails closed before Critical Feedback is resident"),
		Host->Attach(Error));
	TestTrue(TEXT("The gated Attach reports an actionable Critical resource diagnostic"),
		Error.Contains(TEXT("Critical Feedback")) && Layer->AddCount == 0);
	Resources->MakeResident(*Profile);
	TestTrue(TEXT("Async Critical completion opens the Host interaction gate"),
		Host->GetSession()->IsReadyForInteraction());
	TestTrue(TEXT("The same Host attaches after Critical completion"), Host->Attach(Error));
	const EWebToUEFeedbackDispatchResult Dispatch =
		Host->GetSession()->DispatchCommittedFeedback(
			Host->GetSession()->MakeFeedbackRequest(
				TEXT("webtoue.feedback.confirm"), TEXT("screen-host"), 901,
				EWebToUEInputModality::Pointer, EWebToUEFeedbackScope::LocalPlayer));
	TestEqual(TEXT("The Host-created Router reaches its injected project backend"),
		Dispatch, EWebToUEFeedbackDispatchResult::Routed);
	TestTrue(TEXT("Screen Host playback remains explicitly 2D"),
		Backend->GetRecords().Num() == 1 &&
		Backend->GetRecords()[0].Mode == EWebToUEFeedbackPlaybackMode::Screen2D);
	Host->Shutdown();
	TestEqual(TEXT("Screen Host shutdown cancels Profile residency ownership"),
		Resources->CancelCount, 1);
	return true;
}

bool FWebToUEAsyncLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::SessionHost::Tests;
	FContextObjects Objects;
	UWebToUEDocument* FirstDocument = NewObject<UWebToUEDocument>(GetTransientPackage());
	UWebToUEDocument* SecondDocument = NewObject<UWebToUEDocument>(GetTransientPackage());
	FirstDocument->AddToRoot();
	SecondDocument->AddToRoot();
	ON_SCOPE_EXIT
	{
		SecondDocument->RemoveFromRoot();
		FirstDocument->RemoveFromRoot();
	};
	for (UWebToUEDocument* Document : {FirstDocument, SecondDocument})
	{
		FWebToUECompiledDocumentData Data;
		Data.RootNodeIndex = 0;
		FWebToUECompiledNode& Root = Data.Nodes.AddDefaulted_GetRef();
		Root.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Root.Tag = TEXT("body");
		Document->CommitCompiledDocument(MoveTemp(Data));
	}

	const TSharedRef<FRecordingScreenLayer> Layer =
		MakeShared<FRecordingScreenLayer>(Objects.LocalPlayer, Objects.World);
	const TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
	FWebToUEScreenHostCreateParams Params;
	Params.Document = FirstDocument;
	Params.DataContext = Objects.Data;
	Params.CommandContext = Objects.Commands;
	Params.SurfaceId = TEXT("webtoue.tests.async-lifecycle");
	Params.Clock = Clock;
	FString Error;
	TUniquePtr<FWebToUEScreenHost> Host =
		FWebToUEScreenHost::CreateWithLayer(Layer, Params, Error);
	TestTrue(TEXT("Lifecycle fixture creates a Screen Host"), Host.IsValid());
	if (!Host)
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Lifecycle fixture attaches so World cleanup ownership is bound"),
		Host->Attach(Error));
	TSharedPtr<FWebToUESession> Session = Host->GetSession();
	TSharedRef<FWebToUEAsyncCoordinator, ESPMode::ThreadSafe> Async =
		Session->GetAsyncCoordinator();
	TArray<FString> Mutations;
	const FWebToUEAsyncHandle OldTimer = Async->ScheduleTimer(
		EWebToUEClockDomain::Test, 1.0,
		[&Mutations](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation(
				[&Mutations]() { Mutations.Add(TEXT("old-timer")); });
		}, Error);
	const FWebToUEAsyncHandle OldCommand = Async->BeginCommand(
		EWebToUEClockDomain::Test, 1.0,
		[&Mutations](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation(
				[&Mutations]() { Mutations.Add(TEXT("old-timeout")); });
		}, Error);
	const FWebToUESessionHandle OldGeneration = Session->GetHandle();
	Host->GetView()->SetDocument(SecondDocument);
	TestTrue(TEXT("Replacing the hosted Document advances the Session generation"),
		Session->GetHandle().GetGeneration() != OldGeneration.GetGeneration());
	TestEqual(TEXT("Document replacement synchronously cancels old Timer and Command work"),
		Async->GetPendingWorkCount(), 0);
	TestTrue(TEXT("A Command Result arriving after View replacement is stale"),
		Async->ResolveCommand(OldCommand,
			[&Mutations](FWebToUEUpdateTransaction& Transaction)
			{
				Transaction.AddStateMutation(
					[&Mutations]() { Mutations.Add(TEXT("old-result")); });
			}) == EWebToUEAsyncResolveResult::DroppedStaleGeneration);
	TestTrue(TEXT("Old Timer handle cannot cancel replacement-generation work"),
		Async->Cancel(OldTimer) == EWebToUEAsyncCancelResult::DroppedStaleGeneration);
	Clock->Advance(EWebToUEClockDomain::Test, 1.0, Error);
	Async->Pump();
	TestTrue(TEXT("No old-generation terminal mutates the replacement View"),
		Mutations.IsEmpty());

	const FWebToUEAsyncHandle CurrentTimer = Async->ScheduleTimer(
		EWebToUEClockDomain::Test, 1.0,
		[&Mutations](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation(
				[&Mutations]() { Mutations.Add(TEXT("cleaned-timer")); });
		}, Error);
	const FWebToUEAsyncHandle CurrentCommand = Async->BeginCommand(
		EWebToUEClockDomain::Test, 1.0,
		[&Mutations](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation(
				[&Mutations]() { Mutations.Add(TEXT("cleaned-timeout")); });
		}, Error);
	TestTrue(TEXT("Replacement generation accepts new lifecycle-owned work"),
		CurrentTimer.IsValid() && CurrentCommand.IsValid());
	FWorldDelegates::OnWorldCleanup.Broadcast(Objects.World, true, true);
	TestTrue(TEXT("Matching World cleanup shuts down the Host and Session"),
		!Session->IsActive() && Host->GetView() == nullptr && !Async->IsActive());
	TestEqual(TEXT("World cleanup synchronously releases every pending async item"),
		Async->GetPendingWorkCount(), 0);
	TestTrue(TEXT("Result arriving after World cleanup is rejected as inactive"),
		Async->ResolveCommand(CurrentCommand,
			[&Mutations](FWebToUEUpdateTransaction& Transaction)
			{
				Transaction.AddStateMutation(
					[&Mutations]() { Mutations.Add(TEXT("cleaned-result")); });
			}) == EWebToUEAsyncResolveResult::RejectedInactive);
	Clock->Advance(EWebToUEClockDomain::Test, 1.0, Error);
	Async->Pump();
	TestTrue(TEXT("World-cleaned Timer, timeout and result produce zero late mutation"),
		Mutations.IsEmpty());
	TestTrue(TEXT("Lifecycle trace distinguishes Generation and Session cancellation"),
		Async->GetTrace().ContainsByPredicate([](const FWebToUEAsyncTrace& Entry)
		{
			return Entry.Outcome == EWebToUEAsyncOutcome::CancelledGeneration;
		}) && Async->GetTrace().ContainsByPredicate([](const FWebToUEAsyncTrace& Entry)
		{
			return Entry.Outcome == EWebToUEAsyncOutcome::CancelledSession;
		}));
	return true;
}

#endif
