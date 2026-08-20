#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEUpdateTransaction.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEUpdateTransactionTest,
	"WebToUE.Runtime.UpdateTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEUpdateQueueTest,
	"WebToUE.Runtime.UpdateQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEUpdateTransactionTest::RunTest(const FString& Parameters)
{
	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Coordinator =
		FWebToUEUpdateCoordinator::Create();
	TArray<FString> Order;
	Coordinator->Submit([&Order](FWebToUEUpdateTransaction& Transaction)
	{
		Transaction.AddStateMutation([&Order]() { Order.Add(TEXT("state")); });
		Transaction.AddStructuralMutation([&Order]() { Order.Add(TEXT("structure")); });
		Transaction.AddPostCommitEffect([&Order]() { Order.Add(TEXT("effect")); });
	});
	TestEqual(TEXT("A successful transaction commits state, structure, then effects"),
		FString::Join(Order, TEXT(",")), FString(TEXT("state,structure,effect")));
	TestEqual(TEXT("The successful transaction records one trace"),
		Coordinator->GetTrace().Num(), 1);
	TestTrue(TEXT("The trace distinguishes every committed work class"),
		Coordinator->GetTrace()[0].Outcome == EWebToUEUpdateOutcome::Committed &&
		Coordinator->GetTrace()[0].StateMutationCount == 1 &&
		Coordinator->GetTrace()[0].StructuralMutationCount == 1 &&
		Coordinator->GetTrace()[0].PostCommitEffectCount == 1);

	Coordinator->ResetTrace();
	Order.Reset();
	Coordinator->Submit([&Order](FWebToUEUpdateTransaction& Transaction)
	{
		Transaction.AddStateMutation([&Order]() { Order.Add(TEXT("must-not-commit")); });
		Transaction.AddPostCommitEffect([&Order]() { Order.Add(TEXT("must-not-dispatch")); });
		Transaction.Reject(TEXT("hostile evaluation failure"));
	});
	TestTrue(TEXT("Evaluation failure atomically discards mutations and post-commit effects"),
		Order.IsEmpty());
	TestTrue(TEXT("Evaluation rejection remains diagnosable"),
		Coordinator->GetTrace().Num() == 1 &&
		Coordinator->GetTrace()[0].Outcome == EWebToUEUpdateOutcome::RejectedEvaluation &&
		Coordinator->GetTrace()[0].Diagnostic.Contains(TEXT("hostile")));

	Coordinator->ResetTrace();
	Order.Reset();
	{
		FWebToUEUpdateTraversalScope Traversal = Coordinator->BeginTraversal();
		Coordinator->Submit([&Order](FWebToUEUpdateTransaction& Transaction)
		{
			Transaction.AddStateMutation([&Order]() { Order.Add(TEXT("partial")); });
			Transaction.AddStructuralMutation([&Order]() { Order.Add(TEXT("unsafe")); });
		});
		Coordinator->Drain();
		TestTrue(TEXT("Traversal defers evaluation to a later safe boundary"), Order.IsEmpty());
	}
	Coordinator->Drain();
	TestTrue(TEXT("A traversal-origin structural Mutation rejects the entire transaction"),
		Order.IsEmpty() && Coordinator->GetTrace().Num() == 1 &&
		Coordinator->GetTrace()[0].Outcome ==
			EWebToUEUpdateOutcome::RejectedStructuralMutationDuringTraversal);

	Coordinator->ResetTrace();
	Order.Reset();
	Coordinator->Submit([Coordinator, &Order](FWebToUEUpdateTransaction& Transaction)
	{
		Transaction.AddStateMutation([&Order]() { Order.Add(TEXT("outer")); });
		Coordinator->Submit([&Order](FWebToUEUpdateTransaction& Reentrant)
		{
			Reentrant.AddStateMutation([&Order]() { Order.Add(TEXT("inner")); });
		});
	});
	TestEqual(TEXT("Evaluation reentrancy joins one atomic transaction"),
		FString::Join(Order, TEXT(",")), FString(TEXT("outer,inner")));
	TestTrue(TEXT("The trace proves two evaluations committed as one transaction"),
		Coordinator->GetTrace().Num() == 1 &&
		Coordinator->GetTrace()[0].EvaluationCount == 2);

	FWebToUEUpdateBudget TightBudget;
	TightBudget.MaxEvaluationsPerTransaction = 2;
	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Bounded =
		FWebToUEUpdateCoordinator::Create(TightBudget);
	int32 MutationCount = 0;
	TSharedRef<FWebToUEUpdateEvaluation, ESPMode::ThreadSafe> Cycle =
		MakeShared<FWebToUEUpdateEvaluation, ESPMode::ThreadSafe>();
	*Cycle = [Bounded, Cycle, &MutationCount](FWebToUEUpdateTransaction& Transaction)
	{
		Transaction.AddStateMutation([&MutationCount]() { ++MutationCount; });
		Bounded->Submit([Cycle](FWebToUEUpdateTransaction& Reentrant)
		{
			(*Cycle)(Reentrant);
		});
	};
	Bounded->Submit([Cycle](FWebToUEUpdateTransaction& Transaction)
	{
		(*Cycle)(Transaction);
	});
	TestEqual(TEXT("A reentrant cycle cannot partially commit before its evaluation budget"),
		MutationCount, 0);
	TestTrue(TEXT("The cycle terminates with an explicit budget trace"),
		Bounded->GetTrace().Num() == 1 &&
		Bounded->GetTrace()[0].Outcome == EWebToUEUpdateOutcome::RejectedBudget);
	return true;
}

bool FWebToUEUpdateQueueTest::RunTest(const FString& Parameters)
{
	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Coordinator =
		FWebToUEUpdateCoordinator::Create();
	TAtomic<int32> CommittedValue{0};
	TFuture<EWebToUEUpdateSubmitResult> Submission = Async(EAsyncExecution::Thread,
		[Coordinator, &CommittedValue]()
		{
			return Coordinator->Submit([&CommittedValue](FWebToUEUpdateTransaction& Transaction)
			{
				Transaction.AddStateMutation([&CommittedValue]()
				{
					CommittedValue.IncrementExchange();
				});
			});
		});
	Submission.Wait();
	TestTrue(TEXT("A non-Game Thread producer is accepted only as queued work"),
		Submission.Get() == EWebToUEUpdateSubmitResult::Queued);
	TestEqual(TEXT("Queued evaluation cannot mutate state on its producer thread"),
		CommittedValue.Load(), 0);
	Coordinator->Drain();
	TestEqual(TEXT("The Game Thread drain commits queued work exactly once"),
		CommittedValue.Load(), 1);
	TestTrue(TEXT("The queued transaction records a committed trace"),
		Coordinator->GetTrace().Num() == 1 &&
		Coordinator->GetTrace()[0].Outcome == EWebToUEUpdateOutcome::Committed);

	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Cancelled =
		FWebToUEUpdateCoordinator::Create();
	TAtomic<int32> CancelledValue{0};
	TFuture<void> LateSubmission = Async(EAsyncExecution::Thread,
		[Cancelled, &CancelledValue]()
		{
			Cancelled->Submit([&CancelledValue](FWebToUEUpdateTransaction& Transaction)
			{
				Transaction.AddStateMutation([&CancelledValue]()
				{
					CancelledValue.IncrementExchange();
				});
			});
		});
	LateSubmission.Wait();
	Cancelled->Shutdown();
	Cancelled->Drain();
	TestEqual(TEXT("Session shutdown drops queued work without committing it"),
		CancelledValue.Load(), 0);
	TestTrue(TEXT("Shutdown records a deterministic inactive drop"),
		Cancelled->GetTrace().Num() == 1 &&
		Cancelled->GetTrace()[0].Outcome == EWebToUEUpdateOutcome::DroppedInactive);
	return true;
}

#endif
