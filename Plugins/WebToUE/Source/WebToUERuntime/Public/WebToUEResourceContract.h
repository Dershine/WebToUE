#pragma once

#include "CoreMinimal.h"

/** Independent versions for compiled layers. An absent optional layer is exactly 0.0. */
struct WEBTOUERUNTIME_API FWebToUEArtifactLayerVersion
{
	uint16 Major = 0;
	uint16 Minor = 0;

	bool IsPresent() const { return Major != 0; }
	bool IsCanonicalAbsent() const { return Major == 0 && Minor == 0; }

	friend bool operator==(
		const FWebToUEArtifactLayerVersion& A,
		const FWebToUEArtifactLayerVersion& B) = default;
};

/** UI and Resource IR are mandatory. Behavior, Animation and Interop Schema are explicit optional layers. */
struct WEBTOUERUNTIME_API FWebToUEArtifactVersionSet
{
	FWebToUEArtifactLayerVersion UiIr;
	FWebToUEArtifactLayerVersion ResourceIr;
	FWebToUEArtifactLayerVersion BehaviorIr;
	FWebToUEArtifactLayerVersion AnimationIr;
	FWebToUEArtifactLayerVersion InteropSchema;

	friend bool operator==(
		const FWebToUEArtifactVersionSet& A,
		const FWebToUEArtifactVersionSet& B) = default;
};

enum class EWebToUEResourceDependencyKind : uint8
{
	Invalid,
	UiSource,
	StyleSource,
	BehaviorSource,
	InteropSchema,
	Resource,
	GeneratedInput
};

/** One sealed compiler input. LogicalId is project-relative and never an absolute machine path. */
struct WEBTOUERUNTIME_API FWebToUEResourceDependency
{
	FString LogicalId;
	EWebToUEResourceDependencyKind Kind = EWebToUEResourceDependencyKind::Invalid;
	FString ContentHashBlake3;

	friend bool operator==(
		const FWebToUEResourceDependency& A,
		const FWebToUEResourceDependency& B) = default;
};

enum class EWebToUEResourceOrigin : uint8
{
	Invalid,
	UnrealAsset,
	RelativeSource,
	Generated
};

/** Diagnostic-only author reference plus the sealed dependency it resolved to. */
struct WEBTOUERUNTIME_API FWebToUEResourceProvenance
{
	EWebToUEResourceOrigin Origin = EWebToUEResourceOrigin::Invalid;
	FString SourceUnit;
	FString AuthorReference;
	FString ResolvedDependencyId;

	friend bool operator==(
		const FWebToUEResourceProvenance& A,
		const FWebToUEResourceProvenance& B) = default;
};

/** Stable only inside one document contract revision; it is not a Runtime Manifest Handle. */
struct WEBTOUERUNTIME_API FWebToUEResourceDescriptor
{
	FString ResourceId;
	FWebToUEResourceProvenance Provenance;

	friend bool operator==(
		const FWebToUEResourceDescriptor& A,
		const FWebToUEResourceDescriptor& B) = default;
};

enum class EWebToUEResidencyClass : uint8
{
	Invalid,
	/** Required before its Document or Route may become interactive. */
	Critical,
	/** Requested when its owner is predicted or actually visible; deterministic fallback is allowed. */
	Visible,
	/** Requested only by an explicit consumer action; never part of default Document activation. */
	Lazy
};

/**
 * Empty RouteId means document scope. A route assignment may only promote a document fallback to
 * an equally or more eager class; it cannot demote a document-level requirement.
 */
struct WEBTOUERUNTIME_API FWebToUEResidencyAssignment
{
	FString ResourceId;
	FString RouteId;
	FString GroupId;
	EWebToUEResidencyClass Residency = EWebToUEResidencyClass::Invalid;

	friend bool operator==(
		const FWebToUEResidencyAssignment& A,
		const FWebToUEResidencyAssignment& B) = default;
};

/** Authoring/compiler input for one independently cooked WTUE Document. */
struct WEBTOUERUNTIME_API FWebToUEResourceContractDescriptor
{
	FWebToUEArtifactLayerVersion ContractVersion{ 1, 0 };
	FString DocumentId;
	FString CompilerFingerprintBlake3;
	FWebToUEArtifactVersionSet ArtifactVersions;
	TArray<FWebToUEResourceDependency> Dependencies;
	TArray<FWebToUEResourceDescriptor> Resources;
	TArray<FWebToUEResidencyAssignment> ResidencyAssignments;
};

/** Serialized alongside future compiled artifacts; Shipping never reads source files to recreate it. */
struct WEBTOUERUNTIME_API FWebToUECookFreshnessStamp
{
	FWebToUEArtifactLayerVersion ContractVersion;
	FString DocumentId;
	FString CompilerFingerprintBlake3;
	FString DependencyClosureBlake3;
	FString ResourceManifestBlake3;
	FWebToUEArtifactVersionSet ArtifactVersions;

	friend bool operator==(
		const FWebToUECookFreshnessStamp& A,
		const FWebToUECookFreshnessStamp& B) = default;
};

/** Validated, deterministically sorted compiler/runtime contract. */
struct WEBTOUERUNTIME_API FWebToUEResourceContractSnapshot
{
	TArray<FWebToUEResourceDependency> Dependencies;
	TArray<FWebToUEResourceDescriptor> Resources;
	TArray<FWebToUEResidencyAssignment> ResidencyAssignments;
	FWebToUECookFreshnessStamp Freshness;

	void Reset();
};

struct WEBTOUERUNTIME_API FWebToUEResourceContractDiagnostic
{
	FString Code;
	FString Path;
	FString Detail;
};

/**
 * Pure policy for M3 resource boundaries. It does not load resources, mutate assets or read source
 * files. Producers supply BLAKE3-256 input hashes; Cook compares the current expected stamp with the
 * stamp embedded in the compiled artifact and fails closed on any mismatch.
 */
class WEBTOUERUNTIME_API FWebToUEResourceContractPolicy final
{
public:
	static bool BuildSnapshot(
		const FWebToUEResourceContractDescriptor& Descriptor,
		FWebToUEResourceContractSnapshot& OutSnapshot,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics);

	static bool IsCookFresh(
		const FWebToUECookFreshnessStamp& Expected,
		const FWebToUECookFreshnessStamp& Compiled,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics);

	/** Producer minor versions may be consumed only when the consumer supports that minor or newer. */
	static bool IsRuntimeCompatible(
		const FWebToUEArtifactVersionSet& Produced,
		const FWebToUEArtifactVersionSet& Supported,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics);
};
