#if WITH_DEV_AUTOMATION_TESTS

#include "Benchmarks/WebToUEBenchmarkPolicy.h"
#include "Benchmarks/WebToUEBenchmarkScenario.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"
#include "WebToUEPerformance.h"
#include "WebToUEView.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "Widgets/SWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeHydrationBenchmarkTest,
	"WebToUE.Editor.RuntimeHydrationBenchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::Benchmark::Hydration::Tests
{
	static constexpr int32 AmplificationViewCount = 4;

	struct FHydrationSample
	{
		double InclusiveMilliseconds = 0.0;
		FWebToUEPerformanceSnapshot Snapshot;
		FWebToUERuntimeMemoryCensus Census;
	};

	static FString MakeTelemetryContext(const FWebToUEBenchmarkEnvironment& Environment,
		const FWebToUEBenchmarkScenarioDefinition& Definition, const TCHAR* Kind,
		int32 SampleIndex = 0)
	{
		return FString::Printf(
			TEXT("benchmark_schema=%d;snapshot_schema=%d;scenario=%s;kind=%s;sample=%d;environment=%s;memory_scope=editor_process_rss_and_known_view_owned"),
			FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
			FWebToUEPerformanceSnapshot::TelemetrySchemaVersion,
			*Definition.Name,
			Kind,
			SampleIndex,
			*Environment.GetFingerprint());
	}

	static void DestroyView(UWebToUEView* View, TSharedPtr<SWidget>& Widget)
	{
		Widget.Reset();
		if (View)
		{
			View->ReleaseSlateResources(true);
			View->RemoveFromRoot();
		}
	}

	static UWebToUEView* CreateEmptyView(TSharedPtr<SWidget>& OutWidget)
	{
		UWebToUEView* View = NewObject<UWebToUEView>(GetTransientPackage());
		View->AddToRoot();
		OutWidget = View->TakeWidget();
		return View;
	}

	static UWebToUEView* CreateHydratedView(
		UWebToUEDocument& Document, TSharedPtr<SWidget>& OutWidget)
	{
		UWebToUEView* View = CreateEmptyView(OutWidget);
		View->SetDocument(&Document);
		return View;
	}

	static int64 SignedDelta(uint64 After, uint64 Before)
	{
		return After >= Before
			? static_cast<int64>(After - Before)
			: -static_cast<int64>(Before - After);
	}
}

bool FWebToUERuntimeHydrationBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Benchmark::Hydration::Tests;

	SetTelemetryStorage(TEXT("WebToUEPerformance"));
	const FWebToUEBenchmarkEnvironment Environment = FWebToUEBenchmarkEnvironment::Capture();
	AddInfo(TEXT("Benchmark policy: ") + FWebToUEBenchmarkSamplingPolicy::ToLogString());
	AddInfo(TEXT("Benchmark environment: ") + Environment.ToLogString());
	AddInfo(TEXT("Memory scope: process RSS deltas are noisy Editor observations; known-owned bytes exclude allocator overhead, FText internals, shared-pointer control blocks, Slate/Yoga internals, and shared Compiled UI IR."));
	TestTrue(TEXT("The hydration benchmark runs in Win64 Editor Development"),
		Environment.IsStandardConfiguration());

	const TArray<FWebToUEBenchmarkScenarioDefinition>& Definitions =
		FWebToUEBenchmarkScenarioGenerator::GetHydrationDefinitions();
	TestEqual(TEXT("The hydration corpus has three scales"), Definitions.Num(), 3);
	const int32 ExpectedNodeCounts[] = { 500, 2000, 10000 };
	const int32 ExpectedRuleCounts[] = { 200, 500, 500 };
	if (Definitions.Num() != UE_ARRAY_COUNT(ExpectedNodeCounts))
	{
		return false;
	}

	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);

	for (int32 ScenarioIndex = 0; ScenarioIndex < Definitions.Num(); ++ScenarioIndex)
	{
		const FWebToUEBenchmarkScenario Scenario =
			FWebToUEBenchmarkScenarioGenerator::Generate(Definitions[ScenarioIndex]);
		const FString Prefix = FString::Printf(TEXT("Hydration scenario %s: "),
			*Scenario.Definition.Name);
		TestEqual(*(Prefix + TEXT("uses the expected node count")),
			Scenario.Definition.NodeCount, ExpectedNodeCounts[ScenarioIndex]);
		TestEqual(*(Prefix + TEXT("uses the expected rule count")),
			Scenario.Definition.RuleCount, ExpectedRuleCounts[ScenarioIndex]);

		const FString TestFilename = FPaths::Combine(TestDirectory,
			Scenario.Definition.Name + TEXT(".html"));
		ON_SCOPE_EXIT
		{
			IFileManager::Get().Delete(*TestFilename, false, true);
		};
		const FString Source = FString::Printf(TEXT("<style>%s</style>%s"),
			*Scenario.Css, *Scenario.Html);
		UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
		const bool bImported = FFileHelper::SaveStringToFile(Source, *TestFilename) &&
			UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, false);
		TestTrue(*(Prefix + TEXT("imports the deterministic source")), bImported);
		if (!bImported)
		{
			return false;
		}
		TestEqual(*(Prefix + TEXT("compiled node count is exact")),
			Document->GetCompiledNodes().Num(), Scenario.Definition.NodeCount);
		TestEqual(*(Prefix + TEXT("compiled rule count is exact")),
			Document->GetCompiledRules().Num(), Scenario.Definition.RuleCount);

		TArray<FHydrationSample> Samples;
		Samples.Reserve(FWebToUEBenchmarkSamplingPolicy::SampleCount);
		const int32 TotalIterations = FWebToUEBenchmarkSamplingPolicy::WarmupCount +
			FWebToUEBenchmarkSamplingPolicy::SampleCount;
		for (int32 Iteration = 0; Iteration < TotalIterations; ++Iteration)
		{
			FHydrationSample Sample;
			TSharedPtr<SWidget> Widget;
			UWebToUEView* View = CreateEmptyView(Widget);
			{
				FWebToUEPerformanceCapture Capture;
				const double StartSeconds = FPlatformTime::Seconds();
				View->SetDocument(Document);
				Sample.InclusiveMilliseconds =
					(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
				Sample.Snapshot = Capture.GetSnapshot();
			}
			const bool bHasCensus = View->GetRuntimeMemoryCensusForTesting(Sample.Census);
			TestTrue(*(Prefix + TEXT("produces a post-hydration memory census")), bHasCensus);
			DestroyView(View, Widget);
			if (!bHasCensus)
			{
				return false;
			}
			if (Iteration < FWebToUEBenchmarkSamplingPolicy::WarmupCount)
			{
				continue;
			}

			const int32 SampleIndex = Samples.Num() + 1;
			const FString SamplePrefix = Prefix +
				FString::Printf(TEXT("sample %d: "), SampleIndex);
			TestEqual(*(SamplePrefix + TEXT("records one Hydrate phase")),
				Sample.Snapshot.Get(EWebToUEPerformancePhase::Hydrate).CallCount, uint64(1));
			TestEqual(*(SamplePrefix + TEXT("hydrates every compiled node")),
				Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::HydratedNodes),
				static_cast<uint64>(Scenario.Definition.NodeCount));
			TestEqual(*(SamplePrefix + TEXT("hydrates every compiled rule")),
				Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::HydratedRules),
				static_cast<uint64>(Scenario.Definition.RuleCount));
			TestEqual(*(SamplePrefix + TEXT("census sees every runtime node")),
				Sample.Census.RuntimeNodeCount, Scenario.Definition.NodeCount);
			TestEqual(*(SamplePrefix + TEXT("census sees every runtime rule")),
				Sample.Census.RuntimeRuleCount, Scenario.Definition.RuleCount);
			TestTrue(*(SamplePrefix + TEXT("records positive runtime-owned bytes")),
				Sample.Census.RuntimeKnownOwnedBytes > 0);
			TestTrue(*(SamplePrefix + TEXT("records positive presentation-owned bytes")),
				Sample.Census.PresentationKnownOwnedBytes > 0);
			TestTrue(*(SamplePrefix + TEXT("records a positive inclusive duration")),
				Sample.InclusiveMilliseconds > 0.0);

			const FString SampleContext = MakeTelemetryContext(Environment,
				Scenario.Definition, TEXT("runtime_hydration_sample"), SampleIndex);
			AddTelemetryData(TEXT("benchmark.schema_version"),
				FWebToUEBenchmarkSamplingPolicy::SchemaVersion, SampleContext);
			AddTelemetryData(TEXT("snapshot.schema_version"),
				FWebToUEPerformanceSnapshot::TelemetrySchemaVersion, SampleContext);
			AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, SampleContext);
			AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, SampleContext);
			AddTelemetryData(TEXT("runtime.hydrate.inclusive_ms"),
				Sample.InclusiveMilliseconds, SampleContext);
			AddTelemetryData(TEXT("runtime.hydrate.phase_ms"),
				Sample.Snapshot.Get(EWebToUEPerformancePhase::Hydrate).GetInclusiveMilliseconds(),
				SampleContext);
			AddTelemetryData(TEXT("runtime.memory.known_owned_bytes"),
				static_cast<double>(Sample.Census.GetTotalKnownOwnedBytes()), SampleContext);
			Samples.Add(Sample);
		}

		TestEqual(*(Prefix + TEXT("records the policy sample count")), Samples.Num(),
			FWebToUEBenchmarkSamplingPolicy::SampleCount);
		if (Samples.Num() != FWebToUEBenchmarkSamplingPolicy::SampleCount)
		{
			return false;
		}

		TArray<double> InclusiveMilliseconds;
		TArray<double> HydratePhaseMilliseconds;
		InclusiveMilliseconds.Reserve(Samples.Num());
		HydratePhaseMilliseconds.Reserve(Samples.Num());
		for (const FHydrationSample& Sample : Samples)
		{
			InclusiveMilliseconds.Add(Sample.InclusiveMilliseconds);
			HydratePhaseMilliseconds.Add(
				Sample.Snapshot.Get(EWebToUEPerformancePhase::Hydrate).GetInclusiveMilliseconds());
		}
		FWebToUEBenchmarkDistribution InclusiveDistribution;
		FWebToUEBenchmarkDistribution PhaseDistribution;
		TestTrue(*(Prefix + TEXT("calculates the inclusive distribution")),
			FWebToUEBenchmarkStatistics::TryCalculate(
				InclusiveMilliseconds, InclusiveDistribution));
		TestTrue(*(Prefix + TEXT("calculates the Hydrate phase distribution")),
			FWebToUEBenchmarkStatistics::TryCalculate(
				HydratePhaseMilliseconds, PhaseDistribution));

		TArray<UWebToUEView*> ResidentViews;
		TArray<TSharedPtr<SWidget>> ResidentWidgets;
		ResidentViews.Reserve(AmplificationViewCount);
		ResidentWidgets.Reserve(AmplificationViewCount);
		ON_SCOPE_EXIT
		{
			for (int32 ViewIndex = 0; ViewIndex < ResidentViews.Num(); ++ViewIndex)
			{
				DestroyView(ResidentViews[ViewIndex], ResidentWidgets[ViewIndex]);
			}
		};

		const uint64 RssBeforeViews = FPlatformMemory::GetStats().UsedPhysical;
		ResidentWidgets.AddDefaulted();
		ResidentViews.Add(CreateHydratedView(*Document, ResidentWidgets.Last()));
		FWebToUERuntimeMemoryCensus FirstViewCensus;
		TestTrue(*(Prefix + TEXT("reads the first resident View census")),
			ResidentViews[0]->GetRuntimeMemoryCensusForTesting(FirstViewCensus));
		const uint64 RssAfterFirstView = FPlatformMemory::GetStats().UsedPhysical;

		ResidentWidgets.AddDefaulted();
		ResidentViews.Add(CreateHydratedView(*Document, ResidentWidgets.Last()));
		FWebToUERuntimeMemoryCensus SecondViewCensus;
		TestTrue(*(Prefix + TEXT("reads the second resident View census")),
			ResidentViews[1]->GetRuntimeMemoryCensusForTesting(SecondViewCensus));
		const uint64 RssAfterSecondView = FPlatformMemory::GetStats().UsedPhysical;
		for (int32 ViewIndex = 2; ViewIndex < AmplificationViewCount; ++ViewIndex)
		{
			ResidentWidgets.AddDefaulted();
			ResidentViews.Add(CreateHydratedView(*Document, ResidentWidgets.Last()));
		}
		const uint64 RssAfterAmplification = FPlatformMemory::GetStats().UsedPhysical;

		TestEqual(*(Prefix + TEXT("first View node count remains exact")),
			FirstViewCensus.RuntimeNodeCount, Scenario.Definition.NodeCount);
		TestEqual(*(Prefix + TEXT("second View node count remains exact")),
			SecondViewCensus.RuntimeNodeCount, Scenario.Definition.NodeCount);
		TestEqual(*(Prefix + TEXT("first and second View known-owned totals are deterministic")),
			FirstViewCensus.GetTotalKnownOwnedBytes(), SecondViewCensus.GetTotalKnownOwnedBytes());

		const int64 FirstViewRssDelta = SignedDelta(RssAfterFirstView, RssBeforeViews);
		const int64 SecondViewRssDelta = SignedDelta(RssAfterSecondView, RssAfterFirstView);
		const int64 AmplifiedRssDelta = SignedDelta(RssAfterAmplification, RssBeforeViews);
		const double AmplifiedRssBytesPerView =
			static_cast<double>(AmplifiedRssDelta) / AmplificationViewCount;
		const FString SummaryContext = MakeTelemetryContext(Environment,
			Scenario.Definition, TEXT("runtime_hydration_summary"));
		AddTelemetryData(TEXT("benchmark.schema_version"),
			FWebToUEBenchmarkSamplingPolicy::SchemaVersion, SummaryContext);
		AddTelemetryData(TEXT("benchmark.warmup_count"),
			FWebToUEBenchmarkSamplingPolicy::WarmupCount, SummaryContext);
		AddTelemetryData(TEXT("benchmark.sample_count"),
			FWebToUEBenchmarkSamplingPolicy::SampleCount, SummaryContext);
		AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, SummaryContext);
		AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, SummaryContext);
		AddTelemetryData(TEXT("runtime.hydrate.inclusive_p50_ms"),
			InclusiveDistribution.P50, SummaryContext);
		AddTelemetryData(TEXT("runtime.hydrate.inclusive_p95_ms"),
			InclusiveDistribution.P95, SummaryContext);
		AddTelemetryData(TEXT("runtime.hydrate.phase_p50_ms"),
			PhaseDistribution.P50, SummaryContext);
		AddTelemetryData(TEXT("runtime.hydrate.phase_p95_ms"),
			PhaseDistribution.P95, SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.first_view.runtime_known_owned_bytes"),
			static_cast<double>(FirstViewCensus.RuntimeKnownOwnedBytes), SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.first_view.presentation_known_owned_bytes"),
			static_cast<double>(FirstViewCensus.PresentationKnownOwnedBytes), SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.first_view.total_known_owned_bytes"),
			static_cast<double>(FirstViewCensus.GetTotalKnownOwnedBytes()), SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.second_view.incremental_known_owned_bytes"),
			static_cast<double>(SecondViewCensus.GetTotalKnownOwnedBytes()), SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.process_rss_before_views_bytes"),
			static_cast<double>(RssBeforeViews), SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.process_rss_first_view_delta_bytes"),
			static_cast<double>(FirstViewRssDelta), SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.process_rss_second_view_delta_bytes"),
			static_cast<double>(SecondViewRssDelta), SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.process_rss_amplification_view_count"),
			AmplificationViewCount, SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.process_rss_amplified_delta_bytes"),
			static_cast<double>(AmplifiedRssDelta), SummaryContext);
		AddTelemetryData(TEXT("runtime.memory.process_rss_amplified_bytes_per_view"),
			AmplifiedRssBytesPerView, SummaryContext);

		AddInfo(Prefix + FString::Printf(
			TEXT("hydrate={inclusive_p50_ms=%.6f,inclusive_p95_ms=%.6f,phase_p50_ms=%.6f,phase_p95_ms=%.6f}; memory={first_view_known_owned_bytes=%llu,second_view_incremental_known_owned_bytes=%llu,rss_first_delta_bytes=%lld,rss_second_delta_bytes=%lld,rss_%d_view_delta_bytes=%lld,rss_amplified_bytes_per_view=%.2f}"),
			InclusiveDistribution.P50, InclusiveDistribution.P95,
			PhaseDistribution.P50, PhaseDistribution.P95,
			FirstViewCensus.GetTotalKnownOwnedBytes(),
			SecondViewCensus.GetTotalKnownOwnedBytes(),
			FirstViewRssDelta, SecondViewRssDelta, AmplificationViewCount,
			AmplifiedRssDelta, AmplifiedRssBytesPerView));
	}

	return true;
}

#endif
