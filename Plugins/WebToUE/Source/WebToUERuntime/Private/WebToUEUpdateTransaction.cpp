#include "WebToUEUpdateTransaction.h"

#include "Async/Async.h"

bool FWebToUEUpdateBudget::Validate(FString& OutError) const
{
	OutError.Reset();
	if (MaxEvaluationsPerTransaction <= 0 || MaxMutationsPerTransaction <= 0 ||
		MaxPostCommitEffectsPerTransaction <= 0 || MaxTransactionsPerDrain <= 0 ||
		MaxTraceEntries <= 0)
	{
		OutError = TEXT("Every WebToUE update budget must be greater than zero.");
		return false;
	}
	return true;
}

bool FWebToUEUpdateTransaction::ReserveMutation()
{
	if (StateMutations.Num() + StructuralMutations.Num() >= Budget.MaxMutationsPerTransaction)
	{
		Reject(EWebToUEUpdateOutcome::RejectedBudget,
			TEXT("The transaction exceeded MaxMutationsPerTransaction."));
		return false;
	}
	return true;
}

bool FWebToUEUpdateTransaction::AddStateMutation(TUniqueFunction<void()>&& Mutation)
{
	if (IsRejected() || !Mutation || !ReserveMutation())
	{
		return false;
	}
	StateMutations.Add(MoveTemp(Mutation));
	return true;
}

bool FWebToUEUpdateTransaction::AddStructuralMutation(TUniqueFunction<void()>&& Mutation)
{
	if (IsRejected() || !Mutation)
	{
		return false;
	}
	if (bCurrentEvaluationOriginatedDuringTraversal)
	{
		Reject(EWebToUEUpdateOutcome::RejectedStructuralMutationDuringTraversal,
			TEXT("Structural Mutation cannot originate during a Runtime tree traversal."));
		return false;
	}
	if (!ReserveMutation())
	{
		return false;
	}
	StructuralMutations.Add(MoveTemp(Mutation));
	return true;
}

bool FWebToUEUpdateTransaction::AddPostCommitEffect(TUniqueFunction<void()>&& Effect)
{
	if (IsRejected() || !Effect)
	{
		return false;
	}
	if (PostCommitEffects.Num() >= Budget.MaxPostCommitEffectsPerTransaction)
	{
		Reject(EWebToUEUpdateOutcome::RejectedBudget,
			TEXT("The transaction exceeded MaxPostCommitEffectsPerTransaction."));
		return false;
	}
	PostCommitEffects.Add(MoveTemp(Effect));
	return true;
}

void FWebToUEUpdateTransaction::Reject(FString InDiagnostic)
{
	Reject(EWebToUEUpdateOutcome::RejectedEvaluation, MoveTemp(InDiagnostic));
}

void FWebToUEUpdateTransaction::Reject(
	EWebToUEUpdateOutcome InOutcome, FString InDiagnostic)
{
	if (IsRejected())
	{
		return;
	}
	Outcome = InOutcome;
	Diagnostic = MoveTemp(InDiagnostic);
}

TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe>
FWebToUEUpdateCoordinator::Create(const FWebToUEUpdateBudget& Budget)
{
	FString Error;
	checkf(Budget.Validate(Error), TEXT("Invalid WebToUE update budget: %s"), *Error);
	return MakeShareable(new FWebToUEUpdateCoordinator(Budget));
}

EWebToUEUpdateSubmitResult FWebToUEUpdateCoordinator::Submit(
	FWebToUEUpdateEvaluation&& Evaluation)
{
	if (!bAccepting.Load() || !Evaluation)
	{
		return EWebToUEUpdateSubmitResult::RejectedInactive;
	}

	TUniquePtr<FPendingEvaluation> Pending = MakeUnique<FPendingEvaluation>();
	Pending->Evaluation = MoveTemp(Evaluation);
	if (IsInGameThread())
	{
		Pending->bOriginatedDuringTraversal = TraversalDepth > 0;
		if (Phase == EPhase::Evaluating)
		{
			ActiveEvaluations.Add(MoveTemp(Pending));
			return EWebToUEUpdateSubmitResult::QueuedReentrant;
		}
	}

	PendingEvaluations.Enqueue(MoveTemp(Pending));
	if (IsInGameThread() && Phase == EPhase::Idle && TraversalDepth == 0 && !bDraining)
	{
		Drain();
		return EWebToUEUpdateSubmitResult::Executed;
	}
	ScheduleDrain();
	return EWebToUEUpdateSubmitResult::Queued;
}

FWebToUEUpdateTraversalScope FWebToUEUpdateCoordinator::BeginTraversal()
{
	check(IsInGameThread());
	check(bAccepting.Load());
	++TraversalDepth;
	return FWebToUEUpdateTraversalScope(AsShared());
}

void FWebToUEUpdateCoordinator::Drain()
{
	check(IsInGameThread());
	bDrainScheduled.Exchange(false);
	if (!bAccepting.Load())
	{
		RejectPendingAsInactive();
		return;
	}
	if (bDraining || TraversalDepth > 0)
	{
		return;
	}

	TGuardValue<bool> DrainGuard(bDraining, true);
	int32 TransactionCount = 0;
	while (TransactionCount < Budget.MaxTransactionsPerDrain)
	{
		TUniquePtr<FPendingEvaluation> RootEvaluation;
		if (!PendingEvaluations.Dequeue(RootEvaluation))
		{
			break;
		}
		++TransactionCount;
		FWebToUEUpdateTransaction Transaction(Budget);
		ActiveEvaluations.Reset();
		ActiveEvaluations.Add(MoveTemp(RootEvaluation));
		Phase = EPhase::Evaluating;

		int32 EvaluationCount = 0;
		for (int32 EvaluationIndex = 0; EvaluationIndex < ActiveEvaluations.Num(); ++EvaluationIndex)
		{
			if (EvaluationCount >= Budget.MaxEvaluationsPerTransaction)
			{
				Transaction.Reject(EWebToUEUpdateOutcome::RejectedBudget,
					TEXT("The transaction exceeded MaxEvaluationsPerTransaction."));
				break;
			}
			FPendingEvaluation& Pending = *ActiveEvaluations[EvaluationIndex];
			Transaction.bCurrentEvaluationOriginatedDuringTraversal =
				Pending.bOriginatedDuringTraversal;
			++EvaluationCount;
			Pending.Evaluation(Transaction);
			if (!bAccepting.Load())
			{
				Transaction.Reject(EWebToUEUpdateOutcome::DroppedInactive,
					TEXT("The owning UI Session was invalidated during evaluation."));
			}
			if (Transaction.IsRejected())
			{
				break;
			}
		}

		if (!Transaction.IsRejected())
		{
			Phase = EPhase::Committing;
			for (TUniqueFunction<void()>& Mutation : Transaction.StateMutations)
			{
				Mutation();
			}
			for (TUniqueFunction<void()>& Mutation : Transaction.StructuralMutations)
			{
				Mutation();
			}
			Phase = EPhase::PostCommit;
			for (TUniqueFunction<void()>& Effect : Transaction.PostCommitEffects)
			{
				Effect();
			}
		}

		Phase = EPhase::Idle;
		RecordTrace(Transaction, EvaluationCount);
		ActiveEvaluations.Reset();
	}

	if (!PendingEvaluations.IsEmpty())
	{
		RejectPendingForBudget();
	}
}

void FWebToUEUpdateCoordinator::Shutdown()
{
	check(IsInGameThread());
	if (!bAccepting.Exchange(false))
	{
		return;
	}
	RejectPendingAsInactive();
	if (!bDraining)
	{
		ActiveEvaluations.Reset();
		Phase = EPhase::Idle;
	}
}

TConstArrayView<FWebToUEUpdateTrace> FWebToUEUpdateCoordinator::GetTrace() const
{
	check(IsInGameThread());
	return Trace;
}

void FWebToUEUpdateCoordinator::ResetTrace()
{
	check(IsInGameThread());
	Trace.Reset();
}

void FWebToUEUpdateCoordinator::ScheduleDrain()
{
	if (bDrainScheduled.Exchange(true))
	{
		return;
	}
	TWeakPtr<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> WeakCoordinator = AsShared();
	AsyncTask(ENamedThreads::GameThread, [WeakCoordinator]()
	{
		if (TSharedPtr<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Coordinator =
			WeakCoordinator.Pin())
		{
			Coordinator->Drain();
		}
	});
}

void FWebToUEUpdateCoordinator::EndTraversal()
{
	check(IsInGameThread());
	check(TraversalDepth > 0);
	--TraversalDepth;
	if (TraversalDepth == 0 && !PendingEvaluations.IsEmpty())
	{
		ScheduleDrain();
	}
}

void FWebToUEUpdateCoordinator::RejectPendingForBudget()
{
	TUniquePtr<FPendingEvaluation> Pending;
	while (PendingEvaluations.Dequeue(Pending))
	{
		FWebToUEUpdateTrace& Entry = AddTraceEntry();
		Entry.TransactionId = NextTransactionId++;
		Entry.Outcome = EWebToUEUpdateOutcome::RejectedBudget;
		Entry.Diagnostic = TEXT("The drain exceeded MaxTransactionsPerDrain.");
	}
}

void FWebToUEUpdateCoordinator::RejectPendingAsInactive()
{
	check(IsInGameThread());
	TUniquePtr<FPendingEvaluation> Pending;
	while (PendingEvaluations.Dequeue(Pending))
	{
		FWebToUEUpdateTrace& Entry = AddTraceEntry();
		Entry.TransactionId = NextTransactionId++;
		Entry.Outcome = EWebToUEUpdateOutcome::DroppedInactive;
		Entry.Diagnostic = TEXT("The owning UI Session was invalidated before evaluation.");
	}
}

FWebToUEUpdateTrace& FWebToUEUpdateCoordinator::AddTraceEntry()
{
	check(IsInGameThread());
	if (Trace.Num() >= Budget.MaxTraceEntries)
	{
		Trace.RemoveAt(0, Trace.Num() - Budget.MaxTraceEntries + 1, EAllowShrinking::No);
	}
	return Trace.AddDefaulted_GetRef();
}

void FWebToUEUpdateCoordinator::RecordTrace(
	const FWebToUEUpdateTransaction& Transaction, int32 EvaluationCount)
{
	FWebToUEUpdateTrace& Entry = AddTraceEntry();
	Entry.TransactionId = NextTransactionId++;
	Entry.Outcome = Transaction.Outcome;
	Entry.EvaluationCount = EvaluationCount;
	Entry.StateMutationCount = Transaction.StateMutations.Num();
	Entry.StructuralMutationCount = Transaction.StructuralMutations.Num();
	Entry.PostCommitEffectCount = Transaction.PostCommitEffects.Num();
	Entry.Diagnostic = Transaction.Diagnostic;
}

FWebToUEUpdateTraversalScope::FWebToUEUpdateTraversalScope(
	FWebToUEUpdateTraversalScope&& Other) noexcept
	: Coordinator(MoveTemp(Other.Coordinator))
{
}

FWebToUEUpdateTraversalScope& FWebToUEUpdateTraversalScope::operator=(
	FWebToUEUpdateTraversalScope&& Other) noexcept
{
	if (this != &Other)
	{
		Release();
		Coordinator = MoveTemp(Other.Coordinator);
	}
	return *this;
}

FWebToUEUpdateTraversalScope::~FWebToUEUpdateTraversalScope()
{
	Release();
}

void FWebToUEUpdateTraversalScope::Release()
{
	if (TSharedPtr<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Pinned = Coordinator.Pin())
	{
		Pinned->EndTraversal();
	}
	Coordinator.Reset();
}
