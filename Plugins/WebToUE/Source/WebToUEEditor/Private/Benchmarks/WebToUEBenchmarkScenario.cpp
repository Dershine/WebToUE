#include "Benchmarks/WebToUEBenchmarkScenario.h"

namespace WebToUE::Benchmark::Private
{
	static FString MakeElementId(int32 PairIndex)
	{
		if (PairIndex == 0)
		{
			return TEXT("benchmark-binding-target");
		}
		if (PairIndex == 1)
		{
			return TEXT("benchmark-hover-target");
		}
		return FString::Printf(TEXT("benchmark-node-%04d"), PairIndex);
	}

	static FString MakeGeneratedRule(int32 RuleIndex)
	{
		switch (RuleIndex % 5)
		{
		case 0:
			return FString::Printf(TEXT(".rule-%d { background-color: #%02x3040; }"),
				RuleIndex, 0x20 + (RuleIndex % 0x40));
		case 1:
			return FString::Printf(TEXT("button.rule-%d:hover { color: #%02x%02xff; }"),
				RuleIndex, 0x40 + (RuleIndex % 0x40), 0x60 + (RuleIndex % 0x40));
		case 2:
			return FString::Printf(TEXT("body > .rule-%d { margin-left: %dpx; }"), RuleIndex, RuleIndex % 4);
		case 3:
			return FString::Printf(TEXT("body .rule-%d { border-radius: %dpx; }"), RuleIndex, RuleIndex % 8);
		default:
			return FString::Printf(TEXT(".benchmark-item.rule-%d { font-size: %dpx; }"),
				RuleIndex, 12 + (RuleIndex % 5));
		}
	}
}

const TArray<FWebToUEBenchmarkScenarioDefinition>& FWebToUEBenchmarkScenarioGenerator::GetStandardDefinitions()
{
	static const TArray<FWebToUEBenchmarkScenarioDefinition> Definitions = {
		{ TEXT("Small_100Nodes_50Rules"), 100, 50 },
		{ TEXT("Medium_500Nodes_200Rules"), 500, 200 },
		{ TEXT("Stress_2000Nodes_500Rules"), 2000, 500 }
	};
	return Definitions;
}

FWebToUEBenchmarkScenario FWebToUEBenchmarkScenarioGenerator::Generate(
	const FWebToUEBenchmarkScenarioDefinition& Definition)
{
	checkf(Definition.NodeCount >= 5, TEXT("A benchmark scenario needs enough nodes for fixed interaction targets."));
	checkf(Definition.RuleCount >= 5, TEXT("A benchmark scenario needs enough rules for the representative base set."));

	FWebToUEBenchmarkScenario Result;
	Result.Definition = Definition;
	Result.Html.Reserve(Definition.NodeCount * 96);
	Result.Css.Reserve(Definition.RuleCount * 64);

	Result.Html = TEXT("<body id='benchmark-root' class='wtue-benchmark'>");
	int32 RemainingNodes = Definition.NodeCount - 1;
	int32 PairIndex = 0;
	int32 ElementOrdinal = 0;
	while (RemainingNodes >= 2)
	{
		const FString ElementId = WebToUE::Benchmark::Private::MakeElementId(PairIndex);
		const FString BindingAttribute = PairIndex == 0
			? TEXT(" data-ue-bind-text='BenchmarkLabel'")
			: FString();
		Result.Html += FString::Printf(
			TEXT("<button id='%s' class='benchmark-item bucket-%d rule-%d parity-%s' data-ue-on-click='BenchmarkClick'%s>Item %d</button>"),
			*ElementId,
			PairIndex % 32,
			ElementOrdinal % Definition.RuleCount,
			PairIndex % 2 == 0 ? TEXT("even") : TEXT("odd"),
			*BindingAttribute,
			PairIndex);
		RemainingNodes -= 2;
		++PairIndex;
		++ElementOrdinal;
	}
	if (RemainingNodes == 1)
	{
		Result.Html += FString::Printf(
			TEXT("<div id='benchmark-filler' class='benchmark-item bucket-%d rule-%d parity-even'></div>"),
			PairIndex % 32,
			ElementOrdinal % Definition.RuleCount);
	}
	Result.Html += TEXT("</body>");

	static const TCHAR* BaseRules[] = {
		TEXT("body { flex-direction: row; flex-wrap: wrap; align-items: flex-start; background-color: #101820; }"),
		TEXT(".benchmark-item { width: 96px; height: 32px; margin: 2px; color: #ffffff; background-color: #203040; }"),
		TEXT(".benchmark-item:hover { background-color: #305070; }"),
		TEXT("#benchmark-hover-target { border-width: 1px; border-color: #ffffff; }"),
		TEXT("body > .benchmark-item { flex-shrink: 0; }")
	};
	for (int32 RuleIndex = 0; RuleIndex < Definition.RuleCount; ++RuleIndex)
	{
		Result.Css += RuleIndex < UE_ARRAY_COUNT(BaseRules)
			? BaseRules[RuleIndex]
			: WebToUE::Benchmark::Private::MakeGeneratedRule(RuleIndex);
	}

	return Result;
}
