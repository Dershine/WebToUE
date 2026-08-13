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
		TEXT("workload.tracked_allocation_payload_bytes")
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
	default: return TEXT("unknown");
	}
}
