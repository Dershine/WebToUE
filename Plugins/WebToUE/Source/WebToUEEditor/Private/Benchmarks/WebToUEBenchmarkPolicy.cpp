#include "Benchmarks/WebToUEBenchmarkPolicy.h"

#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "Misc/App.h"
#include "Misc/Crc.h"
#include "Misc/EngineVersion.h"

const TCHAR* FWebToUEBenchmarkSamplingPolicy::GetPercentileMethod()
{
	return TEXT("p50=median;p95=nearest-rank");
}

FString FWebToUEBenchmarkSamplingPolicy::ToLogString()
{
	return FString::Printf(TEXT("schema=%d,warmup=%d,samples=%d,percentiles=%s"),
		SchemaVersion, WarmupCount, SampleCount, GetPercentileMethod());
}

FWebToUEBenchmarkEnvironment FWebToUEBenchmarkEnvironment::Capture()
{
	FWebToUEBenchmarkEnvironment Result;
	Result.EngineVersion = FEngineVersion::Current().ToString();
	Result.BuildConfiguration = LexToString(FApp::GetBuildConfiguration());
	Result.Platform = ANSI_TO_TCHAR(FPlatformProperties::PlatformName());
	FPlatformMisc::GetOSVersions(Result.OSVersion, Result.OSSubVersion);
	Result.CPUBrand = FPlatformMisc::GetCPUBrand().TrimStartAndEnd();
	Result.GPUBrand = FPlatformMisc::GetPrimaryGPUBrand().TrimStartAndEnd();
	Result.PhysicalCoreCount = FPlatformMisc::NumberOfCores();
	Result.LogicalCoreCount = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
	Result.PhysicalMemoryGB = FPlatformMemory::GetPhysicalGBRam();
	return Result;
}

bool FWebToUEBenchmarkEnvironment::IsStandardConfiguration() const
{
	return BuildConfiguration == TEXT("Development") && Platform == TEXT("WindowsEditor");
}

FString FWebToUEBenchmarkEnvironment::GetFingerprint() const
{
	const FString Canonical = FString::Printf(TEXT("engine=%s|configuration=%s|platform=%s|os=%s|os_sub=%s|cpu=%s|gpu=%s|physical_cores=%d|logical_cores=%d|memory_gb=%u"),
		*EngineVersion, *BuildConfiguration, *Platform, *OSVersion, *OSSubVersion, *CPUBrand, *GPUBrand,
		PhysicalCoreCount, LogicalCoreCount, PhysicalMemoryGB);
	return FString::Printf(TEXT("%08X"), FCrc::StrCrc32(*Canonical));
}

FString FWebToUEBenchmarkEnvironment::ToLogString() const
{
	return FString::Printf(TEXT("fingerprint=%s,engine=%s,configuration=%s,platform=%s,os=%s (%s),cpu=%s,gpu=%s,cores=%d/%d,memory_gb=%u"),
		*GetFingerprint(), *EngineVersion, *BuildConfiguration, *Platform, *OSVersion, *OSSubVersion,
		*CPUBrand, *GPUBrand, PhysicalCoreCount, LogicalCoreCount, PhysicalMemoryGB);
}

bool FWebToUEBenchmarkStatistics::TryCalculate(
	TConstArrayView<double> Samples, FWebToUEBenchmarkDistribution& OutDistribution)
{
	if (Samples.IsEmpty())
	{
		return false;
	}

	TArray<double> SortedSamples;
	SortedSamples.Append(Samples.GetData(), Samples.Num());
	for (const double Sample : SortedSamples)
	{
		if (!FMath::IsFinite(Sample) || Sample < 0.0)
		{
			return false;
		}
	}
	SortedSamples.Sort();

	const int32 SampleCount = SortedSamples.Num();
	const int32 MiddleIndex = SampleCount / 2;
	OutDistribution.Minimum = SortedSamples[0];
	OutDistribution.P50 = SampleCount % 2 == 0
		? (SortedSamples[MiddleIndex - 1] + SortedSamples[MiddleIndex]) * 0.5
		: SortedSamples[MiddleIndex];
	const int32 P95Index = FMath::Clamp(FMath::CeilToInt(0.95 * SampleCount) - 1, 0, SampleCount - 1);
	OutDistribution.P95 = SortedSamples[P95Index];
	OutDistribution.Maximum = SortedSamples.Last();
	return true;
}
