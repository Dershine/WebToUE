#if WITH_DEV_AUTOMATION_TESTS

#include "Benchmarks/WebToUEBenchmarkPolicy.h"
#include "Benchmarks/WebToUEBenchmarkScenario.h"
#include "Misc/AutomationTest.h"
#include "WebToUECompiler.h"
#include "WebToUEPerformance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEBenchmarkStatisticsTest, "WebToUE.Editor.BenchmarkStatistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEBenchmarkScenarioTest, "WebToUE.Editor.BenchmarkScenarios",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::Benchmark::Tests
{
	static constexpr const TCHAR* PhaseTelemetryNames[] = {
		TEXT("hydrate"),
		TEXT("style"),
		TEXT("measure"),
		TEXT("layout"),
		TEXT("paint_build"),
		TEXT("hit_test"),
		TEXT("binding")
	};
	static_assert(UE_ARRAY_COUNT(PhaseTelemetryNames) == FWebToUEPerformanceSnapshot::PhaseCount);

	static FString MakeTelemetryContext(const FWebToUEBenchmarkEnvironment& Environment,
		const FWebToUEBenchmarkScenarioDefinition& Definition, const TCHAR* Kind, int32 SampleIndex = 0)
	{
		return FString::Printf(TEXT("benchmark_schema=%d;snapshot_schema=%d;scenario=%s;kind=%s;sample=%d;environment=%s"),
			FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
			FWebToUEPerformanceSnapshot::TelemetrySchemaVersion,
			*Definition.Name,
			Kind,
			SampleIndex,
			*Environment.GetFingerprint());
	}
}

bool FWebToUEBenchmarkStatisticsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("The benchmark policy schema is stable"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion, 1);
	TestEqual(TEXT("The standard policy performs one warmup"), FWebToUEBenchmarkSamplingPolicy::WarmupCount, 1);
	TestEqual(TEXT("The standard policy records twenty samples"), FWebToUEBenchmarkSamplingPolicy::SampleCount, 20);
	TestEqual(TEXT("The benchmark budget policy schema is stable"), FWebToUEBenchmarkBudgetPolicy::SchemaVersion, 6);
	TestEqual(TEXT("The medium single-node hover P95 target is explicit"),
		FWebToUEBenchmarkBudgetPolicy::MediumSingleNodeHoverP95Milliseconds, 0.5);
	TestEqual(TEXT("The medium single-FieldNotify P95 target is explicit"),
		FWebToUEBenchmarkBudgetPolicy::MediumSingleFieldNotifyP95Milliseconds, 0.5);
	TestEqual(TEXT("The medium warm full-layout P95 target is explicit"),
		FWebToUEBenchmarkBudgetPolicy::MediumWarmFullLayoutP95Milliseconds, 2.0);
	TestEqual(TEXT("The medium unchanged-paint allocation target is explicit"),
		FWebToUEBenchmarkBudgetPolicy::MediumUnchangedPaintMaximumTrackedAllocations, uint64(0));
	TestEqual(TEXT("The medium unchanged-paint allocation payload-byte target is explicit"),
		FWebToUEBenchmarkBudgetPolicy::MediumUnchangedPaintMaximumTrackedAllocationPayloadBytes, uint64(0));
	TestFalse(TEXT("The medium single-node hover target remains observational until it meets budget"),
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumSingleNodeHoverBudget);
	TestFalse(TEXT("The medium single-FieldNotify target remains observational until it meets budget"),
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumSingleFieldNotifyBudget);
	TestTrue(TEXT("The medium warm full-layout target is an enforced regression gate"),
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumWarmFullLayoutBudget);
	TestTrue(TEXT("The medium unchanged-paint zero-allocation target is an enforced regression gate"),
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumUnchangedPaintBudget);
	TestEqual(TEXT("The percentile method is explicit"),
		FString(FWebToUEBenchmarkSamplingPolicy::GetPercentileMethod()),
		FString(TEXT("p50=median;p95=nearest-rank")));

	TArray<double> Samples;
	for (int32 Value = FWebToUEBenchmarkSamplingPolicy::SampleCount; Value >= 1; --Value)
	{
		Samples.Add(static_cast<double>(Value));
	}
	FWebToUEBenchmarkDistribution Distribution;
	TestTrue(TEXT("A non-empty finite sample set produces a distribution"),
		FWebToUEBenchmarkStatistics::TryCalculate(Samples, Distribution));
	TestEqual(TEXT("The minimum is the first sorted sample"), Distribution.Minimum, 1.0);
	TestEqual(TEXT("P50 is the conventional even-sample median"), Distribution.P50, 10.5);
	TestEqual(TEXT("Nearest-rank P95 for twenty samples is rank nineteen"), Distribution.P95, 19.0);
	TestEqual(TEXT("The maximum is the last sorted sample"), Distribution.Maximum, 20.0);

	const TArray<double> EmptySamples;
	TestFalse(TEXT("An empty sample set is rejected"),
		FWebToUEBenchmarkStatistics::TryCalculate(EmptySamples, Distribution));
	const TArray<double> NegativeSamples = { 1.0, -1.0 };
	TestFalse(TEXT("A negative duration sample is rejected"),
		FWebToUEBenchmarkStatistics::TryCalculate(NegativeSamples, Distribution));

	const FWebToUEBenchmarkEnvironment Environment = FWebToUEBenchmarkEnvironment::Capture();
	AddInfo(TEXT("Benchmark policy: ") + FWebToUEBenchmarkSamplingPolicy::ToLogString());
	AddInfo(TEXT("Benchmark environment: ") + Environment.ToLogString());
	TestTrue(TEXT("The standard benchmark runs in Win64 Editor Development"),
		Environment.IsStandardConfiguration());
	TestEqual(TEXT("The non-sensitive environment fingerprint is eight hexadecimal characters"),
		Environment.GetFingerprint().Len(), 8);
	TestFalse(TEXT("The CPU identity is available"), Environment.CPUBrand.IsEmpty());
	TestFalse(TEXT("The GPU identity is available"), Environment.GPUBrand.IsEmpty());
	TestTrue(TEXT("The physical core count is available"), Environment.PhysicalCoreCount > 0);
	TestTrue(TEXT("The logical core count is not lower than the physical core count"),
		Environment.LogicalCoreCount >= Environment.PhysicalCoreCount);
	TestTrue(TEXT("The physical memory size is available"), Environment.PhysicalMemoryGB > 0);
	return true;
}

bool FWebToUEBenchmarkScenarioTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Benchmark::Tests;

	SetTelemetryStorage(TEXT("WebToUEPerformance"));
	const FWebToUEBenchmarkEnvironment Environment = FWebToUEBenchmarkEnvironment::Capture();
	AddInfo(TEXT("Benchmark policy: ") + FWebToUEBenchmarkSamplingPolicy::ToLogString());
	AddInfo(TEXT("Benchmark environment: ") + Environment.ToLogString());
	TestTrue(TEXT("The standard benchmark runs in Win64 Editor Development"),
		Environment.IsStandardConfiguration());

	const TArray<FWebToUEBenchmarkScenarioDefinition>& Definitions =
		FWebToUEBenchmarkScenarioGenerator::GetStandardDefinitions();
	TestEqual(TEXT("The benchmark corpus has three standard scales"), Definitions.Num(), 3);

	const int32 ExpectedNodeCounts[] = { 100, 500, 2000 };
	const int32 ExpectedRuleCounts[] = { 50, 200, 500 };
	for (int32 ScenarioIndex = 0; ScenarioIndex < Definitions.Num(); ++ScenarioIndex)
	{
		const FWebToUEBenchmarkScenario First =
			FWebToUEBenchmarkScenarioGenerator::Generate(Definitions[ScenarioIndex]);
		const FWebToUEBenchmarkScenario Second =
			FWebToUEBenchmarkScenarioGenerator::Generate(Definitions[ScenarioIndex]);

		const FString Prefix = FString::Printf(TEXT("Scenario %s: "), *Definitions[ScenarioIndex].Name);
		TestEqual(*(Prefix + TEXT("declares the expected node count")),
			First.Definition.NodeCount, ExpectedNodeCounts[ScenarioIndex]);
		TestEqual(*(Prefix + TEXT("declares the expected rule count")),
			First.Definition.RuleCount, ExpectedRuleCounts[ScenarioIndex]);
		TestEqual(*(Prefix + TEXT("HTML generation is deterministic")), First.Html, Second.Html);
		TestEqual(*(Prefix + TEXT("CSS generation is deterministic")), First.Css, Second.Css);

		for (int32 WarmupIndex = 0; WarmupIndex < FWebToUEBenchmarkSamplingPolicy::WarmupCount; ++WarmupIndex)
		{
			const TSharedRef<FWebToUEDocument> WarmupDocument = FWebToUECompiler::Compile(
				First.Html, First.Css, First.Definition.Name + TEXT(".html"));
			TestFalse(*(Prefix + FString::Printf(TEXT("warmup %d compiles without errors"), WarmupIndex + 1)),
				WarmupDocument->HasErrors());
		}

		TArray<FWebToUEPerformanceSnapshot> PerformanceSamples;
		PerformanceSamples.Reserve(FWebToUEBenchmarkSamplingPolicy::SampleCount);
		TSharedPtr<FWebToUEDocument> RepresentativeDocument;
		for (int32 SampleIndex = 0; SampleIndex < FWebToUEBenchmarkSamplingPolicy::SampleCount; ++SampleIndex)
		{
			FWebToUEPerformanceSnapshot PerformanceSnapshot;
			const TSharedRef<FWebToUEDocument> Document = [&]()
			{
				FWebToUEPerformanceCapture Capture;
				const TSharedRef<FWebToUEDocument> Compiled = FWebToUECompiler::Compile(
					First.Html, First.Css, First.Definition.Name + TEXT(".html"));
				PerformanceSnapshot = Capture.GetSnapshot();
				return Compiled;
			}();
			PerformanceSamples.Add(PerformanceSnapshot);
			if (!RepresentativeDocument.IsValid())
			{
				RepresentativeDocument = Document;
			}

			const FString SamplePrefix = Prefix + FString::Printf(TEXT("sample %d: "), SampleIndex + 1);
			TestFalse(*(SamplePrefix + TEXT("compiles without errors")), Document->HasErrors());
			TestEqual(*(SamplePrefix + TEXT("visits every style node exactly once")),
				PerformanceSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits),
				static_cast<uint64>(First.Definition.NodeCount));
			TestEqual(*(SamplePrefix + TEXT("evaluates every rule for every node")),
				PerformanceSnapshot.GetCounter(EWebToUEPerformanceCounter::SelectorEvaluations),
				static_cast<uint64>(First.Definition.NodeCount) * static_cast<uint64>(First.Definition.RuleCount));
			TestTrue(*(SamplePrefix + TEXT("records successful selector matches")),
				PerformanceSnapshot.GetCounter(EWebToUEPerformanceCounter::SelectorMatches) > 0);

			const FString TelemetryContext = MakeTelemetryContext(Environment, Definitions[ScenarioIndex],
				TEXT("sample"), SampleIndex + 1);
			AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
				TelemetryContext);
			AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
				TelemetryContext);
			AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
				TelemetryContext);
			AddTelemetryData(TEXT("sample.index"), SampleIndex + 1, TelemetryContext);
			AddTelemetryData(TEXT("schema.version"), FWebToUEPerformanceSnapshot::TelemetrySchemaVersion,
				TelemetryContext);
			AddTelemetryData(TEXT("scenario.node_count"), First.Definition.NodeCount, TelemetryContext);
			AddTelemetryData(TEXT("scenario.rule_count"), First.Definition.RuleCount, TelemetryContext);
			int32 TelemetryMeasurementCount = 0;
			PerformanceSnapshot.ForEachTelemetryMeasurement(
				[&](const TCHAR* Name, double Value)
				{
					AddTelemetryData(Name, Value, TelemetryContext);
					++TelemetryMeasurementCount;
				});
			TestEqual(*(SamplePrefix + TEXT("emits every snapshot telemetry measurement")),
				TelemetryMeasurementCount, FWebToUEPerformanceSnapshot::TelemetryMeasurementCount);
		}
		TestEqual(*(Prefix + TEXT("records the policy sample count")),
			PerformanceSamples.Num(), FWebToUEBenchmarkSamplingPolicy::SampleCount);

		const FString SummaryContext = MakeTelemetryContext(Environment, Definitions[ScenarioIndex], TEXT("summary"));
		AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
			SummaryContext);
		AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
			SummaryContext);
		AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
			SummaryContext);
		AddTelemetryData(TEXT("schema.version"), FWebToUEPerformanceSnapshot::TelemetrySchemaVersion,
			SummaryContext);
		AddTelemetryData(TEXT("scenario.node_count"), First.Definition.NodeCount, SummaryContext);
		AddTelemetryData(TEXT("scenario.rule_count"), First.Definition.RuleCount, SummaryContext);
		for (int32 PhaseIndex = 0; PhaseIndex < FWebToUEPerformanceSnapshot::PhaseCount; ++PhaseIndex)
		{
			const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(PhaseIndex);
			TArray<double> PhaseSamples;
			PhaseSamples.Reserve(PerformanceSamples.Num());
			for (const FWebToUEPerformanceSnapshot& Sample : PerformanceSamples)
			{
				PhaseSamples.Add(Sample.Get(Phase).GetInclusiveMilliseconds());
			}
			FWebToUEBenchmarkDistribution Distribution;
			TestTrue(*(Prefix + FString::Printf(TEXT("calculates the %s distribution"), PhaseTelemetryNames[PhaseIndex])),
				FWebToUEBenchmarkStatistics::TryCalculate(PhaseSamples, Distribution));
			const FString P50Name = FString::Printf(TEXT("phase.%s.p50_ms"), PhaseTelemetryNames[PhaseIndex]);
			const FString P95Name = FString::Printf(TEXT("phase.%s.p95_ms"), PhaseTelemetryNames[PhaseIndex]);
			AddTelemetryData(*P50Name, Distribution.P50, SummaryContext);
			AddTelemetryData(*P95Name, Distribution.P95, SummaryContext);
			AddInfo(Prefix + FString::Printf(TEXT("%s={p50_ms=%.6f,p95_ms=%.6f,min_ms=%.6f,max_ms=%.6f}"),
				PhaseTelemetryNames[PhaseIndex], Distribution.P50, Distribution.P95,
				Distribution.Minimum, Distribution.Maximum));
		}

		TestNotNull(*(Prefix + TEXT("retains a representative compiled document")), RepresentativeDocument.Get());
		if (!RepresentativeDocument.IsValid())
		{
			continue;
		}
		const TSharedRef<FWebToUEDocument> Document = RepresentativeDocument.ToSharedRef();
		TestEqual(*(Prefix + TEXT("compiles the exact rule count")),
			Document->Rules.Num(), First.Definition.RuleCount);

		int32 ActualNodeCount = 0;
		FWebToUENode* BindingTarget = nullptr;
		FWebToUENode* HoverTarget = nullptr;
		Document->ForEachNode([&](FWebToUENode& Node)
		{
			++ActualNodeCount;
			const FString Id = Node.GetAttribute(TEXT("id"));
			if (Id == TEXT("benchmark-binding-target"))
			{
				BindingTarget = &Node;
			}
			else if (Id == TEXT("benchmark-hover-target"))
			{
				HoverTarget = &Node;
			}
		});
		TestEqual(*(Prefix + TEXT("compiles the exact runtime node count")),
			ActualNodeCount, First.Definition.NodeCount);
		TestNotNull(*(Prefix + TEXT("contains the fixed binding target")), BindingTarget);
		if (BindingTarget)
		{
			TestEqual(*(Prefix + TEXT("binding target uses the fixed field")),
				BindingTarget->GetAttribute(TEXT("data-ue-bind-text")), FString(TEXT("BenchmarkLabel")));
		}
		TestNotNull(*(Prefix + TEXT("contains the fixed hover target")), HoverTarget);
		if (HoverTarget)
		{
			TestTrue(*(Prefix + TEXT("hover target is interactive")), HoverTarget->IsInteractive());
		}
	}

	return true;
}

#endif
