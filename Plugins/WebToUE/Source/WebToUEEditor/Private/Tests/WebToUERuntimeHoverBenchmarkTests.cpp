#if WITH_DEV_AUTOMATION_TESTS

#include "Benchmarks/WebToUEBenchmarkPolicy.h"
#include "Benchmarks/WebToUEBenchmarkScenario.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"
#include "WebToUEPerformance.h"
#include "WebToUERuntimeBenchmarkViewModel.h"
#include "WebToUEView.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "UObject/Package.h"
#include "Widgets/SWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeHoverBenchmarkTest,
	"WebToUE.Editor.RuntimeHoverBenchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeFieldNotifyBenchmarkTest,
	"WebToUE.Editor.RuntimeFieldNotifyBenchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeWarmLayoutBenchmarkTest,
	"WebToUE.Editor.RuntimeWarmLayoutBenchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERuntimeUnchangedPaintBenchmarkTest,
	"WebToUE.Editor.RuntimeUnchangedPaintBenchmark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::Benchmark::Tests
{
	static constexpr const TCHAR* RuntimePhaseTelemetryNames[] = {
		TEXT("hydrate"),
		TEXT("style"),
		TEXT("measure"),
		TEXT("layout"),
		TEXT("paint_build"),
		TEXT("hit_test"),
		TEXT("binding")
	};
	static_assert(UE_ARRAY_COUNT(RuntimePhaseTelemetryNames) == FWebToUEPerformanceSnapshot::PhaseCount);

	struct FRuntimeUpdateSample
	{
		double InclusiveMilliseconds = 0.0;
		bool bValueChanged = false;
		FWebToUEPerformanceSnapshot Snapshot;
	};

	static FString MakeRuntimeTelemetryContext(const FWebToUEBenchmarkEnvironment& Environment,
		const FWebToUEBenchmarkScenarioDefinition& Definition, const TCHAR* Kind, int32 SampleIndex = 0)
	{
		return FString::Printf(
			TEXT("benchmark_schema=%d;budget_schema=%d;snapshot_schema=%d;scenario=%s;kind=%s;sample=%d;environment=%s"),
			FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
			FWebToUEBenchmarkBudgetPolicy::SchemaVersion,
			FWebToUEPerformanceSnapshot::TelemetrySchemaVersion,
			*Definition.Name,
			Kind,
			SampleIndex,
			*Environment.GetFingerprint());
	}

	static void PaintRuntimeView(const TSharedRef<SWidget>& View, const FGeometry& Geometry,
		FHittestGrid& HittestGrid, FSlateWindowElementList& DrawElements)
	{
		const FPaintArgs PaintArgs(nullptr, HittestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
		View->Paint(PaintArgs, Geometry,
			FSlateRect(0.0f, 0.0f, Geometry.GetLocalSize().X, Geometry.GetLocalSize().Y),
			DrawElements, 0, FWidgetStyle(), true);
	}

	static void PaintRuntimeView(const TSharedRef<SWidget>& View, const FGeometry& Geometry)
	{
		FHittestGrid HittestGrid;
		FSlateWindowElementList DrawElements(nullptr);
		PaintRuntimeView(View, Geometry, HittestGrid, DrawElements);
	}
}

bool FWebToUERuntimeHoverBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Benchmark::Tests;

	SetTelemetryStorage(TEXT("WebToUEPerformance"));
	const FWebToUEBenchmarkEnvironment Environment = FWebToUEBenchmarkEnvironment::Capture();
	AddInfo(TEXT("Benchmark policy: ") + FWebToUEBenchmarkSamplingPolicy::ToLogString());
	AddInfo(TEXT("Benchmark environment: ") + Environment.ToLogString());
	TestTrue(TEXT("The runtime benchmark runs in Win64 Editor Development"),
		Environment.IsStandardConfiguration());

	const TArray<FWebToUEBenchmarkScenarioDefinition>& Definitions =
		FWebToUEBenchmarkScenarioGenerator::GetStandardDefinitions();
	TestTrue(TEXT("The medium benchmark scenario is available"), Definitions.IsValidIndex(1));
	if (!Definitions.IsValidIndex(1))
	{
		return false;
	}

	const FWebToUEBenchmarkScenario Scenario = FWebToUEBenchmarkScenarioGenerator::Generate(Definitions[1]);
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(TestDirectory, TEXT("RuntimeHoverBenchmark.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*TestFilename, false, true);
	};

	const FString Source = FString::Printf(TEXT("<style>%s</style>%s"), *Scenario.Css, *Scenario.Html);
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	const bool bImported = FFileHelper::SaveStringToFile(Source, *TestFilename) &&
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, false);
	TestTrue(TEXT("The medium runtime benchmark document imports"), bImported);
	if (!bImported)
	{
		return false;
	}

	TestEqual(TEXT("The runtime document has exactly 500 compiled nodes"),
		Document->GetCompiledNodes().Num(), Scenario.Definition.NodeCount);
	TestEqual(TEXT("The runtime document has exactly 200 compiled rules"),
		Document->GetCompiledRules().Num(), Scenario.Definition.RuleCount);
	const FWebToUECompiledNode* HoverTarget = Document->GetCompiledNodes().FindByPredicate(
		[](const FWebToUECompiledNode& Node)
		{
			return Node.Attributes.ContainsByPredicate([](const FWebToUECompiledAttribute& Attribute)
			{
				return Attribute.Name == TEXT("id") && Attribute.Value == TEXT("benchmark-hover-target");
			});
		});
	TestNotNull(TEXT("The runtime document retains the fixed hover target"), HoverTarget);
	if (!HoverTarget)
	{
		return false;
	}

	UWebToUEView* RuntimeView = NewObject<UWebToUEView>(GetTransientPackage());
	RuntimeView->SetDocument(Document);
	const TSharedRef<SWidget> SlateView = RuntimeView->TakeWidget();
	TestFalse(TEXT("The runtime view has no default Tick"), SlateView->GetCanTick());

	const FVector2D ViewportSize(1280.0, 720.0);
	const FGeometry Geometry = FGeometry::MakeRoot(ViewportSize, FSlateLayoutTransform());
	const FVector2D HoverPosition(150.0, 18.0);
	const FPointerEvent HoverEvent(0, HoverPosition, HoverPosition, TSet<FKey>(), EKeys::Invalid,
		0.0f, FModifierKeysState());
	PaintRuntimeView(SlateView, Geometry);

	TArray<FRuntimeUpdateSample> Samples;
	Samples.Reserve(FWebToUEBenchmarkSamplingPolicy::SampleCount);
	const int32 TotalIterations = FWebToUEBenchmarkSamplingPolicy::WarmupCount +
		FWebToUEBenchmarkSamplingPolicy::SampleCount;
	for (int32 Iteration = 0; Iteration < TotalIterations; ++Iteration)
	{
		SlateView->OnMouseLeave(FPointerEvent());
		PaintRuntimeView(SlateView, Geometry);

		FRuntimeUpdateSample Sample;
		FHittestGrid TimedHittestGrid;
		FSlateWindowElementList TimedDrawElements(nullptr);
		{
			FWebToUEPerformanceCapture Capture;
			const double StartSeconds = FPlatformTime::Seconds();
			SlateView->OnMouseMove(Geometry, HoverEvent);
			PaintRuntimeView(SlateView, Geometry, TimedHittestGrid, TimedDrawElements);
			Sample.InclusiveMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			Sample.Snapshot = Capture.GetSnapshot();
		}

		if (Iteration < FWebToUEBenchmarkSamplingPolicy::WarmupCount)
		{
			continue;
		}

		const int32 SampleIndex = Samples.Num() + 1;
		const FString Prefix = FString::Printf(TEXT("Runtime hover sample %d: "), SampleIndex);
		TestEqual(*(Prefix + TEXT("performs one hit test")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::HitTest).CallCount, uint64(1));
		TestEqual(*(Prefix + TEXT("performs one style update")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Style).CallCount, uint64(1));
		TestEqual(*(Prefix + TEXT("does not perform layout")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Layout).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("performs one paint build")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::PaintBuild).CallCount, uint64(1));
		TestEqual(*(Prefix + TEXT("visits only the changed style target")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits),
			uint64(1));
		TestEqual(*(Prefix + TEXT("marks only one dirty style target")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::StyleDirtyTargets), uint64(1));
		TestEqual(*(Prefix + TEXT("changes one paint property")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::StylePropertyChanges), uint64(1));
		const uint64 FullScanWork = static_cast<uint64>(Scenario.Definition.NodeCount) *
			static_cast<uint64>(Scenario.Definition.RuleCount);
		const uint64 CandidateCount =
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorCandidates);
		TestTrue(*(Prefix + TEXT("selects fewer candidates than the 100,000-rule full scan")),
			CandidateCount < FullScanWork);
		TestEqual(*(Prefix + TEXT("evaluates every selector candidate exactly once")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorEvaluations), CandidateCount);
		TestEqual(*(Prefix + TEXT("does not rebuild Yoga")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt),
			uint64(0));
		TestEqual(*(Prefix + TEXT("does not invalidate text caches")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TextCacheInvalidations), uint64(0));
		TestEqual(*(Prefix + TEXT("does not rebuild paint order")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::PaintOrderCacheBuilds), uint64(0));
		TestEqual(*(Prefix + TEXT("performs no resource load attempt")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::ResourceLoadAttempts), uint64(0));
		TestTrue(*(Prefix + TEXT("records a positive end-to-end duration")),
			Sample.InclusiveMilliseconds > 0.0);

		const FString TelemetryContext = MakeRuntimeTelemetryContext(Environment,
			Scenario.Definition, TEXT("runtime_hover_sample"), SampleIndex);
		AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
			TelemetryContext);
		AddTelemetryData(TEXT("budget.schema_version"), FWebToUEBenchmarkBudgetPolicy::SchemaVersion,
			TelemetryContext);
		AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
			TelemetryContext);
		AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
			TelemetryContext);
		AddTelemetryData(TEXT("sample.index"), SampleIndex, TelemetryContext);
		AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, TelemetryContext);
		AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, TelemetryContext);
		AddTelemetryData(TEXT("runtime.hover_update.inclusive_ms"), Sample.InclusiveMilliseconds,
			TelemetryContext);
		Sample.Snapshot.ForEachTelemetryMeasurement(
			[&](const TCHAR* Name, double Value)
			{
				AddTelemetryData(Name, Value, TelemetryContext);
			});
		Samples.Add(MoveTemp(Sample));
	}

	TestEqual(TEXT("The runtime hover benchmark records the policy sample count"),
		Samples.Num(), FWebToUEBenchmarkSamplingPolicy::SampleCount);
	if (Samples.Num() != FWebToUEBenchmarkSamplingPolicy::SampleCount)
	{
		RuntimeView->ReleaseSlateResources(true);
		return false;
	}

	TArray<double> InclusiveSamples;
	InclusiveSamples.Reserve(Samples.Num());
	for (const FRuntimeUpdateSample& Sample : Samples)
	{
		InclusiveSamples.Add(Sample.InclusiveMilliseconds);
	}
	FWebToUEBenchmarkDistribution InclusiveDistribution;
	TestTrue(TEXT("The runtime hover duration produces a distribution"),
		FWebToUEBenchmarkStatistics::TryCalculate(InclusiveSamples, InclusiveDistribution));
	const bool bMeetsTarget = InclusiveDistribution.P95 <
		FWebToUEBenchmarkBudgetPolicy::MediumSingleNodeHoverP95Milliseconds;
	if (FWebToUEBenchmarkBudgetPolicy::bEnforceMediumSingleNodeHoverBudget)
	{
		TestTrue(TEXT("The runtime hover P95 stays below the enforced budget"), bMeetsTarget);
	}
	const FString SummaryContext = MakeRuntimeTelemetryContext(Environment,
		Scenario.Definition, TEXT("runtime_hover_summary"));
	AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
		SummaryContext);
	AddTelemetryData(TEXT("budget.schema_version"), FWebToUEBenchmarkBudgetPolicy::SchemaVersion,
		SummaryContext);
	AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
		SummaryContext);
	AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
		SummaryContext);
	AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, SummaryContext);
	AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, SummaryContext);
	AddTelemetryData(TEXT("runtime.hover_update.p50_ms"), InclusiveDistribution.P50, SummaryContext);
	AddTelemetryData(TEXT("runtime.hover_update.p95_ms"), InclusiveDistribution.P95, SummaryContext);
	AddTelemetryData(TEXT("runtime.hover_update.target_p95_ms"),
		FWebToUEBenchmarkBudgetPolicy::MediumSingleNodeHoverP95Milliseconds, SummaryContext);
	AddTelemetryData(TEXT("runtime.hover_update.target_enforced"),
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumSingleNodeHoverBudget ? 1.0 : 0.0, SummaryContext);
	AddTelemetryData(TEXT("runtime.hover_update.target_met"), bMeetsTarget ? 1.0 : 0.0, SummaryContext);
	AddInfo(FString::Printf(
		TEXT("Runtime hover: p50_ms=%.6f,p95_ms=%.6f,min_ms=%.6f,max_ms=%.6f,target_p95_ms=%.6f,target_enforced=%s,target_met=%s"),
		InclusiveDistribution.P50, InclusiveDistribution.P95, InclusiveDistribution.Minimum,
		InclusiveDistribution.Maximum,
		FWebToUEBenchmarkBudgetPolicy::MediumSingleNodeHoverP95Milliseconds,
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumSingleNodeHoverBudget ? TEXT("true") : TEXT("false"),
		bMeetsTarget ? TEXT("true") : TEXT("false")));

	for (int32 PhaseIndex = 0; PhaseIndex < FWebToUEPerformanceSnapshot::PhaseCount; ++PhaseIndex)
	{
		const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(PhaseIndex);
		TArray<double> PhaseSamples;
		PhaseSamples.Reserve(Samples.Num());
		for (const FRuntimeUpdateSample& Sample : Samples)
		{
			PhaseSamples.Add(Sample.Snapshot.Get(Phase).GetInclusiveMilliseconds());
		}
		FWebToUEBenchmarkDistribution PhaseDistribution;
		TestTrue(*FString::Printf(TEXT("The runtime hover %s phase produces a distribution"),
			RuntimePhaseTelemetryNames[PhaseIndex]),
			FWebToUEBenchmarkStatistics::TryCalculate(PhaseSamples, PhaseDistribution));
		const FString P50Name = FString::Printf(TEXT("runtime.phase.%s.p50_ms"),
			RuntimePhaseTelemetryNames[PhaseIndex]);
		const FString P95Name = FString::Printf(TEXT("runtime.phase.%s.p95_ms"),
			RuntimePhaseTelemetryNames[PhaseIndex]);
		AddTelemetryData(*P50Name, PhaseDistribution.P50, SummaryContext);
		AddTelemetryData(*P95Name, PhaseDistribution.P95, SummaryContext);
		AddInfo(FString::Printf(TEXT("Runtime hover %s: p50_ms=%.6f,p95_ms=%.6f"),
			RuntimePhaseTelemetryNames[PhaseIndex], PhaseDistribution.P50, PhaseDistribution.P95));
	}

	RuntimeView->ReleaseSlateResources(true);
	return true;
}

bool FWebToUERuntimeFieldNotifyBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Benchmark::Tests;

	SetTelemetryStorage(TEXT("WebToUEPerformance"));
	const FWebToUEBenchmarkEnvironment Environment = FWebToUEBenchmarkEnvironment::Capture();
	AddInfo(TEXT("Benchmark policy: ") + FWebToUEBenchmarkSamplingPolicy::ToLogString());
	AddInfo(TEXT("Benchmark environment: ") + Environment.ToLogString());
	TestTrue(TEXT("The runtime benchmark runs in Win64 Editor Development"),
		Environment.IsStandardConfiguration());

	const TArray<FWebToUEBenchmarkScenarioDefinition>& Definitions =
		FWebToUEBenchmarkScenarioGenerator::GetStandardDefinitions();
	TestTrue(TEXT("The medium benchmark scenario is available"), Definitions.IsValidIndex(1));
	if (!Definitions.IsValidIndex(1))
	{
		return false;
	}

	const FWebToUEBenchmarkScenario Scenario = FWebToUEBenchmarkScenarioGenerator::Generate(Definitions[1]);
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(TestDirectory, TEXT("RuntimeFieldNotifyBenchmark.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*TestFilename, false, true);
	};

	const FString Source = FString::Printf(TEXT("<style>%s</style>%s"), *Scenario.Css, *Scenario.Html);
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	const bool bImported = FFileHelper::SaveStringToFile(Source, *TestFilename) &&
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, false);
	TestTrue(TEXT("The medium runtime benchmark document imports"), bImported);
	if (!bImported)
	{
		return false;
	}

	TestEqual(TEXT("The runtime document has exactly 500 compiled nodes"),
		Document->GetCompiledNodes().Num(), Scenario.Definition.NodeCount);
	TestEqual(TEXT("The runtime document has exactly 200 compiled rules"),
		Document->GetCompiledRules().Num(), Scenario.Definition.RuleCount);
	const FWebToUECompiledNode* BindingTarget = Document->GetCompiledNodes().FindByPredicate(
		[](const FWebToUECompiledNode& Node)
		{
			return Node.Attributes.ContainsByPredicate([](const FWebToUECompiledAttribute& Attribute)
			{
				return Attribute.Name == TEXT("id") && Attribute.Value == TEXT("benchmark-binding-target");
			});
		});
	TestNotNull(TEXT("The runtime document retains the fixed binding target"), BindingTarget);
	if (!BindingTarget)
	{
		return false;
	}
	TestTrue(TEXT("The fixed target retains the BenchmarkLabel binding"),
		BindingTarget->Attributes.ContainsByPredicate([](const FWebToUECompiledAttribute& Attribute)
		{
			return Attribute.Name == TEXT("data-ue-bind-text") && Attribute.Value == TEXT("BenchmarkLabel");
		}));

	const FText BaselineValue = FText::FromString(TEXT("Benchmark A"));
	const FText UpdatedValue = FText::FromString(TEXT("Benchmark B"));
	UWebToUERuntimeBenchmarkViewModel* ViewModel =
		NewObject<UWebToUERuntimeBenchmarkViewModel>(GetTransientPackage());
	TestTrue(TEXT("The benchmark ViewModel accepts its initial value"),
		ViewModel->SetBenchmarkLabel(BaselineValue));

	UWebToUEView* RuntimeView = NewObject<UWebToUEView>(GetTransientPackage());
	RuntimeView->SetDocument(Document);
	const TSharedRef<SWidget> SlateView = RuntimeView->TakeWidget();
	RuntimeView->SetDataContext(ViewModel);
	TestFalse(TEXT("The runtime view has no default Tick"), SlateView->GetCanTick());

	const FVector2D ViewportSize(1280.0, 720.0);
	const FGeometry Geometry = FGeometry::MakeRoot(ViewportSize, FSlateLayoutTransform());
	PaintRuntimeView(SlateView, Geometry);

	TArray<FRuntimeUpdateSample> Samples;
	Samples.Reserve(FWebToUEBenchmarkSamplingPolicy::SampleCount);
	const int32 TotalIterations = FWebToUEBenchmarkSamplingPolicy::WarmupCount +
		FWebToUEBenchmarkSamplingPolicy::SampleCount;
	for (int32 Iteration = 0; Iteration < TotalIterations; ++Iteration)
	{
		ViewModel->SetBenchmarkLabel(BaselineValue);
		PaintRuntimeView(SlateView, Geometry);

		FRuntimeUpdateSample Sample;
		FHittestGrid TimedHittestGrid;
		FSlateWindowElementList TimedDrawElements(nullptr);
		{
			FWebToUEPerformanceCapture Capture;
			const double StartSeconds = FPlatformTime::Seconds();
			Sample.bValueChanged = ViewModel->SetBenchmarkLabel(UpdatedValue);
			PaintRuntimeView(SlateView, Geometry, TimedHittestGrid, TimedDrawElements);
			Sample.InclusiveMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			Sample.Snapshot = Capture.GetSnapshot();
		}

		if (Iteration < FWebToUEBenchmarkSamplingPolicy::WarmupCount)
		{
			continue;
		}

		const int32 SampleIndex = Samples.Num() + 1;
		const FString Prefix = FString::Printf(TEXT("Runtime FieldNotify sample %d: "), SampleIndex);
		TestTrue(*(Prefix + TEXT("changes the bound value")), Sample.bValueChanged);
		TestTrue(*(Prefix + TEXT("stores the updated value")), ViewModel->BenchmarkLabel.EqualTo(UpdatedValue));
		TestEqual(*(Prefix + TEXT("performs one binding refresh")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Binding).CallCount, uint64(1));
		TestEqual(*(Prefix + TEXT("does not enter style resolution")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Style).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("unchanged Desired Size skips layout")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Layout).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("performs one paint build")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::PaintBuild).CallCount, uint64(1));
		TestEqual(*(Prefix + TEXT("does not perform a hit test")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::HitTest).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("reads exactly one root field")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::BindingFieldsRead), uint64(1));
		TestEqual(*(Prefix + TEXT("executes exactly one direct binding op")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::BindingOpsExecuted), uint64(1));
		TestEqual(*(Prefix + TEXT("updates exactly one direct node")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::BindingNodesUpdated), uint64(1));
		TestEqual(*(Prefix + TEXT("does not visit style nodes")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(0));
		TestEqual(*(Prefix + TEXT("does not select style candidates")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorCandidates), uint64(0));
		TestEqual(*(Prefix + TEXT("does not evaluate selectors")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorEvaluations), uint64(0));
		TestEqual(*(Prefix + TEXT("does not rebuild Yoga nodes")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));
		TestEqual(*(Prefix + TEXT("recomputes only the changed text")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(1));
		TestEqual(*(Prefix + TEXT("does not rebuild the text layout object")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutBuilds), uint64(0));
		TestEqual(*(Prefix + TEXT("does not evict text caches")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TextCacheInvalidations), uint64(0));
		TestEqual(*(Prefix + TEXT("does not rebuild brushes")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::BrushBuilds), uint64(0));
		TestEqual(*(Prefix + TEXT("does not rebuild paint order")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::PaintOrderCacheBuilds), uint64(0));
		TestEqual(*(Prefix + TEXT("performs no resource load attempt")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::ResourceLoadAttempts), uint64(0));
		TestTrue(*(Prefix + TEXT("records a positive end-to-end duration")),
			Sample.InclusiveMilliseconds > 0.0);

		const FString TelemetryContext = MakeRuntimeTelemetryContext(Environment,
			Scenario.Definition, TEXT("runtime_field_notify_sample"), SampleIndex);
		AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
			TelemetryContext);
		AddTelemetryData(TEXT("budget.schema_version"), FWebToUEBenchmarkBudgetPolicy::SchemaVersion,
			TelemetryContext);
		AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
			TelemetryContext);
		AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
			TelemetryContext);
		AddTelemetryData(TEXT("sample.index"), SampleIndex, TelemetryContext);
		AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, TelemetryContext);
		AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, TelemetryContext);
		AddTelemetryData(TEXT("runtime.field_notify_update.inclusive_ms"), Sample.InclusiveMilliseconds,
			TelemetryContext);
		Sample.Snapshot.ForEachTelemetryMeasurement(
			[&](const TCHAR* Name, double Value)
			{
				AddTelemetryData(Name, Value, TelemetryContext);
			});
		Samples.Add(MoveTemp(Sample));
	}

	TestEqual(TEXT("The runtime FieldNotify benchmark records the policy sample count"),
		Samples.Num(), FWebToUEBenchmarkSamplingPolicy::SampleCount);
	if (Samples.Num() != FWebToUEBenchmarkSamplingPolicy::SampleCount)
	{
		RuntimeView->ReleaseSlateResources(true);
		return false;
	}

	TArray<double> InclusiveSamples;
	InclusiveSamples.Reserve(Samples.Num());
	for (const FRuntimeUpdateSample& Sample : Samples)
	{
		InclusiveSamples.Add(Sample.InclusiveMilliseconds);
	}
	FWebToUEBenchmarkDistribution InclusiveDistribution;
	TestTrue(TEXT("The runtime FieldNotify duration produces a distribution"),
		FWebToUEBenchmarkStatistics::TryCalculate(InclusiveSamples, InclusiveDistribution));
	const bool bMeetsTarget = InclusiveDistribution.P95 <
		FWebToUEBenchmarkBudgetPolicy::MediumSingleFieldNotifyP95Milliseconds;
	if (FWebToUEBenchmarkBudgetPolicy::bEnforceMediumSingleFieldNotifyBudget)
	{
		TestTrue(TEXT("The runtime FieldNotify P95 stays below the enforced budget"),
			bMeetsTarget);
	}
	const FString SummaryContext = MakeRuntimeTelemetryContext(Environment,
		Scenario.Definition, TEXT("runtime_field_notify_summary"));
	AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
		SummaryContext);
	AddTelemetryData(TEXT("budget.schema_version"), FWebToUEBenchmarkBudgetPolicy::SchemaVersion,
		SummaryContext);
	AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
		SummaryContext);
	AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
		SummaryContext);
	AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, SummaryContext);
	AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, SummaryContext);
	AddTelemetryData(TEXT("runtime.field_notify_update.p50_ms"), InclusiveDistribution.P50, SummaryContext);
	AddTelemetryData(TEXT("runtime.field_notify_update.p95_ms"), InclusiveDistribution.P95, SummaryContext);
	AddTelemetryData(TEXT("runtime.field_notify_update.target_p95_ms"),
		FWebToUEBenchmarkBudgetPolicy::MediumSingleFieldNotifyP95Milliseconds, SummaryContext);
	AddTelemetryData(TEXT("runtime.field_notify_update.target_enforced"),
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumSingleFieldNotifyBudget ? 1.0 : 0.0, SummaryContext);
	AddTelemetryData(TEXT("runtime.field_notify_update.target_met"), bMeetsTarget ? 1.0 : 0.0, SummaryContext);
	AddInfo(FString::Printf(
		TEXT("Runtime FieldNotify: p50_ms=%.6f,p95_ms=%.6f,min_ms=%.6f,max_ms=%.6f,target_p95_ms=%.6f,target_enforced=%s,target_met=%s"),
		InclusiveDistribution.P50, InclusiveDistribution.P95, InclusiveDistribution.Minimum,
		InclusiveDistribution.Maximum,
		FWebToUEBenchmarkBudgetPolicy::MediumSingleFieldNotifyP95Milliseconds,
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumSingleFieldNotifyBudget ? TEXT("true") : TEXT("false"),
		bMeetsTarget ? TEXT("true") : TEXT("false")));

	for (int32 PhaseIndex = 0; PhaseIndex < FWebToUEPerformanceSnapshot::PhaseCount; ++PhaseIndex)
	{
		const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(PhaseIndex);
		TArray<double> PhaseSamples;
		PhaseSamples.Reserve(Samples.Num());
		for (const FRuntimeUpdateSample& Sample : Samples)
		{
			PhaseSamples.Add(Sample.Snapshot.Get(Phase).GetInclusiveMilliseconds());
		}
		FWebToUEBenchmarkDistribution PhaseDistribution;
		TestTrue(*FString::Printf(TEXT("The runtime FieldNotify %s phase produces a distribution"),
			RuntimePhaseTelemetryNames[PhaseIndex]),
			FWebToUEBenchmarkStatistics::TryCalculate(PhaseSamples, PhaseDistribution));
		const FString P50Name = FString::Printf(TEXT("runtime.phase.%s.p50_ms"),
			RuntimePhaseTelemetryNames[PhaseIndex]);
		const FString P95Name = FString::Printf(TEXT("runtime.phase.%s.p95_ms"),
			RuntimePhaseTelemetryNames[PhaseIndex]);
		AddTelemetryData(*P50Name, PhaseDistribution.P50, SummaryContext);
		AddTelemetryData(*P95Name, PhaseDistribution.P95, SummaryContext);
		AddInfo(FString::Printf(TEXT("Runtime FieldNotify %s: p50_ms=%.6f,p95_ms=%.6f"),
			RuntimePhaseTelemetryNames[PhaseIndex], PhaseDistribution.P50, PhaseDistribution.P95));
	}

	RuntimeView->ReleaseSlateResources(true);
	return true;
}

bool FWebToUERuntimeWarmLayoutBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Benchmark::Tests;

	SetTelemetryStorage(TEXT("WebToUEPerformance"));
	const FWebToUEBenchmarkEnvironment Environment = FWebToUEBenchmarkEnvironment::Capture();
	AddInfo(TEXT("Benchmark policy: ") + FWebToUEBenchmarkSamplingPolicy::ToLogString());
	AddInfo(TEXT("Benchmark environment: ") + Environment.ToLogString());
	TestTrue(TEXT("The runtime benchmark runs in Win64 Editor Development"),
		Environment.IsStandardConfiguration());

	const TArray<FWebToUEBenchmarkScenarioDefinition>& Definitions =
		FWebToUEBenchmarkScenarioGenerator::GetStandardDefinitions();
	TestTrue(TEXT("The medium benchmark scenario is available"), Definitions.IsValidIndex(1));
	if (!Definitions.IsValidIndex(1))
	{
		return false;
	}

	const FWebToUEBenchmarkScenario Scenario = FWebToUEBenchmarkScenarioGenerator::Generate(Definitions[1]);
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(TestDirectory, TEXT("RuntimeWarmLayoutBenchmark.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*TestFilename, false, true);
	};

	const FString Source = FString::Printf(TEXT("<style>%s</style>%s"), *Scenario.Css, *Scenario.Html);
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	const bool bImported = FFileHelper::SaveStringToFile(Source, *TestFilename) &&
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, false);
	TestTrue(TEXT("The medium runtime benchmark document imports"), bImported);
	if (!bImported)
	{
		return false;
	}

	TestEqual(TEXT("The runtime document has exactly 500 compiled nodes"),
		Document->GetCompiledNodes().Num(), Scenario.Definition.NodeCount);
	TestEqual(TEXT("The runtime document has exactly 200 compiled rules"),
		Document->GetCompiledRules().Num(), Scenario.Definition.RuleCount);
	int32 TextNodeCount = 0;
	for (const FWebToUECompiledNode& Node : Document->GetCompiledNodes())
	{
		if (Node.Tag == TEXT("#text"))
		{
			++TextNodeCount;
		}
	}
	TestEqual(TEXT("The medium runtime document has exactly 249 measurable text nodes"),
		TextNodeCount, 249);

	UWebToUEView* RuntimeView = NewObject<UWebToUEView>(GetTransientPackage());
	RuntimeView->SetDocument(Document);
	const TSharedRef<SWidget> SlateView = RuntimeView->TakeWidget();
	TestFalse(TEXT("The runtime view has no default Tick"), SlateView->GetCanTick());

	const FVector2D ViewportSize(1280.0, 720.0);
	const FVector2f RuntimeViewportSize(1280.0f, 720.0f);
	const FGeometry Geometry = FGeometry::MakeRoot(ViewportSize, FSlateLayoutTransform());
	PaintRuntimeView(SlateView, Geometry);

	TArray<FRuntimeUpdateSample> Samples;
	Samples.Reserve(FWebToUEBenchmarkSamplingPolicy::SampleCount);
	const int32 TotalIterations = FWebToUEBenchmarkSamplingPolicy::WarmupCount +
		FWebToUEBenchmarkSamplingPolicy::SampleCount;
	for (int32 Iteration = 0; Iteration < TotalIterations; ++Iteration)
	{
		FRuntimeUpdateSample Sample;
		{
			FWebToUEPerformanceCapture Capture;
			const double StartSeconds = FPlatformTime::Seconds();
			RuntimeView->LayoutForTesting(RuntimeViewportSize);
			Sample.InclusiveMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			Sample.Snapshot = Capture.GetSnapshot();
		}

		if (Iteration < FWebToUEBenchmarkSamplingPolicy::WarmupCount)
		{
			continue;
		}

		const int32 SampleIndex = Samples.Num() + 1;
		const FString Prefix = FString::Printf(TEXT("Runtime warm layout sample %d: "), SampleIndex);
		TestEqual(*(Prefix + TEXT("performs one full layout")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Layout).CallCount, uint64(1));
		TestEqual(*(Prefix + TEXT("measures every text node once")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Measure).CallCount,
			static_cast<uint64>(TextNodeCount));
		TestEqual(*(Prefix + TEXT("does not hydrate")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Hydrate).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not resolve styles")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Style).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not build paint data")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::PaintBuild).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not hit test")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::HitTest).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not refresh bindings")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Binding).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("rebuilds one Yoga node per runtime node")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt),
			static_cast<uint64>(Scenario.Definition.NodeCount));
		TestEqual(*(Prefix + TEXT("reuses every text layout object")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutBuilds), uint64(0));
		TestEqual(*(Prefix + TEXT("reuses every warm Desired Size result")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes),
			uint64(0));
		TestEqual(*(Prefix + TEXT("does not rebuild brushes")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::BrushBuilds), uint64(0));
		TestEqual(*(Prefix + TEXT("does not visit style nodes")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(0));
		TestEqual(*(Prefix + TEXT("does not evaluate selectors")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorEvaluations), uint64(0));
		TestEqual(*(Prefix + TEXT("marks Yoga and measure-context allocations")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocations),
			static_cast<uint64>(Scenario.Definition.NodeCount + TextNodeCount));
		TestTrue(*(Prefix + TEXT("records a positive end-to-end duration")),
			Sample.InclusiveMilliseconds > 0.0);

		const FString TelemetryContext = MakeRuntimeTelemetryContext(Environment,
			Scenario.Definition, TEXT("runtime_warm_layout_sample"), SampleIndex);
		AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
			TelemetryContext);
		AddTelemetryData(TEXT("budget.schema_version"), FWebToUEBenchmarkBudgetPolicy::SchemaVersion,
			TelemetryContext);
		AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
			TelemetryContext);
		AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
			TelemetryContext);
		AddTelemetryData(TEXT("sample.index"), SampleIndex, TelemetryContext);
		AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, TelemetryContext);
		AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, TelemetryContext);
		AddTelemetryData(TEXT("scenario.text_node_count"), TextNodeCount, TelemetryContext);
		AddTelemetryData(TEXT("runtime.warm_full_layout.inclusive_ms"), Sample.InclusiveMilliseconds,
			TelemetryContext);
		Sample.Snapshot.ForEachTelemetryMeasurement(
			[&](const TCHAR* Name, double Value)
			{
				AddTelemetryData(Name, Value, TelemetryContext);
			});
		Samples.Add(MoveTemp(Sample));
	}

	TestEqual(TEXT("The runtime warm layout benchmark records the policy sample count"),
		Samples.Num(), FWebToUEBenchmarkSamplingPolicy::SampleCount);
	if (Samples.Num() != FWebToUEBenchmarkSamplingPolicy::SampleCount)
	{
		RuntimeView->ReleaseSlateResources(true);
		return false;
	}

	TArray<double> InclusiveSamples;
	InclusiveSamples.Reserve(Samples.Num());
	for (const FRuntimeUpdateSample& Sample : Samples)
	{
		InclusiveSamples.Add(Sample.InclusiveMilliseconds);
	}
	FWebToUEBenchmarkDistribution InclusiveDistribution;
	TestTrue(TEXT("The runtime warm layout duration produces a distribution"),
		FWebToUEBenchmarkStatistics::TryCalculate(InclusiveSamples, InclusiveDistribution));
	const bool bMeetsTarget = InclusiveDistribution.P95 <
		FWebToUEBenchmarkBudgetPolicy::MediumWarmFullLayoutP95Milliseconds;
	if (FWebToUEBenchmarkBudgetPolicy::bEnforceMediumWarmFullLayoutBudget)
	{
		TestTrue(TEXT("The runtime warm full-layout P95 stays below the enforced budget"), bMeetsTarget);
	}
	const FString SummaryContext = MakeRuntimeTelemetryContext(Environment,
		Scenario.Definition, TEXT("runtime_warm_layout_summary"));
	AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
		SummaryContext);
	AddTelemetryData(TEXT("budget.schema_version"), FWebToUEBenchmarkBudgetPolicy::SchemaVersion,
		SummaryContext);
	AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
		SummaryContext);
	AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
		SummaryContext);
	AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, SummaryContext);
	AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, SummaryContext);
	AddTelemetryData(TEXT("scenario.text_node_count"), TextNodeCount, SummaryContext);
	AddTelemetryData(TEXT("runtime.warm_full_layout.p50_ms"), InclusiveDistribution.P50, SummaryContext);
	AddTelemetryData(TEXT("runtime.warm_full_layout.p95_ms"), InclusiveDistribution.P95, SummaryContext);
	AddTelemetryData(TEXT("runtime.warm_full_layout.target_p95_ms"),
		FWebToUEBenchmarkBudgetPolicy::MediumWarmFullLayoutP95Milliseconds, SummaryContext);
	AddTelemetryData(TEXT("runtime.warm_full_layout.target_enforced"),
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumWarmFullLayoutBudget ? 1.0 : 0.0, SummaryContext);
	AddTelemetryData(TEXT("runtime.warm_full_layout.target_met"), bMeetsTarget ? 1.0 : 0.0, SummaryContext);
	AddInfo(FString::Printf(
		TEXT("Runtime warm full layout: p50_ms=%.6f,p95_ms=%.6f,min_ms=%.6f,max_ms=%.6f,target_p95_ms=%.6f,target_enforced=%s,target_met=%s"),
		InclusiveDistribution.P50, InclusiveDistribution.P95, InclusiveDistribution.Minimum,
		InclusiveDistribution.Maximum,
		FWebToUEBenchmarkBudgetPolicy::MediumWarmFullLayoutP95Milliseconds,
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumWarmFullLayoutBudget ? TEXT("true") : TEXT("false"),
		bMeetsTarget ? TEXT("true") : TEXT("false")));

	for (int32 PhaseIndex = 0; PhaseIndex < FWebToUEPerformanceSnapshot::PhaseCount; ++PhaseIndex)
	{
		const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(PhaseIndex);
		TArray<double> PhaseSamples;
		PhaseSamples.Reserve(Samples.Num());
		for (const FRuntimeUpdateSample& Sample : Samples)
		{
			PhaseSamples.Add(Sample.Snapshot.Get(Phase).GetInclusiveMilliseconds());
		}
		FWebToUEBenchmarkDistribution PhaseDistribution;
		TestTrue(*FString::Printf(TEXT("The runtime warm layout %s phase produces a distribution"),
			RuntimePhaseTelemetryNames[PhaseIndex]),
			FWebToUEBenchmarkStatistics::TryCalculate(PhaseSamples, PhaseDistribution));
		const FString P50Name = FString::Printf(TEXT("runtime.phase.%s.p50_ms"),
			RuntimePhaseTelemetryNames[PhaseIndex]);
		const FString P95Name = FString::Printf(TEXT("runtime.phase.%s.p95_ms"),
			RuntimePhaseTelemetryNames[PhaseIndex]);
		AddTelemetryData(*P50Name, PhaseDistribution.P50, SummaryContext);
		AddTelemetryData(*P95Name, PhaseDistribution.P95, SummaryContext);
		AddInfo(FString::Printf(TEXT("Runtime warm layout %s: p50_ms=%.6f,p95_ms=%.6f"),
			RuntimePhaseTelemetryNames[PhaseIndex], PhaseDistribution.P50, PhaseDistribution.P95));
	}

	RuntimeView->ReleaseSlateResources(true);
	return true;
}

bool FWebToUERuntimeUnchangedPaintBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Benchmark::Tests;

	SetTelemetryStorage(TEXT("WebToUEPerformance"));
	const FWebToUEBenchmarkEnvironment Environment = FWebToUEBenchmarkEnvironment::Capture();
	AddInfo(TEXT("Benchmark policy: ") + FWebToUEBenchmarkSamplingPolicy::ToLogString());
	AddInfo(TEXT("Benchmark environment: ") + Environment.ToLogString());
	TestTrue(TEXT("The runtime benchmark runs in Win64 Editor Development"),
		Environment.IsStandardConfiguration());

	const TArray<FWebToUEBenchmarkScenarioDefinition>& Definitions =
		FWebToUEBenchmarkScenarioGenerator::GetStandardDefinitions();
	TestTrue(TEXT("The medium benchmark scenario is available"), Definitions.IsValidIndex(1));
	if (!Definitions.IsValidIndex(1))
	{
		return false;
	}

	const FWebToUEBenchmarkScenario Scenario = FWebToUEBenchmarkScenarioGenerator::Generate(Definitions[1]);
	const FString TestDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(TestDirectory, TEXT("RuntimeUnchangedPaintBenchmark.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*TestFilename, false, true);
	};

	const FString Source = FString::Printf(TEXT("<style>%s</style>%s"), *Scenario.Css, *Scenario.Html);
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	const bool bImported = FFileHelper::SaveStringToFile(Source, *TestFilename) &&
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, false);
	TestTrue(TEXT("The medium runtime benchmark document imports"), bImported);
	if (!bImported)
	{
		return false;
	}

	TestEqual(TEXT("The runtime document has exactly 500 compiled nodes"),
		Document->GetCompiledNodes().Num(), Scenario.Definition.NodeCount);
	TestEqual(TEXT("The runtime document has exactly 200 compiled rules"),
		Document->GetCompiledRules().Num(), Scenario.Definition.RuleCount);
	int32 TextNodeCount = 0;
	TSet<int32> ParentNodeIndices;
	for (const FWebToUECompiledNode& Node : Document->GetCompiledNodes())
	{
		if (Node.Tag == TEXT("#text"))
		{
			++TextNodeCount;
		}
		if (Node.ParentIndex != INDEX_NONE)
		{
			ParentNodeIndices.Add(Node.ParentIndex);
		}
	}
	const int32 NonLeafNodeCount = ParentNodeIndices.Num();
	TestEqual(TEXT("The medium runtime document has exactly 249 paintable text nodes"),
		TextNodeCount, 249);
	TestEqual(TEXT("The medium runtime document has exactly 250 non-leaf nodes"),
		NonLeafNodeCount, 250);

	UWebToUEView* RuntimeView = NewObject<UWebToUEView>(GetTransientPackage());
	RuntimeView->SetDocument(Document);
	const TSharedRef<SWidget> SlateView = RuntimeView->TakeWidget();
	const bool bCanTick = SlateView->GetCanTick();
	TestFalse(TEXT("The unchanged runtime view has no default Tick"), bCanTick);

	const FVector2D ViewportSize(1280.0, 720.0);
	const FGeometry Geometry = FGeometry::MakeRoot(ViewportSize, FSlateLayoutTransform());
	PaintRuntimeView(SlateView, Geometry);

	TArray<FRuntimeUpdateSample> Samples;
	Samples.Reserve(FWebToUEBenchmarkSamplingPolicy::SampleCount);
	const int32 TotalIterations = FWebToUEBenchmarkSamplingPolicy::WarmupCount +
		FWebToUEBenchmarkSamplingPolicy::SampleCount;
	for (int32 Iteration = 0; Iteration < TotalIterations; ++Iteration)
	{
		FRuntimeUpdateSample Sample;
		FHittestGrid TimedHittestGrid;
		FSlateWindowElementList TimedDrawElements(nullptr);
		{
			FWebToUEPerformanceCapture Capture;
			const double StartSeconds = FPlatformTime::Seconds();
			PaintRuntimeView(SlateView, Geometry, TimedHittestGrid, TimedDrawElements);
			Sample.InclusiveMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			Sample.Snapshot = Capture.GetSnapshot();
		}

		if (Iteration < FWebToUEBenchmarkSamplingPolicy::WarmupCount)
		{
			continue;
		}

		const int32 SampleIndex = Samples.Num() + 1;
		const FString Prefix = FString::Printf(TEXT("Runtime unchanged paint sample %d: "), SampleIndex);
		TestEqual(*(Prefix + TEXT("performs one paint build")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::PaintBuild).CallCount, uint64(1));
		TestEqual(*(Prefix + TEXT("does not hydrate")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Hydrate).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not resolve styles")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Style).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not measure through Yoga")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Measure).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not lay out")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Layout).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not hit test")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::HitTest).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not refresh bindings")),
			Sample.Snapshot.Get(EWebToUEPerformancePhase::Binding).CallCount, uint64(0));
		TestEqual(*(Prefix + TEXT("does not hydrate nodes")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::HydratedNodes), uint64(0));
		TestEqual(*(Prefix + TEXT("does not hydrate rules")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::HydratedRules), uint64(0));
		TestEqual(*(Prefix + TEXT("does not visit style nodes")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(0));
		TestEqual(*(Prefix + TEXT("does not evaluate selectors")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorEvaluations), uint64(0));
		TestEqual(*(Prefix + TEXT("does not match selectors")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorMatches), uint64(0));
		TestEqual(*(Prefix + TEXT("does not rebuild Yoga nodes")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));
		TestEqual(*(Prefix + TEXT("reuses every text layout object")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutBuilds), uint64(0));
		TestEqual(*(Prefix + TEXT("reuses every warm Desired Size result for paint")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes),
			uint64(0));
		TestEqual(*(Prefix + TEXT("does not rebuild brushes")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::BrushBuilds), uint64(0));
		TestEqual(*(Prefix + TEXT("does not mark any WebToUE allocation")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocations), uint64(0));
		TestEqual(*(Prefix + TEXT("does not report an allocation payload event")),
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadEvents), uint64(0));
		const uint64 TrackedAllocationPayloadBytes =
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes);
		TestEqual(*(Prefix + TEXT("does not report allocation payload bytes")),
			TrackedAllocationPayloadBytes, uint64(0));
		TestTrue(*(Prefix + TEXT("records a positive end-to-end duration")),
			Sample.InclusiveMilliseconds > 0.0);

		const FString TelemetryContext = MakeRuntimeTelemetryContext(Environment,
			Scenario.Definition, TEXT("runtime_unchanged_paint_sample"), SampleIndex);
		AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
			TelemetryContext);
		AddTelemetryData(TEXT("budget.schema_version"), FWebToUEBenchmarkBudgetPolicy::SchemaVersion,
			TelemetryContext);
		AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
			TelemetryContext);
		AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
			TelemetryContext);
		AddTelemetryData(TEXT("sample.index"), SampleIndex, TelemetryContext);
		AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, TelemetryContext);
		AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, TelemetryContext);
		AddTelemetryData(TEXT("scenario.text_node_count"), TextNodeCount, TelemetryContext);
		AddTelemetryData(TEXT("scenario.non_leaf_node_count"), NonLeafNodeCount, TelemetryContext);
		AddTelemetryData(TEXT("runtime.unchanged_paint.tick_enabled"), bCanTick ? 1.0 : 0.0,
			TelemetryContext);
		AddTelemetryData(TEXT("runtime.unchanged_paint.inclusive_ms"), Sample.InclusiveMilliseconds,
			TelemetryContext);
		Sample.Snapshot.ForEachTelemetryMeasurement(
			[&](const TCHAR* Name, double Value)
			{
				AddTelemetryData(Name, Value, TelemetryContext);
			});
		Samples.Add(MoveTemp(Sample));
	}

	TestEqual(TEXT("The runtime unchanged paint benchmark records the policy sample count"),
		Samples.Num(), FWebToUEBenchmarkSamplingPolicy::SampleCount);
	if (Samples.Num() != FWebToUEBenchmarkSamplingPolicy::SampleCount)
	{
		RuntimeView->ReleaseSlateResources(true);
		return false;
	}

	TArray<double> InclusiveSamples;
	InclusiveSamples.Reserve(Samples.Num());
	uint64 MaximumTrackedAllocations = 0;
	uint64 MaximumTrackedAllocationPayloadEvents = 0;
	uint64 MinimumTrackedAllocationPayloadBytes = MAX_uint64;
	uint64 MaximumTrackedAllocationPayloadBytes = 0;
	for (const FRuntimeUpdateSample& Sample : Samples)
	{
		InclusiveSamples.Add(Sample.InclusiveMilliseconds);
		MaximumTrackedAllocations = FMath::Max(MaximumTrackedAllocations,
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocations));
		MaximumTrackedAllocationPayloadEvents = FMath::Max(MaximumTrackedAllocationPayloadEvents,
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadEvents));
		const uint64 TrackedAllocationPayloadBytes =
			Sample.Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes);
		MinimumTrackedAllocationPayloadBytes = FMath::Min(MinimumTrackedAllocationPayloadBytes,
			TrackedAllocationPayloadBytes);
		MaximumTrackedAllocationPayloadBytes = FMath::Max(MaximumTrackedAllocationPayloadBytes,
			TrackedAllocationPayloadBytes);
	}
	TestEqual(TEXT("Every unchanged-paint allocation event has known payload bytes"),
		MaximumTrackedAllocationPayloadEvents, MaximumTrackedAllocations);
	TestEqual(TEXT("The fixed Corpus produces a stable allocation payload byte count"),
		MinimumTrackedAllocationPayloadBytes, MaximumTrackedAllocationPayloadBytes);
	FWebToUEBenchmarkDistribution InclusiveDistribution;
	TestTrue(TEXT("The runtime unchanged paint duration produces a distribution"),
		FWebToUEBenchmarkStatistics::TryCalculate(InclusiveSamples, InclusiveDistribution));
	const bool bMeetsTarget = !bCanTick && MaximumTrackedAllocations <=
		FWebToUEBenchmarkBudgetPolicy::MediumUnchangedPaintMaximumTrackedAllocations &&
		MaximumTrackedAllocationPayloadBytes <=
		FWebToUEBenchmarkBudgetPolicy::MediumUnchangedPaintMaximumTrackedAllocationPayloadBytes;
	if (FWebToUEBenchmarkBudgetPolicy::bEnforceMediumUnchangedPaintBudget)
	{
		TestTrue(TEXT("The runtime unchanged-paint path stays within the enforced zero-allocation budget"),
			bMeetsTarget);
	}
	const FString SummaryContext = MakeRuntimeTelemetryContext(Environment,
		Scenario.Definition, TEXT("runtime_unchanged_paint_summary"));
	AddTelemetryData(TEXT("benchmark.schema_version"), FWebToUEBenchmarkSamplingPolicy::SchemaVersion,
		SummaryContext);
	AddTelemetryData(TEXT("budget.schema_version"), FWebToUEBenchmarkBudgetPolicy::SchemaVersion,
		SummaryContext);
	AddTelemetryData(TEXT("benchmark.warmup_count"), FWebToUEBenchmarkSamplingPolicy::WarmupCount,
		SummaryContext);
	AddTelemetryData(TEXT("benchmark.sample_count"), FWebToUEBenchmarkSamplingPolicy::SampleCount,
		SummaryContext);
	AddTelemetryData(TEXT("scenario.node_count"), Scenario.Definition.NodeCount, SummaryContext);
	AddTelemetryData(TEXT("scenario.rule_count"), Scenario.Definition.RuleCount, SummaryContext);
	AddTelemetryData(TEXT("scenario.text_node_count"), TextNodeCount, SummaryContext);
	AddTelemetryData(TEXT("scenario.non_leaf_node_count"), NonLeafNodeCount, SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.tick_enabled"), bCanTick ? 1.0 : 0.0, SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.p50_ms"), InclusiveDistribution.P50, SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.p95_ms"), InclusiveDistribution.P95, SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.maximum_tracked_allocations"),
		static_cast<double>(MaximumTrackedAllocations), SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.maximum_tracked_allocation_payload_events"),
		static_cast<double>(MaximumTrackedAllocationPayloadEvents), SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.minimum_tracked_allocation_payload_bytes"),
		static_cast<double>(MinimumTrackedAllocationPayloadBytes), SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.maximum_tracked_allocation_payload_bytes"),
		static_cast<double>(MaximumTrackedAllocationPayloadBytes), SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.tracked_allocation_payload_coverage"),
		MaximumTrackedAllocations > 0
			? static_cast<double>(MaximumTrackedAllocationPayloadEvents) / MaximumTrackedAllocations
			: 1.0,
		SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.target_maximum_tracked_allocations"),
		static_cast<double>(FWebToUEBenchmarkBudgetPolicy::MediumUnchangedPaintMaximumTrackedAllocations),
		SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.target_maximum_tracked_allocation_payload_bytes"),
		static_cast<double>(
			FWebToUEBenchmarkBudgetPolicy::MediumUnchangedPaintMaximumTrackedAllocationPayloadBytes),
		SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.target_enforced"),
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumUnchangedPaintBudget ? 1.0 : 0.0, SummaryContext);
	AddTelemetryData(TEXT("runtime.unchanged_paint.target_met"), bMeetsTarget ? 1.0 : 0.0, SummaryContext);
	AddInfo(FString::Printf(
		TEXT("Runtime unchanged paint: p50_ms=%.6f,p95_ms=%.6f,min_ms=%.6f,max_ms=%.6f,tick_enabled=%s,maximum_tracked_allocations=%llu,payload_events=%llu,payload_bytes=%llu,target_maximum_tracked_allocations=%llu,target_maximum_payload_bytes=%llu,target_enforced=%s,target_met=%s"),
		InclusiveDistribution.P50, InclusiveDistribution.P95, InclusiveDistribution.Minimum,
		InclusiveDistribution.Maximum, bCanTick ? TEXT("true") : TEXT("false"), MaximumTrackedAllocations,
		MaximumTrackedAllocationPayloadEvents, MaximumTrackedAllocationPayloadBytes,
		FWebToUEBenchmarkBudgetPolicy::MediumUnchangedPaintMaximumTrackedAllocations,
		FWebToUEBenchmarkBudgetPolicy::MediumUnchangedPaintMaximumTrackedAllocationPayloadBytes,
		FWebToUEBenchmarkBudgetPolicy::bEnforceMediumUnchangedPaintBudget ? TEXT("true") : TEXT("false"),
		bMeetsTarget ? TEXT("true") : TEXT("false")));

	for (int32 PhaseIndex = 0; PhaseIndex < FWebToUEPerformanceSnapshot::PhaseCount; ++PhaseIndex)
	{
		const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(PhaseIndex);
		TArray<double> PhaseSamples;
		PhaseSamples.Reserve(Samples.Num());
		for (const FRuntimeUpdateSample& Sample : Samples)
		{
			PhaseSamples.Add(Sample.Snapshot.Get(Phase).GetInclusiveMilliseconds());
		}
		FWebToUEBenchmarkDistribution PhaseDistribution;
		TestTrue(*FString::Printf(TEXT("The runtime unchanged paint %s phase produces a distribution"),
			RuntimePhaseTelemetryNames[PhaseIndex]),
			FWebToUEBenchmarkStatistics::TryCalculate(PhaseSamples, PhaseDistribution));
		const FString P50Name = FString::Printf(TEXT("runtime.phase.%s.p50_ms"),
			RuntimePhaseTelemetryNames[PhaseIndex]);
		const FString P95Name = FString::Printf(TEXT("runtime.phase.%s.p95_ms"),
			RuntimePhaseTelemetryNames[PhaseIndex]);
		AddTelemetryData(*P50Name, PhaseDistribution.P50, SummaryContext);
		AddTelemetryData(*P95Name, PhaseDistribution.P95, SummaryContext);
		AddInfo(FString::Printf(TEXT("Runtime unchanged paint %s: p50_ms=%.6f,p95_ms=%.6f"),
			RuntimePhaseTelemetryNames[PhaseIndex], PhaseDistribution.P50, PhaseDistribution.P95));
	}

	RuntimeView->ReleaseSlateResources(true);
	return true;
}

#endif
