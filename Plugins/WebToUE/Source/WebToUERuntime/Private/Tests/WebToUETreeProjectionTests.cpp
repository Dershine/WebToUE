#if WITH_DEV_AUTOMATION_TESTS

#include "WebToUETreeProjection.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETreeProjectionTest,
	"WebToUE.Runtime.TreeProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPortalFocusRestoreTest,
	"WebToUE.Runtime.PortalFocusRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::TreeProjection::Tests
{
	static FWebToUEInstanceHandle H(int32 Slot, uint32 Generation = 7)
	{
		return FWebToUEInstanceHandle::Create(41, Generation, Slot);
	}

	static FWebToUESessionHandle Session(uint32 Generation = 3)
	{
		return FWebToUESessionHandle::Create(23, Generation);
	}

	static bool Add(
		FWebToUETreeProjectionPolicy& Policy,
		int32 Slot,
		int32 LogicalParent,
		FString& Error,
		EWebToUETreeParticipation Participation = EWebToUETreeParticipation::All,
		int32 ComponentParent = INDEX_NONE)
	{
		FWebToUETreeNodeDescriptor Descriptor;
		Descriptor.Handle = H(Slot);
		Descriptor.LogicalParent = LogicalParent == INDEX_NONE
			? FWebToUEInstanceHandle() : H(LogicalParent);
		Descriptor.ComponentParent = ComponentParent == INDEX_NONE
			? Descriptor.LogicalParent : H(ComponentParent);
		Descriptor.Participation = Participation;
		return Policy.AddNode(Descriptor, Error);
	}

	static FWebToUEOverlayAnchorDescriptor Anchor(
		FName Id, int32 PaintParent, int32 SemanticParent, bool bAllowsModal)
	{
		FWebToUEOverlayAnchorDescriptor Result;
		Result.AnchorId = Id;
		Result.Session = Session();
		Result.SurfaceId = TEXT("screen.player0");
		Result.PaintParent = H(PaintParent);
		Result.SemanticParent = H(SemanticParent);
		Result.bAllowsModal = bAllowsModal;
		return Result;
	}
}

bool FWebToUETreeProjectionTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::TreeProjection::Tests;
	FWebToUETreeProjectionPolicy Policy(Session(), TEXT("screen.player0"));
	FString Error;
	TestTrue(TEXT("The logical root registers"), Add(Policy, 0, INDEX_NONE, Error));
	TestTrue(TEXT("A component-only wrapper registers"), Add(Policy, 1, 0, Error,
		EWebToUETreeParticipation::None));
	TestTrue(TEXT("The focus origin registers through the wrapper"),
		Add(Policy, 2, 1, Error));
	TestTrue(TEXT("The explicit overlay target registers"), Add(Policy, 3, 0, Error));
	TestTrue(TEXT("The Portal root registers under its logical owner"),
		Add(Policy, 4, 1, Error, EWebToUETreeParticipation::All, 1));
	TestTrue(TEXT("Portal content registers below the Portal root"),
		Add(Policy, 5, 4, Error));

	const FWebToUETreeNodeProjection* Before = Policy.FindProjection(H(4));
	TestNotNull(TEXT("The Portal candidate has a projection"), Before);
	if (!Before)
	{
		return false;
	}
	TestTrue(TEXT("Excluded wrappers flatten out of the Layout projection"),
		Before->LayoutParent == H(0));
	TestTrue(TEXT("Excluded wrappers flatten out of the Paint projection"),
		Before->PaintParent == H(0));
	TestTrue(TEXT("Excluded wrappers flatten out of the Semantic projection"),
		Before->SemanticParent == H(0));
	TestTrue(TEXT("Component ownership remains independently explicit"),
		Before->ComponentParent == H(1));

	TestTrue(TEXT("A same-Session/Surface anchor registers"),
		Policy.RegisterOverlayAnchor(Anchor(TEXT("modal"), 3, 3, true), Error));
	FWebToUEPortalMount Mount;
	Mount.PortalRoot = H(4);
	Mount.AnchorId = TEXT("modal");
	Mount.OverlayOrder = 20;
	Mount.bModal = true;
	TestTrue(TEXT("A valid modal Portal mounts"), Policy.MountPortal(Mount, Error));
	const FWebToUETreeNodeProjection* Mounted = Policy.FindProjection(H(4));
	TestNotNull(TEXT("The mounted Portal remains queryable"), Mounted);
	if (!Mounted)
	{
		return false;
	}
	TestTrue(TEXT("Portal keeps its Component owner"), Mounted->ComponentParent == H(1));
	TestTrue(TEXT("Portal keeps its Logical owner"), Mounted->LogicalParent == H(1));
	TestFalse(TEXT("Portal becomes an independent Layout root"),
		Mounted->LayoutParent.IsValid());
	TestTrue(TEXT("Portal Paint attaches to the explicit Overlay Anchor"),
		Mounted->PaintParent == H(3));
	TestTrue(TEXT("Modal Semantic projection attaches only to the explicit Semantic anchor"),
		Mounted->SemanticParent == H(3));

	TArray<FWebToUEInstanceHandle> LogicalPath;
	TArray<FWebToUEInstanceHandle> PaintPath;
	TestTrue(TEXT("Logical event/state path remains available"),
		Policy.BuildPath(EWebToUETreeKind::Logical, H(5), LogicalPath));
	TestTrue(TEXT("Paint path follows the visual Overlay projection"),
		Policy.BuildPath(EWebToUETreeKind::Paint, H(5), PaintPath));
	TestTrue(TEXT("Portal does not rewrite the logical event/state route"),
		LogicalPath == TArray<FWebToUEInstanceHandle>({ H(0), H(1), H(4), H(5) }));
	TestTrue(TEXT("Portal paint is rooted at the Overlay target"),
		PaintPath == TArray<FWebToUEInstanceHandle>({ H(0), H(3), H(4), H(5) }));

	FWebToUEOverlayAnchorDescriptor WrongSurface = Anchor(TEXT("wrong"), 3, 3, true);
	WrongSurface.SurfaceId = TEXT("screen.player1");
	TestFalse(TEXT("Cross-Surface anchors are rejected"),
		Policy.RegisterOverlayAnchor(WrongSurface, Error));
	TestTrue(TEXT("Cross-Surface rejection is stable and actionable"),
		Error.StartsWith(TEXT("WTUE-TREE-003")));

	FWebToUEOverlayAnchorDescriptor CycleAnchor = Anchor(TEXT("cycle"), 5, 3, false);
	TestTrue(TEXT("A descendant Paint node can be named as an anchor before mounting"),
		Policy.RegisterOverlayAnchor(CycleAnchor, Error));
	TestTrue(TEXT("The active Portal unmounts without changing ownership"),
		Policy.UnmountPortal(H(4), Error));
	Mount.AnchorId = TEXT("cycle");
	Mount.bModal = false;
	TestFalse(TEXT("A Paint projection cycle is rejected"), Policy.MountPortal(Mount, Error));
	TestTrue(TEXT("Cycle rejection has a stable diagnostic"),
		Error.StartsWith(TEXT("WTUE-TREE-004")));

	FWebToUETreeNodeDescriptor Stale;
	Stale.Handle = H(6, 8);
	Stale.LogicalParent = H(0);
	TestFalse(TEXT("Cross-generation nodes are rejected"), Policy.AddNode(Stale, Error));
	TestTrue(TEXT("Generation rejection uses the node-domain diagnostic"),
		Error.StartsWith(TEXT("WTUE-TREE-001")));
	return true;
}

bool FWebToUEPortalFocusRestoreTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::TreeProjection::Tests;
	FWebToUETreeProjectionPolicy Policy(Session(), TEXT("screen.player0"));
	FString Error;
	Add(Policy, 0, INDEX_NONE, Error);
	Add(Policy, 1, 0, Error);
	Add(Policy, 2, 1, Error);
	Add(Policy, 3, 0, Error);
	Add(Policy, 4, 0, Error);
	Add(Policy, 5, 4, Error);
	Add(Policy, 6, 0, Error);
	Add(Policy, 7, 6, Error);
	Policy.RegisterOverlayAnchor(Anchor(TEXT("modal"), 3, 3, true), Error);

	FWebToUEFocusRestoreToken Token;
	TestTrue(TEXT("Focus restore captures the origin and logical ancestors before open"),
		Policy.CaptureFocusRestore(H(4), H(2), Token, Error));
	TestTrue(TEXT("Focus candidates follow Logical rather than Paint ancestry"),
		Token.LogicalCandidates ==
			TArray<FWebToUEInstanceHandle>({ H(2), H(1), H(0) }));

	FWebToUEPortalMount First;
	First.PortalRoot = H(4);
	First.AnchorId = TEXT("modal");
	First.OverlayOrder = 10;
	First.bModal = true;
	TestTrue(TEXT("The first modal Portal mounts"), Policy.MountPortal(First, Error));
	TestTrue(TEXT("The active modal owns the Semantic focus scope"),
		Policy.GetTopModalPortal() == H(4));
	TestFalse(TEXT("Background focus is inert while a modal is active"),
		Policy.IsInTopModalScope(H(2)));
	TestTrue(TEXT("Modal descendants remain in the active focus scope"),
		Policy.IsInTopModalScope(H(5)));
	TestFalse(TEXT("Focus restore cannot escape an active modal"),
		Policy.ResolveFocusRestore(Token, [](FWebToUEInstanceHandle) { return true; }).IsValid());

	TestTrue(TEXT("Closing the modal restores its base projections"),
		Policy.UnmountPortal(H(4), Error));
	const FWebToUEInstanceHandle Fallback = Policy.ResolveFocusRestore(
		Token, [](FWebToUEInstanceHandle Candidate)
		{
			return Candidate.GetSlot() == 1;
		});
	TestTrue(TEXT("Restore skips an unavailable origin and uses its live Logical ancestor"),
		Fallback == H(1));

	FWebToUEFocusRestoreToken WrongSession = Token;
	WrongSession.Session = Session(4);
	TestFalse(TEXT("Cross-generation Session tokens fail closed"),
		Policy.ResolveFocusRestore(
			WrongSession, [](FWebToUEInstanceHandle) { return true; }).IsValid());
	FWebToUEFocusRestoreToken StaleHandles = Token;
	for (FWebToUEInstanceHandle& Candidate : StaleHandles.LogicalCandidates)
	{
		Candidate = H(Candidate.GetSlot(), 8);
	}
	TestFalse(TEXT("Cross-reload handles do not fall back through a Stable Key"),
		Policy.ResolveFocusRestore(
			StaleHandles, [](FWebToUEInstanceHandle) { return true; }).IsValid());

	FWebToUEFocusRestoreToken InvalidOrigin;
	TestFalse(TEXT("A focus origin inside the Portal subtree is rejected"),
		Policy.CaptureFocusRestore(H(4), H(5), InvalidOrigin, Error));
	TestTrue(TEXT("Invalid focus capture uses a stable diagnostic"),
		Error.StartsWith(TEXT("WTUE-TREE-005")));

	FWebToUEPortalMount Lower = First;
	Lower.PortalRoot = H(4);
	Lower.OverlayOrder = 10;
	TestTrue(TEXT("The lower modal can reopen"), Policy.MountPortal(Lower, Error));
	FWebToUEPortalMount Higher = First;
	Higher.PortalRoot = H(6);
	Higher.OverlayOrder = 20;
	TestTrue(TEXT("A higher modal can mount at the same explicit anchor"),
		Policy.MountPortal(Higher, Error));
	TestTrue(TEXT("Overlay order deterministically selects the top modal"),
		Policy.GetTopModalPortal() == H(6));
	TArray<FWebToUEInstanceHandle> PaintOrder;
	Policy.GetPortalRootsInPaintOrder(PaintOrder);
	TestTrue(TEXT("Portal Paint order is deterministic"), PaintOrder ==
		TArray<FWebToUEInstanceHandle>({ H(4), H(6) }));
	TestFalse(TEXT("The lower modal is inert while covered"),
		Policy.IsInTopModalScope(H(5)));
	TestTrue(TEXT("The top modal descendant remains active"),
		Policy.IsInTopModalScope(H(7)));
	return true;
}

#endif
