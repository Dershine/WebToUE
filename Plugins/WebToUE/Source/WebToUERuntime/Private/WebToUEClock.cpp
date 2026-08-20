#include "WebToUEClock.h"

#include "Engine/World.h"

namespace WebToUE::Clock::Private
{
	static bool IsValidDomain(EWebToUEClockDomain Domain)
	{
		return static_cast<uint8>(Domain) < static_cast<uint8>(EWebToUEClockDomain::Count);
	}
}

bool FWebToUEWorldClock::SupportsDomain(EWebToUEClockDomain Domain) const
{
	return World.IsValid() && Domain != EWebToUEClockDomain::Test &&
		WebToUE::Clock::Private::IsValidDomain(Domain);
}

double FWebToUEWorldClock::GetTimeSeconds(EWebToUEClockDomain Domain) const
{
	const UWorld* ResolvedWorld = World.Get();
	if (!ResolvedWorld)
	{
		return 0.0;
	}

	switch (Domain)
	{
	case EWebToUEClockDomain::Game:
		return ResolvedWorld->GetTimeSeconds();
	case EWebToUEClockDomain::Unscaled:
		return ResolvedWorld->GetAudioTimeSeconds();
	case EWebToUEClockDomain::Real:
		return ResolvedWorld->GetRealTimeSeconds();
	case EWebToUEClockDomain::Test:
	case EWebToUEClockDomain::Count:
	default:
		return 0.0;
	}
}

bool FWebToUEVirtualClock::SupportsDomain(EWebToUEClockDomain Domain) const
{
	return WebToUE::Clock::Private::IsValidDomain(Domain);
}

double FWebToUEVirtualClock::GetTimeSeconds(EWebToUEClockDomain Domain) const
{
	return SupportsDomain(Domain) ? Times[static_cast<int32>(Domain)] : 0.0;
}

bool FWebToUEVirtualClock::SetTimeSeconds(
	EWebToUEClockDomain Domain, double TimeSeconds, FString& OutError)
{
	OutError.Reset();
	if (!SupportsDomain(Domain))
	{
		OutError = TEXT("Virtual UI Clock requires a valid time domain.");
		return false;
	}
	if (!FMath::IsFinite(TimeSeconds) || TimeSeconds < GetTimeSeconds(Domain))
	{
		OutError = TEXT("Virtual UI Clock time must be finite and monotonic.");
		return false;
	}
	Times[static_cast<int32>(Domain)] = TimeSeconds;
	return true;
}

bool FWebToUEVirtualClock::Advance(
	EWebToUEClockDomain Domain, double DeltaSeconds, FString& OutError)
{
	OutError.Reset();
	if (!SupportsDomain(Domain) || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0)
	{
		OutError = TEXT("Virtual UI Clock advance requires a valid domain and finite non-negative delta.");
		return false;
	}
	const double Advanced = GetTimeSeconds(Domain) + DeltaSeconds;
	if (!FMath::IsFinite(Advanced))
	{
		OutError = TEXT("Virtual UI Clock advance overflowed its finite time range.");
		return false;
	}
	Times[static_cast<int32>(Domain)] = Advanced;
	return true;
}
