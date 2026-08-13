#pragma once

#include "CoreMinimal.h"

struct FWebToUEBenchmarkScenarioDefinition
{
	FString Name;
	int32 NodeCount = 0;
	int32 RuleCount = 0;
};

struct FWebToUEBenchmarkScenario
{
	FWebToUEBenchmarkScenarioDefinition Definition;
	FString Html;
	FString Css;
};

class FWebToUEBenchmarkScenarioGenerator
{
public:
	static const TArray<FWebToUEBenchmarkScenarioDefinition>& GetStandardDefinitions();
	static const TArray<FWebToUEBenchmarkScenarioDefinition>& GetHydrationDefinitions();
	static FWebToUEBenchmarkScenario Generate(const FWebToUEBenchmarkScenarioDefinition& Definition);
};
