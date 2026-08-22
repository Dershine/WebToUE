#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "WebToUECompositing.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUECompositingPlanLifecycleTest,
	"WebToUE.Runtime.CompositingPlanLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::Compositing::Lifecycle
{
	static FWebToUEInstanceHandle H(int32 Slot, uint32 Generation = 7)
	{
		return FWebToUEInstanceHandle::Create(101, Generation, Slot);
	}

	static FWebToUECompositingBudget Budget()
	{
		FWebToUECompositingBudget Result;
		Result.MaxActiveLayers = 2;
		Result.MaxActiveSurfaces = 1;
		Result.MaxAllocatedPixels = 128 * 128 * 2;
		Result.MaxAllocatedBytes = 128 * 128 * 2 * 4;
		return Result;
	}

	static FWebToUECompositingBackend Backend()
	{
		FWebToUECompositingBackend Result;
		Result.bSubtreeLayerAvailable = true;
		Result.bRenderTargetAvailable = true;
		return Result;
	}

	static FWebToUECompositingNodeRequest Node(
		int32 Slot, int32 PaintSequence, EWebToUECompositingRequirement Requirements)
	{
		FWebToUECompositingNodeRequest Result;
		Result.Owner = H(Slot);
		Result.PaintSequence = PaintSequence;
		Result.Request.Requirements = Requirements;
		if (EnumHasAnyFlags(Requirements,
			EWebToUECompositingRequirement::IsolatedSubtree |
			EWebToUECompositingRequirement::SamplesCompositedSubtree))
		{
			Result.Request.PixelExtent = FIntPoint(128, 128);
		}
		return Result;
	}
}

bool FWebToUECompositingPlanLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Compositing::Lifecycle;
	FWebToUECompositingPlan Plan;
	TArray<FWebToUECompositingNodeRequest> Nodes = {
		Node(2, 20, EWebToUECompositingRequirement::IsolatedSubtree),
		Node(1, 10, EWebToUECompositingRequirement::None)
	};
	TestTrue(TEXT("The deterministic mixed-tier plan builds"),
		Plan.Build(Nodes, Backend(), Budget()));
	TestEqual(TEXT("Paint sequence, not input order, owns plan order"),
		Plan.GetEntries()[0].Owner, H(1));
	TestEqual(TEXT("The legal plan reserves one active layer"),
		Plan.GetReservedUsage().ActiveLayers, 1);

	FWebToUECompositingCache Cache(101, 7, TEXT("screen.player0"));
	FString Error;
	TestTrue(TEXT("The View/Surface cache accepts its generation"),
		Cache.ApplyPlan(Plan, 1, Error));
	TestEqual(TEXT("Both direct and layered decisions are cached"),
		Cache.GetStats().CachedEntries, 2);
	TestEqual(TEXT("Only Tier 2 contributes an active layer"),
		Cache.GetStats().Usage.ActiveLayers, 1);
	TestEqual(TEXT("The first plan allocates two cache entries"),
		Cache.GetStats().Allocated, uint64(2));
	TestTrue(TEXT("An unchanged plan reuses every entry"),
		Cache.ApplyPlan(Plan, 1, Error));
	TestEqual(TEXT("Two entries are reused"), Cache.GetStats().Reused, uint64(2));

	Nodes[0] = Node(2, 20, EWebToUECompositingRequirement::None);
	TestTrue(TEXT("A Tier change rebuilds the plan"),
		Plan.Build(Nodes, Backend(), Budget()));
	TestTrue(TEXT("A Tier change replaces only its owner entry"),
		Cache.ApplyPlan(Plan, 1, Error));
	TestEqual(TEXT("Tier replacement releases one prior entry"),
		Cache.GetStats().Released, uint64(1));
	TestEqual(TEXT("Tier replacement leaves no active layer"),
		Cache.GetStats().Usage.ActiveLayers, 0);

	TestTrue(TEXT("A Projection revision is accepted"),
		Cache.ApplyPlan(Plan, 2, Error));
	TestEqual(TEXT("Projection revision evicts both cached projections"),
		Cache.GetStats().Evicted, uint64(3));
	Cache.RemoveOwner(H(2));
	TestEqual(TEXT("Node deletion releases the exact owner"),
		Cache.GetStats().CachedEntries, 1);
	Cache.AdvanceGeneration(8);
	TestEqual(TEXT("Generation advance releases all old handles"),
		Cache.GetStats().CachedEntries, 0);

	TArray<FWebToUECompositingNodeRequest> Stale = {
		Node(1, 0, EWebToUECompositingRequirement::None)
	};
	Plan.Build(Stale, Backend(), Budget());
	TestFalse(TEXT("Old-generation work cannot repopulate the cache"),
		Cache.ApplyPlan(Plan, 3, Error));
	TestTrue(TEXT("Old-generation refusal is stable"),
		Error.StartsWith(TEXT("WTUE-COMP-001")));

	Cache.DetachSurface();
	TestFalse(TEXT("A detached Surface rejects late plans"),
		Cache.ApplyPlan(Plan, 4, Error));
	TestTrue(TEXT("Surface detach refusal is stable"),
		Error.StartsWith(TEXT("WTUE-COMP-005")));
	Cache.Shutdown();
	TestTrue(TEXT("Session/World teardown leaves the cache shut down"),
		Cache.IsShutdown());
	TestEqual(TEXT("Session/World teardown retains no entry"),
		Cache.GetStats().CachedEntries, 0);

	FWebToUECompositingPlan OverBudget;
	TArray<FWebToUECompositingNodeRequest> ThreeLayers = {
		Node(1, 1, EWebToUECompositingRequirement::IsolatedSubtree),
		Node(2, 2, EWebToUECompositingRequirement::IsolatedSubtree),
		Node(3, 3, EWebToUECompositingRequirement::IsolatedSubtree)
	};
	TestFalse(TEXT("The full legal in-flight set is budgeted cumulatively"),
		OverBudget.Build(ThreeLayers, Backend(), Budget()));
	TestTrue(TEXT("Cumulative capacity failure is stable"),
		OverBudget.GetDiagnostic().StartsWith(TEXT("WTUE-COMP-003")));
	return true;
}

#endif
