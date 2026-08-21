#include "WebToUEPerformance.h"

#include "HAL/PlatformTime.h"

DEFINE_STAT(STAT_WebToUE_Hydrate);
DEFINE_STAT(STAT_WebToUE_Style);
DEFINE_STAT(STAT_WebToUE_Measure);
DEFINE_STAT(STAT_WebToUE_Layout);
DEFINE_STAT(STAT_WebToUE_PaintBuild);
DEFINE_STAT(STAT_WebToUE_HitTest);
DEFINE_STAT(STAT_WebToUE_Binding);

namespace WebToUE::Performance::Private
{
	static thread_local FWebToUEPerformanceCapture* ActiveCapture = nullptr;

	struct FPhaseTelemetryNames
	{
		const TCHAR* Calls;
		const TCHAR* InclusiveMilliseconds;
	};

	static constexpr FPhaseTelemetryNames PhaseTelemetryNames[] = {
		{ TEXT("phase.hydrate.calls"), TEXT("phase.hydrate.inclusive_ms") },
		{ TEXT("phase.style.calls"), TEXT("phase.style.inclusive_ms") },
		{ TEXT("phase.measure.calls"), TEXT("phase.measure.inclusive_ms") },
		{ TEXT("phase.layout.calls"), TEXT("phase.layout.inclusive_ms") },
		{ TEXT("phase.paint_build.calls"), TEXT("phase.paint_build.inclusive_ms") },
		{ TEXT("phase.hit_test.calls"), TEXT("phase.hit_test.inclusive_ms") },
		{ TEXT("phase.binding.calls"), TEXT("phase.binding.inclusive_ms") }
	};

	static constexpr const TCHAR* CounterTelemetryNames[] = {
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
		TEXT("workload.exact_clip_tests")
	};

	static_assert(UE_ARRAY_COUNT(PhaseTelemetryNames) == FWebToUEPerformanceSnapshot::PhaseCount);
	static_assert(UE_ARRAY_COUNT(CounterTelemetryNames) == FWebToUEPerformanceSnapshot::CounterCount);

	static int32 ToIndex(EWebToUEPerformancePhase Phase)
	{
		const int32 Index = static_cast<int32>(Phase);
		check(Index >= 0 && Index < FWebToUEPerformanceSnapshot::PhaseCount);
		return Index;
	}

	static int32 ToIndex(EWebToUEPerformanceCounter Counter)
	{
		const int32 Index = static_cast<int32>(Counter);
		check(Index >= 0 && Index < FWebToUEPerformanceSnapshot::CounterCount);
		return Index;
	}
}

double FWebToUEPerformanceMetric::GetInclusiveMilliseconds() const
{
	return FPlatformTime::ToMilliseconds64(InclusiveCycles);
}

const FWebToUEPerformanceMetric& FWebToUEPerformanceSnapshot::Get(EWebToUEPerformancePhase Phase) const
{
	return Metrics[WebToUE::Performance::Private::ToIndex(Phase)];
}

uint64 FWebToUEPerformanceSnapshot::GetCounter(EWebToUEPerformanceCounter Counter) const
{
	return Counters[WebToUE::Performance::Private::ToIndex(Counter)];
}

void FWebToUEPerformanceSnapshot::ForEachTelemetryMeasurement(
	TFunctionRef<void(const TCHAR* Name, double Value)> Visitor) const
{
	for (int32 Index = 0; Index < PhaseCount; ++Index)
	{
		const FWebToUEPerformanceMetric& Metric = Metrics[Index];
		const WebToUE::Performance::Private::FPhaseTelemetryNames& Names =
			WebToUE::Performance::Private::PhaseTelemetryNames[Index];
		Visitor(Names.Calls, static_cast<double>(Metric.CallCount));
		Visitor(Names.InclusiveMilliseconds, Metric.GetInclusiveMilliseconds());
	}

	for (int32 Index = 0; Index < CounterCount; ++Index)
	{
		Visitor(WebToUE::Performance::Private::CounterTelemetryNames[Index],
			static_cast<double>(Counters[Index]));
	}
}

FString FWebToUEPerformanceSnapshot::ToLogString() const
{
	FString Result;
	for (int32 Index = 0; Index < PhaseCount; ++Index)
	{
		const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(Index);
		const FWebToUEPerformanceMetric& Metric = Metrics[Index];
		if (!Result.IsEmpty())
		{
			Result += TEXT(", ");
		}
		Result += FString::Printf(TEXT("%s={calls=%llu,inclusive_ms=%.6f}"), LexToString(Phase),
			static_cast<unsigned long long>(Metric.CallCount), Metric.GetInclusiveMilliseconds());
	}
	Result += TEXT("; workload={");
	for (int32 Index = 0; Index < CounterCount; ++Index)
	{
		if (Index > 0)
		{
			Result += TEXT(",");
		}
		const EWebToUEPerformanceCounter Counter = static_cast<EWebToUEPerformanceCounter>(Index);
		Result += FString::Printf(TEXT("%s=%llu"), LexToString(Counter),
			static_cast<unsigned long long>(Counters[Index]));
	}
	Result += TEXT("}");
	return Result;
}

FWebToUEPerformanceCapture::FWebToUEPerformanceCapture()
{
	PreviousCapture = WebToUE::Performance::Private::ActiveCapture;
	WebToUE::Performance::Private::ActiveCapture = this;
}

FWebToUEPerformanceCapture::~FWebToUEPerformanceCapture()
{
	check(WebToUE::Performance::Private::ActiveCapture == this);
	WebToUE::Performance::Private::ActiveCapture = PreviousCapture;
}

void FWebToUEPerformanceCapture::Reset()
{
	Snapshot = FWebToUEPerformanceSnapshot();
}

FWebToUEPerformanceSnapshot FWebToUEPerformanceCapture::GetSnapshot() const
{
	return Snapshot;
}

FWebToUEPerformanceCapture* FWebToUEPerformanceCapture::GetActiveCapture()
{
	return WebToUE::Performance::Private::ActiveCapture;
}

void FWebToUEPerformanceCapture::Record(EWebToUEPerformancePhase Phase, uint64 InclusiveCycles)
{
	FWebToUEPerformanceMetric& Metric = Snapshot.Metrics[WebToUE::Performance::Private::ToIndex(Phase)];
	++Metric.CallCount;
	Metric.InclusiveCycles += InclusiveCycles;
}

void FWebToUEPerformanceCapture::AddCounter(EWebToUEPerformanceCounter Counter, uint64 Amount)
{
	Snapshot.Counters[WebToUE::Performance::Private::ToIndex(Counter)] += Amount;
}

void FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter Counter, uint64 Amount)
{
	if (FWebToUEPerformanceCapture* Capture = FWebToUEPerformanceCapture::GetActiveCapture())
	{
		Capture->AddCounter(Counter, Amount);
	}
}

void FWebToUEPerformanceCapture::RecordAllocationPayload(uint64 PayloadBytes)
{
	check(PayloadBytes > 0);
	if (FWebToUEPerformanceCapture* Capture = FWebToUEPerformanceCapture::GetActiveCapture())
	{
		Capture->AddCounter(EWebToUEPerformanceCounter::TrackedAllocations, 1);
		Capture->AddCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadEvents, 1);
		Capture->AddCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes, PayloadBytes);
	}
}

FWebToUEPerformanceScope::FWebToUEPerformanceScope(EWebToUEPerformancePhase InPhase)
	: Capture(FWebToUEPerformanceCapture::GetActiveCapture())
	, Phase(InPhase)
{
	if (Capture)
	{
		StartCycles = FPlatformTime::Cycles64();
	}
}

FWebToUEPerformanceScope::~FWebToUEPerformanceScope()
{
	if (Capture)
	{
		Capture->Record(Phase, FPlatformTime::Cycles64() - StartCycles);
	}
}

const TCHAR* LexToString(EWebToUEPerformancePhase Phase)
{
	switch (Phase)
	{
	case EWebToUEPerformancePhase::Hydrate: return TEXT("Hydrate");
	case EWebToUEPerformancePhase::Style: return TEXT("Style");
	case EWebToUEPerformancePhase::Measure: return TEXT("Measure");
	case EWebToUEPerformancePhase::Layout: return TEXT("Layout");
	case EWebToUEPerformancePhase::PaintBuild: return TEXT("PaintBuild");
	case EWebToUEPerformancePhase::HitTest: return TEXT("HitTest");
	case EWebToUEPerformancePhase::Binding: return TEXT("Binding");
	default: return TEXT("Unknown");
	}
}

const TCHAR* LexToString(EWebToUEPerformanceCounter Counter)
{
	switch (Counter)
	{
	case EWebToUEPerformanceCounter::HydratedNodes: return TEXT("hydrated_nodes");
	case EWebToUEPerformanceCounter::HydratedRules: return TEXT("hydrated_rules");
	case EWebToUEPerformanceCounter::StyleNodeVisits: return TEXT("style_node_visits");
	case EWebToUEPerformanceCounter::SelectorCandidates: return TEXT("selector_candidates");
	case EWebToUEPerformanceCounter::SelectorEvaluations: return TEXT("selector_evaluations");
	case EWebToUEPerformanceCounter::SelectorMatches: return TEXT("selector_matches");
	case EWebToUEPerformanceCounter::YogaNodesBuilt: return TEXT("yoga_nodes_built");
	case EWebToUEPerformanceCounter::TextLayoutBuilds: return TEXT("text_layout_builds");
	case EWebToUEPerformanceCounter::TextLayoutComputes: return TEXT("text_layout_computes");
	case EWebToUEPerformanceCounter::BrushBuilds: return TEXT("brush_builds");
	case EWebToUEPerformanceCounter::TrackedAllocations: return TEXT("tracked_allocations");
	case EWebToUEPerformanceCounter::TrackedAllocationPayloadEvents: return TEXT("tracked_allocation_payload_events");
	case EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes: return TEXT("tracked_allocation_payload_bytes");
	case EWebToUEPerformanceCounter::PseudoStateNodesChanged: return TEXT("pseudo_state_nodes_changed");
	case EWebToUEPerformanceCounter::PseudoTargetCandidates: return TEXT("pseudo_target_candidates");
	case EWebToUEPerformanceCounter::StyleDirtyTargets: return TEXT("style_dirty_targets");
	case EWebToUEPerformanceCounter::StylePropertyChanges: return TEXT("style_property_changes");
	case EWebToUEPerformanceCounter::TextCacheInvalidations: return TEXT("text_cache_invalidations");
	case EWebToUEPerformanceCounter::PaintOrderCacheBuilds: return TEXT("paint_order_cache_builds");
	case EWebToUEPerformanceCounter::BindingFieldsRead: return TEXT("binding_fields_read");
	case EWebToUEPerformanceCounter::BindingOpsExecuted: return TEXT("binding_ops_executed");
	case EWebToUEPerformanceCounter::BindingNodesUpdated: return TEXT("binding_nodes_updated");
	case EWebToUEPerformanceCounter::ResourceLoadAttempts: return TEXT("resource_load_attempts");
	case EWebToUEPerformanceCounter::YogaStyleWrites: return TEXT("yoga_style_writes");
	case EWebToUEPerformanceCounter::YogaNodesDirtied: return TEXT("yoga_nodes_dirtied");
	case EWebToUEPerformanceCounter::YogaLayoutResultsChanged: return TEXT("yoga_layout_results_changed");
	case EWebToUEPerformanceCounter::ResourceManifestEntries: return TEXT("resource_manifest_entries");
	case EWebToUEPerformanceCounter::ResourceAsyncRequests: return TEXT("resource_async_requests");
	case EWebToUEPerformanceCounter::ResourceCacheHits: return TEXT("resource_cache_hits");
	case EWebToUEPerformanceCounter::ResourceFailures: return TEXT("resource_failures");
	case EWebToUEPerformanceCounter::ResourceCancellations: return TEXT("resource_cancellations");
	case EWebToUEPerformanceCounter::ResourceKnownOwnedBytes: return TEXT("resource_known_owned_bytes");
	case EWebToUEPerformanceCounter::DisplayListBuilds: return TEXT("display_list_builds");
	case EWebToUEPerformanceCounter::DisplayCommandsBuilt: return TEXT("display_commands_built");
	case EWebToUEPerformanceCounter::DisplayCommandsPatched: return TEXT("display_commands_patched");
	case EWebToUEPerformanceCounter::DisplayCommandsReused: return TEXT("display_commands_reused");
	case EWebToUEPerformanceCounter::PaintCommandsVisited: return TEXT("paint_commands_visited");
	case EWebToUEPerformanceCounter::PaintDrawElements: return TEXT("paint_draw_elements");
	case EWebToUEPerformanceCounter::DisplaySpatialIndexBuilds: return TEXT("display_spatial_index_builds");
	case EWebToUEPerformanceCounter::DisplaySpatialIndexPatches: return TEXT("display_spatial_index_patches");
	case EWebToUEPerformanceCounter::DisplayCommandsSpatiallyIndexed: return TEXT("display_commands_spatially_indexed");
	case EWebToUEPerformanceCounter::DisplayCommandsRejectedHidden: return TEXT("display_commands_rejected_hidden");
	case EWebToUEPerformanceCounter::DisplayCommandsRejectedUnusableBounds: return TEXT("display_commands_rejected_unusable_bounds");
	case EWebToUEPerformanceCounter::DisplayCommandsRejectedInert: return TEXT("display_commands_rejected_inert");
	case EWebToUEPerformanceCounter::DirtyRectsAdded: return TEXT("dirty_rects_added");
	case EWebToUEPerformanceCounter::PaintCommandsCulled: return TEXT("paint_commands_culled");
	case EWebToUEPerformanceCounter::HitTestCandidates: return TEXT("hit_test_candidates");
	case EWebToUEPerformanceCounter::HitTestCommandsVisited: return TEXT("hit_test_commands_visited");
	case EWebToUEPerformanceCounter::PaintBatchRuns: return TEXT("paint_batch_runs");
	case EWebToUEPerformanceCounter::PaintCommandsLayerMerged: return TEXT("paint_commands_layer_merged");
	case EWebToUEPerformanceCounter::MaterialParameterLookups: return TEXT("material_parameter_lookups");
	case EWebToUEPerformanceCounter::MaterialParameterEvaluations: return TEXT("material_parameter_evaluations");
	case EWebToUEPerformanceCounter::MaterialInstancesCreated: return TEXT("material_instances_created");
	case EWebToUEPerformanceCounter::MaterialInstancesReused: return TEXT("material_instances_reused");
	case EWebToUEPerformanceCounter::MaterialInstancesReleased: return TEXT("material_instances_released");
	case EWebToUEPerformanceCounter::MaterialBrushPatches: return TEXT("material_brush_patches");
	case EWebToUEPerformanceCounter::VisualTransformCommandsResolved: return TEXT("visual_transform_commands_resolved");
	case EWebToUEPerformanceCounter::ClipChainZonesResolved: return TEXT("clip_chain_zones_resolved");
	case EWebToUEPerformanceCounter::InverseHitTests: return TEXT("inverse_hit_tests");
	case EWebToUEPerformanceCounter::ExactClipTests: return TEXT("exact_clip_tests");
	default: return TEXT("unknown");
	}
}
