#include "WebToUESession.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Templates/Atomic.h"

namespace WebToUE::Session::Private
{
	static TAtomic<uint64> NextSessionId{1};

	static uint64 AllocateSessionId()
	{
		uint64 Result = NextSessionId.IncrementExchange();
		if (Result == 0)
		{
			Result = NextSessionId.IncrementExchange();
		}
		return Result;
	}

	static bool IsNamespacedName(FName Name)
	{
		if (Name.IsNone())
		{
			return false;
		}
		const FString Value = Name.ToString();
		if (!Value.Contains(TEXT(".")))
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (FChar::IsWhitespace(Character))
			{
				return false;
			}
		}
		return true;
	}
}

double FWebToUEWorldGameClock::GetTimeSeconds() const
{
	const UWorld* ResolvedWorld = World.Get();
	return ResolvedWorld ? static_cast<double>(ResolvedWorld->GetTimeSeconds()) : 0.0;
}

bool FWebToUEFeedbackRequest::Validate(FString& OutError) const
{
	OutError.Reset();
	if (!Session.IsValid())
	{
		OutError = TEXT("Feedback request requires a valid UI Session generation.");
		return false;
	}
	if (!WebToUE::Session::Private::IsNamespacedName(CueId))
	{
		OutError = TEXT("Feedback CueId must be namespaced and contain no whitespace.");
		return false;
	}
	if (EventCorrelationId == 0)
	{
		OutError = TEXT("Feedback request requires a non-zero EventCorrelationId.");
		return false;
	}
	return true;
}

bool FWebToUENullFeedbackRouter::RouteCommittedFeedback(
	const FWebToUEFeedbackRequest& Request,
	const FWebToUEFeedbackRoutingContext& Context)
{
	return false;
}

bool FWebToUERecordingFeedbackRouter::RouteCommittedFeedback(
	const FWebToUEFeedbackRequest& Request,
	const FWebToUEFeedbackRoutingContext& Context)
{
	check(IsInGameThread());
	Records.Add({Request, Context});
	return true;
}

TSharedPtr<FWebToUESession> FWebToUESession::Create(
	const FWebToUESessionCreateParams& Params, FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("UI Session creation is Game Thread-only.");
		return nullptr;
	}
	if (!Params.LocalPlayer || !Params.World)
	{
		OutError = TEXT("UI Session requires LocalPlayer and World context.");
		return nullptr;
	}
	if (UWorld* PlayerWorld = Params.LocalPlayer->GetWorld();
		PlayerWorld && PlayerWorld != Params.World)
	{
		OutError = TEXT("UI Session LocalPlayer and World contexts do not match.");
		return nullptr;
	}
	if (Params.Surface.SurfaceId.IsNone())
	{
		OutError = TEXT("UI Session requires a stable SurfaceId.");
		return nullptr;
	}
	if (Params.Environment.DpiScale <= 0.0f || !FMath::IsFinite(Params.Environment.DpiScale))
	{
		OutError = TEXT("UI Session requires a finite positive environment DPI scale.");
		return nullptr;
	}

	FWebToUESessionCreateParams Resolved = Params;
	if (!Resolved.Clock)
	{
		Resolved.Clock = MakeShared<FWebToUEWorldGameClock>(Params.World);
	}
	if (!Resolved.FeedbackRouter)
	{
		Resolved.FeedbackRouter = MakeShared<FWebToUENullFeedbackRouter>();
	}
	return TSharedPtr<FWebToUESession>(new FWebToUESession(Resolved));
}

FWebToUESession::FWebToUESession(const FWebToUESessionCreateParams& Params)
	: SessionId(WebToUE::Session::Private::AllocateSessionId())
	, LocalPlayer(Params.LocalPlayer)
	, World(Params.World)
	, Surface(Params.Surface)
	, DataContext(Params.DataContext)
	, CommandContext(Params.CommandContext)
	, Environment(Params.Environment)
	, Clock(Params.Clock)
	, FeedbackRouter(Params.FeedbackRouter)
	, UpdateCoordinator(FWebToUEUpdateCoordinator::Create())
{
}

bool FWebToUESession::IsActive() const
{
	return IsInGameThread() && bActive && LocalPlayer.IsValid() && World.IsValid();
}

FWebToUESessionHandle FWebToUESession::GetHandle() const
{
	return bActive ? FWebToUESessionHandle::Create(SessionId, Generation)
		: FWebToUESessionHandle();
}

FWebToUESessionHandle FWebToUESession::AdvanceGeneration()
{
	check(IsInGameThread());
	if (!bActive)
	{
		return FWebToUESessionHandle();
	}
	++Generation;
	if (Generation == 0)
	{
		++Generation;
	}
	return GetHandle();
}

void FWebToUESession::Invalidate()
{
	check(IsInGameThread());
	if (!bActive)
	{
		return;
	}
	UpdateCoordinator->Shutdown();
	bActive = false;
	++Generation;
	if (Generation == 0)
	{
		++Generation;
	}
	DataContext.Reset();
	CommandContext.Reset();
	LocalPlayer.Reset();
	World.Reset();
}

FWebToUEFeedbackRequest FWebToUESession::MakeFeedbackRequest(
	FName CueId,
	FName SourceSemanticKey,
	uint64 EventCorrelationId,
	EWebToUEInputModality InputModality,
	EWebToUEFeedbackScope Scope) const
{
	FWebToUEFeedbackRequest Result;
	Result.Session = GetHandle();
	Result.CueId = CueId;
	Result.SourceSemanticKey = SourceSemanticKey;
	Result.EventCorrelationId = EventCorrelationId;
	Result.InputModality = InputModality;
	Result.Scope = Scope;
	return Result;
}

EWebToUEFeedbackDispatchResult FWebToUESession::DispatchCommittedFeedback(
	const FWebToUEFeedbackRequest& Request)
{
	check(IsInGameThread());
	if (!IsActive())
	{
		return EWebToUEFeedbackDispatchResult::DroppedInactiveSession;
	}
	FString Error;
	if (!Request.Validate(Error))
	{
		return EWebToUEFeedbackDispatchResult::DroppedInvalidRequest;
	}
	if (Request.Session.GetSessionId() != SessionId)
	{
		return EWebToUEFeedbackDispatchResult::DroppedWrongSession;
	}
	if (Request.Session.GetGeneration() != Generation)
	{
		return EWebToUEFeedbackDispatchResult::DroppedStaleGeneration;
	}

	FWebToUEFeedbackRoutingContext Context;
	Context.Session = GetHandle();
	Context.LocalPlayer = LocalPlayer;
	Context.World = World;
	Context.Surface = Surface;
	Context.Environment = Environment;
	return FeedbackRouter->RouteCommittedFeedback(Request, Context)
		? EWebToUEFeedbackDispatchResult::Routed
		: EWebToUEFeedbackDispatchResult::DroppedByRouter;
}
