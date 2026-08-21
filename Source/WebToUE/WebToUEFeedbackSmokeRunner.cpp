#include "WebToUEFeedbackSmokeRunner.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "WebToUEDocument.h"
#include "WebToUEFeedbackProfile.h"
#include "WebToUEFeedbackRouter.h"
#include "WebToUEScreenHost.h"

namespace WebToUE::FeedbackSmoke::Private
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

	static FString DispatchName(EWebToUEFeedbackDispatchResult Result)
	{
		switch (Result)
		{
		case EWebToUEFeedbackDispatchResult::Routed: return TEXT("Routed");
		case EWebToUEFeedbackDispatchResult::DroppedByRouter:
			return TEXT("DroppedByRouter");
		case EWebToUEFeedbackDispatchResult::DroppedInactiveSession:
			return TEXT("DroppedInactiveSession");
		case EWebToUEFeedbackDispatchResult::DroppedInvalidRequest:
			return TEXT("DroppedInvalidRequest");
		case EWebToUEFeedbackDispatchResult::DroppedWrongSession:
			return TEXT("DroppedWrongSession");
		case EWebToUEFeedbackDispatchResult::DroppedStaleGeneration:
			return TEXT("DroppedStaleGeneration");
		default: return TEXT("Unknown");
		}
	}
}

FWebToUEFeedbackSmokeRunner::FWebToUEFeedbackSmokeRunner()
{
	FParse::Value(FCommandLine::Get(), TEXT("WTUEFeedbackSmokeOutput="), OutputDirectory);
	if (OutputDirectory.IsEmpty())
	{
		OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(),
			TEXT("WebToUEFeedbackSmoke"), FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")));
	}
	FPaths::NormalizeDirectoryName(OutputDirectory);
	JsonPath = FPaths::Combine(OutputDirectory, TEXT("result.json"));
}

FWebToUEFeedbackSmokeRunner::~FWebToUEFeedbackSmokeRunner()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}
	ShutdownUi();
}

bool FWebToUEFeedbackSmokeRunner::IsRequested()
{
	FString Value;
	return FParse::Value(FCommandLine::Get(), TEXT("WTUEFeedbackSmokeOutput="), Value) &&
		!Value.IsEmpty();
}

void FWebToUEFeedbackSmokeRunner::Start()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDirectory);
	StartTimeSeconds = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FWebToUEFeedbackSmokeRunner::Tick));
	UE_LOG(LogTemp, Display, TEXT("WTUE_FEEDBACK_SMOKE_START output=%s"),
		*OutputDirectory);
}

bool FWebToUEFeedbackSmokeRunner::Tick(float DeltaSeconds)
{
	if (bFinished) return false;
	if (FPlatformTime::Seconds() - StartTimeSeconds > 60.0)
	{
		Finish(false, TEXT("Timed out waiting for Packaged Feedback readiness."));
		return false;
	}
	if (!bSetup)
	{
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetWorld() ||
			!GEngine->GameViewport->GetWorld()->HasBegunPlay() ||
			!GEngine->GameViewport->GetGameInstance() ||
			!GEngine->GameViewport->GetGameInstance()->GetFirstGamePlayer())
		{
			return true;
		}
		if (!Setup()) return false;
		bSetup = true;
		return true;
	}
	if (!Host || !Host->GetSession())
	{
		Finish(false, TEXT("Packaged Feedback Host disappeared before readiness."));
		return false;
	}
	if (!Host->GetSession()->IsReadyForInteraction())
	{
		bCriticalPendingObserved = true;
		return true;
	}
	RunPolicyAndFinish();
	return false;
}

bool FWebToUEFeedbackSmokeRunner::Setup()
{
	UWebToUEDocument* LoadedDocument = LoadObject<UWebToUEDocument>(nullptr,
		TEXT("/Game/WebToUEExamples/MainMenu.MainMenu"));
	UWebToUEFeedbackProfile* LoadedProfile = LoadObject<UWebToUEFeedbackProfile>(nullptr,
		TEXT("/Game/WebToUEExamples/Audio/DA_WTUE_FeedbackProfile."
			"DA_WTUE_FeedbackProfile"));
	if (!LoadedDocument || LoadedDocument->GetCompiledNodes().IsEmpty() || !LoadedProfile)
	{
		Finish(false, TEXT("Packaged Feedback document or Profile fixture is missing."));
		return false;
	}
	Document.Reset(LoadedDocument);
	Profile.Reset(LoadedProfile);
	Backend = MakeShared<FWebToUEEngineFeedbackBackend>();
	Settings = MakeShared<FWebToUEFixedFeedbackSettingsProvider>();
	Router = FWebToUEProfileFeedbackRouter::Create(LoadedProfile, Backend, Settings);
	FWebToUEScreenHostCreateParams Params;
	Params.Document = LoadedDocument;
	Params.SurfaceId = TEXT("webtoue.feedback-smoke.screen");
	Params.FeedbackRouter = Router;
	Params.ZOrder = 1000;
	FString Error;
	Host = FWebToUEScreenHost::CreateForLocalPlayer(
		GEngine->GameViewport->GetGameInstance()->GetFirstGamePlayer(), Params, Error);
	if (!Host || !Host->BuildContent(Error))
	{
		Finish(false, FString::Printf(TEXT("Packaged Feedback Host setup failed: %s"),
			*Error));
		return false;
	}
	bCriticalPendingObserved = !Host->GetSession()->IsReadyForInteraction();
	return true;
}

void FWebToUEFeedbackSmokeRunner::RunPolicyAndFinish()
{
	FString Error;
	if (!Host->Attach(Error))
	{
		Finish(false, FString::Printf(TEXT("Packaged Feedback Host attach failed: %s"),
			*Error));
		return;
	}
	const TSharedPtr<FWebToUESession> Session = Host->GetSession();
	auto Dispatch = [Session](FName Cue, uint64 Correlation,
		EWebToUEInputModality Modality = EWebToUEInputModality::Pointer)
	{
		return Session->DispatchCommittedFeedback(Session->MakeFeedbackRequest(
			Cue, TEXT("packaged-smoke"), Correlation, Modality,
			EWebToUEFeedbackScope::LocalPlayer));
	};
	const EWebToUEFeedbackDispatchResult Confirm =
		Dispatch(TEXT("webtoue.feedback.confirm"), 1002);
	const EWebToUEFeedbackDispatchResult Cooldown =
		Dispatch(TEXT("webtoue.feedback.confirm"), 1003);
	const EWebToUEFeedbackDispatchResult Hover =
		Dispatch(TEXT("webtoue.feedback.hover"), 1100,
			EWebToUEInputModality::Gamepad);
	const EWebToUEFeedbackDispatchResult Deduplicated =
		Dispatch(TEXT("webtoue.feedback.focus"), 1100,
			EWebToUEInputModality::Gamepad);
	TArray<EWebToUEFeedbackDispatchResult> Navigate;
	for (uint64 Correlation = 1200; Correlation <= 1204; ++Correlation)
	{
		Navigate.Add(Dispatch(TEXT("webtoue.feedback.navigate"), Correlation,
			EWebToUEInputModality::Gamepad));
	}
	FWebToUEFeedbackUserSettings Muted;
	Muted.bMuted = true;
	Settings->SetSettings(Muted);
	const EWebToUEFeedbackDispatchResult MutedResult =
		Dispatch(TEXT("webtoue.feedback.cancel"), 1300);
	Settings->SetSettings(FWebToUEFeedbackUserSettings());
	const EWebToUEFeedbackDispatchResult Missing =
		Dispatch(TEXT("webtoue.feedback.missing"), 1400);

	auto TraceCount = [this](EWebToUEFeedbackTraceOutcome Outcome)
	{
		int32 Count = 0;
		for (const FWebToUEFeedbackTrace& Entry : Router->GetTrace())
		{
			if (Entry.Outcome == Outcome)
			{
				++Count;
			}
		}
		return Count;
	};
	const bool bCriticalReady = TraceCount(EWebToUEFeedbackTraceOutcome::CriticalReady) == 1;
	const bool bPolicyPass = Confirm == EWebToUEFeedbackDispatchResult::Routed &&
		Cooldown == EWebToUEFeedbackDispatchResult::DroppedByRouter &&
		Hover == EWebToUEFeedbackDispatchResult::Routed &&
		Deduplicated == EWebToUEFeedbackDispatchResult::DroppedByRouter &&
		Navigate.Num() == 5 &&
		Navigate[0] == EWebToUEFeedbackDispatchResult::Routed &&
		Navigate[1] == EWebToUEFeedbackDispatchResult::Routed &&
		Navigate[2] == EWebToUEFeedbackDispatchResult::Routed &&
		Navigate[3] == EWebToUEFeedbackDispatchResult::Routed &&
		Navigate[4] == EWebToUEFeedbackDispatchResult::DroppedByRouter &&
		MutedResult == EWebToUEFeedbackDispatchResult::DroppedByRouter &&
		Missing == EWebToUEFeedbackDispatchResult::DroppedByRouter;
	const bool bTracePass = TraceCount(EWebToUEFeedbackTraceOutcome::Requested) == 11 &&
		TraceCount(EWebToUEFeedbackTraceOutcome::Committed) == 11 &&
		TraceCount(EWebToUEFeedbackTraceOutcome::Routed) == 6 &&
		TraceCount(EWebToUEFeedbackTraceOutcome::Cooldown) == 1 &&
		TraceCount(EWebToUEFeedbackTraceOutcome::Deduplicated) == 1 &&
		TraceCount(EWebToUEFeedbackTraceOutcome::Throttled) == 1 &&
		TraceCount(EWebToUEFeedbackTraceOutcome::Muted) == 1 &&
		TraceCount(EWebToUEFeedbackTraceOutcome::MissingCue) == 1;
	const bool bBackendPass = Backend->GetAttemptCount() == 6 &&
		Backend->GetSuccessfulPlayCount() == 6 &&
		Backend->GetLastSuccessfulRequest().IsSet() &&
		Backend->GetLastSuccessfulRequest()->Mode ==
			EWebToUEFeedbackPlaybackMode::Screen2D &&
		Backend->GetLastSuccessfulRequest()->Concurrency != nullptr;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetBoolField(TEXT("success"),
		bCriticalReady && bPolicyPass && bTracePass && bBackendPass);
	Root->SetStringField(TEXT("build_configuration"),
		WebToUE::FeedbackSmoke::Private::GetBuildConfiguration());
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("profile_path"), Profile->GetPathName());
	Root->SetStringField(TEXT("profile_id"), Profile->ProfileId.ToString());
	Root->SetNumberField(TEXT("profile_schema_major"), Profile->SchemaMajor);
	Root->SetNumberField(TEXT("profile_schema_minor"), Profile->SchemaMinor);
	Root->SetNumberField(TEXT("sealed_dependency_count"),
		Profile->GetSealedResourceDependencies().Num());
	Root->SetStringField(TEXT("resource_request_contract"), TEXT("async-only"));
	Root->SetBoolField(TEXT("critical_pending_observed"), bCriticalPendingObserved);
	Root->SetBoolField(TEXT("critical_ready_observed"), bCriticalReady);
	Root->SetBoolField(TEXT("host_attached"), Host->IsAttached());
	Root->SetStringField(TEXT("confirm"),
		WebToUE::FeedbackSmoke::Private::DispatchName(Confirm));
	Root->SetStringField(TEXT("cooldown"),
		WebToUE::FeedbackSmoke::Private::DispatchName(Cooldown));
	Root->SetStringField(TEXT("hover"),
		WebToUE::FeedbackSmoke::Private::DispatchName(Hover));
	Root->SetStringField(TEXT("deduplicated_focus"),
		WebToUE::FeedbackSmoke::Private::DispatchName(Deduplicated));
	Root->SetStringField(TEXT("throttled_navigate"),
		WebToUE::FeedbackSmoke::Private::DispatchName(Navigate.Last()));
	Root->SetStringField(TEXT("muted_cancel"),
		WebToUE::FeedbackSmoke::Private::DispatchName(MutedResult));
	Root->SetStringField(TEXT("missing_cue"),
		WebToUE::FeedbackSmoke::Private::DispatchName(Missing));
	Root->SetNumberField(TEXT("requested_count"),
		TraceCount(EWebToUEFeedbackTraceOutcome::Requested));
	Root->SetNumberField(TEXT("committed_count"),
		TraceCount(EWebToUEFeedbackTraceOutcome::Committed));
	Root->SetNumberField(TEXT("routed_count"),
		TraceCount(EWebToUEFeedbackTraceOutcome::Routed));
	Root->SetNumberField(TEXT("backend_attempt_count"), Backend->GetAttemptCount());
	Root->SetNumberField(TEXT("backend_success_count"),
		Backend->GetSuccessfulPlayCount());
	Root->SetStringField(TEXT("last_playback_mode"), TEXT("Screen2D"));
	Root->SetBoolField(TEXT("last_request_has_concurrency"),
		Backend->GetLastSuccessfulRequest().IsSet() &&
		Backend->GetLastSuccessfulRequest()->Concurrency != nullptr);
	Root->SetStringField(TEXT("evidence_boundary"),
		TEXT("Proves packaged async residency, deterministic Router policy, and an in-process default UE backend call; does not prove audible hardware output or acoustic latency."));

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	const bool bWritten = FFileHelper::SaveStringToFile(Json, *JsonPath);
	Finish(bWritten && bCriticalReady && bPolicyPass && bTracePass && bBackendPass,
		bWritten ? FString() : TEXT("Failed to write Packaged Feedback result.json."));
}

void FWebToUEFeedbackSmokeRunner::Finish(bool bSuccess, FString Error)
{
	if (bFinished) return;
	bFinished = true;
	if (!bSuccess && !Error.IsEmpty())
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schema_version"), 1);
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("build_configuration"),
			WebToUE::FeedbackSmoke::Private::GetBuildConfiguration());
		Root->SetStringField(TEXT("error"), Error);
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Root, Writer);
		FFileHelper::SaveStringToFile(Json, *JsonPath);
	}
	UE_LOG(LogTemp, Display, TEXT("WTUE_FEEDBACK_SMOKE_COMPLETE success=%s json=%s error=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), *JsonPath, *Error);
	ShutdownUi();
	FPlatformMisc::RequestExit(!bSuccess);
}

void FWebToUEFeedbackSmokeRunner::ShutdownUi()
{
	if (Host)
	{
		Host->Shutdown();
		Host.Reset();
	}
	Router.Reset();
	Settings.Reset();
	Backend.Reset();
	Profile.Reset();
	Document.Reset();
}
