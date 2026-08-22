#pragma once

#include "CoreMinimal.h"

struct FWebToUEPackagedBenchmarkEvidence
{
	int32 CompiledNodeCount = 0;
	int32 CompiledResourceCount = 0;
	int32 MeasurementTrajectorySteps = 0;
	uint64 SetupHydratedNodes = 0;
	uint64 MeasurementHydratedNodes = 0;
	uint64 MeasurementStyleNodeVisits = 0;
	uint64 MeasurementSelectorEvaluations = 0;
	uint64 MeasurementBindingNodesUpdated = 0;
	uint64 SetupResourceLoadAttempts = 0;
	uint64 SetupResourceAsyncRequests = 0;
	uint64 SetupResourceCacheHits = 0;
	uint64 SetupResourceFailures = 0;
	uint64 SetupResourceCancellations = 0;
	uint64 SetupBrushBuilds = 0;
	uint64 WarmupResourceLoadAttempts = 0;
	uint64 WarmupResourceAsyncRequests = 0;
	uint64 WarmupResourceCacheHits = 0;
	uint64 WarmupResourceFailures = 0;
	uint64 WarmupResourceCancellations = 0;
	uint64 WarmupBrushBuilds = 0;
	uint64 MeasurementResourceLoadAttempts = 0;
	uint64 MeasurementResourceAsyncRequests = 0;
	uint64 MeasurementResourceFailures = 0;
	uint64 MeasurementResourceCancellations = 0;
	uint64 SecondViewHydratedNodes = 0;
	uint64 SecondViewResourceLoadAttempts = 0;
	uint64 SecondViewResourceAsyncRequests = 0;
	uint64 SecondViewResourceCacheHits = 0;
	uint64 SecondViewResourceFailures = 0;
	uint64 SecondViewResourceCancellations = 0;
	bool bDynamicMaterialParameterSmoke = false;
	bool bCompositingSmoke = false;
	uint64 WarmupMaterialParameterLookups = 0;
	uint64 WarmupMaterialParameterEvaluations = 0;
	uint64 WarmupMaterialInstancesCreated = 0;
	uint64 MeasurementMaterialParameterLookups = 0;
	uint64 MeasurementMaterialParameterEvaluations = 0;
	uint64 MeasurementMaterialInstancesReused = 0;
	uint64 MeasurementMaterialBrushPatches = 0;
	uint64 MeasurementDisplayCommandsPatched = 0;
	uint64 SecondViewMaterialInstancesCreated = 0;
	double SecondViewRssDeltaMiB = 0.0;
	double SecondViewLlmDeltaMiB = 0.0;
	bool bLlmAvailable = false;
	bool bKnownOwnedCensusAvailable = false;
	uint64 FirstViewKnownOwnedBytes = 0;
	uint64 SecondViewKnownOwnedBytes = 0;
	uint64 FirstViewSharedTemplateBytes = 0;
	uint64 SecondViewSharedTemplateBytes = 0;
};

/** Frozen PersonalGame M2 exit policy for raw packaged benchmark evidence. */
struct FWebToUEPackagedBenchmarkPolicy
{
	static constexpr int32 ResultSchemaVersion = 6;
	static constexpr int32 FrozenCorpusMaximumCompiledResources = 0;
	static constexpr int32 ResourceSmokeExpectedCompiledResources = 1;
	static constexpr uint64 MaximumStyleNodeVisitsPerTrajectory = 4;
	static constexpr uint64 MaximumSelectorEvaluationsPerTrajectory = 16;
	static constexpr uint64 MaximumBindingNodesUpdatedPerTrajectory = 2;
	static constexpr double MaximumSecondViewProcessMemoryDeltaMiB = 32.0;
	static constexpr double MaximumSecondViewKnownOwnedRatio = 1.10;

	static bool ValidateWebToUEEvidence(
		const FWebToUEPackagedBenchmarkEvidence& Evidence,
		TArray<FString>& OutFailures);
	static bool ValidateResourceSmokeEvidence(
		const FWebToUEPackagedBenchmarkEvidence& Evidence,
		TArray<FString>& OutFailures);
};
