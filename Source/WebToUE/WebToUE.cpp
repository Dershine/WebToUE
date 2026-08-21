// Fill out your copyright notice in the Description page of Project Settings.

#include "WebToUE.h"
#include "WebToUEBenchmarkRunner.h"
#include "WebToUEFeedbackSmokeRunner.h"
#include "Modules/ModuleManager.h"

void FWebToUEModule::StartupModule()
{
	FDefaultGameModuleImpl::StartupModule();
	if (FWebToUEFeedbackSmokeRunner::IsRequested())
	{
		FeedbackSmokeRunner = MakeUnique<FWebToUEFeedbackSmokeRunner>();
		FeedbackSmokeRunner->Start();
	}
	else if (FWebToUEBenchmarkRunner::IsRequested())
	{
		BenchmarkRunner = MakeUnique<FWebToUEBenchmarkRunner>();
		BenchmarkRunner->Start();
	}
}

void FWebToUEModule::ShutdownModule()
{
	FeedbackSmokeRunner.Reset();
	BenchmarkRunner.Reset();
	FDefaultGameModuleImpl::ShutdownModule();
}

IMPLEMENT_PRIMARY_GAME_MODULE(FWebToUEModule, WebToUE, "WebToUE");
