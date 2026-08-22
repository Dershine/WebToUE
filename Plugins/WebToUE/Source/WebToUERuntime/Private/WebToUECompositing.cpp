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
