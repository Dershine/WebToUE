#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEScreenHost.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "WebToUEDocument.h"
#include "WebToUEView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUESessionFeedbackTest,
	"WebToUE.Runtime.SessionFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEScreenHostTest,
	"WebToUE.Runtime.ScreenHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::SessionHost::Tests
{
	class FManualClock final : public IWebToUEClock
	{
	public:
		virtual double GetTimeSeconds() const override { return TimeSeconds; }
		double TimeSeconds = 12.5;
	};

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
		Params.Clock = MakeShared<FManualClock>();
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
		FMath::IsNearlyEqual(Session->GetClock()->GetTimeSeconds(), 12.5));

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
	Params.Clock = MakeShared<FManualClock>();
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

#endif
