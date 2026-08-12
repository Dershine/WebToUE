#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SWebToUEView.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"

#include "Input/HittestGrid.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPerformanceInstrumentationTest,
	"WebToUE.Runtime.PerformanceInstrumentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::Performance::Tests
{
	static void AddAttribute(FWebToUECompiledNode& Node, const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUECompiledAttribute& Attribute = Node.Attributes.AddDefaulted_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
	}

	static UWebToUEDocument* MakeDocument()
	{
		UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
		FWebToUECompiledDocumentData CompiledDocument;
		CompiledDocument.LocalizationNamespace = TEXT("Measured Label");

		FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Body.Tag = TEXT("body");

		FWebToUECompiledNode& Button = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Button.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Button.Tag = TEXT("button");
		Button.ParentIndex = 0;
		AddAttribute(Button, TEXT("id"), TEXT("performance-target"));
		AddAttribute(Button, TEXT("data-ue-bind-text"), TEXT("LocalizationNamespace"));
		AddAttribute(Button, TEXT("data-ue-on-click"), TEXT("MeasuredClick"));

		FWebToUECompiledNode& Text = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Text.Type = static_cast<uint8>(EWebToUENodeType::Text);
		Text.Tag = TEXT("#text");
		Text.Text = TEXT("Initial Label");
		Text.LocalizedText = FText::FromString(Text.Text);
		Text.ParentIndex = 1;

		FWebToUECompiledRule& Rule = CompiledDocument.Rules.AddDefaulted_GetRef();
		Rule.Specificity = 1;
		FWebToUECompiledSelectorSegment& Selector = Rule.Selector.AddDefaulted_GetRef();
		Selector.Type = TEXT("button");
		FWebToUECompiledDeclaration& Width = Rule.Declarations.AddDefaulted_GetRef();
		Width.Name = TEXT("width");
		Width.Value = TEXT("120px");
		FWebToUECompiledDeclaration& Height = Rule.Declarations.AddDefaulted_GetRef();
		Height.Name = TEXT("height");
		Height.Value = TEXT("40px");

		CompiledDocument.RootNodeIndex = 0;
		Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
		return Document;
	}
}

bool FWebToUEPerformanceInstrumentationTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Performance::Tests;

	UWebToUEDocument* Document = MakeDocument();
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);

	FWebToUEPerformanceSnapshot Snapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetDocument(Document);
		View->RefreshBindings(Document);
		View->LayoutForTesting(FVector2f(320.0f, 180.0f));

		FHittestGrid HittestGrid;
		FSlateWindowElementList DrawElements(nullptr);
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(320.0, 180.0), FSlateLayoutTransform());
		const FPaintArgs PaintArgs(&View.Get(), HittestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
		View->OnPaint(PaintArgs, Geometry, FSlateRect(0.0f, 0.0f, 320.0f, 180.0f), DrawElements,
			0, FWidgetStyle(), true);
		TestNotNull(TEXT("The instrumented document remains hittable"),
			View->HitTestForTesting(FVector2f(10.0f, 10.0f)));

		Snapshot = Capture.GetSnapshot();
		AddInfo(Snapshot.ToLogString());
	}

	for (int32 Index = 0; Index < FWebToUEPerformanceSnapshot::PhaseCount; ++Index)
	{
		const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(Index);
		TestTrue(*FString::Printf(TEXT("%s records at least one call"), LexToString(Phase)),
			Snapshot.Get(Phase).CallCount > 0);
	}

	TestEqual(TEXT("Hydrate records the compiled node count"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::HydratedNodes), uint64(3));
	TestEqual(TEXT("Hydrate records the compiled rule count"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::HydratedRules), uint64(1));
	TestEqual(TEXT("Three style passes visit all three nodes"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(9));
	TestEqual(TEXT("Three style passes evaluate the rule for every node"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorEvaluations), uint64(9));
	TestEqual(TEXT("The button selector matches once per style pass"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::SelectorMatches), uint64(3));
	TestEqual(TEXT("Layout builds one Yoga node per runtime node"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(3));
	TestEqual(TEXT("The text cache is built once"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutBuilds), uint64(1));
	TestEqual(TEXT("Text layout is computed for measure and paint"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(2));
	TestEqual(TEXT("Two style refreshes each build body and button brushes"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::BrushBuilds), uint64(4));
	TestEqual(TEXT("The representative workflow records all marked allocation events"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocations), uint64(20));
	TestEqual(TEXT("Runtime state and render data each have a known owned payload"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadEvents), uint64(2));
	TestEqual(TEXT("The separated runtime payloads are sized for every hydrated node"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes),
		uint64(3 * (sizeof(FWebToUERuntimeNodeState) + sizeof(FWebToUERuntimeRenderData))));
	TestTrue(TEXT("The log snapshot exposes stable workload fields"),
		Snapshot.ToLogString().Contains(TEXT("workload={hydrated_nodes=3")));

	TMap<FString, double> TelemetryMeasurements;
	Snapshot.ForEachTelemetryMeasurement(
		[&](const TCHAR* Name, double Value)
		{
			TestFalse(*FString::Printf(TEXT("Telemetry measurement %s is unique"), Name),
				TelemetryMeasurements.Contains(Name));
			TelemetryMeasurements.Add(Name, Value);
		});
	TestEqual(TEXT("The telemetry schema has the expected version"),
		FWebToUEPerformanceSnapshot::TelemetrySchemaVersion, 2);
	TestEqual(TEXT("The telemetry schema exposes every phase field and workload counter"),
		TelemetryMeasurements.Num(), FWebToUEPerformanceSnapshot::TelemetryMeasurementCount);
	static constexpr const TCHAR* ExpectedTelemetryNames[] = {
		TEXT("phase.hydrate.calls"),
		TEXT("phase.hydrate.inclusive_ms"),
		TEXT("phase.style.calls"),
		TEXT("phase.style.inclusive_ms"),
		TEXT("phase.measure.calls"),
		TEXT("phase.measure.inclusive_ms"),
		TEXT("phase.layout.calls"),
		TEXT("phase.layout.inclusive_ms"),
		TEXT("phase.paint_build.calls"),
		TEXT("phase.paint_build.inclusive_ms"),
		TEXT("phase.hit_test.calls"),
		TEXT("phase.hit_test.inclusive_ms"),
		TEXT("phase.binding.calls"),
		TEXT("phase.binding.inclusive_ms"),
		TEXT("workload.hydrated_nodes"),
		TEXT("workload.hydrated_rules"),
		TEXT("workload.style_node_visits"),
		TEXT("workload.selector_evaluations"),
		TEXT("workload.selector_matches"),
		TEXT("workload.yoga_nodes_built"),
		TEXT("workload.text_layout_builds"),
		TEXT("workload.text_layout_computes"),
		TEXT("workload.brush_builds"),
		TEXT("workload.tracked_allocations"),
		TEXT("workload.tracked_allocation_payload_events"),
		TEXT("workload.tracked_allocation_payload_bytes")
	};
	static_assert(UE_ARRAY_COUNT(ExpectedTelemetryNames) ==
		FWebToUEPerformanceSnapshot::TelemetryMeasurementCount);
	for (const TCHAR* ExpectedName : ExpectedTelemetryNames)
	{
		TestTrue(*FString::Printf(TEXT("Telemetry exposes stable field %s"), ExpectedName),
			TelemetryMeasurements.Contains(ExpectedName));
	}
	TestEqual(TEXT("Telemetry exposes hydrate call count"),
		TelemetryMeasurements.FindRef(TEXT("phase.hydrate.calls")), 1.0);
	TestTrue(TEXT("Telemetry exposes hydrate inclusive time"),
		TelemetryMeasurements.FindRef(TEXT("phase.hydrate.inclusive_ms")) > 0.0);
	TestEqual(TEXT("Telemetry exposes selector evaluations"),
		TelemetryMeasurements.FindRef(TEXT("workload.selector_evaluations")), 9.0);
	TestEqual(TEXT("Telemetry exposes tracked allocation events"),
		TelemetryMeasurements.FindRef(TEXT("workload.tracked_allocations")), 20.0);
	TestEqual(TEXT("Telemetry exposes tracked allocation payload events"),
		TelemetryMeasurements.FindRef(TEXT("workload.tracked_allocation_payload_events")), 2.0);
	TestEqual(TEXT("Telemetry exposes tracked allocation payload bytes"),
		TelemetryMeasurements.FindRef(TEXT("workload.tracked_allocation_payload_bytes")),
		static_cast<double>(Snapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes)));

	FWebToUEPerformanceCapture EmptyCapture;
	const FWebToUEPerformanceSnapshot EmptySnapshot = EmptyCapture.GetSnapshot();
	for (int32 Index = 0; Index < FWebToUEPerformanceSnapshot::PhaseCount; ++Index)
	{
		const EWebToUEPerformancePhase Phase = static_cast<EWebToUEPerformancePhase>(Index);
		TestEqual(*FString::Printf(TEXT("A new capture isolates %s"), LexToString(Phase)),
			EmptySnapshot.Get(Phase).CallCount, uint64(0));
	}
	for (int32 Index = 0; Index < FWebToUEPerformanceSnapshot::CounterCount; ++Index)
	{
		const EWebToUEPerformanceCounter Counter = static_cast<EWebToUEPerformanceCounter>(Index);
		TestEqual(*FString::Printf(TEXT("A new capture isolates %s"), LexToString(Counter)),
			EmptySnapshot.GetCounter(Counter), uint64(0));
	}
	int32 EmptyTelemetryMeasurements = 0;
	EmptySnapshot.ForEachTelemetryMeasurement(
		[&](const TCHAR* Name, double Value)
		{
			TestEqual(*FString::Printf(TEXT("An empty snapshot reports zero for %s"), Name), Value, 0.0);
			++EmptyTelemetryMeasurements;
		});
	TestEqual(TEXT("An empty snapshot preserves the telemetry schema"), EmptyTelemetryMeasurements,
		FWebToUEPerformanceSnapshot::TelemetryMeasurementCount);

	FWebToUEPerformanceSnapshot OuterSnapshot;
	FWebToUEPerformanceSnapshot InnerSnapshot;
	{
		FWebToUEPerformanceCapture OuterCapture;
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::StyleNodeVisits, 1);
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::TrackedAllocations);
		{
			FWebToUEPerformanceCapture InnerCapture;
			FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::StyleNodeVisits, 2);
			FWebToUEPerformanceCapture::RecordAllocationPayload(64);
			InnerSnapshot = InnerCapture.GetSnapshot();
		}
		FWebToUEPerformanceCapture::RecordCounter(EWebToUEPerformanceCounter::StyleNodeVisits, 4);
		FWebToUEPerformanceCapture::RecordAllocationPayload(32);
		OuterSnapshot = OuterCapture.GetSnapshot();
	}
	TestEqual(TEXT("The inner capture records only inner work"),
		InnerSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(2));
	TestEqual(TEXT("The outer capture excludes inner work and resumes afterward"),
		OuterSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(5));
	TestEqual(TEXT("The inner capture owns its known allocation event"),
		InnerSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocations), uint64(1));
	TestEqual(TEXT("The inner capture owns its known allocation payload bytes"),
		InnerSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes), uint64(64));
	TestEqual(TEXT("The outer capture counts one unknown and one known allocation event"),
		OuterSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocations), uint64(2));
	TestEqual(TEXT("The outer capture counts only its known allocation payload event"),
		OuterSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadEvents), uint64(1));
	TestEqual(TEXT("The outer capture excludes inner allocation payload bytes"),
		OuterSnapshot.GetCounter(EWebToUEPerformanceCounter::TrackedAllocationPayloadBytes), uint64(32));
	return true;
}

#endif
