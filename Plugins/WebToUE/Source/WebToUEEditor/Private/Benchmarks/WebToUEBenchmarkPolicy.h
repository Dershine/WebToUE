#pragma once

#include "Containers/ArrayView.h"
#include "CoreMinimal.h"

struct FWebToUEBenchmarkSamplingPolicy
{
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 WarmupCount = 1;
	static constexpr int32 SampleCount = 20;

	static const TCHAR* GetPercentileMethod();
	static FString ToLogString();
};

struct FWebToUEBenchmarkBudgetPolicy
{
	static constexpr int32 SchemaVersion = 7;
	static constexpr double MediumSingleNodeHoverP95Milliseconds = 0.5;
	static constexpr double MediumSingleFieldNotifyP95Milliseconds = 0.5;
	static constexpr double MediumWarmFullLayoutP95Milliseconds = 2.0;
	static constexpr uint64 MediumUnchangedPaintMaximumTrackedAllocations = 0;
	static constexpr uint64 MediumUnchangedPaintMaximumTrackedAllocationPayloadBytes = 0;

	static constexpr bool bEnforceMediumSingleNodeHoverBudget = true;
	static constexpr bool bEnforceMediumSingleFieldNotifyBudget = false;
	static constexpr bool bEnforceMediumWarmFullLayoutBudget = true;
	static constexpr bool bEnforceMediumUnchangedPaintBudget = true;
};

struct FWebToUEBenchmarkEnvironment
{
	FString EngineVersion;
	FString BuildConfiguration;
	FString Platform;
	FString OSVersion;
	FString OSSubVersion;
	FString CPUBrand;
	FString GPUBrand;
	int32 PhysicalCoreCount = 0;
	int32 LogicalCoreCount = 0;
	uint32 PhysicalMemoryGB = 0;

	static FWebToUEBenchmarkEnvironment Capture();

	bool IsStandardConfiguration() const;
	FString GetFingerprint() const;
	FString ToLogString() const;
};

struct FWebToUEBenchmarkDistribution
{
	double Minimum = 0.0;
	double P50 = 0.0;
	double P95 = 0.0;
	double Maximum = 0.0;
};

class FWebToUEBenchmarkStatistics
{
public:
	static bool TryCalculate(TConstArrayView<double> Samples, FWebToUEBenchmarkDistribution& OutDistribution);
};
