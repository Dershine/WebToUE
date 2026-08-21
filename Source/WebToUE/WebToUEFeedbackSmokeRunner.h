#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class FWebToUEEngineFeedbackBackend;
class FWebToUEFixedFeedbackSettingsProvider;
class FWebToUEProfileFeedbackRouter;
class FWebToUEScreenHost;
class UWebToUEDocument;
class UWebToUEFeedbackProfile;

/** Command-line-only packaged Feedback acceptance runner; dormant in normal launches. */
class FWebToUEFeedbackSmokeRunner
{
public:
	FWebToUEFeedbackSmokeRunner();
	~FWebToUEFeedbackSmokeRunner();

	static bool IsRequested();
	void Start();

private:
	bool Tick(float DeltaSeconds);
	bool Setup();
	void RunPolicyAndFinish();
	void Finish(bool bSuccess, FString Error = {});
	void ShutdownUi();

	FString OutputDirectory;
	FString JsonPath;
	double StartTimeSeconds = 0.0;
	bool bSetup = false;
	bool bFinished = false;
	bool bCriticalPendingObserved = false;
	FTSTicker::FDelegateHandle TickerHandle;
	TStrongObjectPtr<UWebToUEDocument> Document;
	TStrongObjectPtr<UWebToUEFeedbackProfile> Profile;
	TSharedPtr<FWebToUEEngineFeedbackBackend> Backend;
	TSharedPtr<FWebToUEFixedFeedbackSettingsProvider> Settings;
	TSharedPtr<FWebToUEProfileFeedbackRouter> Router;
	TUniquePtr<FWebToUEScreenHost> Host;
};
