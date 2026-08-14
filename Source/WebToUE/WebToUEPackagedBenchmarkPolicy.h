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
	uint64 SetupResourceFailures = 0;
	uint64 MeasurementResourceLoadAttempts = 0;
	uint64 MeasurementResourceAsyncRequests = 0;
	uint64 SecondViewHydratedNodes = 0;
	uint64 SecondViewResourceLoadAttempts = 0;
	uint64 SecondViewResourceFailures = 0;
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
	static constexpr uint64 MaximumStyleNodeVisitsPerTrajectory = 4;
	static constexpr uint64 MaximumSelectorEvaluationsPerTrajectory = 16;
	static constexpr uint64 MaximumBindingNodesUpdatedPerTrajectory = 2;
	static constexpr double MaximumSecondViewProcessMemoryDeltaMiB = 32.0;
	static constexpr double MaximumSecondViewKnownOwnedRatio = 1.10;

	static bool ValidateWebToUEEvidence(
		const FWebToUEPackagedBenchmarkEvidence& Evidence,
		TArray<FString>& OutFailures);
};
