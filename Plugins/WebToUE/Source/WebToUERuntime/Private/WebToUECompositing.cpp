#include "WebToUECompositing.h"

namespace
{
	static bool Has(
		EWebToUECompositingRequirement Requirements,
		EWebToUECompositingRequirement Requirement)
	{
		return EnumHasAnyFlags(Requirements, Requirement);
	}

	static FWebToUECompositingDecision Reject(
		EWebToUECompositingTier Tier, const TCHAR* Code, const FString& Detail)
	{
		FWebToUECompositingDecision Result;
		Result.Tier = Tier;
		Result.Diagnostic = FString::Printf(TEXT("%s: %s"), Code, *Detail);
		return Result;
	}

	static uint64 RequestedPixels(const FWebToUECompositingRequest& Request)
	{
		return Request.PixelExtent.X > 0 && Request.PixelExtent.Y > 0
			? static_cast<uint64>(Request.PixelExtent.X) *
				static_cast<uint64>(Request.PixelExtent.Y)
			: 0;
	}

	static bool HandleLess(
		const FWebToUEInstanceHandle& A, const FWebToUEInstanceHandle& B)
	{
		if (A.GetOwnerId() != B.GetOwnerId()) return A.GetOwnerId() < B.GetOwnerId();
		if (A.GetGeneration() != B.GetGeneration())
		{
			return A.GetGeneration() < B.GetGeneration();
		}
		return A.GetSlot() < B.GetSlot();
	}
}

FWebToUECompositingDecision FWebToUECompositingPolicy::Select(
	const FWebToUECompositingRequest& Request,
	const FWebToUECompositingBackend& Backend,
	const FWebToUECompositingBudget& Budget,
	const FWebToUECompositingUsage& Usage)
{
	EWebToUECompositingTier Tier = EWebToUECompositingTier::DirectPaint;
	if (Has(Request.Requirements,
		EWebToUECompositingRequirement::SamplesCompositedSubtree) ||
		Has(Request.Requirements,
			EWebToUECompositingRequirement::IndependentSurface))
	{
		Tier = EWebToUECompositingTier::RenderTarget;
	}
	else if (Has(Request.Requirements,
		EWebToUECompositingRequirement::IsolatedSubtree))
	{
		Tier = EWebToUECompositingTier::SubtreeLayer;
	}
	else if (Has(Request.Requirements,
		EWebToUECompositingRequirement::MaterialBrush))
	{
		Tier = EWebToUECompositingTier::MaterialBrush;
	}

	if (Tier == EWebToUECompositingTier::MaterialBrush &&
		!Backend.bMaterialBrushAvailable)
	{
		return Reject(Tier, TEXT("WTUE-COMP-002"),
			TEXT("Tier 1 Material Brush backend is unavailable"));
	}
	if (Tier == EWebToUECompositingTier::SubtreeLayer &&
		!Backend.bSubtreeLayerAvailable)
	{
		return Reject(Tier, TEXT("WTUE-COMP-002"),
			TEXT("Tier 2 Subtree Layer backend is unavailable"));
	}
	if (Tier == EWebToUECompositingTier::RenderTarget &&
		!Backend.bRenderTargetAvailable)
	{
		return Reject(Tier, TEXT("WTUE-COMP-002"),
			TEXT("Tier 3 Render Target backend is unavailable"));
	}

	if (Tier >= EWebToUECompositingTier::SubtreeLayer)
	{
		if (Request.PixelExtent.X <= 0 || Request.PixelExtent.Y <= 0 ||
			Request.BytesPerPixel == 0)
		{
			return Reject(Tier, TEXT("WTUE-COMP-001"),
				TEXT("isolated compositing requires a positive sealed pixel extent"));
		}
		const uint64 RequestedPixels =
			static_cast<uint64>(Request.PixelExtent.X) *
			static_cast<uint64>(Request.PixelExtent.Y);
		if (RequestedPixels > MAX_uint64 / Request.BytesPerPixel)
		{
			return Reject(Tier, TEXT("WTUE-COMP-001"),
				TEXT("isolated compositing byte size overflows uint64"));
		}
		const uint64 RequestedBytes = RequestedPixels * Request.BytesPerPixel;
		const int32 RequestedLayers =
			Tier == EWebToUECompositingTier::SubtreeLayer ? 1 : 0;
		const int32 RequestedSurfaces =
			Tier == EWebToUECompositingTier::RenderTarget ? 1 : 0;
		const bool bCapacityExceeded =
			Usage.ActiveLayers + RequestedLayers > Budget.MaxActiveLayers ||
			Usage.ActiveSurfaces + RequestedSurfaces > Budget.MaxActiveSurfaces ||
			RequestedPixels > Budget.MaxAllocatedPixels -
				FMath::Min(Usage.AllocatedPixels, Budget.MaxAllocatedPixels) ||
			RequestedBytes > Budget.MaxAllocatedBytes -
				FMath::Min(Usage.AllocatedBytes, Budget.MaxAllocatedBytes);
		if (bCapacityExceeded)
		{
			return Reject(Tier, TEXT("WTUE-COMP-003"), FString::Printf(
				TEXT("tier budget exceeded: extent=%dx%d pixels=%llu bytes=%llu"),
				Request.PixelExtent.X, Request.PixelExtent.Y,
				RequestedPixels, RequestedBytes));
		}
	}

	FWebToUECompositingDecision Result;
	Result.Tier = Tier;
	Result.bAccepted = true;
	return Result;
}

bool FWebToUECompositingPlan::Build(
	TConstArrayView<FWebToUECompositingNodeRequest> Requests,
	const FWebToUECompositingBackend& Backend,
	const FWebToUECompositingBudget& Budget)
{
	Entries.Reset(Requests.Num());
	ReservedUsage = {};
	bAccepted = false;
	Diagnostic.Reset();
	TArray<FWebToUECompositingNodeRequest> Sorted;
	Sorted.Append(Requests);
	Sorted.Sort([](const FWebToUECompositingNodeRequest& A,
		const FWebToUECompositingNodeRequest& B)
	{
		return A.PaintSequence != B.PaintSequence
			? A.PaintSequence < B.PaintSequence
			: HandleLess(A.Owner, B.Owner);
	});
	TSet<FWebToUEInstanceHandle> Seen;
	for (const FWebToUECompositingNodeRequest& Node : Sorted)
	{
		FWebToUECompositingPlanEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Owner = Node.Owner;
		Entry.Request = Node.Request;
		Entry.PaintSequence = Node.PaintSequence;
		if (!Node.Owner.IsValid())
		{
			Entry.Decision = Reject(EWebToUECompositingTier::DirectPaint,
				TEXT("WTUE-COMP-001"), TEXT("plan owner handle is invalid"));
		}
		else if (Seen.Contains(Node.Owner))
		{
			Entry.Decision = Reject(EWebToUECompositingTier::DirectPaint,
				TEXT("WTUE-COMP-004"), TEXT("plan contains a duplicate owner"));
		}
		else
		{
			Seen.Add(Node.Owner);
			Entry.Decision = FWebToUECompositingPolicy::Select(
				Node.Request, Backend, Budget, ReservedUsage);
			if (Entry.Decision.bAccepted)
			{
				const uint64 Pixels = RequestedPixels(Node.Request);
				const uint64 Bytes = Pixels * Node.Request.BytesPerPixel;
				if (Entry.Decision.Tier == EWebToUECompositingTier::SubtreeLayer)
				{
					++ReservedUsage.ActiveLayers;
					ReservedUsage.AllocatedPixels += Pixels;
					ReservedUsage.AllocatedBytes += Bytes;
				}
				else if (Entry.Decision.Tier == EWebToUECompositingTier::RenderTarget)
				{
					++ReservedUsage.ActiveSurfaces;
					ReservedUsage.AllocatedPixels += Pixels;
					ReservedUsage.AllocatedBytes += Bytes;
				}
			}
		}
		if (!Entry.Decision.bAccepted && Diagnostic.IsEmpty())
		{
			Diagnostic = Entry.Decision.Diagnostic;
		}
	}
	bAccepted = Diagnostic.IsEmpty();
	return bAccepted;
}

FWebToUECompositingCache::FWebToUECompositingCache(
	uint64 InOwnerId, uint32 InGeneration, FName InSurfaceId)
	: OwnerId(InOwnerId)
	, Generation(InGeneration)
	, SurfaceId(InSurfaceId)
{
}

bool FWebToUECompositingCache::ApplyPlan(
	const FWebToUECompositingPlan& Plan,
	uint64 ProjectionRevision,
	FString& OutDiagnostic)
{
	OutDiagnostic.Reset();
	if (bShutdown || SurfaceId.IsNone())
	{
		OutDiagnostic = TEXT("WTUE-COMP-005: cache is detached or shut down");
		return false;
	}
	if (!Plan.IsAccepted())
	{
		OutDiagnostic = Plan.GetDiagnostic();
		return false;
	}
	for (const FWebToUECompositingPlanEntry& Planned : Plan.GetEntries())
	{
		if (Planned.Owner.GetOwnerId() != OwnerId ||
			Planned.Owner.GetGeneration() != Generation)
		{
			OutDiagnostic = TEXT("WTUE-COMP-001: plan owner is outside the cache generation");
			return false;
		}
	}

	TSet<FWebToUEInstanceHandle> Requested;
	for (const FWebToUECompositingPlanEntry& Planned : Plan.GetEntries())
	{
		Requested.Add(Planned.Owner);
		FEntry* Existing = Entries.Find(Planned.Owner);
		const bool bReusable = Existing &&
			Existing->Tier == Planned.Decision.Tier &&
			Existing->PixelExtent == Planned.Request.PixelExtent &&
			Existing->BytesPerPixel == Planned.Request.BytesPerPixel &&
			Existing->ProjectionRevision == ProjectionRevision;
		if (bReusable)
		{
			++Stats.Reused;
			continue;
		}
		if (Existing) ReleaseEntry(Planned.Owner, true);
		FEntry& Added = Entries.Add(Planned.Owner);
		Added.Owner = Planned.Owner;
		Added.Tier = Planned.Decision.Tier;
		Added.PixelExtent = Planned.Request.PixelExtent;
		Added.BytesPerPixel = Planned.Request.BytesPerPixel;
		Added.ProjectionRevision = ProjectionRevision;
		++Stats.Allocated;
	}

	TArray<FWebToUEInstanceHandle> Removed;
	Entries.GetKeys(Removed);
	Removed.Sort(HandleLess);
	for (const FWebToUEInstanceHandle Handle : Removed)
	{
		if (!Requested.Contains(Handle)) ReleaseEntry(Handle, false);
	}
	RefreshStats();
	return true;
}

void FWebToUECompositingCache::RemoveOwner(FWebToUEInstanceHandle Owner)
{
	ReleaseEntry(Owner, false);
	RefreshStats();
}

void FWebToUECompositingCache::AdvanceGeneration(uint32 NewGeneration)
{
	if (NewGeneration == Generation) return;
	ReleaseAll(true);
	Generation = NewGeneration;
}

void FWebToUECompositingCache::DetachSurface()
{
	ReleaseAll(true);
	SurfaceId = NAME_None;
}

void FWebToUECompositingCache::Shutdown()
{
	ReleaseAll(true);
	bShutdown = true;
}

void FWebToUECompositingCache::ReleaseEntry(
	FWebToUEInstanceHandle Owner, bool bEvicted)
{
	if (Entries.Remove(Owner) > 0)
	{
		++Stats.Released;
		if (bEvicted) ++Stats.Evicted;
	}
}

void FWebToUECompositingCache::ReleaseAll(bool bEvicted)
{
	TArray<FWebToUEInstanceHandle> Handles;
	Entries.GetKeys(Handles);
	Handles.Sort(HandleLess);
	for (const FWebToUEInstanceHandle Handle : Handles)
	{
		ReleaseEntry(Handle, bEvicted);
	}
	RefreshStats();
}

void FWebToUECompositingCache::RefreshStats()
{
	Stats.CachedEntries = Entries.Num();
	Stats.Usage = {};
	for (const TPair<FWebToUEInstanceHandle, FEntry>& Pair : Entries)
	{
		const FEntry& Entry = Pair.Value;
		const uint64 Pixels = RequestedPixels({
			EWebToUECompositingRequirement::None,
			Entry.PixelExtent,
			Entry.BytesPerPixel
		});
		if (Entry.Tier == EWebToUECompositingTier::SubtreeLayer)
		{
			++Stats.Usage.ActiveLayers;
		}
		else if (Entry.Tier == EWebToUECompositingTier::RenderTarget)
		{
			++Stats.Usage.ActiveSurfaces;
		}
		if (Entry.Tier >= EWebToUECompositingTier::SubtreeLayer)
		{
			Stats.Usage.AllocatedPixels += Pixels;
			Stats.Usage.AllocatedBytes += Pixels * Entry.BytesPerPixel;
		}
	}
}
