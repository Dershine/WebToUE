#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SWebToUEView.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUEStyleProperties.h"

#include "Input/HittestGrid.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPerformanceInstrumentationTest,
	"WebToUE.Runtime.PerformanceInstrumentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::Performance::Tests
{
	static void AddAttribute(FWebToUECompiledNode& Node, const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUECompiledAttribute& Attribute = Node.Attributes.AddDefaulted_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
	}

	static UWebToUEDocument* MakeDocument()
	{
		UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
		FWebToUECompiledDocumentData CompiledDocument;
		CompiledDocument.LocalizationNamespace = TEXT("Measured Label");

		FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Body.Tag = TEXT("body");

		FWebToUECompiledNode& Button = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Button.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Button.Tag = TEXT("button");
		Button.ParentIndex = 0;
		AddAttribute(Button, TEXT("id"), TEXT("performance-target"));
		AddAttribute(Button, TEXT("data-ue-bind-text"), TEXT("LocalizationNamespace"));
		AddAttribute(Button, TEXT("data-ue-on-click"), TEXT("MeasuredClick"));
		FWebToUECompiledBindingOp& BindingOp = CompiledDocument.BindingOps.AddDefaulted_GetRef();
		BindingOp.RootField = FName(TEXT("LocalizationNamespace"));
		BindingOp.Kind = EWebToUEBindingKind::Text;
		BindingOp.TargetNodeIndex = 1;

		FWebToUECompiledNode& Text = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Text.Type = static_cast<uint8>(EWebToUENodeType::Text);
		Text.Tag = TEXT("#text");
		Text.Text = TEXT("Initial Label");
		Text.LocalizedText = FText::FromString(Text.Text);
		Text.ParentIndex = 1;

		FWebToUECompiledRule& Rule = CompiledDocument.Rules.AddDefaulted_GetRef();
		Rule.Specificity = 1;
		FWebToUECompiledSelectorSegment& Selector = Rule.Selector.AddDefaulted_GetRef();
		Selector.Type = TEXT("button");
		for (const TPair<const TCHAR*, const TCHAR*>& Pair : {
			TPair<const TCHAR*, const TCHAR*>(TEXT("width"), TEXT("120px")),
			TPair<const TCHAR*, const TCHAR*>(TEXT("height"), TEXT("40px")) })
		{
			FWebToUEStyleDeclaration Parsed;
			check(WebToUE::Private::TryParseCssDeclaration(Pair.Key, Pair.Value, Parsed));
			FWebToUECompiledDeclaration& Declaration = Rule.Declarations.AddDefaulted_GetRef();
			Declaration.Property = Parsed.Property;
			Declaration.TypedValue = Parsed.TypedValue;
		}

		CompiledDocument.RootNodeIndex = 0;
		Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
		return Document;
	}
}

bool FWebToUEPerformanceInstrumentationTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Performance::Tests;

	UWebToUEDocument* Document = MakeDocument();
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);

	FWebToUEPerformanceSnapshot Snapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetDocument(Document);
		View->RefreshBindings(Document);
		View->LayoutForTesting(FVector2f(320.0f, 180.0f));

		FHittestGrid HittestGrid;
		FSlateWindowElementList DrawElements(nullptr);
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(320.0, 180.0), FSlateLayoutTransform());
		const FPaintArgs PaintArgs(&View.Get(), HittestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
		View->OnPaint(PaintArgs, Geometry, FSlateRect(0.0f, 0.0f, 320.0f, 180.0f), DrawElements,
			0, FWidgetStyle(), true);
		TestNotNull(TEXT("The instrumented document remains hittable"),
			View->HitTestForTesting(FVector2f(10.0f, 10.0f)));

		Snapshot = Capture.GetSnapshot();
		AddInfo(Snapshot.ToLogString());
	}

	for (int32 Index = 0; Index < FWebToUEPerformanceSnapshot::PhaseCount; ++Index)
	{
		const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(Index);
		TestTrue(*FString::Printf(TEXT("%s records at least one call"), LexToString(Phase)),
			Snapshot.Get(Phase).CallCount > 0);
	}

	TestEqual(TEXT("Hydrate records the compiled node count"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::HydratedNodes), uint64(3));
	TestEqual(TEXT("Hydrate records the compiled rule count"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::HydratedRules), uint64(1));
	TestEqual(TEXT("Hydrate and initial cache setup visit all three style nodes"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(6));
	TestEqual(TEXT("Two style passes select only the button rule candidate"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorCandidates), uint64(2));
	TestEqual(TEXT("Two style passes evaluate only the selected candidate"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorEvaluations), uint64(2));
	TestEqual(TEXT("The button selector matches once per style pass"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorMatches), uint64(2));
	TestEqual(TEXT("Layout builds one Yoga node per runtime node"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(3));
	TestEqual(TEXT("The text cache is built once"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutBuilds), uint64(1));
	TestEqual(TEXT("Text layout is computed for measure and paint"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(2));
	TestEqual(TEXT("Initial cache setup builds body and button brushes once"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::BrushBuilds), uint64(2));
	TestEqual(TEXT("Direct binding avoids global cache rebuild allocations"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocations), uint64(15));
	TestEqual(TEXT("The direct binding reads one root field"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::BindingFieldsRead), uint64(1));
	TestEqual(TEXT("The direct binding executes one compiled op"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::BindingOpsExecuted), uint64(1));
	TestEqual(TEXT("The direct binding updates one text node"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::BindingNodesUpdated), uint64(1));
	TestEqual(TEXT("Runtime state and render data each have a known owned payload"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadEvents), uint64(2));
	TestEqual(TEXT("The separated runtime payloads are sized for every hydrated node"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes),
		uint64(3 * (sizeof(FWebToUERuntimeNodeState) + sizeof(FWebToUERuntimeRenderData))));
	TestTrue(TEXT("The log snapshot exposes stable workload fields"),
		Snapshot.ToLogString().Contains(TEXT("workload={hydrated_nodes=3")));
	TestEqual(TEXT("The Display rebuild produces one compositing plan"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::CompositingPlanBuilds),
		uint64(1));
	TestEqual(TEXT("The plain fixture classifies all three nodes as Tier 0"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::CompositingTier0Decisions),
		uint64(3));
	TestEqual(TEXT("The View cache owns one logical entry per planned node"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::CompositingCacheAllocated),
		uint64(3));
	TestEqual(TEXT("A visible paint records one compositing redraw and pass"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::CompositingRedraws), uint64(1));
	TestEqual(TEXT("A visible paint records one active compositing pass"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::CompositingPasses), uint64(1));

	TMap<FString, double> TelemetryMeasurements;
	Snapshot.ForEachTelemetryMeasurement(
		[&](const TCHAR* Name, double Value)
		{
			TestFalse(*FString::Printf(TEXT("Telemetry measurement %s is unique"), Name),
				TelemetryMeasurements.Contains(Name));
			TelemetryMeasurements.Add(Name, Value);
		});
	TestEqual(TEXT("The telemetry schema has the expected version"),
		FWebToUEPerformanceSnapshot::TelemetrySchemaVersion, 15);
	TestEqual(TEXT("The telemetry schema exposes every phase field and workload counter"),
		TelemetryMeasurements.Num(), FWebToUEPerformanceSnapshot::TelemetryMeasurementCount);
	static constexpr const TCHAR* ExpectedTelemetryNames[] = {
		TEXT("phase.hydrate.calls"),
		TEXT("phase.hydrate.inclusive_ms"),
		TEXT("phase.style.calls"),
		TEXT("phase.style.inclusive_ms"),
		TEXT("phase.measure.calls"),
		TEXT("phase.measure.inclusive_ms"),
		TEXT("phase.layout.calls"),
		TEXT("phase.layout.inclusive_ms"),
		TEXT("phase.paint_build.calls"),
		TEXT("phase.paint_build.inclusive_ms"),
		TEXT("phase.hit_test.calls"),
		TEXT("phase.hit_test.inclusive_ms"),
		TEXT("phase.binding.calls"),
		TEXT("phase.binding.inclusive_ms"),
		TEXT("workload.hydrated_nodes"),
		TEXT("workload.hydrated_rules"),
		TEXT("workload.style_node_visits"),
		TEXT("workload.selector_candidates"),
		TEXT("workload.selector_evaluations"),
		TEXT("workload.selector_matches"),
		TEXT("workload.yoga_nodes_built"),
		TEXT("workload.text_layout_builds"),
		TEXT("workload.text_layout_computes"),
		TEXT("workload.brush_builds"),
		TEXT("workload.tracked_allocations"),
		TEXT("workload.tracked_allocation_payload_events"),
		TEXT("workload.tracked_allocation_payload_bytes"),
		TEXT("workload.pseudo_state_nodes_changed"),
		TEXT("workload.pseudo_target_candidates"),
		TEXT("workload.style_dirty_targets"),
		TEXT("workload.style_property_changes"),
		TEXT("workload.text_cache_invalidations"),
		TEXT("workload.paint_order_cache_builds"),
		TEXT("workload.binding_fields_read"),
		TEXT("workload.binding_ops_executed"),
		TEXT("workload.binding_nodes_updated"),
		TEXT("workload.resource_load_attempts"),
		TEXT("workload.yoga_style_writes"),
		TEXT("workload.yoga_nodes_dirtied"),
		TEXT("workload.yoga_layout_results_changed"),
		TEXT("workload.resource_manifest_entries"),
		TEXT("workload.resource_async_requests"),
		TEXT("workload.resource_cache_hits"),
		TEXT("workload.resource_failures"),
		TEXT("workload.resource_cancellations"),
		TEXT("workload.resource_known_owned_bytes"),
		TEXT("workload.display_list_builds"),
		TEXT("workload.display_commands_built"),
		TEXT("workload.display_commands_patched"),
		TEXT("workload.display_commands_reused"),
		TEXT("workload.paint_commands_visited"),
		TEXT("workload.paint_draw_elements"),
		TEXT("workload.display_spatial_index_builds"),
		TEXT("workload.display_spatial_index_patches"),
		TEXT("workload.display_commands_spatially_indexed"),
		TEXT("workload.display_commands_rejected_hidden"),
		TEXT("workload.display_commands_rejected_unusable_bounds"),
		TEXT("workload.display_commands_rejected_inert"),
		TEXT("workload.dirty_rects_added"),
		TEXT("workload.paint_commands_culled"),
		TEXT("workload.hit_test_candidates"),
		TEXT("workload.hit_test_commands_visited"),
		TEXT("workload.paint_batch_runs"),
		TEXT("workload.paint_commands_layer_merged"),
		TEXT("workload.material_parameter_lookups"),
		TEXT("workload.material_parameter_evaluations"),
		TEXT("workload.material_instances_created"),
		TEXT("workload.material_instances_reused"),
		TEXT("workload.material_instances_released"),
		TEXT("workload.material_brush_patches"),
		TEXT("workload.visual_transform_commands_resolved"),
		TEXT("workload.clip_chain_zones_resolved"),
		TEXT("workload.inverse_hit_tests"),
		TEXT("workload.exact_clip_tests"),
		TEXT("workload.compositing_plan_builds"),
		TEXT("workload.compositing_tier_decisions"),
		TEXT("workload.compositing_tier_0_decisions"),
		TEXT("workload.compositing_tier_1_decisions"),
		TEXT("workload.compositing_tier_2_decisions"),
		TEXT("workload.compositing_tier_3_decisions"),
		TEXT("workload.compositing_plan_rejections"),
		TEXT("workload.compositing_cache_allocated"),
		TEXT("workload.compositing_cache_reused"),
		TEXT("workload.compositing_cache_released"),
		TEXT("workload.compositing_cache_evicted"),
		TEXT("workload.compositing_active_layers"),
		TEXT("workload.compositing_active_surfaces"),
		TEXT("workload.compositing_allocated_pixels"),
		TEXT("workload.compositing_allocated_bytes"),
		TEXT("workload.compositing_redraws"),
		TEXT("workload.compositing_passes"),
		TEXT("workload.compositing_commands")
	};
	static_assert(UE_ARRAY_COUNT(ExpectedTelemetryNames) ==
		FWebToUEPerformanceSnapshot::TelemetryMeasurementCount);
	for (const TCHAR* ExpectedName : ExpectedTelemetryNames)
	{
		TestTrue(*FString::Printf(TEXT("Telemetry exposes stable field %s"), ExpectedName),
			TelemetryMeasurements.Contains(ExpectedName));
	}
	TestEqual(TEXT("Telemetry exposes hydrate call count"),
		TelemetryMeasurements.FindRef(TEXT("phase.hydrate.calls")), 1.0);
	TestTrue(TEXT("Telemetry exposes hydrate inclusive time"),
		TelemetryMeasurements.FindRef(TEXT("phase.hydrate.inclusive_ms")) > 0.0);
	TestEqual(TEXT("Telemetry exposes selector candidates"),
		TelemetryMeasurements.FindRef(TEXT("workload.selector_candidates")), 2.0);
	TestEqual(TEXT("Telemetry exposes selector evaluations"),
		TelemetryMeasurements.FindRef(TEXT("workload.selector_evaluations")), 2.0);
	TestEqual(TEXT("Telemetry exposes tracked allocation events"),
		TelemetryMeasurements.FindRef(TEXT("workload.tracked_allocations")), 15.0);
	TestEqual(TEXT("Telemetry exposes tracked allocation payload events"),
		TelemetryMeasurements.FindRef(TEXT("workload.tracked_allocation_payload_events")), 2.0);
	TestEqual(TEXT("Telemetry exposes tracked allocation payload bytes"),
		TelemetryMeasurements.FindRef(TEXT("workload.tracked_allocation_payload_bytes")),
		static_cast<double>(Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes)));

	FWebToUEPerformanceCapture EmptyCapture;
	const FWebToUEPerformanceSnapshot EmptySnapshot = EmptyCapture.GetSnapshot();
	for (int32 Index = 0; Index < FWebToUEPerformanceSnapshot::PhaseCount; ++Index)
	{
		const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(Index);
		TestEqual(*FString::Printf(TEXT("A new capture isolates %s"), LexToString(Phase)),
			EmptySnapshot.Get(Phase).CallCount, uint64(0));
	}
	for (int32 Index = 0; Index < FWebToUEPerformanceSnapshot::CounterCount; ++Index)
	{
		const EWebToUEPerformanceCounter Counter = static_cast<EWebToUEPerformanceCounter>(Index);
		TestEqual(*FString::Printf(TEXT("A new capture isolates %s"), LexToString(Counter)),
			EmptySnapshot.GetCounter(Counter), uint64(0));
	}
	int32 EmptyTelemetryMeasurements = 0;
	EmptySnapshot.ForEachTelemetryMeasurement(
		[&](const TCHAR* Name, double Value)
		{
			TestEqual(*FString::Printf(TEXT("An empty snapshot reports zero for %s"), Name), Value, 0.0);
			++EmptyTelemetryMeasurements;
		});
	TestEqual(TEXT("An empty snapshot preserves the telemetry schema"), EmptyTelemetryMeasurements,
		FWebToUEPerformanceSnapshot::TelemetryMeasurementCount);

	FWebToUEPerformanceSnapshot OuterSnapshot;
	FWebToUEPerformanceSnapshot InnerSnapshot;
	{
		FWebToUEPerformanceCapture OuterCapture;
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::StyleNodeVisits, 1);
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		{
			FWebToUEPerformanceCapture InnerCapture;
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::StyleNodeVisits, 2);
			FWebToUEPerformanceCapture::RecordAllocationPayload(64);
			InnerSnapshot = InnerCapture.GetSnapshot();
		}
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::StyleNodeVisits, 4);
		FWebToUEPerformanceCapture::RecordAllocationPayload(32);
		OuterSnapshot = OuterCapture.GetSnapshot();
	}
	TestEqual(TEXT("The inner capture records only inner work"),
		InnerSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(2));
	TestEqual(TEXT("The outer capture excludes inner work and resumes afterward"),
		OuterSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(5));
	TestEqual(TEXT("The inner capture owns its known allocation event"),
		InnerSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocations), uint64(1));
	TestEqual(TEXT("The inner capture owns its known allocation payload bytes"),
		InnerSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes), uint64(64));
	TestEqual(TEXT("The outer capture counts one unknown and one known allocation event"),
		OuterSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocations), uint64(2));
	TestEqual(TEXT("The outer capture counts only its known allocation payload event"),
		OuterSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadEvents), uint64(1));
	TestEqual(TEXT("The outer capture excludes inner allocation payload bytes"),
		OuterSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes), uint64(32));
	return true;
}

#endif
