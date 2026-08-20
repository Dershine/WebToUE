#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "WebToUEIdentity.h"
#include "WebToUESession.h"

class FWebToUEUpdateTransaction;

/** Bounded native event kinds currently produced by the Runtime input bridge. */
enum class EWebToUERuntimeEventType : uint8
{
	Click,
	PointerCaptureLost
};

enum class EWebToUERuntimeEventPhase : uint8
{
	Capture,
	Target,
	Bubble
};

/** Slate input identity retained across the internal single-Leaf event path. */
struct WEBTOUERUNTIME_API FWebToUEInteractionIdentity
{
	static FWebToUEInteractionIdentity Pointer(uint32 InSlateUserIndex, uint32 InPointerIndex)
	{
		return {InSlateUserIndex, static_cast<int32>(InPointerIndex)};
	}

	static FWebToUEInteractionIdentity NonPointer(uint32 InSlateUserIndex)
	{
		return {InSlateUserIndex, INDEX_NONE};
	}

	bool IsValid() const { return SlateUserIndex != MAX_uint32; }
	bool HasPointer() const { return PointerIndex != INDEX_NONE; }

	friend bool operator==(
		const FWebToUEInteractionIdentity& A, const FWebToUEInteractionIdentity& B)
	{
		return A.SlateUserIndex == B.SlateUserIndex && A.PointerIndex == B.PointerIndex;
	}

	uint32 SlateUserIndex = MAX_uint32;
	int32 PointerIndex = INDEX_NONE;
};

FORCEINLINE uint32 GetTypeHash(const FWebToUEInteractionIdentity& Identity)
{
	return HashCombineFast(
		GetTypeHash(Identity.SlateUserIndex), GetTypeHash(Identity.PointerIndex));
}

/** Immutable root-to-target route captured before event evaluation starts. */
struct WEBTOUERUNTIME_API FWebToUEEventPathSnapshot
{
	EWebToUERuntimeEventType Type = EWebToUERuntimeEventType::Click;
	FWebToUESessionHandle Session;
	TArray<FWebToUEInstanceHandle> RootToTarget;
	FWebToUEInteractionIdentity Interaction;
	EWebToUEInputModality InputModality = EWebToUEInputModality::Unknown;
	uint64 CorrelationId = 0;
	bool bBubbles = true;
	bool bCancelable = true;

	bool IsValid() const
	{
		return !RootToTarget.IsEmpty() && RootToTarget.Last().IsValid() &&
			Interaction.IsValid() && CorrelationId != 0;
	}

	FWebToUEInstanceHandle GetTarget() const
	{
		return RootToTarget.IsEmpty() ? FWebToUEInstanceHandle() : RootToTarget.Last();
	}
};

/** Mutable propagation controls for one immutable event snapshot. */
class WEBTOUERUNTIME_API FWebToUERuntimeEvent final
{
public:
	const FWebToUEEventPathSnapshot& GetSnapshot() const { return Snapshot; }
	EWebToUERuntimeEventPhase GetPhase() const { return Phase; }
	FWebToUEInstanceHandle GetCurrentTarget() const { return CurrentTarget; }

	void StopPropagation() { bPropagationStopped = true; }
	void StopImmediatePropagation()
	{
		bImmediatePropagationStopped = true;
		bPropagationStopped = true;
	}
	void PreventDefault()
	{
		if (Snapshot.bCancelable)
		{
			bDefaultPrevented = true;
		}
	}

	bool IsPropagationStopped() const { return bPropagationStopped; }
	bool IsImmediatePropagationStopped() const { return bImmediatePropagationStopped; }
	bool IsDefaultPrevented() const { return bDefaultPrevented; }

private:
	friend class SWebToUEView;
	explicit FWebToUERuntimeEvent(const FWebToUEEventPathSnapshot& InSnapshot)
		: Snapshot(InSnapshot)
	{
	}

	void BeginCurrentTarget(
		FWebToUEInstanceHandle InCurrentTarget, EWebToUERuntimeEventPhase InPhase)
	{
		CurrentTarget = InCurrentTarget;
		Phase = InPhase;
		bImmediatePropagationStopped = false;
	}

	const FWebToUEEventPathSnapshot& Snapshot;
	FWebToUEInstanceHandle CurrentTarget;
	EWebToUERuntimeEventPhase Phase = EWebToUERuntimeEventPhase::Target;
	bool bPropagationStopped = false;
	bool bImmediatePropagationStopped = false;
	bool bDefaultPrevented = false;
};

struct WEBTOUERUNTIME_API FWebToUEEventListenerHandle
{
	explicit FWebToUEEventListenerHandle(uint64 InValue = 0) : Value(InValue) {}
	bool IsValid() const { return Value != 0; }
	uint64 GetValue() const { return Value; }

private:
	uint64 Value = 0;
};

using FWebToUEEventListener =
	TFunction<void(FWebToUERuntimeEvent&, FWebToUEUpdateTransaction&)>;

enum class EWebToUEEventDispatchResult : uint8
{
	Dispatched,
	DefaultPrevented,
	DroppedInvalidPath,
	DroppedStalePath,
	RejectedTransaction,
	RejectedInactive
};
