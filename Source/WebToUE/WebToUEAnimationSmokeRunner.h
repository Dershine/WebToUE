#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"

/** Command-line-only packaged Animation kernel acceptance runner; dormant in normal launches. */
class FWebToUEAnimationSmokeRunner
{
public:
	FWebToUEAnimationSmokeRunner();
	~FWebToUEAnimationSmokeRunner();

	static bool IsRequested();
	void Start();

private:
	bool Tick(float DeltaSeconds);
	void RunAndFinish();
	void Finish(bool bSuccess, FString Error = {});

	FString OutputDirectory;
	FString JsonPath;
	double StartTimeSeconds = 0.0;
	bool bFinished = false;
	FTSTicker::FDelegateHandle TickerHandle;
};
