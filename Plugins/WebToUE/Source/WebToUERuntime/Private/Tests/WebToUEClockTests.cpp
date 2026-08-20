#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEClock.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEClockDomainsTest,
	"WebToUE.Runtime.ClockDomains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEClockDomainsTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
	FString Error;
	TestTrue(TEXT("Virtual Clock exposes every deterministic UI time domain"),
		Clock->SupportsDomain(EWebToUEClockDomain::Game) &&
		Clock->SupportsDomain(EWebToUEClockDomain::Unscaled) &&
		Clock->SupportsDomain(EWebToUEClockDomain::Real) &&
		Clock->SupportsDomain(EWebToUEClockDomain::Test));
	TestTrue(TEXT("Virtual Clock domains advance independently"),
		Clock->SetTimeSeconds(EWebToUEClockDomain::Game, 3.0, Error) &&
		Clock->SetTimeSeconds(EWebToUEClockDomain::Unscaled, 5.0, Error) &&
		Clock->SetTimeSeconds(EWebToUEClockDomain::Real, 7.0, Error) &&
		Clock->SetTimeSeconds(EWebToUEClockDomain::Test, 11.0, Error) &&
		Clock->Advance(EWebToUEClockDomain::Test, 2.5, Error));
	TestTrue(TEXT("Independent domains preserve their exact deterministic values"),
		FMath::IsNearlyEqual(Clock->GetTimeSeconds(EWebToUEClockDomain::Game), 3.0) &&
		FMath::IsNearlyEqual(Clock->GetTimeSeconds(EWebToUEClockDomain::Unscaled), 5.0) &&
		FMath::IsNearlyEqual(Clock->GetTimeSeconds(EWebToUEClockDomain::Real), 7.0) &&
		FMath::IsNearlyEqual(Clock->GetTimeSeconds(EWebToUEClockDomain::Test), 13.5));
	TestFalse(TEXT("Virtual Clock rejects time travel"),
		Clock->SetTimeSeconds(EWebToUEClockDomain::Test, 12.0, Error));
	TestTrue(TEXT("Rejected time travel is diagnosable"), Error.Contains(TEXT("monotonic")));
	TestFalse(TEXT("Virtual Clock rejects a negative advance"),
		Clock->Advance(EWebToUEClockDomain::Test, -1.0, Error));

	UWorld* World = NewObject<UWorld>(GetTransientPackage());
	World->AddToRoot();
	const FWebToUEWorldClock WorldClock(World);
	TestTrue(TEXT("World Clock exposes Game, pause-aware Unscaled and Real domains"),
		WorldClock.SupportsDomain(EWebToUEClockDomain::Game) &&
		WorldClock.SupportsDomain(EWebToUEClockDomain::Unscaled) &&
		WorldClock.SupportsDomain(EWebToUEClockDomain::Real));
	TestFalse(TEXT("Production World Clock never impersonates deterministic Test time"),
		WorldClock.SupportsDomain(EWebToUEClockDomain::Test));
	World->RemoveFromRoot();
	return true;
}

#endif
