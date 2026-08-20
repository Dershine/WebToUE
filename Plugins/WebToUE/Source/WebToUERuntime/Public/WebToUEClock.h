#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Explicit UI time domains. Callers must not infer pause or dilation behavior. */
enum class EWebToUEClockDomain : uint8
{
	Game,
	Unscaled,
	Real,
	Test,
	Count
};

/** Injected, read-only time source used by one UI Session. */
class WEBTOUERUNTIME_API IWebToUEClock
{
public:
	virtual ~IWebToUEClock() = default;
	virtual bool SupportsDomain(EWebToUEClockDomain Domain) const = 0;
	virtual double GetTimeSeconds(EWebToUEClockDomain Domain) const = 0;
};

/**
 * Production World clock.
 *
 * Game is paused and dilated, Unscaled is paused but not dilated, and Real is neither paused nor
 * dilated. Test time is intentionally unavailable outside an injected virtual clock.
 */
class WEBTOUERUNTIME_API FWebToUEWorldClock final : public IWebToUEClock
{
public:
	explicit FWebToUEWorldClock(UWorld* InWorld) : World(InWorld) {}
	virtual bool SupportsDomain(EWebToUEClockDomain Domain) const override;
	virtual double GetTimeSeconds(EWebToUEClockDomain Domain) const override;

private:
	TWeakObjectPtr<UWorld> World;
};

/** Deterministic multi-domain clock for tests, tools and future virtual-time playback. */
class WEBTOUERUNTIME_API FWebToUEVirtualClock final : public IWebToUEClock
{
public:
	virtual bool SupportsDomain(EWebToUEClockDomain Domain) const override;
	virtual double GetTimeSeconds(EWebToUEClockDomain Domain) const override;

	bool SetTimeSeconds(EWebToUEClockDomain Domain, double TimeSeconds, FString& OutError);
	bool Advance(EWebToUEClockDomain Domain, double DeltaSeconds, FString& OutError);

private:
	static constexpr int32 DomainCount = static_cast<int32>(EWebToUEClockDomain::Count);
	double Times[DomainCount] = {};
};
