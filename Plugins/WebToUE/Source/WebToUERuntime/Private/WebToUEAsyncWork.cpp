#include "WebToUEAsyncWork.h"

#include "Async/Async.h"

bool FWebToUEAsyncBudget::Validate(FString& OutError) const
{
	OutError.Reset();
	if (MaxPendingWork <= 0 || MaxTerminalsPerPump <= 0 || MaxTraceEntries <= 0)
	{
		OutError = TEXT("Every WebToUE async-work budget must be greater than zero.");
		return false;
	}
	return true;
}

TSharedRef<FWebToUEAsyncCoordinator, ESPMode::ThreadSafe> FWebToUEAsyncCoordinator::Create(
	FWebToUESessionHandle Session,
	TSharedRef<IWebToUEClock> Clock,
	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> UpdateCoordinator,
	const FWebToUEAsyncBudget& Budget)
{
	check(IsInGameThread());
	check(Session.IsValid());
	FString Error;
	checkf(Budget.Validate(Error), TEXT("%s"), *Error);
	return MakeShareable(new FWebToUEAsyncCoordinator(Session, Clock, UpdateCoordinator, Budget));
}

FWebToUEAsyncHandle FWebToUEAsyncCoordinator::ScheduleTimer(
	EWebToUEClockDomain Domain,
	double DelaySeconds,
	FWebToUEUpdateEvaluation&& Evaluation,
	FString& OutError)
{
	return AddWork(EWebToUEAsyncWorkKind::Timer, Domain, DelaySeconds,
		MoveTemp(Evaluation), OutError);
}

FWebToUEAsyncHandle FWebToUEAsyncCoordinator::BeginCommand(
	EWebToUEClockDomain TimeoutDomain,
	double TimeoutSeconds,
	FWebToUEUpdateEvaluation&& TimeoutEvaluation,
	FString& OutError)
{
	return AddWork(EWebToUEAsyncWorkKind::CommandResult, TimeoutDomain, TimeoutSeconds,
		MoveTemp(TimeoutEvaluation), OutError);
}

FWebToUEAsyncHandle FWebToUEAsyncCoordinator::AddWork(
	EWebToUEAsyncWorkKind Kind,
	EWebToUEClockDomain Domain,
	double DelaySeconds,
	FWebToUEUpdateEvaluation&& Evaluation,
	FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("UI async work must be scheduled on the Game Thread.");
		return FWebToUEAsyncHandle();
	}
	if (!bAccepting.Load())
	{
		OutError = TEXT("The UI Session no longer accepts async work.");
		return FWebToUEAsyncHandle();
	}
	if (!Clock->SupportsDomain(Domain))
	{
		OutError = TEXT("The UI Session Clock does not support the requested time domain.");
		return FWebToUEAsyncHandle();
	}
	if (!Evaluation || !FMath::IsFinite(DelaySeconds) || DelaySeconds < 0.0)
	{
		OutError = TEXT("UI async work requires a callback and finite non-negative delay.");
		return FWebToUEAsyncHandle();
	}
	if (Work.Num() >= Budget.MaxPendingWork)
	{
		OutError = TEXT("The UI Session exceeded MaxPendingWork.");
		return FWebToUEAsyncHandle();
	}

	const double Now = Clock->GetTimeSeconds(Domain);
	const double Deadline = Now + DelaySeconds;
	if (!FMath::IsFinite(Now) || !FMath::IsFinite(Deadline))
	{
		OutError = TEXT("The UI Clock produced a non-finite deadline.");
		return FWebToUEAsyncHandle();
	}

	uint64 WorkId = NextWorkId++;
	if (WorkId == 0)
	{
		WorkId = NextWorkId++;
	}
	FWork NewWork;
	NewWork.Handle = FWebToUEAsyncHandle::Create(Session, WorkId);
	NewWork.Kind = Kind;
	NewWork.Domain = Domain;
	NewWork.DeadlineSeconds = Deadline;
	NewWork.TerminalEvaluation = MoveTemp(Evaluation);
	const FWebToUEAsyncHandle Handle = NewWork.Handle;
	Work.Add(WorkId, MoveTemp(NewWork));
	return Handle;
}

EWebToUEAsyncResolveResult FWebToUEAsyncCoordinator::ResolveCommand(
	const FWebToUEAsyncHandle& Handle,
	FWebToUEUpdateEvaluation&& ResultEvaluation)
{
	if (!Handle.IsValid() || !ResultEvaluation)
	{
		return EWebToUEAsyncResolveResult::RejectedInvalidHandle;
	}
	if (!bAccepting.Load())
	{
		return EWebToUEAsyncResolveResult::RejectedInactive;
	}
	if (IsInGameThread())
	{
		return ResolveOnGameThread(Handle, MoveTemp(ResultEvaluation));
	}

	TUniquePtr<FPendingResolution> Pending = MakeUnique<FPendingResolution>();
	Pending->Handle = Handle;
	Pending->Evaluation = MoveTemp(ResultEvaluation);
	PendingResolutions.Enqueue(MoveTemp(Pending));
	SchedulePump();
	return EWebToUEAsyncResolveResult::Queued;
}

EWebToUEAsyncResolveResult FWebToUEAsyncCoordinator::ResolveOnGameThread(
	const FWebToUEAsyncHandle& Handle,
	FWebToUEUpdateEvaluation&& Evaluation)
{
	check(IsInGameThread());
	if (!bAccepting.Load())
	{
		RecordDrop(Handle, EWebToUEAsyncWorkKind::CommandResult,
			EWebToUEAsyncOutcome::DroppedInactive);
		return EWebToUEAsyncResolveResult::RejectedInactive;
	}
	if (Handle.GetSession().GetSessionId() != Session.GetSessionId())
	{
		RecordDrop(Handle, EWebToUEAsyncWorkKind::CommandResult,
			EWebToUEAsyncOutcome::DroppedWrongSession);
		return EWebToUEAsyncResolveResult::DroppedWrongSession;
	}
	if (Handle.GetSession().GetGeneration() != Session.GetGeneration())
	{
		RecordDrop(Handle, EWebToUEAsyncWorkKind::CommandResult,
			EWebToUEAsyncOutcome::DroppedStaleGeneration);
		return EWebToUEAsyncResolveResult::DroppedStaleGeneration;
	}

	FWork* Existing = Work.Find(Handle.GetWorkId());
	if (!Existing || Existing->Kind != EWebToUEAsyncWorkKind::CommandResult)
	{
		RecordDrop(Handle, EWebToUEAsyncWorkKind::CommandResult,
			EWebToUEAsyncOutcome::DroppedAlreadyTerminal);
		return EWebToUEAsyncResolveResult::DroppedAlreadyTerminal;
	}
	const FWork Terminal = MoveTemp(*Existing);
	Work.Remove(Handle.GetWorkId());
	const double Observed = Clock->GetTimeSeconds(Terminal.Domain);
	UpdateCoordinator->Submit(MoveTemp(Evaluation));
	RecordTerminal(Terminal, EWebToUEAsyncOutcome::CommandResolved, Observed);
	return EWebToUEAsyncResolveResult::Resolved;
}

EWebToUEAsyncCancelResult FWebToUEAsyncCoordinator::Cancel(const FWebToUEAsyncHandle& Handle)
{
	if (!IsInGameThread() || !Handle.IsValid())
	{
		return EWebToUEAsyncCancelResult::RejectedInvalidHandle;
	}
	if (!bAccepting.Load())
	{
		return EWebToUEAsyncCancelResult::RejectedInactive;
	}
	if (Handle.GetSession().GetSessionId() != Session.GetSessionId())
	{
		return EWebToUEAsyncCancelResult::DroppedWrongSession;
	}
	if (Handle.GetSession().GetGeneration() != Session.GetGeneration())
	{
		return EWebToUEAsyncCancelResult::DroppedStaleGeneration;
	}
	FWork* Existing = Work.Find(Handle.GetWorkId());
	if (!Existing)
	{
		return EWebToUEAsyncCancelResult::DroppedAlreadyTerminal;
	}
	const FWork Terminal = MoveTemp(*Existing);
	Work.Remove(Handle.GetWorkId());
	RecordTerminal(Terminal, EWebToUEAsyncOutcome::CancelledExplicit,
		Clock->GetTimeSeconds(Terminal.Domain));
	return EWebToUEAsyncCancelResult::Cancelled;
}

int32 FWebToUEAsyncCoordinator::Pump()
{
	check(IsInGameThread());
	bPumpScheduled.Store(false);
	int32 TerminalCount = 0;
	TUniquePtr<FPendingResolution> Pending;
	while (TerminalCount < Budget.MaxTerminalsPerPump && PendingResolutions.Dequeue(Pending))
	{
		ResolveOnGameThread(Pending->Handle, MoveTemp(Pending->Evaluation));
		++TerminalCount;
	}

	if (!bAccepting.Load())
	{
		while (PendingResolutions.Dequeue(Pending))
		{
			RecordDrop(Pending->Handle, EWebToUEAsyncWorkKind::CommandResult,
				EWebToUEAsyncOutcome::DroppedInactive);
		}
		return TerminalCount;
	}

	const uint64 PumpWorkHighWatermark = NextWorkId - 1;
	TArray<uint64> WorkIds;
	Work.GenerateKeyArray(WorkIds);
	WorkIds.Sort();
	for (const uint64 WorkId : WorkIds)
	{
		if (TerminalCount >= Budget.MaxTerminalsPerPump || WorkId > PumpWorkHighWatermark)
		{
			break;
		}
		FWork* Existing = Work.Find(WorkId);
		if (!Existing)
		{
			continue;
		}
		const double Observed = Clock->GetTimeSeconds(Existing->Domain);
		if (!FMath::IsFinite(Observed) || Observed < Existing->DeadlineSeconds)
		{
			continue;
		}

		FWork Terminal = MoveTemp(*Existing);
		Work.Remove(WorkId);
		UpdateCoordinator->Submit(MoveTemp(Terminal.TerminalEvaluation));
		RecordTerminal(Terminal,
			Terminal.Kind == EWebToUEAsyncWorkKind::Timer
				? EWebToUEAsyncOutcome::TimerFired
				: EWebToUEAsyncOutcome::CommandTimedOut,
			Observed);
		++TerminalCount;
	}
	return TerminalCount;
}

void FWebToUEAsyncCoordinator::AdvanceGeneration(FWebToUESessionHandle NewSession)
{
	check(IsInGameThread());
	check(NewSession.IsValid());
	check(NewSession.GetSessionId() == Session.GetSessionId());
	for (const TPair<uint64, FWork>& Pair : Work)
	{
		RecordTerminal(Pair.Value, EWebToUEAsyncOutcome::CancelledGeneration,
			Clock->GetTimeSeconds(Pair.Value.Domain));
	}
	Work.Reset();
	Session = NewSession;
}

void FWebToUEAsyncCoordinator::Shutdown()
{
	check(IsInGameThread());
	if (!bAccepting.Exchange(false))
	{
		return;
	}
	for (const TPair<uint64, FWork>& Pair : Work)
	{
		RecordTerminal(Pair.Value, EWebToUEAsyncOutcome::CancelledSession,
			Clock->GetTimeSeconds(Pair.Value.Domain));
	}
	Work.Reset();
	TUniquePtr<FPendingResolution> Pending;
	while (PendingResolutions.Dequeue(Pending))
	{
		RecordDrop(Pending->Handle, EWebToUEAsyncWorkKind::CommandResult,
			EWebToUEAsyncOutcome::DroppedInactive);
	}
}

int32 FWebToUEAsyncCoordinator::GetPendingWorkCount() const
{
	check(IsInGameThread());
	return Work.Num();
}

TConstArrayView<FWebToUEAsyncTrace> FWebToUEAsyncCoordinator::GetTrace() const
{
	check(IsInGameThread());
	return Trace;
}

void FWebToUEAsyncCoordinator::ResetTrace()
{
	check(IsInGameThread());
	Trace.Reset();
}

void FWebToUEAsyncCoordinator::SchedulePump()
{
	if (bPumpScheduled.Exchange(true))
	{
		return;
	}
	TWeakPtr<FWebToUEAsyncCoordinator, ESPMode::ThreadSafe> WeakCoordinator = AsShared();
	AsyncTask(ENamedThreads::GameThread, [WeakCoordinator]()
	{
		if (TSharedPtr<FWebToUEAsyncCoordinator, ESPMode::ThreadSafe> Coordinator =
			WeakCoordinator.Pin())
		{
			Coordinator->Pump();
		}
	});
}

void FWebToUEAsyncCoordinator::RecordTerminal(
	const FWork& WorkItem, EWebToUEAsyncOutcome Outcome, double ObservedSeconds)
{
	FWebToUEAsyncTrace& Entry = Trace.AddDefaulted_GetRef();
	Entry.Handle = WorkItem.Handle;
	Entry.Kind = WorkItem.Kind;
	Entry.ClockDomain = WorkItem.Domain;
	Entry.Outcome = Outcome;
	Entry.DeadlineSeconds = WorkItem.DeadlineSeconds;
	Entry.ObservedSeconds = ObservedSeconds;
	TrimTrace();
}

void FWebToUEAsyncCoordinator::RecordDrop(
	const FWebToUEAsyncHandle& Handle,
	EWebToUEAsyncWorkKind Kind,
	EWebToUEAsyncOutcome Outcome)
{
	FWebToUEAsyncTrace& Entry = Trace.AddDefaulted_GetRef();
	Entry.Handle = Handle;
	Entry.Kind = Kind;
	Entry.Outcome = Outcome;
	TrimTrace();
}

void FWebToUEAsyncCoordinator::TrimTrace()
{
	if (Trace.Num() > Budget.MaxTraceEntries)
	{
		Trace.RemoveAt(0, Trace.Num() - Budget.MaxTraceEntries, EAllowShrinking::No);
	}
}
