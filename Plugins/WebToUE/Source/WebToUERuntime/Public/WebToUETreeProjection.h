#pragma once

#include "CoreMinimal.h"
#include "WebToUEIdentity.h"
#include "WebToUESession.h"

enum class EWebToUETreeKind : uint8
{
	Component,
	Logical,
	Layout,
	Paint,
	Semantic
};

enum class EWebToUETreeParticipation : uint8
{
	None = 0,
	Layout = 1 << 0,
	Paint = 1 << 1,
	Semantic = 1 << 2,
	All = 7
};
ENUM_CLASS_FLAGS(EWebToUETreeParticipation)

/** Input for one node. Parents must already be registered in this projection. */
struct WEBTOUERUNTIME_API FWebToUETreeNodeDescriptor
{
	FWebToUEInstanceHandle Handle;
	FWebToUEInstanceHandle ComponentParent;
	FWebToUEInstanceHandle LogicalParent;
	EWebToUETreeParticipation Participation = EWebToUETreeParticipation::All;
};

/** Host-owned mount point. Anchors never change Logical or Component ownership. */
struct WEBTOUERUNTIME_API FWebToUEOverlayAnchorDescriptor
{
	FName AnchorId;
	FWebToUESessionHandle Session;
	FName SurfaceId;
	FWebToUEInstanceHandle PaintParent;
	FWebToUEInstanceHandle SemanticParent;
	bool bAllowsModal = false;
};

/** A Portal is a projection of an existing logical subtree, not a second node owner. */
struct WEBTOUERUNTIME_API FWebToUEPortalMount
{
	FWebToUEInstanceHandle PortalRoot;
	FName AnchorId;
	int32 OverlayOrder = 0;
	bool bModal = false;
};

struct WEBTOUERUNTIME_API FWebToUETreeNodeProjection
{
	FWebToUEInstanceHandle Handle;
	FWebToUEInstanceHandle ComponentParent;
	FWebToUEInstanceHandle LogicalParent;
	FWebToUEInstanceHandle LayoutParent;
	FWebToUEInstanceHandle PaintParent;
	FWebToUEInstanceHandle SemanticParent;
	FName OverlayAnchorId;
	int32 OverlayOrder = 0;
	bool bInLayoutTree = false;
	bool bInPaintTree = false;
	bool bInSemanticTree = false;
	bool bPortalRoot = false;
	bool bModal = false;
};

/**
 * Same-generation focus fallback captured before a Portal opens.
 * It deliberately contains no Stable Semantic Key or cross-reload lookup.
 */
struct WEBTOUERUNTIME_API FWebToUEFocusRestoreToken
{
	FWebToUESessionHandle Session;
	FName SurfaceId;
	FWebToUEInstanceHandle PortalRoot;
	TArray<FWebToUEInstanceHandle> LogicalCandidates;

	bool IsValid() const
	{
		return Session.IsValid() && !SurfaceId.IsNone() && PortalRoot.IsValid() &&
			!LogicalCandidates.IsEmpty();
	}
};

/**
 * Deterministic M3 tree/Portal contract.
 *
 * Component and Logical parents are durable runtime ownership for one instance generation.
 * Layout/Paint/Semantic are projections. A Portal keeps Component/Logical ownership, becomes
 * an independent Layout root, and redirects Paint to an explicit same-Session/Surface anchor.
 * Modal semantics may redirect only to the anchor's explicit Semantic parent.
 */
class WEBTOUERUNTIME_API FWebToUETreeProjectionPolicy final
{
public:
	FWebToUETreeProjectionPolicy(
		FWebToUESessionHandle InSession, FName InSurfaceId);

	bool AddNode(const FWebToUETreeNodeDescriptor& Descriptor, FString& OutError);
	bool RegisterOverlayAnchor(
		const FWebToUEOverlayAnchorDescriptor& Descriptor, FString& OutError);
	bool MountPortal(const FWebToUEPortalMount& Mount, FString& OutError);
	bool UnmountPortal(FWebToUEInstanceHandle PortalRoot, FString& OutError);

	const FWebToUETreeNodeProjection* FindProjection(
		FWebToUEInstanceHandle Handle) const;
	bool BuildPath(
		EWebToUETreeKind Tree,
		FWebToUEInstanceHandle Target,
		TArray<FWebToUEInstanceHandle>& OutRootToTarget) const;
	void GetPortalRootsInPaintOrder(
		TArray<FWebToUEInstanceHandle>& OutPortalRoots) const;

	bool CaptureFocusRestore(
		FWebToUEInstanceHandle PortalRoot,
		FWebToUEInstanceHandle Origin,
		FWebToUEFocusRestoreToken& OutToken,
		FString& OutError) const;
	FWebToUEInstanceHandle ResolveFocusRestore(
		const FWebToUEFocusRestoreToken& Token,
		TFunctionRef<bool(FWebToUEInstanceHandle)> IsFocusable) const;

	FWebToUEInstanceHandle GetTopModalPortal() const;
	bool IsInTopModalScope(FWebToUEInstanceHandle Handle) const;

private:
	struct FNodeEntry
	{
		FWebToUETreeNodeProjection Projection;
		FWebToUEInstanceHandle BaseLayoutParent;
		FWebToUEInstanceHandle BasePaintParent;
		FWebToUEInstanceHandle BaseSemanticParent;
	};

	FWebToUEInstanceHandle FindNearestParticipant(
		FWebToUEInstanceHandle Start,
		EWebToUETreeParticipation Required) const;
	FWebToUEInstanceHandle GetParent(
		const FWebToUETreeNodeProjection& Projection,
		EWebToUETreeKind Tree) const;
	bool WouldCreateCycle(
		FWebToUEInstanceHandle Child,
		FWebToUEInstanceHandle NewParent,
		EWebToUETreeKind Tree) const;
	bool IsLogicalDescendantOrSelf(
		FWebToUEInstanceHandle Candidate,
		FWebToUEInstanceHandle Ancestor) const;
	bool ValidateHandleDomain(FWebToUEInstanceHandle Handle) const;

	FWebToUESessionHandle Session;
	FName SurfaceId;
	uint64 OwnerId = 0;
	uint32 Generation = 0;
	FWebToUEInstanceHandle LogicalRoot;
	TMap<FWebToUEInstanceHandle, FNodeEntry> Nodes;
	TMap<FName, FWebToUEOverlayAnchorDescriptor> Anchors;
};
