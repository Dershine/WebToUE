// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FWebToUEBenchmarkRunner;
class FWebToUEAnimationSmokeRunner;
class FWebToUEFeedbackSmokeRunner;

class FWebToUEModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TUniquePtr<FWebToUEBenchmarkRunner> BenchmarkRunner;
	TUniquePtr<FWebToUEAnimationSmokeRunner> AnimationSmokeRunner;
	TUniquePtr<FWebToUEFeedbackSmokeRunner> FeedbackSmokeRunner;
};

