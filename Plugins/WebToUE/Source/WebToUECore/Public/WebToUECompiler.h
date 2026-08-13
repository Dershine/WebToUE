#pragma once

#include "Containers/ArrayView.h"
#include "WebToUECoreTypes.h"
#include "WebToUEStyleProperties.h"

struct WEBTOUECORE_API FWebToUEStyleSheetSource
{
	FString Css;
	FString SourceName = TEXT("<memory>");
	int32 StartLine = 1;
	int32 StartColumn = 1;
};

struct WEBTOUECORE_API FWebToUEStyleChangeSet
{
	TArray<EWebToUECssProperty> ChangedProperties;
	EWebToUEStyleImpact Impacts = EWebToUEStyleImpact::None;

	bool IsEmpty() const { return ChangedProperties.IsEmpty(); }
	bool HasInheritedPropertyChange() const;
};

struct WEBTOUECORE_API FWebToUEStyleUpdate
{
	FWebToUEInstanceHandle Target;
	FWebToUEStyleChangeSet Changes;
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
	static void ResolveIncremental(FWebToUEDocument& Document,
		TConstArrayView<FWebToUEInstanceHandle> Targets,
		TArray<FWebToUEStyleUpdate>& OutUpdates);
	static bool Matches(const FWebToUEStyleRule& Rule, const FWebToUENode& Node,
		const FWebToUEDocument& Document);
	static bool MatchesWithReason(const FWebToUEStyleRule& Rule,
		const FWebToUENode& Target, const FWebToUEDocument& Document,
		int32 ReasonSegmentIndex, const FWebToUENode& ReasonNode);
};

class WEBTOUECORE_API FWebToUELayoutEngine
{
public:
	FWebToUELayoutEngine();
	~FWebToUELayoutEngine();

	FWebToUELayoutEngine(const FWebToUELayoutEngine&) = delete;
	FWebToUELayoutEngine& operator=(const FWebToUELayoutEngine&) = delete;

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

	void Reset();
	void ApplyStyleUpdates(FWebToUEDocument& Document,
		TConstArrayView<FWebToUEStyleUpdate> Updates);
	void MarkMeasureDirty(FWebToUEDocument& Document, FWebToUEInstanceHandle Target);
	void LayoutPersistent(
		FWebToUEDocument& Document,
		const FVector2f& ViewportSize,
		const FMeasureNode& MeasureNode);

	static void Layout(
		FWebToUEDocument& Document,
		const FVector2f& ViewportSize,
		const FMeasureNode& MeasureNode);

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
};
