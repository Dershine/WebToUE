#pragma once

#include "CoreMinimal.h"
#include "WebToUEClock.h"
#include "WebToUEUpdateTransaction.h"

class ULocalPlayer;
class UObject;
class UWorld;
class FWebToUEAsyncCoordinator;

/** Stable identity for one active UI Session generation. */
struct WEBTOUERUNTIME_API FWebToUESessionHandle
{
	static FWebToUESessionHandle Create(uint64 InSessionId, uint32 InGeneration)
	{
		FWebToUESessionHandle Result;
		Result.SessionId = InSessionId;
		Result.Generation = InGeneration;
		return Result;
	}

	bool IsValid() const { return SessionId != 0 && Generation != 0; }
	uint64 GetSessionId() const { return SessionId; }
	uint32 GetGeneration() const { return Generation; }

	friend bool operator==(const FWebToUESessionHandle& A, const FWebToUESessionHandle& B)
	{
		return A.SessionId == B.SessionId && A.Generation == B.Generation;
	}

	friend bool operator!=(const FWebToUESessionHandle& A, const FWebToUESessionHandle& B)
	{
		return !(A == B);
	}

private:
	uint64 SessionId = 0;
	uint32 Generation = 0;
};

enum class EWebToUESurfaceKind : uint8
{
	Screen,
	World
};

/** Host-owned Surface identity. World positioning and audio policy remain later route concerns. */
struct WEBTOUERUNTIME_API FWebToUESurfaceContext
{
	EWebToUESurfaceKind Kind = EWebToUESurfaceKind::Screen;
	FName SurfaceId;
	TWeakObjectPtr<UObject> Owner;
	/** Host-resolved audio anchor. Behavior and Cue requests never submit raw world coordinates. */
	bool bHasFeedbackWorldLocation = false;
	FVector FeedbackWorldLocation = FVector::ZeroVector;
};

/** Session environment snapshot supplied by the Host. */
struct WEBTOUERUNTIME_API FWebToUEEnvironmentContext
{
	FName CultureName;
	FVector2f ViewportSize = FVector2f::ZeroVector;
	float DpiScale = 1.0f;
	bool bReducedMotion = false;
};

enum class EWebToUEInputModality : uint8
{
	Unknown,
	Pointer,
	Keyboard,
	Gamepad,
	Touch
};

enum class EWebToUEFeedbackScope : uint8
{
	Session,
	LocalPlayer,
	Viewport,
	Surface
};

/** A semantic feedback intent collected by an event and dispatched only after commit. */
struct WEBTOUERUNTIME_API FWebToUEFeedbackRequest
{
	FWebToUESessionHandle Session;
	FName CueId;
	FName SourceSemanticKey;
	uint64 EventCorrelationId = 0;
	EWebToUEInputModality InputModality = EWebToUEInputModality::Unknown;
	EWebToUEFeedbackScope Scope = EWebToUEFeedbackScope::Session;

	bool Validate(FString& OutError) const;
};

/** Immutable routing context resolved from the owning UI Session. */
struct WEBTOUERUNTIME_API FWebToUEFeedbackRoutingContext
{
	FWebToUESessionHandle Session;
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;
	TWeakObjectPtr<UWorld> World;
	FWebToUESurfaceContext Surface;
	FWebToUEEnvironmentContext Environment;
	double RealTimeSeconds = 0.0;
};

/** Project-injected feedback boundary. Actual Sound/Profile policy belongs to M4. */
class WEBTOUERUNTIME_API IWebToUEFeedbackRouter
{
public:
	virtual ~IWebToUEFeedbackRouter() = default;
	virtual bool ActivateSession(
		const FWebToUEFeedbackRoutingContext& Context, FString& OutError)
	{
		OutError.Reset();
		return true;
	}
	virtual void ObserveRequestedFeedback(const FWebToUEFeedbackRequest& Request) {}
	virtual void OnSessionGenerationAdvanced(
		const FWebToUEFeedbackRoutingContext& Context) {}
	virtual void DeactivateSession(const FWebToUESessionHandle& Session) {}
	virtual bool IsReadyForInteraction(const FWebToUESessionHandle& Session) const
	{
		return true;
	}
	virtual bool RouteCommittedFeedback(
		const FWebToUEFeedbackRequest& Request,
		const FWebToUEFeedbackRoutingContext& Context) = 0;
};

class WEBTOUERUNTIME_API FWebToUENullFeedbackRouter final : public IWebToUEFeedbackRouter
{
public:
	virtual bool RouteCommittedFeedback(
		const FWebToUEFeedbackRequest& Request,
		const FWebToUEFeedbackRoutingContext& Context) override;
};

struct WEBTOUERUNTIME_API FWebToUERecordedFeedback
{
	FWebToUEFeedbackRequest Request;
	FWebToUEFeedbackRoutingContext Context;
};

/** Deterministic test/project adapter that records routing without playing media. */
class WEBTOUERUNTIME_API FWebToUERecordingFeedbackRouter final : public IWebToUEFeedbackRouter
{
public:
	virtual bool RouteCommittedFeedback(
		const FWebToUEFeedbackRequest& Request,
		const FWebToUEFeedbackRoutingContext& Context) override;

	TConstArrayView<FWebToUERecordedFeedback> GetRecords() const { return Records; }
	void Reset() { Records.Reset(); }

private:
	TArray<FWebToUERecordedFeedback> Records;
};

enum class EWebToUEFeedbackDispatchResult : uint8
{
	Routed,
	DroppedByRouter,
	DroppedInvalidRequest,
	DroppedWrongSession,
	DroppedStaleGeneration,
	DroppedInactiveSession
};

struct WEBTOUERUNTIME_API FWebToUESessionCreateParams
{
	ULocalPlayer* LocalPlayer = nullptr;
	UWorld* World = nullptr;
	FWebToUESurfaceContext Surface;
	UObject* DataContext = nullptr;
	UObject* CommandContext = nullptr;
	FWebToUEEnvironmentContext Environment;
	TSharedPtr<IWebToUEClock> Clock;
	TSharedPtr<IWebToUEFeedbackRouter> FeedbackRouter;
};
/**
 * Game Thread-owned binding between one Runtime UI Instance and its Host context.
 *
 * The Session owns no gameplay authority. Data/Command objects, LocalPlayer, World and Surface
 * remain host-owned weak references. Generation changes invalidate delayed feedback requests.
 */
class WEBTOUERUNTIME_API FWebToUESession final
{
public:
	static TSharedPtr<FWebToUESession> Create(
		const FWebToUESessionCreateParams& Params, FString& OutError);
	~FWebToUESession();

	bool IsActive() const;
	FWebToUESessionHandle GetHandle() const;
	FWebToUESessionHandle AdvanceGeneration();
	void Invalidate();
	bool IsReadyForInteraction() const;

	FWebToUEFeedbackRequest MakeFeedbackRequest(
		FName CueId,
		FName SourceSemanticKey,
		uint64 EventCorrelationId,
		EWebToUEInputModality InputModality,
		EWebToUEFeedbackScope Scope) const;
	EWebToUEFeedbackDispatchResult DispatchCommittedFeedback(
		const FWebToUEFeedbackRequest& Request);

	ULocalPlayer* GetLocalPlayer() const { return LocalPlayer.Get(); }
	UWorld* GetWorld() const { return World.Get(); }
	UObject* GetDataContext() const { return DataContext.Get(); }
	UObject* GetCommandContext() const { return CommandContext.Get(); }
	const FWebToUESurfaceContext& GetSurface() const { return Surface; }
	const FWebToUEEnvironmentContext& GetEnvironment() const { return Environment; }
	TSharedRef<IWebToUEClock> GetClock() const { return Clock.ToSharedRef(); }
	TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> GetUpdateCoordinator() const
	{
		return UpdateCoordinator.ToSharedRef();
	}
	TSharedRef<FWebToUEAsyncCoordinator, ESPMode::ThreadSafe> GetAsyncCoordinator() const
	{
		return AsyncCoordinator.ToSharedRef();
	}

private:
	explicit FWebToUESession(const FWebToUESessionCreateParams& Params);
	FWebToUEFeedbackRoutingContext MakeFeedbackRoutingContext() const;

	uint64 SessionId = 0;
	uint32 Generation = 1;
	bool bActive = true;
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;
	TWeakObjectPtr<UWorld> World;
	FWebToUESurfaceContext Surface;
	TWeakObjectPtr<UObject> DataContext;
	TWeakObjectPtr<UObject> CommandContext;
	FWebToUEEnvironmentContext Environment;
	TSharedPtr<IWebToUEClock> Clock;
	TSharedPtr<IWebToUEFeedbackRouter> FeedbackRouter;
	TSharedPtr<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> UpdateCoordinator;
	TSharedPtr<FWebToUEAsyncCoordinator, ESPMode::ThreadSafe> AsyncCoordinator;
};
