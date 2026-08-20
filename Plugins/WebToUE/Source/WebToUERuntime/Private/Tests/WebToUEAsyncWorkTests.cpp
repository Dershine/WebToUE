#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEAsyncWork.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEAsyncTimerTest,
	"WebToUE.Runtime.AsyncTimer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEAsyncCommandTest,
	"WebToUE.Runtime.AsyncCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::AsyncWork::Tests
{
	struct FFixture
	{
		TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
		TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Updates =
			FWebToUEUpdateCoordinator::Create();
		FWebToUESessionHandle Session = FWebToUESessionHandle::Create(7001, 1);
		TSharedRef<FWebToUEAsyncCoordinator, ESPMode::ThreadSafe> Async;

		explicit FFixture(const FWebToUEAsyncBudget& Budget = FWebToUEAsyncBudget())
			: Async(FWebToUEAsyncCoordinator::Create(Session, Clock, Updates, Budget))
		{
		}
	};
}

bool FWebToUEAsyncTimerTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::AsyncWork::Tests;
	FFixture Fixture;
	FString Error;
	Fixture.Clock->SetTimeSeconds(EWebToUEClockDomain::Test, 10.0, Error);
	TArray<FString> Order;
	const FWebToUEAsyncHandle First = Fixture.Async->ScheduleTimer(
		EWebToUEClockDomain::Test, 5.0,
		[&Order](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation([&Order]() { Order.Add(TEXT("first")); });
		}, Error);
	TestTrue(TEXT("A finite one-shot Test timer receives a Session generation handle"),
		First.IsValid() && First.GetSession() == Fixture.Session);
	Fixture.Clock->Advance(EWebToUEClockDomain::Test, 4.0, Error);
	Fixture.Async->Pump();
	TestTrue(TEXT("Timer does not fire before its exact virtual deadline"), Order.IsEmpty());
	Fixture.Clock->Advance(EWebToUEClockDomain::Test, 1.0, Error);
	Fixture.Async->Pump();
	TestEqual(TEXT("Timer terminal evaluation commits through the update transaction"),
		FString::Join(Order, TEXT(",")), FString(TEXT("first")));

	Fixture.Async->ScheduleTimer(EWebToUEClockDomain::Test, 0.0,
		[&Fixture, &Order, &Error](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddPostCommitEffect([&Fixture, &Order, &Error]()
			{
				Order.Add(TEXT("outer"));
				Fixture.Async->ScheduleTimer(EWebToUEClockDomain::Test, 0.0,
					[&Order](FWebToUEUpdateTransaction& Inner)
					{
						Inner.AddStateMutation([&Order]() { Order.Add(TEXT("inner")); });
					}, Error);
			});
		}, Error);
	Fixture.Async->Pump();
	TestEqual(TEXT("Timer scheduled during a terminal callback waits for the next Pump"),
		FString::Join(Order, TEXT(",")), FString(TEXT("first,outer")));
	Fixture.Async->Pump();
	TestEqual(TEXT("The deferred zero-delay timer fires exactly once on the next Pump"),
		FString::Join(Order, TEXT(",")), FString(TEXT("first,outer,inner")));

	const FWebToUEAsyncHandle Cancelled = Fixture.Async->ScheduleTimer(
		EWebToUEClockDomain::Test, 1.0,
		[&Order](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation([&Order]() { Order.Add(TEXT("cancelled")); });
		}, Error);
	TestTrue(TEXT("Explicit cancellation wins before the deadline"),
		Fixture.Async->Cancel(Cancelled) == EWebToUEAsyncCancelResult::Cancelled);
	Fixture.Clock->Advance(EWebToUEClockDomain::Test, 1.0, Error);
	Fixture.Async->Pump();
	TestFalse(TEXT("Cancelled Timer never mutates Runtime state"), Order.Contains(TEXT("cancelled")));
	TestTrue(TEXT("Timer fire and cancellation remain separately traceable"),
		Fixture.Async->GetTrace().Num() >= 4 &&
		Fixture.Async->GetTrace().Last().Outcome == EWebToUEAsyncOutcome::CancelledExplicit);
	return true;
}

bool FWebToUEAsyncCommandTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::AsyncWork::Tests;
	FWebToUEAsyncBudget Budget;
	Budget.MaxPendingWork = 3;
	Budget.MaxTerminalsPerPump = 1;
	Budget.MaxTraceEntries = 4;
	FFixture Fixture(Budget);
	FString Error;
	TArray<FString> Results;
	const FWebToUEAsyncHandle Command = Fixture.Async->BeginCommand(
		EWebToUEClockDomain::Test, 5.0,
		[&Results](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation([&Results]() { Results.Add(TEXT("timeout")); });
		}, Error);
	TFuture<EWebToUEAsyncResolveResult> WorkerResult = Async(EAsyncExecution::Thread,
		[Async = Fixture.Async, Command, &Results]() mutable
		{
			return Async->ResolveCommand(Command,
				[&Results](FWebToUEUpdateTransaction& Transaction)
				{
					Transaction.AddStateMutation(
						[&Results]() { Results.Add(TEXT("resolved")); });
				});
		});
	WorkerResult.Wait();
	TestTrue(TEXT("Worker Command Result is accepted only as queued immutable work"),
		WorkerResult.Get() == EWebToUEAsyncResolveResult::Queued);
	TestTrue(TEXT("Worker completion cannot mutate UI state before the Game Thread Pump"),
		Results.IsEmpty());
	Fixture.Async->Pump();
	TestEqual(TEXT("Queued Command Result commits exactly once on the Game Thread"),
		FString::Join(Results, TEXT(",")), FString(TEXT("resolved")));
	TestTrue(TEXT("A second result for the same token is deterministically dropped"),
		Fixture.Async->ResolveCommand(Command,
			[](FWebToUEUpdateTransaction& Transaction) {}) ==
			EWebToUEAsyncResolveResult::DroppedAlreadyTerminal);

	const FWebToUEAsyncHandle Timeout = Fixture.Async->BeginCommand(
		EWebToUEClockDomain::Test, 2.0,
		[&Results](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation([&Results]() { Results.Add(TEXT("timeout")); });
		}, Error);
	Fixture.Clock->Advance(EWebToUEClockDomain::Test, 2.0, Error);
	Fixture.Async->Pump();
	TestEqual(TEXT("Unresolved Command times out at the exact virtual deadline"),
		FString::Join(Results, TEXT(",")), FString(TEXT("resolved,timeout")));
	TestTrue(TEXT("Result arriving after timeout cannot enter a second transaction"),
		Fixture.Async->ResolveCommand(Timeout,
			[](FWebToUEUpdateTransaction& Transaction) {}) ==
			EWebToUEAsyncResolveResult::DroppedAlreadyTerminal);

	const FWebToUEAsyncHandle Stale = Fixture.Async->BeginCommand(
		EWebToUEClockDomain::Test, 3.0,
		[](FWebToUEUpdateTransaction& Transaction) {}, Error);
	const FWebToUEAsyncHandle Timer = Fixture.Async->ScheduleTimer(
		EWebToUEClockDomain::Test, 3.0,
		[&Results](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation([&Results]() { Results.Add(TEXT("stale")); });
		}, Error);
	Fixture.Session = FWebToUESessionHandle::Create(7001, 2);
	Fixture.Async->AdvanceGeneration(Fixture.Session);
	TestEqual(TEXT("Generation advance synchronously cancels every prior-generation work item"),
		Fixture.Async->GetPendingWorkCount(), 0);
	TestTrue(TEXT("Late old-generation result is explicitly stale"),
		Fixture.Async->ResolveCommand(Stale,
			[](FWebToUEUpdateTransaction& Transaction) {}) ==
			EWebToUEAsyncResolveResult::DroppedStaleGeneration);
	Fixture.Clock->Advance(EWebToUEClockDomain::Test, 3.0, Error);
	Fixture.Async->Pump();
	TestFalse(TEXT("Generation-cancelled Timer cannot mutate the replacement View"),
		Results.Contains(TEXT("stale")));
	TestTrue(TEXT("Handles from the cancelled generation no longer cancel current work"),
		Fixture.Async->Cancel(Timer) == EWebToUEAsyncCancelResult::DroppedStaleGeneration);
	TestTrue(TEXT("Async Trace is bounded under hostile duplicate and stale terminals"),
		Fixture.Async->GetTrace().Num() <= Budget.MaxTraceEntries);
	return true;
}

#endif
