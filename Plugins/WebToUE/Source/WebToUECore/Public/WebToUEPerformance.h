#pragma once

#include "Containers/StaticArray.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Templates/Function.h"

DECLARE_STATS_GROUP(TEXT("WebToUE"), STATGROUP_WebToUE, STATCAT_Advanced);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Hydrate"), STAT_WebToUE_Hydrate, STATGROUP_WebToUE, WEBTOUECORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Style"), STAT_WebToUE_Style, STATGROUP_WebToUE, WEBTOUECORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Measure"), STAT_WebToUE_Measure, STATGROUP_WebToUE, WEBTOUECORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Layout"), STAT_WebToUE_Layout, STATGROUP_WebToUE, WEBTOUECORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Paint Build"), STAT_WebToUE_PaintBuild, STATGROUP_WebToUE, WEBTOUECORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Hit Test"), STAT_WebToUE_HitTest, STATGROUP_WebToUE, WEBTOUECORE_API);
DECLARE_CYCLE_STAT_EXTERN(TEXT("Binding"), STAT_WebToUE_Binding, STATGROUP_WebToUE, WEBTOUECORE_API);

enum class EWebToUEPerformancePhase : uint8
{
	Hydrate,
	Style,
	Measure,
	Layout,
	PaintBuild,
	HitTest,
	Binding,
	Count
};

enum class EWebToUEPerformanceCounter : uint8
{
	HydratedNodes,
	HydratedRules,
	StyleNodeVisits,
	SelectorEvaluations,
	SelectorMatches,
	YogaNodesBuilt,
	TextLayoutBuilds,
	TextLayoutComputes,
	BrushBuilds,
	// WebToUE source-level allocation events explicitly marked at owned hot-path call sites.
	// This is intentionally not a process-wide allocator or Slate/Yoga-internal allocation total.
	TrackedAllocations,
	Count
};

struct WEBTOUECORE_API FWebToUEPerformanceMetric
{
	uint64 CallCount = 0;
	uint64 InclusiveCycles = 0;

	double GetInclusiveMilliseconds() const;
};

struct WEBTOUECORE_API FWebToUEPerformanceSnapshot
{
	static constexpr int32 PhaseCount = static_cast<int32>(EWebToUEPerformancePhase::Count);
	static constexpr int32 CounterCount = static_cast<int32>(EWebToUEPerformanceCounter::Count);
	static constexpr int32 TelemetrySchemaVersion = 1;
	static constexpr int32 TelemetryMeasurementCount = (PhaseCount * 2) + CounterCount;
	TStaticArray<FWebToUEPerformanceMetric, PhaseCount> Metrics;
	TStaticArray<uint64, CounterCount> Counters{};

	const FWebToUEPerformanceMetric& Get(EWebToUEPerformancePhase Phase) const;
	uint64 GetCounter(EWebToUEPerformanceCounter Counter) const;
	void ForEachTelemetryMeasurement(TFunctionRef<void(const TCHAR* Name, double Value)> Visitor) const;
	FString ToLogString() const;
};

class FWebToUEPerformanceScope;

class WEBTOUECORE_API FWebToUEPerformanceCapture
{
public:
	FWebToUEPerformanceCapture();
	~FWebToUEPerformanceCapture();

	FWebToUEPerformanceCapture(const FWebToUEPerformanceCapture&) = delete;
	FWebToUEPerformanceCapture& operator=(const FWebToUEPerformanceCapture&) = delete;

	void Reset();
	FWebToUEPerformanceSnapshot GetSnapshot() const;
	static void RecordCounter(EWebToUEPerformanceCounter Counter, uint64 Amount = 1);

private:
	friend class FWebToUEPerformanceScope;

	FWebToUEPerformanceCapture* PreviousCapture = nullptr;
	FWebToUEPerformanceSnapshot Snapshot;

	static FWebToUEPerformanceCapture* GetActiveCapture();
	void Record(EWebToUEPerformancePhase Phase, uint64 InclusiveCycles);
	void AddCounter(EWebToUEPerformanceCounter Counter, uint64 Amount);
};

class WEBTOUECORE_API FWebToUEPerformanceScope
{
public:
	explicit FWebToUEPerformanceScope(EWebToUEPerformancePhase InPhase);
	~FWebToUEPerformanceScope();

	FWebToUEPerformanceScope(const FWebToUEPerformanceScope&) = delete;
	FWebToUEPerformanceScope& operator=(const FWebToUEPerformanceScope&) = delete;

private:
	FWebToUEPerformanceCapture* Capture = nullptr;
	EWebToUEPerformancePhase Phase;
	uint64 StartCycles = 0;
};

WEBTOUECORE_API const TCHAR* LexToString(EWebToUEPerformancePhase Phase);
WEBTOUECORE_API const TCHAR* LexToString(EWebToUEPerformanceCounter Counter);
