#pragma once

#include "CoreMinimal.h"
#include "WebToUEIdentity.h"

/**
 * An author-stable key scoped to one component instance.
 *
 * This is not a Runtime UI Instance handle, DOM id, source position, or structural path.
 * An invalid key means that the node is intentionally unkeyed and cannot retain state across
 * a compiled-document revision.
 */
struct WEBTOUERUNTIME_API FWebToUEStableSemanticKey
{
	static FWebToUEStableSemanticKey FromString(FString InValue)
	{
		FWebToUEStableSemanticKey Result;
		Result.Value = MoveTemp(InValue);
		return Result;
	}

	bool IsValid() const { return !Value.IsEmpty(); }
	const FString& ToString() const { return Value; }

	friend bool operator==(
		const FWebToUEStableSemanticKey& A,
		const FWebToUEStableSemanticKey& B)
	{
		return A.Value == B.Value;
	}

private:
	FString Value;
};

FORCEINLINE uint32 GetTypeHash(const FWebToUEStableSemanticKey& Key)
{
	return GetTypeHash(Key.ToString());
}

/**
 * Stable identity of one component instance within a route/document scope.
 * KeyPath contains explicit keyed component/list boundaries from root to leaf.
 */
struct WEBTOUERUNTIME_API FWebToUEComponentInstanceIdentity
{
	FString RouteId;
	TArray<FString> KeyPath;
	uint32 ContractVersion = 1;

	bool IsValid() const
	{
		return !RouteId.IsEmpty() && ContractVersion != 0 &&
			!KeyPath.ContainsByPredicate([](const FString& Segment)
			{
				return Segment.IsEmpty();
			});
	}

	friend bool operator==(
		const FWebToUEComponentInstanceIdentity& A,
		const FWebToUEComponentInstanceIdentity& B)
	{
		return A.RouteId == B.RouteId && A.KeyPath == B.KeyPath &&
			A.ContractVersion == B.ContractVersion;
	}
};

FORCEINLINE uint32 GetTypeHash(const FWebToUEComponentInstanceIdentity& Identity)
{
	uint32 Hash = HashCombineFast(
		GetTypeHash(Identity.RouteId), GetTypeHash(Identity.ContractVersion));
	for (const FString& Segment : Identity.KeyPath)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Segment));
	}
	return Hash;
}

/** Diagnostic/source-map provenance. It never participates in semantic identity equality. */
struct WEBTOUERUNTIME_API FWebToUESourceProvenance
{
	FString SourceUnit;
	int32 Line = 1;
	int32 Column = 1;
	int32 EndLine = 1;
	int32 EndColumn = 1;

	bool IsValid() const
	{
		return !SourceUnit.IsEmpty() && Line > 0 && Column > 0 && EndLine > 0 &&
			EndColumn > 0 && (EndLine > Line || (EndLine == Line && EndColumn >= Column));
	}
};

/** A Stable Semantic Key is unique only inside its Component Instance. */
struct WEBTOUERUNTIME_API FWebToUESemanticAddress
{
	FWebToUEComponentInstanceIdentity Component;
	FWebToUEStableSemanticKey Key;

	bool IsValid() const { return Component.IsValid() && Key.IsValid(); }

	friend bool operator==(
		const FWebToUESemanticAddress& A,
		const FWebToUESemanticAddress& B)
	{
		return A.Component == B.Component && A.Key == B.Key;
	}
};

FORCEINLINE uint32 GetTypeHash(const FWebToUESemanticAddress& Address)
{
	return HashCombineFast(GetTypeHash(Address.Component), GetTypeHash(Address.Key));
}

enum class EWebToUESemanticNodeKind : uint8
{
	Invalid,
	Element,
	Text,
	ComponentBoundary,
	NativeComponent
};

/**
 * Explicitly retainable state classes. All omitted state is reset on reimport, including
 * Instance Handles, pointer/capture/pseudo state, derived style/layout/paint/resource caches,
 * Animation overlays, Timer/Command work, and Binding outputs.
 */
enum class EWebToUECrossReloadState : uint8
{
	None = 0,
	LocalState = 1 << 0,
	ScrollIntent = 1 << 1,
	FocusIntent = 1 << 2,
	All = 7
};
ENUM_CLASS_FLAGS(EWebToUECrossReloadState)

struct WEBTOUERUNTIME_API FWebToUESemanticNodeDescriptor
{
	FWebToUEInstanceHandle Handle;
	FWebToUEComponentInstanceIdentity Component;
	FWebToUEStableSemanticKey StableKey;
	FWebToUESourceProvenance Provenance;
	EWebToUESemanticNodeKind Kind = EWebToUESemanticNodeKind::Invalid;
	uint32 StateContractVersion = 1;
	EWebToUECrossReloadState RetainableState = EWebToUECrossReloadState::None;

	FWebToUESemanticAddress GetAddress() const
	{
		return { Component, StableKey };
	}
};

struct WEBTOUERUNTIME_API FWebToUEReimportIdentityContext
{
	uint64 OwnerId = 0;
	uint32 PreviousGeneration = 0;
	uint32 CurrentGeneration = 0;

	bool IsValid() const
	{
		return OwnerId != 0 && PreviousGeneration != 0 && CurrentGeneration != 0 &&
			PreviousGeneration != CurrentGeneration;
	}
};

enum class EWebToUEReimportStateDisposition : uint8
{
	Matched,
	ResetUnkeyed,
	ResetAdded,
	ResetIncompatible,
	Removed
};

struct WEBTOUERUNTIME_API FWebToUEReimportStateAction
{
	EWebToUEReimportStateDisposition Disposition =
		EWebToUEReimportStateDisposition::ResetAdded;
	FWebToUESemanticAddress Address;
	FWebToUEInstanceHandle PreviousHandle;
	FWebToUEInstanceHandle CurrentHandle;
	EWebToUECrossReloadState PreservedState = EWebToUECrossReloadState::None;
	FWebToUESourceProvenance PreviousProvenance;
	FWebToUESourceProvenance CurrentProvenance;
};

enum class EWebToUESemanticIdentityDiagnosticSeverity : uint8
{
	Warning,
	Error
};

struct WEBTOUERUNTIME_API FWebToUESemanticIdentityDiagnostic
{
	EWebToUESemanticIdentityDiagnosticSeverity Severity =
		EWebToUESemanticIdentityDiagnosticSeverity::Error;
	FString Code;
	FString Detail;
	FWebToUESourceProvenance Provenance;
};

struct WEBTOUERUNTIME_API FWebToUEReimportStatePlan
{
	TArray<FWebToUEReimportStateAction> Actions;
	TArray<FWebToUESemanticIdentityDiagnostic> Diagnostics;

	void Reset()
	{
		Actions.Reset();
		Diagnostics.Reset();
	}
};

/**
 * Deterministic M3 cross-reload identity policy.
 *
 * It matches nodes only by (Component Instance Identity, Stable Semantic Key), never by
 * Instance Handle, Template Node ID, DOM id, source span, or structural position. The returned
 * plan is an explicit pre-commit transfer/rebind plan; current-generation handles remain the
 * only legal runtime access identity after the revision is committed.
 */
class WEBTOUERUNTIME_API FWebToUESemanticIdentityPolicy final
{
public:
	static bool BuildReimportPlan(
		const FWebToUEReimportIdentityContext& Context,
		TConstArrayView<FWebToUESemanticNodeDescriptor> PreviousNodes,
		TConstArrayView<FWebToUESemanticNodeDescriptor> CurrentNodes,
		FWebToUEReimportStatePlan& OutPlan);
};
