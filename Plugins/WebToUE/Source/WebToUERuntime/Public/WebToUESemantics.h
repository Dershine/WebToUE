#pragma once

#include "CoreMinimal.h"
#include "Layout/SlateRect.h"
#include "WebToUEIdentity.h"

enum class EWebToUESemanticRole : uint8
{
	GenericAction,
	Button
};

/** A short-lived semantic projection of one element in a Runtime UI Instance. */
struct WEBTOUERUNTIME_API FWebToUESemanticNode
{
	FWebToUEInstanceHandle Handle;
	FName ElementId;
	FText Label;
	FSlateRect Bounds;
	EWebToUESemanticRole Role = EWebToUESemanticRole::GenericAction;
	bool bFocusable = false;
	bool bEnabled = false;
	bool bVisible = false;
};

/**
 * Internal adapter boundary for navigation, future IME ownership and future accessibility.
 * Handles are valid only for the owning Runtime UI Instance generation.
 */
class WEBTOUERUNTIME_API IWebToUESemanticFocusSource
{
public:
	virtual ~IWebToUESemanticFocusSource() = default;

	virtual void GetSemanticNodes(TArray<FWebToUESemanticNode>& OutNodes) const = 0;
	virtual FWebToUEInstanceHandle GetFocusedSemanticNode() const = 0;
	virtual bool RequestSemanticFocus(FWebToUEInstanceHandle Handle) = 0;
	virtual bool ActivateSemanticNode(FWebToUEInstanceHandle Handle) = 0;
};
