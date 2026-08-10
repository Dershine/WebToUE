#pragma once

#include "WebToUECoreTypes.h"

class WEBTOUECORE_API FWebToUECompiler
{
public:
	static TSharedRef<FWebToUEDocument> Compile(
		const FString& Html,
		const FString& ExternalCss = FString(),
		const FString& SourceName = TEXT("<memory>"));
};

class WEBTOUECORE_API FWebToUEStyleResolver
{
public:
	static void Resolve(FWebToUEDocument& Document);
	static bool Matches(const FWebToUEStyleRule& Rule, const FWebToUENode& Node);
};

class WEBTOUECORE_API FWebToUELayoutEngine
{
public:
	using FMeasureNode = TFunction<FVector2f(const FWebToUENode&)>;

	static void Layout(
		FWebToUEDocument& Document,
		const FVector2f& ViewportSize,
		const FMeasureNode& MeasureNode);
};
