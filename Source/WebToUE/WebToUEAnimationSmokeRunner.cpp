#include "WebToUEAnimationSmokeRunner.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WebToUEAnimation.h"
#include "WebToUESession.h"

namespace WebToUE::AnimationSmoke::Private
{
	static FString GetBuildConfiguration()
	{
#if UE_BUILD_SHIPPING
		return TEXT("Shipping");
#elif UE_BUILD_TEST
		return TEXT("Test");
#elif UE_BUILD_DEBUG
		return TEXT("Debug");
#else
		return TEXT("Development");
#endif
	}

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
				OutError = TEXT("Packaged recording target rejected the request.");
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
}

FWebToUEAnimationSmokeRunner::FWebToUEAnimationSmokeRunner()
{
	FParse::Value(FCommandLine::Get(), TEXT("WTUEAnimationSmokeOutput="),
		OutputDirectory);
	if (OutputDirectory.IsEmpty())
	{
		OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(),
			TEXT("WebToUEAnimationSmoke"),
			FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")));
	}
	FPaths::NormalizeDirectoryName(OutputDirectory);
	JsonPath = FPaths::Combine(OutputDirectory, TEXT("result.json"));
}

FWebToUEAnimationSmokeRunner::~FWebToUEAnimationSmokeRunner()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}
}

bool FWebToUEAnimationSmokeRunner::IsRequested()
{
	FString Value;
	return FParse::Value(FCommandLine::Get(), TEXT("WTUEAnimationSmokeOutput="),
		Value) && !Value.IsEmpty();
}

void FWebToUEAnimationSmokeRunner::Start()
{
	IPlatformFile& PlatformFile =
		FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDirectory);
	StartTimeSeconds = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this,
			&FWebToUEAnimationSmokeRunner::Tick));
	UE_LOG(LogTemp, Display, TEXT("WTUE_ANIMATION_SMOKE_START output=%s"),
		*OutputDirectory);
}

bool FWebToUEAnimationSmokeRunner::Tick(float DeltaSeconds)
{
	if (bFinished)
	{
		return false;
	}
	if (FPlatformTime::Seconds() - StartTimeSeconds > 60.0)
	{
		Finish(false,
			TEXT("Timed out waiting for Packaged Animation readiness."));
		return false;
	}
	if (!GEngine || !GEngine->GameViewport ||
		!GEngine->GameViewport->GetWorld() ||
		!GEngine->GameViewport->GetWorld()->HasBegunPlay() ||
		!GEngine->GameViewport->GetGameInstance() ||
		!GEngine->GameViewport->GetGameInstance()->GetFirstGamePlayer())
	{
		return true;
	}
	RunAndFinish();
	return false;
}

void FWebToUEAnimationSmokeRunner::RunAndFinish()
{
	using namespace WebToUE::AnimationSmoke::Private;

	FString Error;
	const TSharedRef<FWebToUEVirtualClock> Clock =
		MakeShared<FWebToUEVirtualClock>();
	if (!Clock->SetTimeSeconds(EWebToUEClockDomain::Test, 10.0, Error))
	{
		Finish(false, Error);
		return;
	}

	FWebToUESessionCreateParams Params;
	Params.World = GEngine->GameViewport->GetWorld();
	Params.LocalPlayer =
		GEngine->GameViewport->GetGameInstance()->GetFirstGamePlayer();
	Params.Surface.SurfaceId = TEXT("webtoue.animation-smoke.screen");
	Params.Clock = Clock;
	const TSharedPtr<FWebToUESession> Session =
		FWebToUESession::Create(Params, Error);
	if (!Session)
	{
		Finish(false, FString::Printf(
			TEXT("Packaged Animation Session setup failed: %s"), *Error));
		return;
	}

	const FWebToUESessionHandle SessionHandle = Session->GetHandle();
	const FWebToUEInstanceHandle TargetHandle =
		FWebToUEInstanceHandle::Create(SessionHandle.GetSessionId(),
			SessionHandle.GetGeneration(), 7);
	const TSharedRef<FRecordingTarget> Target =
		MakeShared<FRecordingTarget>(TargetHandle);
	const TSharedRef<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe>
		Animation = Session->GetAnimationCoordinator();
	const TSharedRef<FWebToUEUpdateCoordinator, ESPMode::ThreadSafe>
		Updates = Session->GetUpdateCoordinator();

	auto Request = [Target, TargetHandle](float From, float To,
		double Duration, FName Name)
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
	};

	const bool bIdleNoTicker = !Animation->IsTickerRegistered();
	const uint64 IdleTickerInvocations = Animation->GetTickerInvocationCount();
	const FWebToUEAnimationStartOutcome Initial = Animation->StartTrack(
		Request(0.0f, 10.0f, 4.0, TEXT("packaged.initial")),
		EWebToUEAnimationConflictPolicy::Reject);
	const bool bInitialStarted =
		Initial.Result == EWebToUEAnimationStartResult::Started &&
		Animation->GetActiveTrackCount() == 1 &&
		Animation->IsTickerRegistered() &&
		Target->Overlay.IsSet() &&
		FMath::IsNearlyEqual(Target->Overlay.GetValue(), 0.0f);

	Clock->Advance(EWebToUEClockDomain::Test, 1.0, Error);
	const int32 QuarterSamples = Animation->Pump();
	const float QuarterValue =
		Target->Overlay.IsSet() ? Target->Overlay.GetValue() : -1.0f;
	const FWebToUEAnimationStartOutcome Retargeted = Animation->StartTrack(
		Request(-999.0f, 20.0f, 3.0, TEXT("packaged.retarget")),
		EWebToUEAnimationConflictPolicy::Retarget);
	const float RetargetStart =
		Target->Overlay.IsSet() ? Target->Overlay.GetValue() : -1.0f;
	Clock->Advance(EWebToUEClockDomain::Test, 1.5, Error);
	const int32 RetargetSamples = Animation->Pump();
	const float RetargetMidpoint =
		Target->Overlay.IsSet() ? Target->Overlay.GetValue() : -1.0f;

	const FWebToUEAnimationStartOutcome Replaced = Animation->StartTrack(
		Request(100.0f, 200.0f, 2.0, TEXT("packaged.replace")),
		EWebToUEAnimationConflictPolicy::Replace);
	const float ReplaceStart =
		Target->Overlay.IsSet() ? Target->Overlay.GetValue() : -1.0f;
	Target->Underlying = 77.0f;
	const EWebToUEAnimationCancelResult Cancelled =
		Animation->Cancel(Replaced.Handle);
	const float CancelEffective = Target->GetEffectiveScalar();

	const FWebToUEAnimationStartOutcome Completed = Animation->StartTrack(
		Request(0.0f, 1.0f, 1.0, TEXT("packaged.complete")),
		EWebToUEAnimationConflictPolicy::Reject);
	Clock->Advance(EWebToUEClockDomain::Test, 1.0, Error);
	const int32 CompletionSamples = Animation->Pump();
	const float CompletionExactTo = Target->Applied.IsEmpty()
		? -1.0f : Target->Applied.Last();
	const bool bCompletionReleased =
		Animation->GetActiveTrackCount() == 0 &&
		!Animation->IsTickerRegistered() && !Target->Overlay.IsSet();

	const FWebToUEAnimationStartOutcome GenerationTrack =
		Animation->StartTrack(
			Request(1.0f, 2.0f, 5.0, TEXT("packaged.generation")),
			EWebToUEAnimationConflictPolicy::Reject);
	Session->AdvanceGeneration();
	const EWebToUEAnimationCancelResult StaleCancel =
		Animation->Cancel(GenerationTrack.Handle);
	const bool bGenerationReleased =
		Animation->GetActiveTrackCount() == 0 &&
		!Animation->IsTickerRegistered() && !Target->Overlay.IsSet();

	bool bTransactionsCommitted = !Updates->GetTrace().IsEmpty();
	for (const FWebToUEUpdateTrace& Trace : Updates->GetTrace())
	{
		bTransactionsCommitted &=
			Trace.Outcome == EWebToUEUpdateOutcome::Committed;
	}
	const int32 TransactionCount = Updates->GetTrace().Num();
	const int32 TraceCount = Animation->GetTrace().Num();
	constexpr int32 TraceBudget = 256;
	const bool bTraceBounded = TraceCount <= TraceBudget;

	const bool bPass = bIdleNoTicker && IdleTickerInvocations == 0 &&
		bInitialStarted && QuarterSamples == 1 &&
		FMath::IsNearlyEqual(QuarterValue, 2.5f) &&
		Retargeted.Result == EWebToUEAnimationStartResult::Retargeted &&
		FMath::IsNearlyEqual(RetargetStart, 2.5f) &&
		RetargetSamples == 1 &&
		FMath::IsNearlyEqual(RetargetMidpoint, 11.25f) &&
		Replaced.Result == EWebToUEAnimationStartResult::Replaced &&
		FMath::IsNearlyEqual(ReplaceStart, 100.0f) &&
		Cancelled == EWebToUEAnimationCancelResult::Cancelled &&
		FMath::IsNearlyEqual(CancelEffective, 77.0f) &&
		Completed.IsAccepted() && CompletionSamples == 1 &&
		FMath::IsNearlyEqual(CompletionExactTo, 1.0f) &&
		bCompletionReleased && GenerationTrack.IsAccepted() &&
		bGenerationReleased &&
		StaleCancel ==
			EWebToUEAnimationCancelResult::DroppedStaleGeneration &&
		bTransactionsCommitted && bTraceBounded;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetBoolField(TEXT("success"), bPass);
	Root->SetStringField(TEXT("build_configuration"),
		GetBuildConfiguration());
	Root->SetStringField(TEXT("engine_version"),
		FEngineVersion::Current().ToString());
	Root->SetNumberField(TEXT("animation_ir_major"),
		FWebToUECompiledAnimationIR::CurrentMajor);
	Root->SetNumberField(TEXT("animation_ir_minor"),
		FWebToUECompiledAnimationIR::CurrentMinor);
	Root->SetStringField(TEXT("clock_domain"), TEXT("Test"));
	Root->SetBoolField(TEXT("idle_ticker_registered"), !bIdleNoTicker);
	Root->SetNumberField(TEXT("idle_ticker_invocations"),
		static_cast<double>(IdleTickerInvocations));
	Root->SetNumberField(TEXT("quarter_value"), QuarterValue);
	Root->SetNumberField(TEXT("retarget_start"), RetargetStart);
	Root->SetNumberField(TEXT("retarget_midpoint"), RetargetMidpoint);
	Root->SetNumberField(TEXT("replace_start"), ReplaceStart);
	Root->SetNumberField(TEXT("cancel_effective"), CancelEffective);
	Root->SetNumberField(TEXT("completion_exact_to"), CompletionExactTo);
	Root->SetBoolField(TEXT("completion_released"),
		bCompletionReleased);
	Root->SetBoolField(TEXT("generation_released"),
		bGenerationReleased);
	Root->SetStringField(TEXT("stale_cancel_result"),
		StaleCancel ==
			EWebToUEAnimationCancelResult::DroppedStaleGeneration
			? TEXT("DroppedStaleGeneration") : TEXT("Unexpected"));
	Root->SetNumberField(TEXT("active_tracks_after_generation"),
		Animation->GetActiveTrackCount());
	Root->SetBoolField(TEXT("ticker_after_generation"),
		Animation->IsTickerRegistered());
	Root->SetNumberField(TEXT("transaction_count"), TransactionCount);
	Root->SetBoolField(TEXT("all_transactions_committed"),
		bTransactionsCommitted);
	Root->SetNumberField(TEXT("trace_count"), TraceCount);
	Root->SetNumberField(TEXT("trace_budget"), TraceBudget);
	Root->SetNumberField(TEXT("release_count"), Target->ReleaseCount);
	Root->SetStringField(TEXT("evidence_boundary"),
		TEXT("Proves the packaged native Animation IR 1.0 kernel, exact Virtual Clock sampling, one-address Retarget/Replace/Cancel lease semantics, active-only ticker ownership, and Session-generation cleanup. It does not prove visual output, transition lowering, easing, reverse/fill, renderer cost, GPU cost, or input-to-pixel latency."));

	Session->Invalidate();
	Root->SetBoolField(TEXT("session_shutdown"),
		!Animation->IsActive() && !Updates->IsActive());
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	const bool bWritten = FFileHelper::SaveStringToFile(Json, *JsonPath);
	Finish(bWritten && bPass,
		bWritten ? FString() :
		TEXT("Failed to write Packaged Animation result.json."));
}

void FWebToUEAnimationSmokeRunner::Finish(bool bSuccess, FString Error)
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	if (!bSuccess && !Error.IsEmpty())
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schema_version"), 1);
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("build_configuration"),
			WebToUE::AnimationSmoke::Private::GetBuildConfiguration());
		Root->SetStringField(TEXT("error"), Error);
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer =
			TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Root, Writer);
		FFileHelper::SaveStringToFile(Json, *JsonPath);
	}
	UE_LOG(LogTemp, Display,
		TEXT("WTUE_ANIMATION_SMOKE_COMPLETE success=%s json=%s error=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), *JsonPath, *Error);
	FPlatformMisc::RequestExit(!bSuccess);
}
