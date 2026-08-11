#if WITH_DEV_AUTOMATION_TESTS

#include "Benchmarks/WebToUEBenchmarkScenario.h"
#include "Misc/AutomationTest.h"
#include "WebToUECompiler.h"
#include "WebToUEPerformance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEBenchmarkScenarioTest, "WebToUE.Editor.BenchmarkScenarios",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEBenchmarkScenarioTest::RunTest(const FString& Parameters)
{
	SetTelemetryStorage(TEXT("WebToUEPerformance"));

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

		FWebToUEPerformanceSnapshot PerformanceSnapshot;
		const TSharedRef<FWebToUEDocument> Document = [&]()
		{
			FWebToUEPerformanceCapture Capture;
			const TSharedRef<FWebToUEDocument> Compiled = FWebToUECompiler::Compile(
				First.Html, First.Css, First.Definition.Name + TEXT(".html"));
			PerformanceSnapshot = Capture.GetSnapshot();
			return Compiled;
		}();
		AddInfo(Prefix + PerformanceSnapshot.ToLogString());

		const FString TelemetryContext = FString::Printf(TEXT("schema=%d;scenario=%s"),
			FWebToUEPerformanceSnapshot::TelemetrySchemaVersion, *Definitions[ScenarioIndex].Name);
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
		TestEqual(*(Prefix + TEXT("emits every snapshot telemetry measurement")),
			TelemetryMeasurementCount, FWebToUEPerformanceSnapshot::TelemetryMeasurementCount);

		TestEqual(*(Prefix + TEXT("visits every style node exactly once")),
			PerformanceSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits),
			static_cast<uint64>(First.Definition.NodeCount));
		TestEqual(*(Prefix + TEXT("evaluates every rule for every node")),
			PerformanceSnapshot.GetCounter(EWebToUEPerformanceCounter::SelectorEvaluations),
			static_cast<uint64>(First.Definition.NodeCount) * static_cast<uint64>(First.Definition.RuleCount));
		TestTrue(*(Prefix + TEXT("records successful selector matches")),
			PerformanceSnapshot.GetCounter(EWebToUEPerformanceCounter::SelectorMatches) > 0);
		TestFalse(*(Prefix + TEXT("compiles without errors")), Document->HasErrors());
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
