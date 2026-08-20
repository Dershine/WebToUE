#include "WebToUETreeProjection.h"

#include "Algo/Reverse.h"

namespace WebToUE::TreeProjection::Private
{
	static bool Fail(FString& OutError, const TCHAR* Code, const FString& Detail)
	{
		OutError = FString::Printf(TEXT("%s: %s"), Code, *Detail);
		return false;
	}
}

FWebToUETreeProjectionPolicy::FWebToUETreeProjectionPolicy(
	FWebToUESessionHandle InSession, FName InSurfaceId)
	: Session(InSession)
	, SurfaceId(InSurfaceId)
{
}

bool FWebToUETreeProjectionPolicy::ValidateHandleDomain(
	FWebToUEInstanceHandle Handle) const
{
	return Handle.IsValid() &&
		(OwnerId == 0 || (Handle.GetOwnerId() == OwnerId &&
			Handle.GetGeneration() == Generation));
}

bool FWebToUETreeProjectionPolicy::AddNode(
	const FWebToUETreeNodeDescriptor& Descriptor, FString& OutError)
{
	using namespace WebToUE::TreeProjection::Private;
	OutError.Reset();
	if (!Session.IsValid() || SurfaceId.IsNone() || !Descriptor.Handle.IsValid())
	{
		return Fail(OutError, TEXT("WTUE-TREE-001"),
			TEXT("projection context and node handle must be valid"));
	}
	if (Nodes.Contains(Descriptor.Handle))
	{
		return Fail(OutError, TEXT("WTUE-TREE-001"),
			TEXT("node handle is already registered"));
	}
	if (OwnerId == 0)
	{
		OwnerId = Descriptor.Handle.GetOwnerId();
		Generation = Descriptor.Handle.GetGeneration();
	}
	if (!ValidateHandleDomain(Descriptor.Handle))
	{
		return Fail(OutError, TEXT("WTUE-TREE-001"),
			TEXT("all projected nodes must share one Runtime UI Instance generation"));
	}
	const auto ValidateParent = [this](FWebToUEInstanceHandle Parent)
	{
		return !Parent.IsValid() || (ValidateHandleDomain(Parent) && Nodes.Contains(Parent));
	};
	if (!ValidateParent(Descriptor.ComponentParent) ||
		!ValidateParent(Descriptor.LogicalParent))
	{
		return Fail(OutError, TEXT("WTUE-TREE-002"),
			TEXT("Component and Logical parents must be registered before their child"));
	}
	if (!Descriptor.LogicalParent.IsValid())
	{
		if (LogicalRoot.IsValid())
		{
			return Fail(OutError, TEXT("WTUE-TREE-002"),
				TEXT("one Runtime UI Instance may have only one Logical root"));
		}
		LogicalRoot = Descriptor.Handle;
	}

	FNodeEntry Entry;
	Entry.Projection.Handle = Descriptor.Handle;
	Entry.Projection.ComponentParent = Descriptor.ComponentParent;
	Entry.Projection.LogicalParent = Descriptor.LogicalParent;
	Entry.Projection.bInLayoutTree = EnumHasAnyFlags(
		Descriptor.Participation, EWebToUETreeParticipation::Layout);
	Entry.Projection.bInPaintTree = EnumHasAnyFlags(
		Descriptor.Participation, EWebToUETreeParticipation::Paint);
	Entry.Projection.bInSemanticTree = EnumHasAnyFlags(
		Descriptor.Participation, EWebToUETreeParticipation::Semantic);
	Entry.Projection.LayoutParent = Entry.Projection.bInLayoutTree
		? FindNearestParticipant(Descriptor.LogicalParent,
			EWebToUETreeParticipation::Layout) : FWebToUEInstanceHandle();
	Entry.Projection.PaintParent = Entry.Projection.bInPaintTree
		? FindNearestParticipant(Descriptor.LogicalParent,
			EWebToUETreeParticipation::Paint) : FWebToUEInstanceHandle();
	Entry.Projection.SemanticParent = Entry.Projection.bInSemanticTree
		? FindNearestParticipant(Descriptor.LogicalParent,
			EWebToUETreeParticipation::Semantic) : FWebToUEInstanceHandle();
	Entry.BaseLayoutParent = Entry.Projection.LayoutParent;
	Entry.BasePaintParent = Entry.Projection.PaintParent;
	Entry.BaseSemanticParent = Entry.Projection.SemanticParent;
	Nodes.Add(Descriptor.Handle, MoveTemp(Entry));
	return true;
}

FWebToUEInstanceHandle FWebToUETreeProjectionPolicy::FindNearestParticipant(
	FWebToUEInstanceHandle Start, EWebToUETreeParticipation Required) const
{
	FWebToUEInstanceHandle Current = Start;
	while (Current.IsValid())
	{
		const FNodeEntry* Entry = Nodes.Find(Current);
		if (!Entry)
		{
			return {};
		}
		const bool bParticipates =
			(Required == EWebToUETreeParticipation::Layout && Entry->Projection.bInLayoutTree) ||
			(Required == EWebToUETreeParticipation::Paint && Entry->Projection.bInPaintTree) ||
			(Required == EWebToUETreeParticipation::Semantic && Entry->Projection.bInSemanticTree);
		if (bParticipates)
		{
			return Current;
		}
		Current = Entry->Projection.LogicalParent;
	}
	return {};
}

bool FWebToUETreeProjectionPolicy::RegisterOverlayAnchor(
	const FWebToUEOverlayAnchorDescriptor& Descriptor, FString& OutError)
{
	using namespace WebToUE::TreeProjection::Private;
	OutError.Reset();
	if (Descriptor.AnchorId.IsNone() || Descriptor.Session != Session ||
		Descriptor.SurfaceId != SurfaceId)
	{
		return Fail(OutError, TEXT("WTUE-TREE-003"),
			TEXT("Overlay Anchor must name the owning Session and Surface exactly"));
	}
	if (Anchors.Contains(Descriptor.AnchorId))
	{
		return Fail(OutError, TEXT("WTUE-TREE-003"),
			TEXT("Overlay Anchor names are unique within a Session Surface"));
	}
	const FNodeEntry* PaintEntry = Nodes.Find(Descriptor.PaintParent);
	const FNodeEntry* SemanticEntry = Descriptor.SemanticParent.IsValid()
		? Nodes.Find(Descriptor.SemanticParent) : nullptr;
	if (!PaintEntry || !PaintEntry->Projection.bInPaintTree ||
		(Descriptor.SemanticParent.IsValid() &&
			(!SemanticEntry || !SemanticEntry->Projection.bInSemanticTree)))
	{
		return Fail(OutError, TEXT("WTUE-TREE-003"),
			TEXT("Overlay Anchor parents must exist in their projected trees"));
	}
	Anchors.Add(Descriptor.AnchorId, Descriptor);
	return true;
}

FWebToUEInstanceHandle FWebToUETreeProjectionPolicy::GetParent(
	const FWebToUETreeNodeProjection& Projection, EWebToUETreeKind Tree) const
{
	switch (Tree)
	{
	case EWebToUETreeKind::Component: return Projection.ComponentParent;
	case EWebToUETreeKind::Logical: return Projection.LogicalParent;
	case EWebToUETreeKind::Layout: return Projection.LayoutParent;
	case EWebToUETreeKind::Paint: return Projection.PaintParent;
	case EWebToUETreeKind::Semantic: return Projection.SemanticParent;
	default: return {};
	}
}

bool FWebToUETreeProjectionPolicy::WouldCreateCycle(
	FWebToUEInstanceHandle Child,
	FWebToUEInstanceHandle NewParent,
	EWebToUETreeKind Tree) const
{
	FWebToUEInstanceHandle Current = NewParent;
	while (Current.IsValid())
	{
		if (Current == Child)
		{
			return true;
		}
		const FNodeEntry* Entry = Nodes.Find(Current);
		if (!Entry)
		{
			return true;
		}
		Current = GetParent(Entry->Projection, Tree);
	}
	return false;
}

bool FWebToUETreeProjectionPolicy::MountPortal(
	const FWebToUEPortalMount& Mount, FString& OutError)
{
	using namespace WebToUE::TreeProjection::Private;
	OutError.Reset();
	FNodeEntry* Root = Nodes.Find(Mount.PortalRoot);
	const FWebToUEOverlayAnchorDescriptor* Anchor = Anchors.Find(Mount.AnchorId);
	if (!Root || !Anchor || Root->Projection.bPortalRoot ||
		!Root->Projection.bInLayoutTree || !Root->Projection.bInPaintTree)
	{
		return Fail(OutError, TEXT("WTUE-TREE-004"),
			TEXT("Portal root and same-context Overlay Anchor must be registered once"));
	}
	if (Mount.bModal && (!Anchor->bAllowsModal ||
		!Anchor->SemanticParent.IsValid() || !Root->Projection.bInSemanticTree))
	{
		return Fail(OutError, TEXT("WTUE-TREE-004"),
			TEXT("modal Portal requires an explicit modal-capable Semantic anchor"));
	}
	if (WouldCreateCycle(Mount.PortalRoot, Anchor->PaintParent,
		EWebToUETreeKind::Paint) ||
		(Mount.bModal && WouldCreateCycle(Mount.PortalRoot,
			Anchor->SemanticParent, EWebToUETreeKind::Semantic)))
	{
		return Fail(OutError, TEXT("WTUE-TREE-004"),
			TEXT("Portal projection would create a Paint or Semantic cycle"));
	}

	Root->Projection.LayoutParent = {};
	Root->Projection.PaintParent = Anchor->PaintParent;
	if (Mount.bModal)
	{
		Root->Projection.SemanticParent = Anchor->SemanticParent;
	}
	Root->Projection.OverlayAnchorId = Mount.AnchorId;
	Root->Projection.OverlayOrder = Mount.OverlayOrder;
	Root->Projection.bPortalRoot = true;
	Root->Projection.bModal = Mount.bModal;
	return true;
}

bool FWebToUETreeProjectionPolicy::UnmountPortal(
	FWebToUEInstanceHandle PortalRoot, FString& OutError)
{
	using namespace WebToUE::TreeProjection::Private;
	OutError.Reset();
	FNodeEntry* Root = Nodes.Find(PortalRoot);
	if (!Root || !Root->Projection.bPortalRoot)
	{
		return Fail(OutError, TEXT("WTUE-TREE-004"),
			TEXT("only an active Portal root can be unmounted"));
	}
	Root->Projection.LayoutParent = Root->BaseLayoutParent;
	Root->Projection.PaintParent = Root->BasePaintParent;
	Root->Projection.SemanticParent = Root->BaseSemanticParent;
	Root->Projection.OverlayAnchorId = NAME_None;
	Root->Projection.OverlayOrder = 0;
	Root->Projection.bPortalRoot = false;
	Root->Projection.bModal = false;
	return true;
}

const FWebToUETreeNodeProjection* FWebToUETreeProjectionPolicy::FindProjection(
	FWebToUEInstanceHandle Handle) const
{
	const FNodeEntry* Entry = Nodes.Find(Handle);
	return Entry ? &Entry->Projection : nullptr;
}

bool FWebToUETreeProjectionPolicy::BuildPath(
	EWebToUETreeKind Tree,
	FWebToUEInstanceHandle Target,
	TArray<FWebToUEInstanceHandle>& OutRootToTarget) const
{
	OutRootToTarget.Reset();
	const FNodeEntry* TargetEntry = Nodes.Find(Target);
	if (!TargetEntry)
	{
		return false;
	}
	if ((Tree == EWebToUETreeKind::Layout && !TargetEntry->Projection.bInLayoutTree) ||
		(Tree == EWebToUETreeKind::Paint && !TargetEntry->Projection.bInPaintTree) ||
		(Tree == EWebToUETreeKind::Semantic && !TargetEntry->Projection.bInSemanticTree))
	{
		return false;
	}
	FWebToUEInstanceHandle Current = Target;
	while (Current.IsValid())
	{
		const FNodeEntry* Entry = Nodes.Find(Current);
		if (!Entry)
		{
			OutRootToTarget.Reset();
			return false;
		}
		OutRootToTarget.Add(Current);
		Current = GetParent(Entry->Projection, Tree);
	}
	Algo::Reverse(OutRootToTarget);
	return true;
}

void FWebToUETreeProjectionPolicy::GetPortalRootsInPaintOrder(
	TArray<FWebToUEInstanceHandle>& OutPortalRoots) const
{
	OutPortalRoots.Reset();
	for (const TPair<FWebToUEInstanceHandle, FNodeEntry>& Pair : Nodes)
	{
		if (Pair.Value.Projection.bPortalRoot)
		{
			OutPortalRoots.Add(Pair.Key);
		}
	}
	OutPortalRoots.Sort([this](FWebToUEInstanceHandle A, FWebToUEInstanceHandle B)
	{
		const FWebToUETreeNodeProjection& PA = Nodes.FindChecked(A).Projection;
		const FWebToUETreeNodeProjection& PB = Nodes.FindChecked(B).Projection;
		return PA.OverlayOrder != PB.OverlayOrder
			? PA.OverlayOrder < PB.OverlayOrder : A.GetSlot() < B.GetSlot();
	});
}

bool FWebToUETreeProjectionPolicy::IsLogicalDescendantOrSelf(
	FWebToUEInstanceHandle Candidate, FWebToUEInstanceHandle Ancestor) const
{
	FWebToUEInstanceHandle Current = Candidate;
	while (Current.IsValid())
	{
		if (Current == Ancestor)
		{
			return true;
		}
		const FNodeEntry* Entry = Nodes.Find(Current);
		if (!Entry)
		{
			return false;
		}
		Current = Entry->Projection.LogicalParent;
	}
	return false;
}

bool FWebToUETreeProjectionPolicy::CaptureFocusRestore(
	FWebToUEInstanceHandle PortalRoot,
	FWebToUEInstanceHandle Origin,
	FWebToUEFocusRestoreToken& OutToken,
	FString& OutError) const
{
	using namespace WebToUE::TreeProjection::Private;
	OutError.Reset();
	OutToken = {};
	if (!Nodes.Contains(PortalRoot) || !Nodes.Contains(Origin) ||
		IsLogicalDescendantOrSelf(Origin, PortalRoot))
	{
		return Fail(OutError, TEXT("WTUE-TREE-005"),
			TEXT("focus origin must be a live node outside the Portal logical subtree"));
	}
	OutToken.Session = Session;
	OutToken.SurfaceId = SurfaceId;
	OutToken.PortalRoot = PortalRoot;
	FWebToUEInstanceHandle Current = Origin;
	while (Current.IsValid())
	{
		const FNodeEntry* Entry = Nodes.Find(Current);
		if (!Entry)
		{
			OutToken = {};
			return Fail(OutError, TEXT("WTUE-TREE-005"),
				TEXT("focus restore path left the current Logical tree"));
		}
		if (Entry->Projection.bInSemanticTree)
		{
			OutToken.LogicalCandidates.Add(Current);
		}
		Current = Entry->Projection.LogicalParent;
	}
	if (OutToken.LogicalCandidates.IsEmpty())
	{
		OutToken = {};
		return Fail(OutError, TEXT("WTUE-TREE-005"),
			TEXT("focus restore path contains no Semantic candidate"));
	}
	return true;
}

FWebToUEInstanceHandle FWebToUETreeProjectionPolicy::ResolveFocusRestore(
	const FWebToUEFocusRestoreToken& Token,
	TFunctionRef<bool(FWebToUEInstanceHandle)> IsFocusable) const
{
	if (!Token.IsValid() || Token.Session != Session || Token.SurfaceId != SurfaceId)
	{
		return {};
	}
	const FWebToUEInstanceHandle TopModal = GetTopModalPortal();
	for (FWebToUEInstanceHandle Candidate : Token.LogicalCandidates)
	{
		const FNodeEntry* Entry = Nodes.Find(Candidate);
		if (!Entry || !Entry->Projection.bInSemanticTree ||
			(TopModal.IsValid() && !IsLogicalDescendantOrSelf(Candidate, TopModal)))
		{
			continue;
		}
		if (IsFocusable(Candidate))
		{
			return Candidate;
		}
	}
	return {};
}

FWebToUEInstanceHandle FWebToUETreeProjectionPolicy::GetTopModalPortal() const
{
	FWebToUEInstanceHandle Best;
	int32 BestOrder = TNumericLimits<int32>::Lowest();
	for (const TPair<FWebToUEInstanceHandle, FNodeEntry>& Pair : Nodes)
	{
		const FWebToUETreeNodeProjection& Projection = Pair.Value.Projection;
		if (!Projection.bPortalRoot || !Projection.bModal)
		{
			continue;
		}
		if (!Best.IsValid() || Projection.OverlayOrder > BestOrder ||
			(Projection.OverlayOrder == BestOrder && Pair.Key.GetSlot() > Best.GetSlot()))
		{
			Best = Pair.Key;
			BestOrder = Projection.OverlayOrder;
		}
	}
	return Best;
}

bool FWebToUETreeProjectionPolicy::IsInTopModalScope(
	FWebToUEInstanceHandle Handle) const
{
	if (!Nodes.Contains(Handle))
	{
		return false;
	}
	const FWebToUEInstanceHandle TopModal = GetTopModalPortal();
	return !TopModal.IsValid() || IsLogicalDescendantOrSelf(Handle, TopModal);
}
