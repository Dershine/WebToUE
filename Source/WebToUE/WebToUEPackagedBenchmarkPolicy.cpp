#include "WebToUEPackagedBenchmarkPolicy.h"

namespace WebToUE::PackagedBenchmark::Private
{
	static void Require(bool bCondition, const TCHAR* Failure, TArray<FString>& OutFailures)
	{
		if (!bCondition)
		{
			OutFailures.Emplace(Failure);
		}
	}
}

bool FWebToUEPackagedBenchmarkPolicy::ValidateWebToUEEvidence(
	const FWebToUEPackagedBenchmarkEvidence& Evidence,
	TArray<FString>& OutFailures)
{
	using namespace WebToUE::PackagedBenchmark::Private;
	OutFailures.Reset();
	Require(Evidence.CompiledNodeCount > 0,
		TEXT("compiled document has no nodes"), OutFailures);
	Require(Evidence.CompiledResourceCount >= 0 &&
		Evidence.CompiledResourceCount <= FrozenCorpusMaximumCompiledResources,
		TEXT("frozen corpus resource limit exceeded"), OutFailures);
	Require(Evidence.SetupHydratedNodes == static_cast<uint64>(Evidence.CompiledNodeCount),
		TEXT("primary view did not hydrate exactly one compiled document"), OutFailures);
	Require(Evidence.MeasurementTrajectorySteps > 0,
		TEXT("measurement window dispatched no trajectory steps"), OutFailures);
	Require(Evidence.MeasurementHydratedNodes == 0,
		TEXT("measurement window rehydrated the document"), OutFailures);
	const uint64 TrajectorySteps = static_cast<uint64>(
		FMath::Max(0, Evidence.MeasurementTrajectorySteps));
	Require(Evidence.MeasurementStyleNodeVisits <=
		TrajectorySteps * MaximumStyleNodeVisitsPerTrajectory,
		TEXT("measurement Style work exceeded the K=1 constant bound"), OutFailures);
	Require(Evidence.MeasurementSelectorEvaluations <=
		TrajectorySteps * MaximumSelectorEvaluationsPerTrajectory,
		TEXT("measurement selector work exceeded the K=1 constant bound"), OutFailures);
	Require(Evidence.MeasurementBindingNodesUpdated <=
		TrajectorySteps * MaximumBindingNodesUpdatedPerTrajectory,
		TEXT("measurement binding work exceeded the K=1 constant bound"), OutFailures);
	Require(Evidence.SetupResourceLoadAttempts == 0,
		TEXT("primary view performed a synchronous resource load"), OutFailures);
	Require(Evidence.SetupResourceFailures == 0,
		TEXT("primary view reported a resource failure"), OutFailures);
	Require(Evidence.MeasurementResourceLoadAttempts == 0,
		TEXT("measurement window performed a synchronous resource load"), OutFailures);
	Require(Evidence.MeasurementResourceAsyncRequests == 0,
		TEXT("measurement window started a resource request"), OutFailures);
	Require(Evidence.SecondViewHydratedNodes ==
		static_cast<uint64>(Evidence.CompiledNodeCount),
		TEXT("second view did not hydrate exactly one compiled document"), OutFailures);
	Require(Evidence.SecondViewResourceLoadAttempts == 0,
		TEXT("second view performed a synchronous resource load"), OutFailures);
	Require(Evidence.SecondViewResourceFailures == 0,
		TEXT("second view reported a resource failure"), OutFailures);
	Require(FMath::Max(0.0, Evidence.SecondViewRssDeltaMiB) <=
		MaximumSecondViewProcessMemoryDeltaMiB,
		TEXT("second view RSS delta exceeded the product bound"), OutFailures);
	if (Evidence.bLlmAvailable)
	{
		Require(FMath::Max(0.0, Evidence.SecondViewLlmDeltaMiB) <=
			MaximumSecondViewProcessMemoryDeltaMiB,
			TEXT("second view LLM delta exceeded the product bound"), OutFailures);
	}
	if (Evidence.bKnownOwnedCensusAvailable)
	{
		Require(Evidence.FirstViewKnownOwnedBytes > 0 &&
			Evidence.SecondViewKnownOwnedBytes > 0,
			TEXT("known-owned census is empty"), OutFailures);
		Require(Evidence.FirstViewSharedTemplateBytes > 0 &&
			Evidence.FirstViewSharedTemplateBytes ==
				Evidence.SecondViewSharedTemplateBytes,
			TEXT("second view did not retain the shared Style Template census"), OutFailures);
		const double MaximumSecondBytes = static_cast<double>(
			Evidence.FirstViewKnownOwnedBytes) * MaximumSecondViewKnownOwnedRatio;
		Require(static_cast<double>(Evidence.SecondViewKnownOwnedBytes) <= MaximumSecondBytes,
			TEXT("second view known-owned bytes exceeded the first-view bound"), OutFailures);
	}
	return OutFailures.IsEmpty();
}

bool FWebToUEPackagedBenchmarkPolicy::ValidateResourceSmokeEvidence(
	const FWebToUEPackagedBenchmarkEvidence& Evidence,
	TArray<FString>& OutFailures)
{
	using namespace WebToUE::PackagedBenchmark::Private;
	OutFailures.Reset();
	Require(Evidence.CompiledNodeCount > 0,
		TEXT("resource smoke document has no nodes"), OutFailures);
	Require(Evidence.CompiledResourceCount == ResourceSmokeExpectedCompiledResources,
		TEXT("resource smoke document does not own exactly one compiled resource"),
		OutFailures);
	Require(Evidence.SetupHydratedNodes == static_cast<uint64>(Evidence.CompiledNodeCount),
		TEXT("resource smoke primary view did not hydrate exactly one document"),
		OutFailures);
	Require(Evidence.SetupResourceLoadAttempts == 0 &&
		Evidence.WarmupResourceLoadAttempts == 0 &&
		Evidence.MeasurementResourceLoadAttempts == 0,
		TEXT("resource smoke performed a synchronous resource load"), OutFailures);
	Require(Evidence.SetupResourceFailures == 0 &&
		Evidence.WarmupResourceFailures == 0 &&
		Evidence.MeasurementResourceFailures == 0,
		TEXT("resource smoke reported a resource failure"), OutFailures);
	Require(Evidence.SetupResourceCancellations == 0 &&
		Evidence.WarmupResourceCancellations == 0 &&
		Evidence.MeasurementResourceCancellations == 0,
		TEXT("resource smoke cancelled its primary resource"), OutFailures);
	Require(Evidence.SetupResourceAsyncRequests + Evidence.SetupResourceCacheHits +
		Evidence.WarmupResourceAsyncRequests + Evidence.WarmupResourceCacheHits == 1,
		TEXT("resource smoke primary view did not consume exactly one resource"),
		OutFailures);
	Require(Evidence.SetupBrushBuilds + Evidence.WarmupBrushBuilds > 0,
		TEXT("resource smoke did not build a Slate brush"), OutFailures);
	Require(Evidence.MeasurementResourceAsyncRequests == 0,
		TEXT("resource smoke started a resource request in the measurement window"),
		OutFailures);
	Require(Evidence.SecondViewHydratedNodes ==
		static_cast<uint64>(Evidence.CompiledNodeCount),
		TEXT("resource smoke second view did not hydrate exactly one document"),
		OutFailures);
	Require(Evidence.SecondViewResourceLoadAttempts == 0,
		TEXT("resource smoke second view performed a synchronous resource load"),
		OutFailures);
	Require(Evidence.SecondViewResourceAsyncRequests == 0 &&
		Evidence.SecondViewResourceCacheHits == 1,
		TEXT("resource smoke second view did not reuse the resident resource"),
		OutFailures);
	Require(Evidence.SecondViewResourceFailures == 0 &&
		Evidence.SecondViewResourceCancellations == 0,
		TEXT("resource smoke second view did not retain a clean resource state"),
		OutFailures);
	if (Evidence.bDynamicMaterialParameterSmoke)
	{
		Require(Evidence.WarmupMaterialParameterLookups == 1 &&
			Evidence.WarmupMaterialParameterEvaluations == 1 &&
			Evidence.WarmupMaterialInstancesCreated == 1,
			TEXT("dynamic Material smoke did not create exactly one primary MID"),
			OutFailures);
		Require(Evidence.MeasurementMaterialParameterLookups == 1 &&
			Evidence.MeasurementMaterialParameterEvaluations == 1 &&
			Evidence.MeasurementMaterialInstancesReused == 1,
			TEXT("dynamic Material smoke did not perform one K=1 MID update"),
			OutFailures);
		Require(Evidence.MeasurementMaterialBrushPatches == 1 &&
			(Evidence.bCompositingSmoke
				? Evidence.MeasurementDisplayCommandsPatched >= 2
				: Evidence.MeasurementDisplayCommandsPatched == 1),
			Evidence.bCompositingSmoke
				? TEXT("compositing smoke did not cover the MID patch plus local hover patch")
				: TEXT("dynamic Material smoke did not patch only the affected brush/display command"),
			OutFailures);
		Require(Evidence.SecondViewMaterialInstancesCreated == 1,
			TEXT("dynamic Material smoke second View did not create an isolated MID"),
			OutFailures);
	}
	return OutFailures.IsEmpty();
}
