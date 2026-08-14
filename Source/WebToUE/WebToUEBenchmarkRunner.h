#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "UObject/StrongObjectPtr.h"
#include "WebToUEPerformance.h"

class FSlateWindowElementList;
class ISlateViewportProvider;
class SWidget;
class SWindow;

struct FWebToUEBenchmarkFrameSample
{
	double GameThreadMs = 0.0;
	double RenderThreadMs = 0.0;
	double GpuMs = 0.0;
	double RhiInputToDisplayMs = 0.0;
	double HardwareInputToDisplayMs = 0.0;
	double RssMiB = 0.0;
	double LlmMiB = 0.0;
	double UiGeometricOverdraw = 0.0;
	int32 UiDrawElements = 0;
	int32 WindowSlateBatches = 0;
	int32 WindowSlateVertices = 0;
	int32 WindowSlateIndices = 0;
	int32 FrameDrawCalls = 0;
	int32 FramePrimitives = 0;
};

/** Command-line-only packaged benchmark runner; dormant in normal game launches. */
class FWebToUEBenchmarkRunner
{
public:
	FWebToUEBenchmarkRunner();
	~FWebToUEBenchmarkRunner();

	static bool IsRequested();
	void Start();
	void ObservePaint(FSlateWindowElementList& ElementList, int32 UiDrawElements,
		double UiCoverageArea, const FVector2f& AllottedSize,
		const FVector2f& DesiredSize, const FVector2f& TargetAllottedSize,
		const FSlateRect& CullingRect);

private:
	enum class EPhase : uint8
	{
		WaitingForViewport,
		Warmup,
		Measuring,
		WaitingForScreenshot,
		Complete
	};

	FString Mode;
	FName Corpus;
	FString OutputDirectory;
	FString ScreenshotPath;
	FString JsonPath;
	FString CsvPath;
	int32 WarmupFrames = 120;
	int32 RequestedSamples = 600;
	int32 LogicalFrame = 0;
	int32 TrajectoryStep = 0;
	bool bUmgTrajectoryEffectObserved = false;
	int32 ScreenshotWaitFrames = 0;
	EPhase Phase = EPhase::WaitingForViewport;
	uint64 UiSetupCycles = 0;
	uint64 FirstRenderCycles = 0;
	uint64 PendingInputCycles = 0;
	double ColdFirstFrameMs = 0.0;
	FVector2D LastPointerPosition = FVector2D::ZeroVector;
	FSlateWindowElementList* LastElementList = nullptr;
	int32 LastUiDrawElements = 0;
	double LastUiCoverageArea = 0.0;
	FVector2f LastTargetAllottedSize = FVector2f::ZeroVector;
	uint64 PaintObservationCount = 0;
	uint64 LastPaintEngineFrame = 0;
	uint64 MatchingRendererFrameCount = 0;
	uint64 StaleRendererFrameCount = 0;
	int32 CompiledNodeCount = 0;
	int32 CompiledRuleCount = 0;
	int32 CompiledBindingOpCount = 0;
	int32 CompiledResourceCount = 0;
	int32 CompiledRootNodeIndex = INDEX_NONE;
	FWebToUEPerformanceSnapshot SetupWorkload;
	FWebToUEPerformanceSnapshot WarmupWorkload;
	FWebToUEPerformanceSnapshot MeasurementWorkload;
	TArray<FWebToUEBenchmarkFrameSample> Samples;
	TArray<double> WarmInputToSlateSubmitMs;
	TArray<double> InputToBackBufferReadyMs;
	TArray<double> RhiInputToDisplayMs;
	TArray<double> HardwareInputToDisplayMs;
	TStrongObjectPtr<UObject> PrimaryUiObject;
	TStrongObjectPtr<UObject> DataContextObject;
	TSharedPtr<SWidget> TargetWidget;
	TSharedPtr<SWidget> InputTargetWidget;
	TSharedPtr<SWidget> ProbeWidget;
	TWeakPtr<SWindow> TargetWindow;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle RendererHandle;
	FDelegateHandle BackBufferHandle;
	void* TargetWindowPtr = nullptr;
	TAtomic<uint64> PendingBackBufferInputCycles{0};
	TAtomic<uint64> BackBufferBytes{0};
	TQueue<double, EQueueMode::Mpsc> PendingBackBufferLatencyMs;
	TUniquePtr<FWebToUEPerformanceCapture> PerformanceCapture;

	bool Tick(float DeltaSeconds);
	bool SetupUi();
	void ApplyTrajectory();
	void OnSlateWindowRendered(SWindow& Window);
	void OnBackBufferReady(SWindow& Window, ISlateViewportProvider& ViewportProvider);
	void RequestScreenshot();
	void Finish();
	void FailAndExit(const FString& Error);
	bool IsGameWindow(const SWindow& Window) const;
	FVector2D GetTrajectoryScreenPosition() const;
};
