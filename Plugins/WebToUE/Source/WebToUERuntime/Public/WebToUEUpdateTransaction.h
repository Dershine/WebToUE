#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Templates/Function.h"

enum class EWebToUEUpdateSubmitResult : uint8
{
	Executed,
	Queued,
	QueuedReentrant,
	RejectedInactive
};

enum class EWebToUEUpdateOutcome : uint8
{
	Committed,
	RejectedEvaluation,
	RejectedStructuralMutationDuringTraversal,
	RejectedBudget,
	DroppedInactive
};

struct WEBTOUERUNTIME_API FWebToUEUpdateBudget
{
	int32 MaxEvaluationsPerTransaction = 64;
	int32 MaxMutationsPerTransaction = 256;
	int32 MaxPostCommitEffectsPerTransaction = 64;
	int32 MaxTransactionsPerDrain = 8;
	int32 MaxTraceEntries = 128;

	bool Validate(FString& OutError) const;
};

struct WEBTOUERUNTIME_API FWebToUEUpdateTrace
{
	uint64 TransactionId = 0;
	EWebToUEUpdateOutcome Outcome = EWebToUEUpdateOutcome::Committed;
	int32 EvaluationCount = 0;
	int32 StateMutationCount = 0;
	int32 StructuralMutationCount = 0;
	int32 PostCommitEffectCount = 0;
	FString Diagnostic;
};

class FWebToUEUpdateCoordinator;

/**
 * Evaluation-only collector for one atomic UI update.
 *
 * Evaluation may fail and is not allowed to mutate Runtime state directly. Collected mutation
 * callbacks are assumed infallible after evaluation and run only when the whole transaction is
 * accepted. Effects run after every mutation has committed.
 */
class WEBTOUERUNTIME_API FWebToUEUpdateTransaction final
{
public:
	FWebToUEUpdateTransaction(const FWebToUEUpdateTransaction&) = delete;
	FWebToUEUpdateTransaction& operator=(const FWebToUEUpdateTransaction&) = delete;
	FWebToUEUpdateTransaction(FWebToUEUpdateTransaction&&) = delete;
	FWebToUEUpdateTransaction& operator=(FWebToUEUpdateTransaction&&) = delete;

	bool AddStateMutation(TUniqueFunction<void()>&& Mutation);
	bool AddStructuralMutation(TUniqueFunction<void()>&& Mutation);
	bool AddPostCommitEffect(TUniqueFunction<void()>&& Effect);
	void Reject(FString Diagnostic);
	bool IsRejected() const { return Outcome != EWebToUEUpdateOutcome::Committed; }

private:
	friend class FWebToUEUpdateCoordinator;
	explicit FWebToUEUpdateTransaction(const FWebToUEUpdateBudget& InBudget)
		: Budget(InBudget)
	{
	}

	bool ReserveMutation();
	void Reject(EWebToUEUpdateOutcome InOutcome, FString InDiagnostic);

	FWebToUEUpdateBudget Budget;
	EWebToUEUpdateOutcome Outcome = EWebToUEUpdateOutcome::Committed;
	FString Diagnostic;
	bool bCurrentEvaluationOriginatedDuringTraversal = false;
	TArray<TUniqueFunction<void()>> StateMutations;
	TArray<TUniqueFunction<void()>> StructuralMutations;
	TArray<TUniqueFunction<void()>> PostCommitEffects;
};

using FWebToUEUpdateEvaluation = TUniqueFunction<void(FWebToUEUpdateTransaction&)>;

/** Marks a Runtime tree walk whose originating request may not collect structural mutation. */
class WEBTOUERUNTIME_API FWebToUEUpdateTraversalScope final
{
public:
	FWebToUEUpdateTraversalScope() = default;
	FWebToUEUpdateTraversalScope(FWebToUEUpdateTraversalScope&& Other) noexcept;
	FWebToUEUpdateTraversalScope& operator=(FWebToUEUpdateTraversalScope&& Other) noexcept;
	~FWebToUEUpdateTraversalScope();

	FWebToUEUpdateTraversalScope(const FWebToUEUpdateTraversalScope&) = delete;
	FWebToUEUpdateTraversalScope& operator=(const FWebToUEUpdateTraversalScope&) = delete;

private:
	friend class FWebToUEUpdateCoordinator;
	explicit FWebToUEUpdateTraversalScope(
		TWeakPtr<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> InCoordinator)
		: Coordinator(MoveTemp(InCoordinator))
	{
	}

	void Release();
	TWeakPtr<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Coordinator;
};

/**
 * Session-owned, Game Thread committing update boundary.
 *
 * Producers may submit immutable evaluation work from any thread. Non-Game Thread work is queued
 * through MPSC and scheduled onto the Game Thread. Evaluation reentrancy joins the active atomic
 * transaction; commit/effect reentrancy starts a later transaction. All paths are budgeted.
 */
class WEBTOUERUNTIME_API FWebToUEUpdateCoordinator final
	: public TSharedFromThis<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe>
{
public:
	static TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Create(
		const FWebToUEUpdateBudget& Budget = FWebToUEUpdateBudget());

	EWebToUEUpdateSubmitResult Submit(FWebToUEUpdateEvaluation&& Evaluation);
	FWebToUEUpdateTraversalScope BeginTraversal();
	void Drain();
	void Shutdown();

	bool IsActive() const { return bAccepting.Load(); }
	TConstArrayView<FWebToUEUpdateTrace> GetTrace() const;
	void ResetTrace();

private:
	friend class FWebToUEUpdateTraversalScope;

	enum class EPhase : uint8
	{
		Idle,
		Evaluating,
		Committing,
		PostCommit
	};

	struct FPendingEvaluation
	{
		FWebToUEUpdateEvaluation Evaluation;
		bool bOriginatedDuringTraversal = false;
	};

	explicit FWebToUEUpdateCoordinator(const FWebToUEUpdateBudget& InBudget)
		: Budget(InBudget)
	{
	}

	void ScheduleDrain();
	void EndTraversal();
	void RejectPendingForBudget();
	void RejectPendingAsInactive();
	FWebToUEUpdateTrace& AddTraceEntry();
	void RecordTrace(const FWebToUEUpdateTransaction& Transaction, int32 EvaluationCount);

	FWebToUEUpdateBudget Budget;
	TQueue<TUniquePtr<FPendingEvaluation>, EQueueMode::Mpsc> PendingEvaluations;
	TArray<TUniquePtr<FPendingEvaluation>> ActiveEvaluations;
	TArray<FWebToUEUpdateTrace> Trace;
	TAtomic<bool> bAccepting{true};
	TAtomic<bool> bDrainScheduled{false};
	EPhase Phase = EPhase::Idle;
	int32 TraversalDepth = 0;
	bool bDraining = false;
	uint64 NextTransactionId = 1;
};
