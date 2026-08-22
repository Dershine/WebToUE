#pragma once

#include "CoreMinimal.h"
#include "WebToUEIdentity.h"

enum class EWebToUECompositingTier : uint8
{
	DirectPaint = 0,
	MaterialBrush = 1,
	SubtreeLayer = 2,
	RenderTarget = 3
};

enum class EWebToUECompositingRequirement : uint8
{
	None = 0,
	MaterialBrush = 1 << 0,
	ProjectedSubtree = 1 << 1,
	IsolatedSubtree = 1 << 2,
	SamplesCompositedSubtree = 1 << 3,
	IndependentSurface = 1 << 4
};
ENUM_CLASS_FLAGS(EWebToUECompositingRequirement)

struct FWebToUECompositingRequest
{
	EWebToUECompositingRequirement Requirements =
		EWebToUECompositingRequirement::None;
	FIntPoint PixelExtent = FIntPoint::ZeroValue;
	uint64 BytesPerPixel = 4;
};

struct FWebToUECompositingBackend
{
	bool bMaterialBrushAvailable = true;
	bool bSubtreeLayerAvailable = false;
	bool bRenderTargetAvailable = false;
};

struct FWebToUECompositingBudget
{
	int32 MaxActiveLayers = 0;
	int32 MaxActiveSurfaces = 0;
	uint64 MaxAllocatedPixels = 0;
	uint64 MaxAllocatedBytes = 0;
};

struct FWebToUECompositingUsage
{
	int32 ActiveLayers = 0;
	int32 ActiveSurfaces = 0;
	uint64 AllocatedPixels = 0;
	uint64 AllocatedBytes = 0;
};

struct FWebToUECompositingDecision
{
	EWebToUECompositingTier Tier = EWebToUECompositingTier::DirectPaint;
	bool bAccepted = false;
	FString Diagnostic;
};

struct FWebToUECompositingNodeRequest
{
	FWebToUEInstanceHandle Owner;
	FWebToUECompositingRequest Request;
	int32 PaintSequence = 0;
};

struct FWebToUECompositingPlanEntry
{
	FWebToUEInstanceHandle Owner;
	FWebToUECompositingRequest Request;
	FWebToUECompositingDecision Decision;
	int32 PaintSequence = 0;
};

class FWebToUECompositingPlan
{
public:
	bool Build(TConstArrayView<FWebToUECompositingNodeRequest> Requests,
		const FWebToUECompositingBackend& Backend,
		const FWebToUECompositingBudget& Budget);
	const TArray<FWebToUECompositingPlanEntry>& GetEntries() const { return Entries; }
	const FWebToUECompositingUsage& GetReservedUsage() const { return ReservedUsage; }
	bool IsAccepted() const { return bAccepted; }
	const FString& GetDiagnostic() const { return Diagnostic; }

private:
	TArray<FWebToUECompositingPlanEntry> Entries;
	FWebToUECompositingUsage ReservedUsage;
	bool bAccepted = false;
	FString Diagnostic;
};

struct FWebToUECompositingCacheStats
{
	uint64 Allocated = 0;
	uint64 Reused = 0;
	uint64 Released = 0;
	uint64 Evicted = 0;
	int32 CachedEntries = 0;
	FWebToUECompositingUsage Usage;
};

/** View/Surface-owned plan cache. Entries are handles, never node-owned UObject/Widget state. */
class FWebToUECompositingCache
{
public:
	FWebToUECompositingCache(uint64 InOwnerId, uint32 InGeneration,
		FName InSurfaceId);
	bool ApplyPlan(const FWebToUECompositingPlan& Plan,
		uint64 ProjectionRevision, FString& OutDiagnostic);
	void RemoveOwner(FWebToUEInstanceHandle Owner);
	void AdvanceGeneration(uint32 NewGeneration);
	void DetachSurface();
	void Shutdown();
	bool IsShutdown() const { return bShutdown; }
	const FWebToUECompositingCacheStats& GetStats() const { return Stats; }

private:
	struct FEntry
	{
		FWebToUEInstanceHandle Owner;
		EWebToUECompositingTier Tier = EWebToUECompositingTier::DirectPaint;
		FIntPoint PixelExtent = FIntPoint::ZeroValue;
		uint64 BytesPerPixel = 0;
		uint64 ProjectionRevision = 0;
	};

	uint64 OwnerId = 0;
	uint32 Generation = 0;
	FName SurfaceId;
	TMap<FWebToUEInstanceHandle, FEntry> Entries;
	FWebToUECompositingCacheStats Stats;
	bool bShutdown = false;

	void ReleaseEntry(FWebToUEInstanceHandle Owner, bool bEvicted);
	void ReleaseAll(bool bEvicted);
	void RefreshStats();
};

/** Pure deterministic policy. It classifies sealed requirements and never silently downgrades. */
class FWebToUECompositingPolicy
{
public:
	static FWebToUECompositingDecision Select(
		const FWebToUECompositingRequest& Request,
		const FWebToUECompositingBackend& Backend,
		const FWebToUECompositingBudget& Budget,
		const FWebToUECompositingUsage& Usage);
};
