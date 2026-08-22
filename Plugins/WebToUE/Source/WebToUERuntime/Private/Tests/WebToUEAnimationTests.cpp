#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEAnimation.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "WebToUESession.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEAnimationIRContractTest,
	"WebToUE.Runtime.AnimationIRContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEAnimationTrackClockTest,
	"WebToUE.Runtime.AnimationTrackClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEAnimationLeaseLifecycleTest,
	"WebToUE.Runtime.AnimationLeaseLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::Animation::Tests
{
	class FRecordingTarget final : public IWebToUEAnimationTarget
	{
	public:
		explicit FRecordingTarget(FWebToUEInstanceHandle InExpected)
			: Expected(InExpected)
		{
		}

		virtual bool ValidateAnimationTarget(
			FWebToUEInstanceHandle Target,
			const FWebToUEPropertyAddress& Address,
			EWebToUEAnimationValueType ValueType,
			FString& OutError) const override
		{
			OutError.Reset();
			if (Target != Expected || !Address.IsValid() ||
				!FWebToUEPropertyOwnershipPolicy::IsAnimationTarget(Address) ||
				ValueType != EWebToUEAnimationValueType::Scalar)
			{
				OutError = TEXT("recording target rejected the handle, address, or value type");
				return false;
			}
			return true;
		}

		virtual void ApplyAnimationOverlay(
			FWebToUEInstanceHandle Target,
			const FWebToUEPropertyAddress& Address,
			const FWebToUEAnimationValue& Value) override
		{
			check(Target == Expected);
			Overlay = Value.Scalar;
			Applied.Add(Value.Scalar);
		}

		virtual void ReleaseAnimationOverlay(
			FWebToUEInstanceHandle Target,
			const FWebToUEPropertyAddress& Address) override
		{
			check(Target == Expected);
			Overlay.Reset();
			++ReleaseCount;
		}

		float GetEffectiveScalar() const
		{
			return Overlay.IsSet() ? Overlay.GetValue() : Underlying;
		}

		FWebToUEInstanceHandle Expected;
		TOptional<float> Overlay;
		TArray<float> Applied;
		float Underlying = 0.0f;
		int32 ReleaseCount = 0;
	};

	struct FFixture
	{
		FWebToUEInstanceHandle TargetHandle =
			FWebToUEInstanceHandle::Create(9001, 1, 7);
		TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
		TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe> Updates =
			FWebToUEUpdateCoordinator::Create();
		TSharedRef<FRecordingTarget> Target = MakeShared<FRecordingTarget>(TargetHandle);
		TSharedRef<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe> Animation;

		explicit FFixture(const FWebToUEAnimationBudget& Budget = {})
			: Animation(FWebToUEAnimationCoordinator::Create(
				{ 5001, 1 }, Clock, Updates, Budget))
		{
		}

		FWebToUEAnimationTrackRequest Request(
			float From, float To, double Duration, FName Name = TEXT("test.opacity"))
		{
			FWebToUEAnimationTrackRequest Result;
			Result.DebugName = Name;
			Result.Target = TargetHandle;
			Result.Address =
				FWebToUEPropertyAddress::Css(EWebToUECssProperty::Opacity);
			Result.From = FWebToUEAnimationValue::MakeScalar(From);
			Result.To = FWebToUEAnimationValue::MakeScalar(To);
			Result.DurationSeconds = Duration;
			Result.ClockDomain = EWebToUEClockDomain::Test;
			Result.TargetAdapter = Target;
			return Result;
		}
	};

	static bool HasCode(
		const TArray<FWebToUEAnimationDiagnostic>& Diagnostics,
		const TCHAR* Code)
	{
		return Diagnostics.ContainsByPredicate([Code](
			const FWebToUEAnimationDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
	}
}

bool FWebToUEAnimationIRContractTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Animation::Tests;
	FWebToUECompiledAnimationIR IR;
	FWebToUECompiledAnimationTrack Track;
	Track.TrackId = TEXT("webtoue.test.opacity");
	Track.Target.TargetNodeIndex = 0;
	Track.Target.Kind = EWebToUECompiledAnimationTargetKind::Opacity;
	Track.From = FWebToUEAnimationValue::MakeScalar(0.0f);
	Track.To = FWebToUEAnimationValue::MakeScalar(1.0f);
	Track.DurationSeconds = 0.25;
	Track.ClockDomain = EWebToUEClockDomain::Test;
	IR.Tracks.Add(Track);
	TArray<FWebToUEAnimationDiagnostic> Diagnostics;
	TestTrue(TEXT("Animation IR 1.0 accepts a finite typed opacity Track"),
		IR.Validate(1, Diagnostics));
	TestEqual(TEXT("Animation IR declares its independent major version"),
		IR.Version.Major, uint16(1));

	FWebToUECompiledAnimationIR Invalid = IR;
	Invalid.Version = { 2, 0 };
	Invalid.Tracks.Add(Track);
	Invalid.Tracks[1].Target.TargetNodeIndex = 4;
	Invalid.Tracks[1].From = FWebToUEAnimationValue::MakeColor(FLinearColor::White);
	Invalid.Tracks[1].DurationSeconds = 0.0;
	TestFalse(TEXT("Animation IR fails closed on version, identity, target, type, and duration"),
		Invalid.Validate(1, Diagnostics));
	TestTrue(TEXT("Version failures use the stable Animation code"),
		HasCode(Diagnostics, TEXT("WTUE-ANI-001")));
	TestTrue(TEXT("Identity and target failures use the stable Animation code"),
		HasCode(Diagnostics, TEXT("WTUE-ANI-002")));
	TestTrue(TEXT("Value and duration failures use the stable Animation code"),
		HasCode(Diagnostics, TEXT("WTUE-ANI-003")));
	FWebToUEAnimationBudget StarvingBudget;
	StarvingBudget.MaxActiveTracks = 2;
	StarvingBudget.MaxSamplesPerPump = 1;
	FString BudgetError;
	TestFalse(TEXT("Animation budgets cannot starve an accepted active Track"),
		StarvingBudget.Validate(BudgetError));
	return true;
}

bool FWebToUEAnimationTrackClockTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Animation::Tests;
	FFixture Fixture;
	FString Error;
	Fixture.Clock->SetTimeSeconds(EWebToUEClockDomain::Test, 10.0, Error);
	TestFalse(TEXT("A Session with no Track owns no animation ticker"),
		Fixture.Animation->IsTickerRegistered());
	TestEqual(TEXT("Idle animation kernel has performed zero ticker invocations"),
		Fixture.Animation->GetTickerInvocationCount(), uint64(0));

	const FWebToUEAnimationStartOutcome Started = Fixture.Animation->StartTrack(
		Fixture.Request(0.0f, 10.0f, 4.0),
		EWebToUEAnimationConflictPolicy::Reject);
	TestTrue(TEXT("A valid Track acquires one active lease"),
		Started.Result == EWebToUEAnimationStartResult::Started &&
		Fixture.Animation->GetActiveTrackCount() == 1 &&
		Fixture.Animation->IsTickerRegistered());
	TestTrue(TEXT("Track start publishes the exact From overlay"),
		Fixture.Target->Overlay.IsSet() &&
		FMath::IsNearlyEqual(Fixture.Target->Overlay.GetValue(), 0.0f));

	Fixture.Clock->Advance(EWebToUEClockDomain::Test, 1.0, Error);
	TestEqual(TEXT("A Pump samples exactly one active Track"),
		Fixture.Animation->Pump(), 1);
	TestTrue(TEXT("Virtual Clock samples the exact linear quarter point"),
		FMath::IsNearlyEqual(Fixture.Target->Overlay.GetValue(), 2.5f));

	Fixture.Clock->Advance(EWebToUEClockDomain::Test, 3.0, Error);
	Fixture.Animation->Pump();
	TestFalse(TEXT("Terminal sample releases the active-only overlay"),
		Fixture.Target->Overlay.IsSet());
	TestTrue(TEXT("Terminal sample applies the exact To value before release"),
		!Fixture.Target->Applied.IsEmpty() &&
		FMath::IsNearlyEqual(Fixture.Target->Applied.Last(), 10.0f));
	TestTrue(TEXT("Completion removes both Track state and ticker"),
		Fixture.Animation->GetActiveTrackCount() == 0 &&
		!Fixture.Animation->IsTickerRegistered());
	TestEqual(TEXT("Explicit virtual Pumps do not invent frame ticker work"),
		Fixture.Animation->GetTickerInvocationCount(), uint64(0));
	return true;
}

bool FWebToUEAnimationLeaseLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Animation::Tests;
	FWebToUEAnimationBudget Budget;
	Budget.MaxTraceEntries = 6;
	FFixture Fixture(Budget);
	FString Error;
	Fixture.Clock->SetTimeSeconds(EWebToUEClockDomain::Test, 20.0, Error);
	const FWebToUEAnimationStartOutcome Initial = Fixture.Animation->StartTrack(
		Fixture.Request(0.0f, 10.0f, 4.0, TEXT("initial")),
		EWebToUEAnimationConflictPolicy::Reject);
	Fixture.Clock->Advance(EWebToUEClockDomain::Test, 1.0, Error);
	Fixture.Animation->Pump();

	FWebToUEAnimationTrackRequest Retarget =
		Fixture.Request(-100.0f, 20.0f, 3.0, TEXT("retarget"));
	const FWebToUEAnimationStartOutcome Retargeted =
		Fixture.Animation->StartTrack(
			Retarget, EWebToUEAnimationConflictPolicy::Retarget);
	TestTrue(TEXT("Retarget starts from the current sampled overlay, not caller From"),
		Retargeted.Result == EWebToUEAnimationStartResult::Retargeted &&
		FMath::IsNearlyEqual(Fixture.Target->Overlay.GetValue(), 2.5f));
	TestTrue(TEXT("Superseded Track handles cannot cancel the replacement lease"),
		Fixture.Animation->Cancel(Initial.Handle) ==
			EWebToUEAnimationCancelResult::DroppedSuperseded);

	Fixture.Clock->Advance(EWebToUEClockDomain::Test, 1.5, Error);
	Fixture.Animation->Pump();
	TestTrue(TEXT("Retargeted Track remains deterministic at its midpoint"),
		FMath::IsNearlyEqual(Fixture.Target->Overlay.GetValue(), 11.25f));
	const FWebToUEAnimationStartOutcome Replaced = Fixture.Animation->StartTrack(
		Fixture.Request(100.0f, 200.0f, 2.0, TEXT("replace")),
		EWebToUEAnimationConflictPolicy::Replace);
	TestTrue(TEXT("Replace uses its explicit From endpoint"),
		Replaced.Result == EWebToUEAnimationStartResult::Replaced &&
		FMath::IsNearlyEqual(Fixture.Target->Overlay.GetValue(), 100.0f));
	Fixture.Target->Underlying = 77.0f;
	TestTrue(TEXT("Explicit Cancel releases the lease"),
		Fixture.Animation->Cancel(Replaced.Handle) ==
			EWebToUEAnimationCancelResult::Cancelled);
	TestTrue(TEXT("Release reveals the latest underlying value, not a cached start value"),
		FMath::IsNearlyEqual(Fixture.Target->GetEffectiveScalar(), 77.0f));

	const FWebToUEAnimationStartOutcome AdapterOriginal =
		Fixture.Animation->StartTrack(
			Fixture.Request(2.0f, 3.0f, 2.0, TEXT("adapter-original")),
			EWebToUEAnimationConflictPolicy::Reject);
	const TSharedRef<FRecordingTarget> ReplacementTarget =
		MakeShared<FRecordingTarget>(Fixture.TargetHandle);
	FWebToUEAnimationTrackRequest AdapterReplacement =
		Fixture.Request(50.0f, 60.0f, 2.0, TEXT("adapter-replacement"));
	AdapterReplacement.TargetAdapter = ReplacementTarget;
	const FWebToUEAnimationStartOutcome AdapterSwapped =
		Fixture.Animation->StartTrack(
			AdapterReplacement, EWebToUEAnimationConflictPolicy::Replace);
	TestTrue(TEXT("Adapter replacement releases the superseded overlay lease"),
		AdapterOriginal.IsAccepted() && AdapterSwapped.IsAccepted() &&
		!Fixture.Target->Overlay.IsSet() &&
		ReplacementTarget->Overlay.IsSet() &&
		FMath::IsNearlyEqual(ReplacementTarget->Overlay.GetValue(), 50.0f));
	TestTrue(TEXT("Replacement adapter owns the cancellable lease"),
		Fixture.Animation->Cancel(AdapterSwapped.Handle) ==
			EWebToUEAnimationCancelResult::Cancelled &&
		!ReplacementTarget->Overlay.IsSet());

	const FWebToUEAnimationStartOutcome GenerationTrack =
		Fixture.Animation->StartTrack(
			Fixture.Request(1.0f, 2.0f, 5.0, TEXT("generation")),
			EWebToUEAnimationConflictPolicy::Reject);
	Fixture.Animation->AdvanceGeneration({ 5001, 2 });
	TestTrue(TEXT("Generation advance synchronously releases all Track leases"),
		Fixture.Animation->GetActiveTrackCount() == 0 &&
		!Fixture.Animation->IsTickerRegistered() &&
		!Fixture.Target->Overlay.IsSet());
	TestTrue(TEXT("Old-generation cancellation is explicitly stale"),
		Fixture.Animation->Cancel(GenerationTrack.Handle) ==
			EWebToUEAnimationCancelResult::DroppedStaleGeneration);
	TestTrue(TEXT("Hostile lifecycle activity keeps Animation Trace bounded"),
		Fixture.Animation->GetTrace().Num() <= Budget.MaxTraceEntries);

	UWorld* World = NewObject<UWorld>(GetTransientPackage());
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	World->AddToRoot();
	Player->AddToRoot();
	FWebToUESessionCreateParams Params;
	Params.World = World;
	Params.LocalPlayer = Player;
	Params.Surface.SurfaceId = TEXT("webtoue.animation-test.screen");
	Params.Clock = Fixture.Clock;
	TSharedPtr<FWebToUESession> Session = FWebToUESession::Create(Params, Error);
	TestTrue(TEXT("UI Session owns an active Animation coordinator"),
		Session && Session->GetAnimationCoordinator()->IsActive());
	Session->Invalidate();
	TestFalse(TEXT("Session invalidation shuts down Animation before updates"),
		Session->GetAnimationCoordinator()->IsActive());
	Player->RemoveFromRoot();
	World->RemoveFromRoot();
	return true;
}

#endif
