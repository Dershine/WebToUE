#pragma once

#include "Containers/ArrayView.h"
#include "WebToUECoreTypes.h"

struct WEBTOUECORE_API FWebToUEStyleSheetSource
{
	FString Css;
	FString SourceName = TEXT("<memory>");
	int32 StartLine = 1;
	int32 StartColumn = 1;
};

class WEBTOUECORE_API FWebToUECompiler
{
public:
	static TSharedRef<FWebToUEDocument> Compile(
		const FString& Html,
		const FString& ExternalCss = FString(),
		const FString& SourceName = TEXT("<memory>"));

	static TSharedRef<FWebToUEDocument> Compile(
		const FString& Html,
		TConstArrayView<FWebToUEStyleSheetSource> ExternalStyleSheets,
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
	enum class EMeasureMode : uint8
	{
		Undefined,
		Exactly,
		AtMost
	};

	struct FMeasureConstraints
	{
		float Width = 0.0f;
		float Height = 0.0f;
		EMeasureMode WidthMode = EMeasureMode::Undefined;
		EMeasureMode HeightMode = EMeasureMode::Undefined;
	};

	using FMeasureNode = TFunction<FVector2f(const FWebToUENode&, const FMeasureConstraints&)>;

	static void Layout(
		FWebToUEDocument& Document,
		const FVector2f& ViewportSize,
		const FMeasureNode& MeasureNode);
};
