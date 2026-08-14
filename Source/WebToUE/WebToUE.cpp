// Fill out your copyright notice in the Description page of Project Settings.

#include "WebToUE.h"
#include "WebToUEBenchmarkRunner.h"
#include "Modules/ModuleManager.h"

void FWebToUEModule::StartupModule()
{
	FDefaultGameModuleImpl::StartupModule();
	if (FWebToUEBenchmarkRunner::IsRequested())
	{
		BenchmarkRunner = MakeUnique<FWebToUEBenchmarkRunner>();
		BenchmarkRunner->Start();
	}
}

void FWebToUEModule::ShutdownModule()
{
	BenchmarkRunner.Reset();
	FDefaultGameModuleImpl::ShutdownModule();
}

IMPLEMENT_PRIMARY_GAME_MODULE(FWebToUEModule, WebToUE, "WebToUE");
