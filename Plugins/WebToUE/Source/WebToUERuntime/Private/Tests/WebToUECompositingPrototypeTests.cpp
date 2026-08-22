#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"
#include "WebToUECompositing.h"
#include "WebToUERuntimePresentation.h"
#include "WebToUETreeProjection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUECompositingTierPolicyPrototypeTest,
	"WebToUE.Runtime.CompositingTierPolicyPrototype",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUECompositingAdversarialSubtreePrototypeTest,
	"WebToUE.Runtime.CompositingAdversarialSubtreePrototype",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUECompositingProjectedSubtreePrototypeTest,
	"WebToUE.Runtime.CompositingProjectedSubtreePrototype",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::Compositing::Prototype
{
	static FWebToUEInstanceHandle H(int32 Slot)
	{
		return FWebToUEInstanceHandle::Create(71, 5, Slot);
	}

	static bool AddNode(FWebToUETreeProjectionPolicy& Policy, int32 Slot,
		int32 LogicalParent, FString& Error)
	{
		FWebToUETreeNodeDescriptor Descriptor;
		Descriptor.Handle = H(Slot);
		Descriptor.LogicalParent = LogicalParent == INDEX_NONE
			? FWebToUEInstanceHandle() : H(LogicalParent);
		Descriptor.ComponentParent = Descriptor.LogicalParent;
		return Policy.AddNode(Descriptor, Error);
	}

	static FWebToUECompositingBudget Budget()
	{
		FWebToUECompositingBudget Result;
		Result.MaxActiveLayers = 2;
		Result.MaxActiveSurfaces = 1;
		Result.MaxAllocatedPixels = 512 * 512;
		Result.MaxAllocatedBytes = 512 * 512 * 4;
		return Result;
	}

	static FWebToUECompositingBackend AllBackends()
	{
		FWebToUECompositingBackend Result;
		Result.bSubtreeLayerAvailable = true;
		Result.bRenderTargetAvailable = true;
		return Result;
	}
}

bool FWebToUECompositingTierPolicyPrototypeTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Compositing::Prototype;
	const FWebToUECompositingUsage EmptyUsage;
	const FWebToUECompositingBackend Backends = AllBackends();
	const FWebToUECompositingBudget Limits = Budget();

	FWebToUECompositingRequest Request;
	TestEqual(TEXT("Plain Display commands remain Tier 0"),
		FWebToUECompositingPolicy::Select(Request, Backends, Limits, EmptyUsage).Tier,
		EWebToUECompositingTier::DirectPaint);
	Request.Requirements = EWebToUECompositingRequirement::MaterialBrush;
	TestEqual(TEXT("A static Material Brush selects Tier 1"),
		FWebToUECompositingPolicy::Select(Request, Backends, Limits, EmptyUsage).Tier,
		EWebToUECompositingTier::MaterialBrush);
	Request.Requirements |= EWebToUECompositingRequirement::IsolatedSubtree;
	Request.PixelExtent = FIntPoint(256, 128);
	TestEqual(TEXT("Isolation dominates Material and selects Tier 2"),
		FWebToUECompositingPolicy::Select(Request, Backends, Limits, EmptyUsage).Tier,
		EWebToUECompositingTier::SubtreeLayer);
	Request.Requirements |=
		EWebToUECompositingRequirement::SamplesCompositedSubtree;
	TestEqual(TEXT("Sampling an already composited subtree selects Tier 3"),
		FWebToUECompositingPolicy::Select(Request, Backends, Limits, EmptyUsage).Tier,
		EWebToUECompositingTier::RenderTarget);

	FWebToUECompositingBackend NoLayer = Backends;
	NoLayer.bSubtreeLayerAvailable = false;
	Request.Requirements = EWebToUECompositingRequirement::IsolatedSubtree;
	const FWebToUECompositingDecision Missing =
		FWebToUECompositingPolicy::Select(Request, NoLayer, Limits, EmptyUsage);
	TestFalse(TEXT("An unavailable required tier fails closed"), Missing.bAccepted);
	TestTrue(TEXT("Backend refusal has a stable diagnostic"),
		Missing.Diagnostic.StartsWith(TEXT("WTUE-COMP-002")));

	FWebToUECompositingUsage FullUsage;
	FullUsage.ActiveLayers = Limits.MaxActiveLayers;
	const FWebToUECompositingDecision Full =
		FWebToUECompositingPolicy::Select(Request, Backends, Limits, FullUsage);
	TestFalse(TEXT("An exhausted legal layer budget fails closed"), Full.bAccepted);
	TestTrue(TEXT("Budget refusal has a stable diagnostic"),
		Full.Diagnostic.StartsWith(TEXT("WTUE-COMP-003")));
	return true;
}

bool FWebToUECompositingAdversarialSubtreePrototypeTest::RunTest(
	const FString& Parameters)
{
	using namespace WebToUE::Compositing::Prototype;
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='group'><div id='lower'></div>"
			"<div id='upper'></div></div></body>"),
		TEXT("body { width: 320px; height: 180px; } "
			"#group { width: 160px; height: 120px; overflow: hidden; opacity: 0.5; "
			"transform: rotate(4deg); } "
			"#lower { width: 100px; height: 70px; background-color: #ff4030; } "
			"#upper { width: 100px; height: 70px; background-color: #3060ff; "
			"transform: translate(35px, -45px); }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	View->LayoutForTesting(FVector2f(320.0f, 180.0f));
	FWebToUENode* Lower = View->FindRuntimeNodeByIdForTesting(TEXT("lower"));
	FWebToUENode* Upper = View->FindRuntimeNodeByIdForTesting(TEXT("upper"));
	TestNotNull(TEXT("The lower sibling exists"), Lower);
	TestNotNull(TEXT("The upper sibling exists"), Upper);
	if (!Lower || !Upper) return false;
	const FWebToUEPaintCommand* LowerCommand =
		View->GetDisplayCommandForTesting(*Lower);
	const FWebToUEPaintCommand* UpperCommand =
		View->GetDisplayCommandForTesting(*Upper);
	TestNotNull(TEXT("The lower sibling owns a Display command"), LowerCommand);
	TestNotNull(TEXT("The upper sibling owns a Display command"), UpperCommand);
	if (!LowerCommand || !UpperCommand) return false;
	TestTrue(TEXT("The transformed siblings overlap in Paint space"),
		FSlateRect::DoRectanglesIntersect(
			LowerCommand->VisibleBounds, UpperCommand->VisibleBounds));
	TestEqual(TEXT("The current direct path multiplies group opacity into the lower command"),
		LowerCommand->Opacity, 0.5f);
	TestEqual(TEXT("The current direct path multiplies group opacity into the upper command"),
		UpperCommand->Opacity, 0.5f);

	FWebToUECompositingRequest Request;
	Request.Requirements = EWebToUECompositingRequirement::IsolatedSubtree;
	Request.PixelExtent = FIntPoint(160, 120);
	const FWebToUECompositingDecision Decision =
		FWebToUECompositingPolicy::Select(
			Request, AllBackends(), Budget(), FWebToUECompositingUsage());
	TestTrue(TEXT("Overlapping descendants under group opacity are accepted with isolation"),
		Decision.bAccepted);
	TestEqual(TEXT("The adversarial ordinary subtree requires Tier 2"),
		Decision.Tier, EWebToUECompositingTier::SubtreeLayer);
	return true;
}

bool FWebToUECompositingProjectedSubtreePrototypeTest::RunTest(
	const FString& Parameters)
{
	using namespace WebToUE::Compositing::Prototype;
	const FWebToUESessionHandle Session = FWebToUESessionHandle::Create(91, 5);
	FWebToUETreeProjectionPolicy Projection(Session, TEXT("screen.player0"));
	FString Error;
	TestTrue(TEXT("The logical root registers"), AddNode(Projection, 0, INDEX_NONE, Error));
	TestTrue(TEXT("The logical owner registers"), AddNode(Projection, 1, 0, Error));
	TestTrue(TEXT("The Overlay Anchor target registers"), AddNode(Projection, 2, 0, Error));
	TestTrue(TEXT("The projected subtree root registers"), AddNode(Projection, 3, 1, Error));
	TestTrue(TEXT("The projected child registers"), AddNode(Projection, 4, 3, Error));
	FWebToUEOverlayAnchorDescriptor Anchor;
	Anchor.AnchorId = TEXT("overlay");
	Anchor.Session = Session;
	Anchor.SurfaceId = TEXT("screen.player0");
	Anchor.PaintParent = H(2);
	TestTrue(TEXT("The same Session/Surface Overlay Anchor registers"),
		Projection.RegisterOverlayAnchor(Anchor, Error));
	FWebToUEPortalMount Mount;
	Mount.PortalRoot = H(3);
	Mount.AnchorId = TEXT("overlay");
	Mount.OverlayOrder = 40;
	TestTrue(TEXT("The projected subtree mounts without changing ownership"),
		Projection.MountPortal(Mount, Error));
	TArray<FWebToUEInstanceHandle> LogicalPath;
	TArray<FWebToUEInstanceHandle> PaintPath;
	Projection.BuildPath(EWebToUETreeKind::Logical, H(4), LogicalPath);
	Projection.BuildPath(EWebToUETreeKind::Paint, H(4), PaintPath);
	TestTrue(TEXT("Projection preserves the Logical owner path"),
		LogicalPath == TArray<FWebToUEInstanceHandle>({ H(0), H(1), H(3), H(4) }));
	TestTrue(TEXT("Projection redirects only the Paint path"),
		PaintPath == TArray<FWebToUEInstanceHandle>({ H(0), H(2), H(3), H(4) }));

	FWebToUECompositingRequest DirectProjection;
	DirectProjection.Requirements = EWebToUECompositingRequirement::ProjectedSubtree;
	const FWebToUECompositingDecision Direct =
		FWebToUECompositingPolicy::Select(
			DirectProjection, AllBackends(), Budget(), FWebToUECompositingUsage());
	TestTrue(TEXT("Projection alone does not force an offscreen allocation"), Direct.bAccepted);
	TestEqual(TEXT("A same-Surface projected subtree can remain Tier 0"),
		Direct.Tier, EWebToUECompositingTier::DirectPaint);
	DirectProjection.Requirements |=
		EWebToUECompositingRequirement::IsolatedSubtree;
	DirectProjection.PixelExtent = FIntPoint(128, 96);
	const FWebToUECompositingDecision Isolated =
		FWebToUECompositingPolicy::Select(
			DirectProjection, AllBackends(), Budget(), FWebToUECompositingUsage());
	TestEqual(TEXT("Projected overlap/group opacity upgrades deterministically to Tier 2"),
		Isolated.Tier, EWebToUECompositingTier::SubtreeLayer);
	return true;
}

#endif
