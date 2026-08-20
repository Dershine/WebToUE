#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Templates/Function.h"
#include "WebToUESession.h"

enum class EWebToUEAsyncWorkKind : uint8
{
	Timer,
	CommandResult
};

enum class EWebToUEAsyncOutcome : uint8
{
	TimerFired,
	CommandResolved,
	CommandTimedOut,
	CancelledExplicit,
	CancelledGeneration,
	CancelledSession,
	DroppedStaleGeneration,
	DroppedWrongSession,
	DroppedAlreadyTerminal,
	DroppedInactive
};

enum class EWebToUEAsyncResolveResult : uint8
{
	Resolved,
	Queued,
	RejectedInvalidHandle,
	RejectedInactive,
	DroppedStaleGeneration,
	DroppedWrongSession,
	DroppedAlreadyTerminal
};

enum class EWebToUEAsyncCancelResult : uint8
{
	Cancelled,
	RejectedInvalidHandle,
	RejectedInactive,
	DroppedStaleGeneration,
	DroppedWrongSession,
	DroppedAlreadyTerminal
};

struct WEBTOUERUNTIME_API FWebToUEAsyncHandle
{
	bool IsValid() const { return Session.IsValid() && WorkId != 0; }
	const FWebToUESessionHandle& GetSession() const { return Session; }
	uint64 GetWorkId() const { return WorkId; }

	friend bool operator==(const FWebToUEAsyncHandle& A, const FWebToUEAsyncHandle& B)
	{
		return A.Session == B.Session && A.WorkId == B.WorkId;
	}

private:
	friend class FWebToUEAsyncCoordinator;
	static FWebToUEAsyncHandle Create(FWebToUESessionHandle InSession, uint64 InWorkId)
	{
		FWebToUEAsyncHandle Result;
		Result.Session = InSession;
		Result.WorkId = InWorkId;
		return Result;
	}

	FWebToUESessionHandle Session;
	uint64 WorkId = 0;
};

struct WEBTOUERUNTIME_API FWebToUEAsyncBudget
{
	int32 MaxPendingWork = 256;
	int32 MaxTerminalsPerPump = 64;
	int32 MaxTraceEntries = 256;

	bool Validate(FString& OutError) const;
};

struct WEBTOUERUNTIME_API FWebToUEAsyncTrace
{
	FWebToUEAsyncHandle Handle;
	EWebToUEAsyncWorkKind Kind = EWebToUEAsyncWorkKind::Timer;
	EWebToUEClockDomain ClockDomain = EWebToUEClockDomain::Game;
	EWebToUEAsyncOutcome Outcome = EWebToUEAsyncOutcome::DroppedInactive;
	double DeadlineSeconds = 0.0;
	double ObservedSeconds = 0.0;
};

/**
 * Session-owned deterministic Timer and asynchronous Command Result boundary.
 *
 * Scheduling, cancellation and Pump are Game Thread-only. Command results may arrive from any
 * thread; non-Game Thread producers only enter an MPSC queue. Terminal evaluations always pass
 * through the Session update coordinator. Timers have no implicit ticker and are observed only at
 * an explicit safe-boundary Pump.
 */
class WEBTOUERUNTIME_API FWebToUEAsyncCoordinator final
	: public TSharedFromThis<FWebToUEAsyncCoordinator, ESPMode::ThreadSafe>
{
public:
	static TSharedRef<FWebToUEAsyncCoordinator, ESPMode::ThreadSafe> Create(
		FWebToUESessionHandle Session,
		TSharedRef<IWebToUEClock> Clock,
		TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> UpdateCoordinator,
		const FWebToUEAsyncBudget& Budget = FWebToUEAsyncBudget());

	FWebToUEAsyncHandle ScheduleTimer(
		EWebToUEClockDomain Domain,
		double DelaySeconds,
		FWebToUEUpdateEvaluation&& Evaluation,
		FString& OutError);
	FWebToUEAsyncHandle BeginCommand(
		EWebToUEClockDomain TimeoutDomain,
		double TimeoutSeconds,
		FWebToUEUpdateEvaluation&& TimeoutEvaluation,
		FString& OutError);
	EWebToUEAsyncResolveResult ResolveCommand(
		const FWebToUEAsyncHandle& Handle,
		FWebToUEUpdateEvaluation&& ResultEvaluation);
	EWebToUEAsyncCancelResult Cancel(const FWebToUEAsyncHandle& Handle);

	int32 Pump();
	void AdvanceGeneration(FWebToUESessionHandle NewSession);
	void Shutdown();

	bool IsActive() const { return bAccepting.Load(); }
	int32 GetPendingWorkCount() const;
	TConstArrayView<FWebToUEAsyncTrace> GetTrace() const;
	void ResetTrace();

private:
	struct FWork
	{
		FWebToUEAsyncHandle Handle;
		EWebToUEAsyncWorkKind Kind = EWebToUEAsyncWorkKind::Timer;
		EWebToUEClockDomain Domain = EWebToUEClockDomain::Game;
		double DeadlineSeconds = 0.0;
		FWebToUEUpdateEvaluation TerminalEvaluation;
	};

	struct FPendingResolution
	{
		FWebToUEAsyncHandle Handle;
		FWebToUEUpdateEvaluation Evaluation;
	};

	FWebToUEAsyncCoordinator(
		FWebToUESessionHandle InSession,
		TSharedRef<IWebToUEClock> InClock,
		TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> InUpdateCoordinator,
		const FWebToUEAsyncBudget& InBudget)
		: Session(InSession)
		, Clock(InClock)
		, UpdateCoordinator(InUpdateCoordinator)
		, Budget(InBudget)
	{
	}

	FWebToUEAsyncHandle AddWork(
		EWebToUEAsyncWorkKind Kind,
		EWebToUEClockDomain Domain,
		double DelaySeconds,
		FWebToUEUpdateEvaluation&& Evaluation,
		FString& OutError);
	EWebToUEAsyncResolveResult ResolveOnGameThread(
		const FWebToUEAsyncHandle& Handle,
		FWebToUEUpdateEvaluation&& Evaluation);
	void SchedulePump();
	void RecordTerminal(const FWork& Work, EWebToUEAsyncOutcome Outcome, double ObservedSeconds);
	void RecordDrop(
		const FWebToUEAsyncHandle& Handle,
		EWebToUEAsyncWorkKind Kind,
		EWebToUEAsyncOutcome Outcome);
	void TrimTrace();

	FWebToUESessionHandle Session;
	TSharedRef<IWebToUEClock> Clock;
	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> UpdateCoordinator;
	FWebToUEAsyncBudget Budget;
	TMap<uint64, FWork> Work;
	TQueue<TUniquePtr<FPendingResolution>, EQueueMode::Mpsc> PendingResolutions;
	TArray<FWebToUEAsyncTrace> Trace;
	TAtomic<bool> bAccepting{true};
	TAtomic<bool> bPumpScheduled{false};
	uint64 NextWorkId = 1;
};
