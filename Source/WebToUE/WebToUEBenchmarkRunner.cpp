#include "WebToUEBenchmarkRunner.h"

#include "WebToUEBenchmarkUserWidget.h"
#include "WebToUEDemoViewModel.h"

#include "WebToUEDocument.h"
#include "WebToUEView.h"

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMemory.h"
#include "Input/Events.h"
#include "Layout/Children.h"
#include "InputCoreTypes.h"
#include "JsonObjectConverter.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/OutputDeviceNull.h"
#include "Performance/LatencyMarkerModule.h"
#include "RenderTimer.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateDrawBuffer.h"
#include "Rendering/SlateRenderer.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIStats.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Slate/SceneViewport.h"
#include "Slate/SlateViewportProvider.h"
#include "UnrealClient.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/SNullWidget.h"

namespace WebToUE::Benchmark::Private
{
	static constexpr double BytesToMiB = 1.0 / (1024.0 * 1024.0);

	static constexpr bool IsLlmCompiledIn()
	{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		return true;
#else
		return false;
#endif
	}

	static bool IsLlmEnabled()
	{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		return FLowLevelMemTracker::IsEnabled();
#else
		return false;
#endif
	}

	static double GetLlmMiB()
	{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		return FLowLevelMemTracker::IsEnabled()
			? FLowLevelMemTracker::Get().GetTotalTrackedMemory(ELLMTracker::Default) *
				BytesToMiB
			: 0.0;
#else
		return 0.0;
#endif
	}

	struct FElementMetrics
	{
		int32 Count = 0;
		double CoverageArea = 0.0;
	};

	template<EElementType ElementType>
	static void AccumulateElements(const FSlateDrawElementMap& Map, FElementMetrics& Metrics)
	{
		const auto& Elements = Map.Get<(uint8)ElementType>();
		Metrics.Count += Elements.Num();
		for (const auto& Element : Elements)
		{
			const FVector2f Size(Element.GetLocalSize());
			const double Scale = FMath::Abs(static_cast<double>(Element.GetScale()));
			Metrics.CoverageArea += FMath::Max(0.0, static_cast<double>(Size.X)) *
				FMath::Max(0.0, static_cast<double>(Size.Y)) * Scale * Scale;
		}
	}

	static FElementMetrics MeasureElements(const FSlateDrawElementMap& Map)
	{
		FElementMetrics Result;
		AccumulateElements<EElementType::ET_Box>(Map, Result);
		AccumulateElements<EElementType::ET_DebugQuad>(Map, Result);
		AccumulateElements<EElementType::ET_Text>(Map, Result);
		AccumulateElements<EElementType::ET_ShapedText>(Map, Result);
		AccumulateElements<EElementType::ET_Spline>(Map, Result);
		AccumulateElements<EElementType::ET_Line>(Map, Result);
		AccumulateElements<EElementType::ET_Gradient>(Map, Result);
		AccumulateElements<EElementType::ET_Viewport>(Map, Result);
		AccumulateElements<EElementType::ET_Border>(Map, Result);
		AccumulateElements<EElementType::ET_Custom>(Map, Result);
		AccumulateElements<EElementType::ET_CustomVerts>(Map, Result);
		AccumulateElements<EElementType::ET_PostProcessPass>(Map, Result);
		AccumulateElements<EElementType::ET_RoundedBox>(Map, Result);
		AccumulateElements<EElementType::ET_NonMapped>(Map, Result);
		return Result;
	}

	static void ForceVolatileRecursive(const TSharedRef<SWidget>& Widget)
	{
		Widget->ForceVolatile(true);
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				ForceVolatileRecursive(Children->GetChildAt(Index));
			}
		}
	}

	static FString GetBuildConfiguration()
	{
#if UE_BUILD_SHIPPING
		return TEXT("Shipping");
#elif UE_BUILD_TEST
		return TEXT("Test");
#elif UE_BUILD_DEBUG
		return TEXT("Debug");
#else
		return TEXT("Development");
#endif
	}

	static double Percentile(TConstArrayView<double> Values, double Fraction)
	{
		if (Values.IsEmpty()) return 0.0;
		TArray<double> Sorted;
		Sorted.Append(Values.GetData(), Values.Num());
		Sorted.Sort();
		const int32 Index = FMath::Clamp(FMath::CeilToInt(Fraction * Sorted.Num()) - 1,
			0, Sorted.Num() - 1);
		return Sorted[Index];
	}

	static TSharedRef<FJsonObject> Distribution(TConstArrayView<double> Values)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Values.Num());
		Result->SetNumberField(TEXT("p50"), Percentile(Values, 0.50));
		Result->SetNumberField(TEXT("p95"), Percentile(Values, 0.95));
		Result->SetNumberField(TEXT("p99"), Percentile(Values, 0.99));
		if (!Values.IsEmpty())
		{
			double Minimum = Values[0];
			double Maximum = Values[0];
			for (const double Value : Values)
			{
				Minimum = FMath::Min(Minimum, Value);
				Maximum = FMath::Max(Maximum, Value);
			}
			Result->SetNumberField(TEXT("min"), Minimum);
			Result->SetNumberField(TEXT("max"), Maximum);
		}
		return Result;
	}

	static TSharedRef<FJsonObject> WorkloadObject(
		const FWebToUEPerformanceSnapshot& Snapshot)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("telemetry_schema_version"),
			FWebToUEPerformanceSnapshot::TelemetrySchemaVersion);
		Snapshot.ForEachTelemetryMeasurement(
			[&Result](const TCHAR* Name, double Value)
			{
				Result->SetNumberField(Name, Value);
			});
		return Result;
	}

	static TArray<double> Select(TConstArrayView<FWebToUEBenchmarkFrameSample> Samples,
		double FWebToUEBenchmarkFrameSample::* Member)
	{
		TArray<double> Values;
		Values.Reserve(Samples.Num());
		for (const FWebToUEBenchmarkFrameSample& Sample : Samples)
		{
			Values.Add(Sample.*Member);
		}
		return Values;
	}

	static TArray<double> SelectInt(TConstArrayView<FWebToUEBenchmarkFrameSample> Samples,
		int32 FWebToUEBenchmarkFrameSample::* Member)
	{
		TArray<double> Values;
		Values.Reserve(Samples.Num());
		for (const FWebToUEBenchmarkFrameSample& Sample : Samples)
		{
			Values.Add(static_cast<double>(Sample.*Member));
		}
		return Values;
	}

	class SBenchmarkProbe final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SBenchmarkProbe) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ARGUMENT(FWebToUEBenchmarkRunner*, Owner)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			Owner = Args._Owner;
			TargetWidget = Args._Content.Widget;
			ChildSlot[Args._Content.Widget];
			ForceVolatile(true);
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
			const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& WidgetStyle,
			bool bParentEnabled) const override
		{
			const FElementMetrics Before = MeasureElements(
				OutDrawElements.GetUncachedDrawElements());
			const int32 Result = SCompoundWidget::OnPaint(Args, AllottedGeometry,
				CullingRect, OutDrawElements, LayerId, WidgetStyle, bParentEnabled);
			const FElementMetrics After = MeasureElements(
				OutDrawElements.GetUncachedDrawElements());
			if (Owner)
			{
				Owner->ObservePaint(OutDrawElements,
					FMath::Max(0, After.Count - Before.Count),
					FMath::Max(0.0, After.CoverageArea - Before.CoverageArea),
					FVector2f(AllottedGeometry.GetLocalSize()),
					FVector2f(GetDesiredSize()),
					TargetWidget.IsValid()
						? FVector2f(TargetWidget->GetPaintSpaceGeometry().GetLocalSize())
						: FVector2f::ZeroVector,
					CullingRect);
			}
			return Result;
		}

	private:
		FWebToUEBenchmarkRunner* Owner = nullptr;
		TSharedPtr<SWidget> TargetWidget;
	};
}

FWebToUEBenchmarkRunner::FWebToUEBenchmarkRunner()
{
	FParse::Value(FCommandLine::Get(), TEXT("WTUEBenchmark="), Mode);
	FString CorpusString;
	FParse::Value(FCommandLine::Get(), TEXT("WTUECorpus="), CorpusString);
	Corpus = FName(CorpusString);
	FParse::Value(FCommandLine::Get(), TEXT("WTUEWarmupFrames="), WarmupFrames);
	FParse::Value(FCommandLine::Get(), TEXT("WTUESamples="), RequestedSamples);
	FParse::Value(FCommandLine::Get(), TEXT("WTUEOutput="), OutputDirectory);
	WarmupFrames = FMath::Clamp(WarmupFrames, 10, 10000);
	RequestedSamples = FMath::Clamp(RequestedSamples, 30, 100000);
	if (OutputDirectory.IsEmpty())
	{
		OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WebToUEBenchmarks"),
			FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")));
	}
	FPaths::NormalizeDirectoryName(OutputDirectory);
	ScreenshotPath = FPaths::Combine(OutputDirectory,
		FString::Printf(TEXT("%s-%s.png"), *Mode, *Corpus.ToString()));
	JsonPath = FPaths::Combine(OutputDirectory, TEXT("result.json"));
	CsvPath = FPaths::Combine(OutputDirectory, TEXT("frames.csv"));
}

FWebToUEBenchmarkRunner::~FWebToUEBenchmarkRunner()
{
	if (RendererHandle.IsValid() && FSlateApplication::IsInitialized() &&
		FSlateApplication::Get().GetRenderer())
	{
		FSlateApplication::Get().GetRenderer()->OnSlateWindowRendered().Remove(RendererHandle);
	}
	if (BackBufferHandle.IsValid() && FSlateApplication::IsInitialized() &&
		FSlateApplication::Get().GetRenderer())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(
			BackBufferHandle);
	}
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}
	if (ProbeWidget.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(ProbeWidget.ToSharedRef());
		}
	}
	PerformanceCapture.Reset();
}

bool FWebToUEBenchmarkRunner::IsRequested()
{
	FString Value;
	return FParse::Value(FCommandLine::Get(), TEXT("WTUEBenchmark="), Value) &&
		!Value.IsEmpty();
}

void FWebToUEBenchmarkRunner::Start()
{
	PerformanceCapture = MakeUnique<FWebToUEPerformanceCapture>();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDirectory);
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		RendererHandle = FSlateApplication::Get().GetRenderer()->OnSlateWindowRendered()
			.AddRaw(this, &FWebToUEBenchmarkRunner::OnSlateWindowRendered);
		BackBufferHandle = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent()
			.AddRaw(this, &FWebToUEBenchmarkRunner::OnBackBufferReady);
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FWebToUEBenchmarkRunner::Tick));
	UE_LOG(LogTemp, Display, TEXT("WTUE_BENCHMARK_START mode=%s corpus=%s output=%s"),
		*Mode, *Corpus.ToString(), *OutputDirectory);
}

void FWebToUEBenchmarkRunner::ObservePaint(FSlateWindowElementList& ElementList,
	int32 UiDrawElements, double UiCoverageArea, const FVector2f& AllottedSize,
	const FVector2f& DesiredSize, const FVector2f& TargetAllottedSize,
	const FSlateRect& CullingRect)
{
	LastElementList = &ElementList;
	LastUiDrawElements = UiDrawElements;
	LastUiCoverageArea = UiCoverageArea;
	LastTargetAllottedSize = TargetAllottedSize;
	LastPaintEngineFrame = GFrameCounter;
	++PaintObservationCount;
	if (PaintObservationCount <= 3)
	{
		UE_LOG(LogTemp, Display,
			TEXT("WTUE_BENCHMARK_PAINT observation=%llu engine_frame=%llu elements=%d coverage=%.3f probe_allotted=(%.1f,%.1f) target_allotted=(%.1f,%.1f) desired=(%.1f,%.1f) culling=(%.1f,%.1f,%.1f,%.1f) paint_window=%p target_window=%p"),
			PaintObservationCount, LastPaintEngineFrame, UiDrawElements, UiCoverageArea,
			AllottedSize.X, AllottedSize.Y, TargetAllottedSize.X,
			TargetAllottedSize.Y, DesiredSize.X, DesiredSize.Y,
			CullingRect.Left, CullingRect.Top, CullingRect.Right, CullingRect.Bottom,
			ElementList.GetPaintWindow(), TargetWindowPtr);
	}
}

bool FWebToUEBenchmarkRunner::Tick(float DeltaSeconds)
{
	if (Phase == EPhase::Complete) return false;
	if (Phase == EPhase::WaitingForViewport)
	{
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetWorld() ||
			!GEngine->GameViewport->GetWorld()->HasBegunPlay() ||
			!GEngine->GameViewport->GetWindow().IsValid() ||
			!GEngine->GameViewport->GetWindow()->HasOverlay() ||
			!FSlateApplication::IsInitialized())
		{
			return true;
		}
		if (!SetupUi()) return false;
		Phase = EPhase::Warmup;
		return true;
	}
	if (ProbeWidget.IsValid())
	{
		ProbeWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
	if (Mode == TEXT("UMG"))
	{
		if (UWebToUEBenchmarkUserWidget* Widget =
			Cast<UWebToUEBenchmarkUserWidget>(PrimaryUiObject.Get()))
		{
			bUmgTrajectoryEffectObserved |= Widget->ObserveTrajectoryEffect();
		}
	}

	++LogicalFrame;
	if ((LogicalFrame == 30 || (LogicalFrame > WarmupFrames && LogicalFrame % 60 == 0)) &&
		Phase != EPhase::WaitingForScreenshot)
	{
		ApplyTrajectory();
	}
	if (Phase == EPhase::Warmup && LogicalFrame >= WarmupFrames)
	{
		Phase = EPhase::Measuring;
		Samples.Reserve(RequestedSamples);
		if (PerformanceCapture)
		{
			WarmupWorkload = PerformanceCapture->GetSnapshot();
			PerformanceCapture->Reset();
		}
	}
	if (Phase == EPhase::Measuring && Samples.Num() >= RequestedSamples)
	{
		RequestScreenshot();
	}
	else if (Phase == EPhase::WaitingForScreenshot)
	{
		++ScreenshotWaitFrames;
		if ((FPaths::FileExists(ScreenshotPath) && ScreenshotWaitFrames >= 3) ||
			ScreenshotWaitFrames >= 180)
		{
			Finish();
			return false;
		}
	}
	return true;
}

bool FWebToUEBenchmarkRunner::SetupUi()
{
	if ((Mode != TEXT("WebToUE") && Mode != TEXT("UMG")) ||
		(Corpus != TEXT("MainMenu") && Corpus != TEXT("HUD") &&
			Corpus != TEXT("ScrollableSettings")))
	{
		FailAndExit(TEXT("Invalid -WTUEBenchmark or -WTUECorpus value"));
		return false;
	}
	UiSetupCycles = FPlatformTime::Cycles64();
	TSharedPtr<SWidget> BuiltTargetWidget;
	UWorld* World = GEngine->GameViewport->GetWorld();
	if (Mode == TEXT("WebToUE"))
	{
		const FString AssetPath = FString::Printf(TEXT("/Game/WebToUEExamples/%s.%s"),
			*Corpus.ToString(), *Corpus.ToString());
		UWebToUEDocument* Document = LoadObject<UWebToUEDocument>(nullptr, *AssetPath);
		if (!Document || Document->GetCompiledNodes().IsEmpty())
		{
			FailAndExit(FString::Printf(TEXT("Missing or empty document asset: %s"), *AssetPath));
			return false;
		}
		CompiledNodeCount = Document->GetCompiledNodes().Num();
		CompiledRuleCount = Document->GetCompiledRules().Num();
		CompiledBindingOpCount = Document->GetCompiledBindingOps().Num();
		CompiledResourceCount = Document->GetResourceManifest().Num();
		CompiledRootNodeIndex = Document->GetRootNodeIndex();
		UE_LOG(LogTemp, Display,
			TEXT("WTUE_BENCHMARK_DOCUMENT path=%s nodes=%d rules=%d binding_ops=%d resources=%d root=%d root_valid=%s"),
			*AssetPath, CompiledNodeCount, CompiledRuleCount, CompiledBindingOpCount,
			CompiledResourceCount, CompiledRootNodeIndex,
			Document->GetCompiledNodes().IsValidIndex(CompiledRootNodeIndex)
				? TEXT("true") : TEXT("false"));
		UWebToUEView* View = NewObject<UWebToUEView>(GetTransientPackage());
		UWebToUEDemoViewModel* ViewModel = NewObject<UWebToUEDemoViewModel>(GetTransientPackage());
		View->SetDocument(Document);
		View->SetDataContext(ViewModel);
		PrimaryUiObject.Reset(View);
		DataContextObject.Reset(ViewModel);
		BuiltTargetWidget = View->TakeWidget();
		InputTargetWidget = BuiltTargetWidget;
	}
	else
	{
		UWebToUEBenchmarkUserWidget* Widget = CreateWidget<UWebToUEBenchmarkUserWidget>(
			World, UWebToUEBenchmarkUserWidget::StaticClass());
		if (!Widget || !Widget->Configure(Corpus))
		{
			FailAndExit(TEXT("Failed to build the UMG corpus widget tree"));
			return false;
		}
		PrimaryUiObject.Reset(Widget);
		BuiltTargetWidget = Widget->TakeWidget();
		InputTargetWidget = Widget->GetTrajectoryInputWidget();
	}
	if (!BuiltTargetWidget.IsValid())
	{
		FailAndExit(TEXT("Benchmark target did not produce a Slate widget"));
		return false;
	}
	TargetWidget = BuiltTargetWidget;
	if (!InputTargetWidget.IsValid()) InputTargetWidget = BuiltTargetWidget;
	BuiltTargetWidget->SlatePrepass(1.0f);
	if (PerformanceCapture)
	{
		SetupWorkload = PerformanceCapture->GetSnapshot();
		PerformanceCapture->Reset();
	}
	const FVector2f TargetDesiredSize(BuiltTargetWidget->GetDesiredSize());
	UE_LOG(LogTemp, Display,
		TEXT("WTUE_BENCHMARK_TARGET mode=%s corpus=%s type=%s desired=(%.1f,%.1f) visibility=%s"),
		*Mode, *Corpus.ToString(), *BuiltTargetWidget->GetTypeAsString(),
		TargetDesiredSize.X, TargetDesiredSize.Y,
		*BuiltTargetWidget->GetVisibility().ToString());
	WebToUE::Benchmark::Private::ForceVolatileRecursive(BuiltTargetWidget.ToSharedRef());
	const TSharedRef<WebToUE::Benchmark::Private::SBenchmarkProbe> MeasuringProbe =
		SNew(WebToUE::Benchmark::Private::SBenchmarkProbe)
		.Owner(this)[BuiltTargetWidget.ToSharedRef()];
	TSharedRef<SInvalidationPanel> UncachedRoot = SNew(SInvalidationPanel)[MeasuringProbe];
	UncachedRoot->SetCanCache(false);
	UncachedRoot->ForceVolatile(true);
	ProbeWidget = UncachedRoot;
	TargetWindow = GEngine->GameViewport->GetWindow();
	TargetWindowPtr = TargetWindow.Pin().Get();
	GEngine->GameViewport->AddViewportWidgetContent(ProbeWidget.ToSharedRef(), 1000);
	UE_LOG(LogTemp, Display, TEXT("WTUE_BENCHMARK_UI_ATTACHED window=%p probe=%p"),
		TargetWindowPtr, ProbeWidget.Get());
	return true;
}

FVector2D FWebToUEBenchmarkRunner::GetTrajectoryScreenPosition() const
{
	if (Mode == TEXT("UMG") && InputTargetWidget.IsValid() &&
		Corpus != TEXT("HUD"))
	{
		const FGeometry& Geometry = InputTargetWidget->GetPaintSpaceGeometry();
		return Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize()) * 0.5);
	}
	if (TargetWidget.IsValid())
	{
		const FGeometry& Geometry = TargetWidget->GetPaintSpaceGeometry();
		const FVector2D LocalSize = Geometry.GetLocalSize();
		if (Corpus == TEXT("MainMenu"))
		{
			return Geometry.LocalToAbsolute(FVector2D(
				(TrajectoryStep & 1) ? LocalSize.X * 0.8 : 300.0,
				(TrajectoryStep & 1) ? LocalSize.Y * 0.2 : 608.0));
		}
		if (Corpus == TEXT("ScrollableSettings"))
		{
			return Geometry.LocalToAbsolute(FVector2D(
				LocalSize.X * 0.5, LocalSize.Y * 0.56));
		}
	}
	FVector2D WindowOrigin = FVector2D::ZeroVector;
	if (GEngine && GEngine->GameViewport)
	{
		if (const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
		{
			WindowOrigin = FVector2D(Window->GetPositionInScreen());
		}
	}
	if (Corpus == TEXT("MainMenu"))
	{
		return WindowOrigin + FVector2D(200.0, (TrajectoryStep & 1) ? 450.0 : 405.0);
	}
	if (Corpus == TEXT("ScrollableSettings"))
	{
		return WindowOrigin + FVector2D(640.0, 420.0);
	}
	return WindowOrigin + FVector2D(170.0, 135.0);
}

void FWebToUEBenchmarkRunner::ApplyTrajectory()
{
	++TrajectoryStep;
	PendingInputCycles = FPlatformTime::Cycles64();
	PendingBackBufferInputCycles.Store(PendingInputCycles);
	UEngine::SetInputSampleLatencyMarker(GFrameCounter);
	if (Corpus == TEXT("HUD"))
	{
		if (Mode == TEXT("WebToUE"))
		{
			if (UWebToUEDemoViewModel* ViewModel = Cast<UWebToUEDemoViewModel>(
				DataContextObject.Get()))
			{
				ViewModel->SetHealth((TrajectoryStep & 1) ? 20 : 85, 100);
			}
		}
		else if (UWebToUEBenchmarkUserWidget* Widget =
			Cast<UWebToUEBenchmarkUserWidget>(PrimaryUiObject.Get()))
		{
			bUmgTrajectoryEffectObserved |= Widget->ApplyTrajectory(TrajectoryStep);
		}
		return;
	}

	FSlateApplication& Slate = FSlateApplication::Get();
	const FVector2D Position = GetTrajectoryScreenPosition();
	FWidgetPath InputPath;
	auto GeneratePointerPath = [&Slate](const TSharedRef<SWidget>& Widget,
		FWidgetPath& OutPath)
	{
		FWidgetPath ArrangedPath;
		if (!Slate.GeneratePathToWidgetUnchecked(
			Widget, ArrangedPath, EVisibility::Visible))
		{
			return false;
		}
		TArray<FWidgetAndPointer> WidgetsAndPointers;
		WidgetsAndPointers.Reserve(ArrangedPath.Widgets.Num());
		for (int32 Index = 0; Index < ArrangedPath.Widgets.Num(); ++Index)
		{
			WidgetsAndPointers.Emplace(ArrangedPath.Widgets[Index]);
		}
		OutPath = FWidgetPath(WidgetsAndPointers);
		return OutPath.IsValid();
	};
	const bool bRouteToInput = Corpus != TEXT("MainMenu") || !(TrajectoryStep & 1);
	if (bRouteToInput && InputTargetWidget.IsValid())
	{
		GeneratePointerPath(InputTargetWidget.ToSharedRef(), InputPath);
	}
	else if (Mode == TEXT("WebToUE") && TargetWidget.IsValid())
	{
		// WebToUE owns one leaf widget and performs DOM hit testing internally, so
		// route the blank-space sample to the root to generate a real mouse leave.
		GeneratePointerPath(TargetWidget.ToSharedRef(), InputPath);
	}
	const FPointerEvent MoveEvent(0, Position, LastPointerPosition, TSet<FKey>(),
		FKey(), 0.0f, FModifierKeysState());
	Slate.SetCursorPos(Position);
	if (Mode == TEXT("WebToUE") && TargetWidget.IsValid())
	{
		TargetWidget->OnMouseMove(TargetWidget->GetPaintSpaceGeometry(), MoveEvent);
	}
	else
	{
		Slate.RoutePointerMoveEvent(InputPath, MoveEvent, false);
	}
	LastPointerPosition = Position;
	if (Corpus == TEXT("ScrollableSettings"))
	{
		const float WheelDelta = (TrajectoryStep & 1) ? -3.0f : 3.0f;
		const FPointerEvent WheelEvent(0, Position, Position, TSet<FKey>(),
			FKey(), WheelDelta, FModifierKeysState());
		if (Mode == TEXT("WebToUE") && TargetWidget.IsValid())
		{
			TargetWidget->OnMouseWheel(
				TargetWidget->GetPaintSpaceGeometry(), WheelEvent);
		}
		else
		{
			Slate.RouteMouseWheelOrGestureEvent(InputPath, WheelEvent, nullptr);
		}
	}
	else if ((TrajectoryStep % 4) == 0)
	{
		TSet<FKey> PressedButtons;
		PressedButtons.Add(EKeys::LeftMouseButton);
		const FPointerEvent DownEvent(0, Position, Position, PressedButtons,
			EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
		if (Mode == TEXT("WebToUE") && TargetWidget.IsValid())
		{
			TargetWidget->OnMouseButtonDown(
				TargetWidget->GetPaintSpaceGeometry(), DownEvent);
		}
		else
		{
			Slate.RoutePointerDownEvent(InputPath, DownEvent);
		}
		const FPointerEvent UpEvent(0, Position, Position, TSet<FKey>(),
			EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
		if (Mode == TEXT("WebToUE") && TargetWidget.IsValid())
		{
			TargetWidget->OnMouseButtonUp(
				TargetWidget->GetPaintSpaceGeometry(), UpEvent);
		}
		else
		{
			Slate.RoutePointerUpEvent(InputPath, UpEvent);
		}
	}
}

bool FWebToUEBenchmarkRunner::IsGameWindow(const SWindow& Window) const
{
	if (!GEngine || !GEngine->GameViewport) return false;
	const TSharedPtr<SWindow> GameWindow = GEngine->GameViewport->GetWindow();
	return GameWindow.IsValid() && GameWindow.Get() == &Window;
}

void FWebToUEBenchmarkRunner::OnSlateWindowRendered(SWindow& Window)
{
	using namespace WebToUE::Benchmark::Private;
	if (!IsGameWindow(Window) || !LastElementList || Phase == EPhase::Complete) return;
	const bool bPaintMatchesRendererFrame = LastPaintEngineFrame == GFrameCounter &&
		LastElementList->GetPaintWindow() == &Window;
	if (bPaintMatchesRendererFrame)
	{
		++MatchingRendererFrameCount;
	}
	else
	{
		++StaleRendererFrameCount;
		if (StaleRendererFrameCount <= 3)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("WTUE_BENCHMARK_STALE_PAINT renderer_frame=%llu paint_frame=%llu paint_window=%p target_window=%p"),
				GFrameCounter, LastPaintEngineFrame, LastElementList->GetPaintWindow(),
				TargetWindowPtr);
		}
	}
	double BackBufferLatencyMs = 0.0;
	while (PendingBackBufferLatencyMs.Dequeue(BackBufferLatencyMs))
	{
		InputToBackBufferReadyMs.Add(BackBufferLatencyMs);
	}
	if (FirstRenderCycles == 0 && UiSetupCycles != 0)
	{
		FirstRenderCycles = FPlatformTime::Cycles64();
		ColdFirstFrameMs = FPlatformTime::ToMilliseconds64(FirstRenderCycles - UiSetupCycles);
	}
	if (PendingInputCycles != 0)
	{
		WarmInputToSlateSubmitMs.Add(FPlatformTime::ToMilliseconds64(
			FPlatformTime::Cycles64() - PendingInputCycles));
		PendingInputCycles = 0;
	}
	if (Phase != EPhase::Measuring || Samples.Num() >= RequestedSamples) return;

	FWebToUEBenchmarkFrameSample Sample;
	Sample.GameThreadMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
	Sample.RenderThreadMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	Sample.GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	Sample.RhiInputToDisplayMs = GInputLatencyTime > 0
		? FPlatformTime::ToMilliseconds64(GInputLatencyTime) : 0.0;
	if (Sample.RhiInputToDisplayMs > 0.0)
	{
		RhiInputToDisplayMs.Add(Sample.RhiInputToDisplayMs);
	}
	for (ILatencyMarkerModule* Module : IModularFeatures::Get()
		.GetModularFeatureImplementations<ILatencyMarkerModule>(
			ILatencyMarkerModule::GetModularFeatureName()))
	{
		if (Module && Module->GetAvailable())
		{
			Sample.HardwareInputToDisplayMs = Module->GetTotalLatencyInMs();
			break;
		}
	}
	if (Sample.HardwareInputToDisplayMs > 0.0)
	{
		HardwareInputToDisplayMs.Add(Sample.HardwareInputToDisplayMs);
	}
	const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
	Sample.RssMiB = Memory.UsedPhysical * BytesToMiB;
	Sample.LlmMiB = GetLlmMiB();
	Sample.UiDrawElements = LastUiDrawElements;
	const FIntPoint ViewportSize = GEngine->GameViewport->Viewport
		? GEngine->GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	const double ViewportArea = static_cast<double>(ViewportSize.X) * ViewportSize.Y;
	Sample.UiGeometricOverdraw = ViewportArea > 0.0
		? LastUiCoverageArea / ViewportArea : 0.0;
	FSlateBatchData& BatchData = LastElementList->GetBatchData();
	Sample.WindowSlateBatches = BatchData.GetNumFinalBatches();
	Sample.WindowSlateVertices = BatchData.GetFinalVertexData().Num();
	Sample.WindowSlateIndices = BatchData.GetFinalIndexData().Num();
	Sample.FrameDrawCalls = GNumDrawCallsRHI[0];
	Sample.FramePrimitives = GNumPrimitivesDrawnRHI[0];
	Samples.Add(Sample);
}

void FWebToUEBenchmarkRunner::OnBackBufferReady(SWindow& Window,
	ISlateViewportProvider& ViewportProvider)
{
	if (&Window != TargetWindowPtr) return;
	if (FRHITexture* BackBuffer = ViewportProvider.GetBackBufferResource())
	{
		BackBufferBytes.Store(BackBuffer->GetDesc().CalcMemorySizeEstimate());
	}
	const uint64 InputCycles = PendingBackBufferInputCycles.Exchange(0);
	if (InputCycles != 0)
	{
		PendingBackBufferLatencyMs.Enqueue(FPlatformTime::ToMilliseconds64(
			FPlatformTime::Cycles64() - InputCycles));
	}
}

void FWebToUEBenchmarkRunner::RequestScreenshot()
{
	if (Phase != EPhase::Measuring) return;
	Phase = EPhase::WaitingForScreenshot;
	ScreenshotWaitFrames = 0;
	FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
}

void FWebToUEBenchmarkRunner::Finish()
{
	using namespace WebToUE::Benchmark::Private;
	Phase = EPhase::Complete;
	const bool bScreenshotExists = FPaths::FileExists(ScreenshotPath);
	const bool bHasRendererEvidence = Samples.ContainsByPredicate(
		[](const FWebToUEBenchmarkFrameSample& Sample)
		{
			return Sample.UiDrawElements > 0 && Sample.WindowSlateBatches > 0 &&
				Sample.WindowSlateVertices > 0 && Sample.GpuMs > 0.0;
		});
	double BackBufferLatencyMs = 0.0;
	while (PendingBackBufferLatencyMs.Dequeue(BackBufferLatencyMs))
	{
		InputToBackBufferReadyMs.Add(BackBufferLatencyMs);
	}
	const bool bHasInputToDisplay = !InputToBackBufferReadyMs.IsEmpty() ||
		!RhiInputToDisplayMs.IsEmpty() || !HardwareInputToDisplayMs.IsEmpty();
	if (PerformanceCapture)
	{
		MeasurementWorkload = PerformanceCapture->GetSnapshot();
	}
	const bool bHasWebToUERuntimeWork = Mode != TEXT("WebToUE") ||
		(CompiledNodeCount > 0 &&
			SetupWorkload.GetCounter(EWebToUEPerformanceCounter::HydratedNodes) ==
				static_cast<uint64>(CompiledNodeCount) &&
			WarmupWorkload.GetCounter(EWebToUEPerformanceCounter::DisplayListBuilds) > 0 &&
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::PaintDrawElements) > 0);
	bool bHasTrajectoryEvidence = TrajectoryStep > 0;
	if (Mode == TEXT("UMG"))
	{
		bHasTrajectoryEvidence &= bUmgTrajectoryEffectObserved;
	}
	else if (Corpus == TEXT("HUD"))
	{
		bHasTrajectoryEvidence &=
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::BindingFieldsRead) > 0 &&
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::BindingOpsExecuted) > 0 &&
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::BindingNodesUpdated) > 0;
	}
	else
	{
		bHasTrajectoryEvidence &=
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::HitTestCandidates) > 0 &&
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::HitTestCommandsVisited) > 0 &&
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::DisplayCommandsPatched) > 0 &&
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::DirtyRectsAdded) > 0;
		if (Corpus == TEXT("MainMenu"))
		{
			bHasTrajectoryEvidence &=
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::PseudoStateNodesChanged) > 0;
		}
		else if (Corpus == TEXT("ScrollableSettings"))
		{
			bHasTrajectoryEvidence &=
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::DisplaySpatialIndexPatches) > 0;
		}
	}
	const bool bSuccess = Samples.Num() == RequestedSamples && bScreenshotExists &&
		bHasRendererEvidence && bHasInputToDisplay && bHasWebToUERuntimeWork &&
		bHasTrajectoryEvidence;

	FString Csv(TEXT("frame,gt_ms,rt_ms,gpu_ms,ui_draw_elements,window_slate_batches,"
		"window_slate_vertices,window_slate_indices,ui_geometric_overdraw_ratio,"
		"frame_draw_calls,frame_primitives,rss_mib,llm_mib,rhi_input_to_display_ms,"
		"hardware_input_to_display_ms\n"));
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const FWebToUEBenchmarkFrameSample& Sample = Samples[Index];
		Csv += FString::Printf(TEXT("%d,%.6f,%.6f,%.6f,%d,%d,%d,%d,%.6f,%d,%d,"
			"%.6f,%.6f,%.6f,%.6f\n"), Index, Sample.GameThreadMs,
			Sample.RenderThreadMs, Sample.GpuMs, Sample.UiDrawElements,
			Sample.WindowSlateBatches, Sample.WindowSlateVertices,
			Sample.WindowSlateIndices, Sample.UiGeometricOverdraw,
			Sample.FrameDrawCalls, Sample.FramePrimitives, Sample.RssMiB,
			Sample.LlmMiB, Sample.RhiInputToDisplayMs,
			Sample.HardwareInputToDisplayMs);
	}
	FFileHelper::SaveStringToFile(Csv, *CsvPath);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 5);
	Root->SetBoolField(TEXT("success"), bSuccess);
	Root->SetStringField(TEXT("mode"), Mode);
	Root->SetStringField(TEXT("corpus"), Corpus.ToString());
	Root->SetStringField(TEXT("build_configuration"), GetBuildConfiguration());
	Root->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("rhi"), GDynamicRHI ? FString(GDynamicRHI->GetName()) : TEXT("Unknown"));
	Root->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand());
	Root->SetStringField(TEXT("gpu"), GRHIAdapterName);
	Root->SetStringField(TEXT("trajectory"), Corpus == TEXT("HUD")
		? TEXT("health FieldNotify/manual text toggle")
		: Corpus == TEXT("MainMenu") ? TEXT("pointer hover plus periodic click")
		: TEXT("pointer hover plus bidirectional wheel scroll"));
	TSharedRef<FJsonObject> TrajectoryEvidence = MakeShared<FJsonObject>();
	TrajectoryEvidence->SetNumberField(TEXT("steps_dispatched"), TrajectoryStep);
	TrajectoryEvidence->SetBoolField(TEXT("ui_effect_observed"), bHasTrajectoryEvidence);
	TrajectoryEvidence->SetStringField(TEXT("contract"), Mode == TEXT("UMG")
		? TEXT("UMG counterpart reports the resulting hover, scroll-offset, or HUD text/visibility state change.")
		: Corpus == TEXT("HUD")
			? TEXT("Measurement window records binding field reads, executed ops, and updated nodes.")
			: Corpus == TEXT("MainMenu")
				? TEXT("Measurement window records spatial hit candidates/visits, pseudo-state change, local display patch, and dirty rect.")
				: TEXT("Measurement window records spatial hit candidates/visits, local display/spatial-index patch, and dirty rect."));
	Root->SetObjectField(TEXT("trajectory_evidence"), TrajectoryEvidence);
	Root->SetNumberField(TEXT("warmup_frames"), WarmupFrames);
	Root->SetNumberField(TEXT("sample_frames"), Samples.Num());
	Root->SetNumberField(TEXT("paint_observations"), PaintObservationCount);
	Root->SetNumberField(TEXT("matching_renderer_frames"), MatchingRendererFrameCount);
	Root->SetNumberField(TEXT("stale_renderer_frames"), StaleRendererFrameCount);
	TSharedRef<FJsonObject> TargetGeometry = MakeShared<FJsonObject>();
	TargetGeometry->SetNumberField(TEXT("width"), LastTargetAllottedSize.X);
	TargetGeometry->SetNumberField(TEXT("height"), LastTargetAllottedSize.Y);
	Root->SetObjectField(TEXT("target_paint_geometry"), TargetGeometry);
	TSharedRef<FJsonObject> DocumentContract = MakeShared<FJsonObject>();
	DocumentContract->SetNumberField(TEXT("compiled_nodes"), CompiledNodeCount);
	DocumentContract->SetNumberField(TEXT("compiled_rules"), CompiledRuleCount);
	DocumentContract->SetNumberField(TEXT("compiled_binding_ops"), CompiledBindingOpCount);
	DocumentContract->SetNumberField(TEXT("compiled_resources"), CompiledResourceCount);
	DocumentContract->SetNumberField(TEXT("root_node_index"), CompiledRootNodeIndex);
	DocumentContract->SetBoolField(TEXT("root_node_valid"), Mode != TEXT("WebToUE") ||
		(CompiledRootNodeIndex >= 0 && CompiledRootNodeIndex < CompiledNodeCount));
	Root->SetObjectField(TEXT("compiled_document"), DocumentContract);
	Root->SetObjectField(TEXT("setup_workload"), WorkloadObject(SetupWorkload));
	Root->SetObjectField(TEXT("warmup_workload"), WorkloadObject(WarmupWorkload));
	Root->SetObjectField(TEXT("measurement_workload"), WorkloadObject(MeasurementWorkload));
	Root->SetStringField(TEXT("workload_contract"),
		TEXT("Setup captures load/hydrate/prepass for K=1; warmup captures first layout/display-list construction; measurement resets after warmup and reports aggregate WebToUE-owned phase and workload counters over the sample window."));
	Root->SetNumberField(TEXT("cold_first_frame_ms"), ColdFirstFrameMs);
	Root->SetObjectField(TEXT("warm_input_to_slate_submit_ms"),
		Distribution(WarmInputToSlateSubmitMs));
	Root->SetObjectField(TEXT("game_thread_ms"),
		Distribution(Select(Samples, &FWebToUEBenchmarkFrameSample::GameThreadMs)));
	Root->SetObjectField(TEXT("render_thread_ms"),
		Distribution(Select(Samples, &FWebToUEBenchmarkFrameSample::RenderThreadMs)));
	Root->SetObjectField(TEXT("gpu_ms"),
		Distribution(Select(Samples, &FWebToUEBenchmarkFrameSample::GpuMs)));
	Root->SetObjectField(TEXT("ui_draw_elements"),
		Distribution(SelectInt(Samples, &FWebToUEBenchmarkFrameSample::UiDrawElements)));
	Root->SetObjectField(TEXT("window_slate_batches"),
		Distribution(SelectInt(Samples, &FWebToUEBenchmarkFrameSample::WindowSlateBatches)));
	Root->SetObjectField(TEXT("window_slate_vertices"),
		Distribution(SelectInt(Samples, &FWebToUEBenchmarkFrameSample::WindowSlateVertices)));
	Root->SetObjectField(TEXT("ui_geometric_overdraw_ratio"),
		Distribution(Select(Samples, &FWebToUEBenchmarkFrameSample::UiGeometricOverdraw)));
	Root->SetObjectField(TEXT("rss_mib"),
		Distribution(Select(Samples, &FWebToUEBenchmarkFrameSample::RssMiB)));
	Root->SetBoolField(TEXT("llm_compiled_in"), IsLlmCompiledIn());
	Root->SetBoolField(TEXT("llm_enabled"), IsLlmEnabled());
	Root->SetStringField(TEXT("llm_availability"),
		!IsLlmCompiledIn() ? TEXT("not_compiled_for_configuration")
		: (IsLlmEnabled() ? TEXT("available") : TEXT("compiled_runtime_disabled")));
	Root->SetObjectField(TEXT("llm_mib"),
		Distribution(Select(Samples, &FWebToUEBenchmarkFrameSample::LlmMiB)));
	Root->SetObjectField(TEXT("rhi_input_to_display_ms"), Distribution(RhiInputToDisplayMs));
	Root->SetObjectField(TEXT("hardware_input_to_display_ms"),
		Distribution(HardwareInputToDisplayMs));
	Root->SetObjectField(TEXT("input_to_backbuffer_ready_ms"),
		Distribution(InputToBackBufferReadyMs));
	Root->SetStringField(TEXT("input_latency_contract"),
		TEXT("Backbuffer-ready measures scripted input dispatch to final GPU backbuffer-ready callback before present; RHI value extends to display composition when flip details are available; warm input-to-Slate-submit is diagnostic only."));
	Root->SetStringField(TEXT("batch_contract"),
		TEXT("Draw Elements and geometric coverage are probe-child scoped; final Slate batches/vertices are exact whole-game-window renderer output."));
	Root->SetStringField(TEXT("screenshot"), ScreenshotPath);
	Root->SetBoolField(TEXT("screenshot_exists"), bScreenshotExists);
	Root->SetStringField(TEXT("frames_csv"), CsvPath);

	FTextureMemoryStats TextureMemory;
	RHIGetTextureMemoryStats(TextureMemory);
	TSharedRef<FJsonObject> Vram = MakeShared<FJsonObject>();
	Vram->SetNumberField(TEXT("dedicated_video_memory_mib"),
		TextureMemory.DedicatedVideoMemory * BytesToMiB);
	Vram->SetNumberField(TEXT("total_graphics_memory_mib"),
		TextureMemory.TotalGraphicsMemory * BytesToMiB);
	Vram->SetNumberField(TEXT("streaming_texture_mib"),
		TextureMemory.StreamingMemorySize * BytesToMiB);
	Vram->SetNumberField(TEXT("non_streaming_texture_mib"),
		TextureMemory.NonStreamingMemorySize * BytesToMiB);
	Vram->SetNumberField(TEXT("texture_pool_mib"), TextureMemory.TexturePoolSize * BytesToMiB);
	uint64 RenderTargetBytes = BackBufferBytes.Load();
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FTextureRHIRef& RenderTarget =
			GEngine->GameViewport->Viewport->GetRenderTargetTexture();
		if (RenderTarget.IsValid())
		{
			RenderTargetBytes = RenderTarget->GetDesc().CalcMemorySizeEstimate();
		}
	}
	Vram->SetNumberField(TEXT("viewport_render_target_mib"), RenderTargetBytes * BytesToMiB);
	Root->SetObjectField(TEXT("vram"), Vram);

	if (!bSuccess)
	{
		TArray<TSharedPtr<FJsonValue>> Failures;
		if (Samples.Num() != RequestedSamples)
			Failures.Add(MakeShared<FJsonValueString>(TEXT("sample count incomplete")));
		if (!bScreenshotExists)
			Failures.Add(MakeShared<FJsonValueString>(TEXT("screenshot missing")));
		if (!bHasRendererEvidence)
			Failures.Add(MakeShared<FJsonValueString>(TEXT("renderer counters missing")));
		if (!bHasInputToDisplay)
			Failures.Add(MakeShared<FJsonValueString>(TEXT("input-to-display provider unavailable")));
		if (!bHasWebToUERuntimeWork)
			Failures.Add(MakeShared<FJsonValueString>(TEXT("WebToUE hydrate/display workload missing")));
		if (!bHasTrajectoryEvidence)
			Failures.Add(MakeShared<FJsonValueString>(TEXT("trajectory did not mutate target UI")));
		Root->SetArrayField(TEXT("failures"), Failures);
	}

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	FFileHelper::SaveStringToFile(Json, *JsonPath);
	UE_LOG(LogTemp, Display, TEXT("WTUE_BENCHMARK_COMPLETE success=%s json=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), *JsonPath);
	FPlatformMisc::RequestExit(!bSuccess);
}

void FWebToUEBenchmarkRunner::FailAndExit(const FString& Error)
{
	Phase = EPhase::Complete;
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema_version"), 1);
	Root->SetBoolField(TEXT("success"), false);
	Root->SetStringField(TEXT("mode"), Mode);
	Root->SetStringField(TEXT("corpus"), Corpus.ToString());
	Root->SetStringField(TEXT("error"), Error);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	FFileHelper::SaveStringToFile(Json, *JsonPath);
	UE_LOG(LogTemp, Error, TEXT("WTUE_BENCHMARK_FAILED %s"), *Error);
	FPlatformMisc::RequestExit(true);
}
