#pragma once

#include "CoreMinimal.h"

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
