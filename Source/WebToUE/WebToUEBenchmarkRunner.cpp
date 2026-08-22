#include "WebToUEBenchmarkRunner.h"

#include "WebToUEPackagedBenchmarkPolicy.h"
#include "WebToUEBenchmarkUserWidget.h"
#include "WebToUEDemoViewModel.h"

#include "WebToUEAnimation.h"
#include "WebToUEDocument.h"
#include "WebToUEScreenHost.h"
#include "WebToUESession.h"
#include "WebToUEView.h"

#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMemory.h"
#include "Hash/Blake3.h"
#include "Input/Events.h"
#include "Input/HittestGrid.h"
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
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
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
#include "Types/PaintArgs.h"
#include "UnrealClient.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/SNullWidget.h"

namespace WebToUE::Benchmark::Private
{
	static constexpr double BytesToMiB = 1.0 / (1024.0 * 1024.0);

	static FString HashUtf8(const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		return LexToString(FBlake3::HashBuffer(Utf8.Get(), Utf8.Length())).ToLower();
	}

	static double ElapsedMilliseconds(uint64 StartCycles, uint64 EndCycles)
	{
		return StartCycles != 0 && EndCycles >= StartCycles
			? FPlatformTime::ToMilliseconds64(EndCycles - StartCycles)
			: 0.0;
	}

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

TSharedPtr<SWidget> WebToUE::Benchmark::ResolvePointerTarget(
	const TSharedRef<SWidget>& HostWidget)
{
	const FChildren* Children = HostWidget->GetChildren();
	return Children && Children->Num() == 1
		? ConstCastSharedRef<SWidget>(Children->GetChildAt(0))
		: HostWidget;
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
	ShutdownUi();
	PerformanceCapture.Reset();
}

void FWebToUEBenchmarkRunner::ShutdownUi()
{
	if (PrimaryScreenHost)
	{
		PrimaryScreenHost->Shutdown();
		PrimaryScreenHost.Reset();
	}
	else if (ProbeWidget.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(ProbeWidget.ToSharedRef());
		}
	}
	if (SecondScreenHost)
	{
		SecondScreenHost->Shutdown();
	}
	SecondScreenHost.Reset();
	InputTargetWidget.Reset();
	TargetWidget.Reset();
	SecondTargetWidget.Reset();
	ProbeWidget.Reset();
	TargetWindow.Reset();
	TargetWindowPtr = nullptr;
	PrimaryUiObject.Reset();
	DataContextObject.Reset();
	BenchmarkDocument.Reset();
	SecondUiObject.Reset();
	SecondDataContextObject.Reset();
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
	const bool bMeasurementComplete =
		Phase == EPhase::Measuring && Samples.Num() >= RequestedSamples;
	if (!bMeasurementComplete &&
		(LogicalFrame == 30 || (LogicalFrame > WarmupFrames && LogicalFrame % 60 == 0)) &&
		Phase != EPhase::WaitingForScreenshot)
	{
		ApplyTrajectory();
	}
	ObserveTransitionState();
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
	if (Phase == EPhase::Measuring && Samples.Num() >= RequestedSamples &&
		(Corpus != TEXT("TransitionSmoke") || PrepareTransitionScreenshot()))
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
	const bool bCompositingSmoke = Corpus == TEXT("CompositingSmoke");
	const bool bResourceSmoke = Corpus == TEXT("ResourceTextureSmoke") ||
		Corpus == TEXT("ResourceMaterialSmoke") ||
		Corpus == TEXT("ResourceMaterialParameterSmoke") || bCompositingSmoke;
	const bool bVisualTransformSmoke =
		Corpus == TEXT("TransformClipSmoke") || bCompositingSmoke;
	const bool bTransitionSmoke = Corpus == TEXT("TransitionSmoke");
	if ((Mode != TEXT("WebToUE") && Mode != TEXT("UMG")) ||
		(Corpus != TEXT("MainMenu") && Corpus != TEXT("HUD") &&
			Corpus != TEXT("ScrollableSettings") && !bResourceSmoke &&
			!bVisualTransformSmoke && !bTransitionSmoke) ||
		((bResourceSmoke || bVisualTransformSmoke || bTransitionSmoke) &&
			Mode != TEXT("WebToUE")))
	{
		FailAndExit(TEXT("Invalid -WTUEBenchmark or -WTUECorpus value"));
		return false;
	}
	UiSetupCycles = FPlatformTime::Cycles64();
	BeforeFirstViewMemory = CaptureMemoryPoint();
	TSharedPtr<SWidget> BuiltTargetWidget;
	UWorld* World = GEngine->GameViewport->GetWorld();
	UGameInstance* GameInstance = GEngine->GameViewport->GetGameInstance();
	ULocalPlayer* LocalPlayer = GameInstance ? GameInstance->GetFirstGamePlayer() : nullptr;
	if (Mode == TEXT("WebToUE"))
	{
		if (!LocalPlayer)
		{
			FailAndExit(TEXT("WebToUE Screen Host requires the first LocalPlayer"));
			return false;
		}
		const uint64 AssetLoadStartCycles = FPlatformTime::Cycles64();
		const FString AssetPath = FString::Printf(TEXT("/Game/WebToUEExamples/%s.%s"),
			*Corpus.ToString(), *Corpus.ToString());
		UWebToUEDocument* Document = LoadObject<UWebToUEDocument>(nullptr, *AssetPath);
		ColdAssetLoadMs = WebToUE::Benchmark::Private::ElapsedMilliseconds(
			AssetLoadStartCycles, FPlatformTime::Cycles64());
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
		BenchmarkDocument.Reset(Document);
		UE_LOG(LogTemp, Display,
			TEXT("WTUE_BENCHMARK_DOCUMENT path=%s nodes=%d rules=%d binding_ops=%d resources=%d root=%d root_valid=%s"),
			*AssetPath, CompiledNodeCount, CompiledRuleCount, CompiledBindingOpCount,
			CompiledResourceCount, CompiledRootNodeIndex,
			Document->GetCompiledNodes().IsValidIndex(CompiledRootNodeIndex)
				? TEXT("true") : TEXT("false"));
		const uint64 UiObjectStartCycles = FPlatformTime::Cycles64();
		UWebToUEDemoViewModel* ViewModel = NewObject<UWebToUEDemoViewModel>(GetTransientPackage());
		FWebToUEScreenHostCreateParams HostParams;
		HostParams.Document = Document;
		HostParams.DataContext = ViewModel;
		HostParams.SurfaceId = FName(*FString::Printf(
			TEXT("webtoue.benchmark.%s.primary-screen"), *Corpus.ToString()));
		HostParams.ZOrder = 1000;
		FString HostError;
		PrimaryScreenHost = FWebToUEScreenHost::CreateForLocalPlayer(
			LocalPlayer, HostParams, HostError);
		if (!PrimaryScreenHost)
		{
			FailAndExit(FString::Printf(TEXT("Failed to create WebToUE Screen Host: %s"),
				*HostError));
			return false;
		}
		UWebToUEView* View = PrimaryScreenHost->GetView();
		PrimaryUiObject.Reset(View);
		DataContextObject.Reset(ViewModel);
		ColdUiObjectConstructionMs = WebToUE::Benchmark::Private::ElapsedMilliseconds(
			UiObjectStartCycles, FPlatformTime::Cycles64());
		const uint64 TakeWidgetStartCycles = FPlatformTime::Cycles64();
		if (!PrimaryScreenHost->BuildContent(HostError))
		{
			FailAndExit(FString::Printf(TEXT("Failed to build WebToUE Screen content: %s"),
				*HostError));
			return false;
		}
		BuiltTargetWidget = PrimaryScreenHost->GetContentWidget();
		ColdTakeWidgetMs = WebToUE::Benchmark::Private::ElapsedMilliseconds(
			TakeWidgetStartCycles, FPlatformTime::Cycles64());
	}
	else
	{
		const uint64 UiObjectStartCycles = FPlatformTime::Cycles64();
		UWebToUEBenchmarkUserWidget* Widget = CreateWidget<UWebToUEBenchmarkUserWidget>(
			World, UWebToUEBenchmarkUserWidget::StaticClass());
		if (!Widget || !Widget->Configure(Corpus))
		{
			FailAndExit(TEXT("Failed to build the UMG corpus widget tree"));
			return false;
		}
		PrimaryUiObject.Reset(Widget);
		ColdUiObjectConstructionMs = WebToUE::Benchmark::Private::ElapsedMilliseconds(
			UiObjectStartCycles, FPlatformTime::Cycles64());
		const uint64 TakeWidgetStartCycles = FPlatformTime::Cycles64();
		BuiltTargetWidget = Widget->TakeWidget();
		ColdTakeWidgetMs = WebToUE::Benchmark::Private::ElapsedMilliseconds(
			TakeWidgetStartCycles, FPlatformTime::Cycles64());
		InputTargetWidget = Widget->GetTrajectoryInputWidget();
	}
	if (!BuiltTargetWidget.IsValid())
	{
		FailAndExit(TEXT("Benchmark target did not produce a Slate widget"));
		return false;
	}
	TargetWidget = BuiltTargetWidget;
	if (Mode == TEXT("WebToUE"))
	{
		InputTargetWidget = WebToUE::Benchmark::ResolvePointerTarget(
			BuiltTargetWidget.ToSharedRef());
	}
	else if (!InputTargetWidget.IsValid())
	{
		InputTargetWidget = BuiltTargetWidget;
	}
	const uint64 PrepassStartCycles = FPlatformTime::Cycles64();
	BuiltTargetWidget->SlatePrepass(1.0f);
	if (bTransitionSmoke)
	{
		if (const UWebToUEView* View = Cast<UWebToUEView>(PrimaryUiObject.Get()))
		{
			TArray<FWebToUESemanticNode> Semantics;
			View->GetSemanticNodes(Semantics);
			if (const FWebToUESemanticNode* Target = Semantics.FindByPredicate(
				[](const FWebToUESemanticNode& Node)
				{
					return Node.ElementId == TEXT("transition-target");
				}))
			{
				InitialTransitionSemanticBounds = Target->Bounds;
				bInitialTransitionSemanticFound = Target->bVisible &&
					Target->bEnabled && Target->Bounds.IsValid() &&
					!Target->Bounds.IsEmpty();
			}
		}
	}
	ColdPrepassMs = WebToUE::Benchmark::Private::ElapsedMilliseconds(
		PrepassStartCycles, FPlatformTime::Cycles64());
	AfterFirstViewMemory = CaptureMemoryPoint();
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
	const uint64 AttachStartCycles = FPlatformTime::Cycles64();
	const FWebToUEScreenContentWrapper BuildMeasuringRoot =
		[this](TSharedRef<SWidget> Content) -> TSharedRef<SWidget>
		{
			WebToUE::Benchmark::Private::ForceVolatileRecursive(Content);
			const TSharedRef<WebToUE::Benchmark::Private::SBenchmarkProbe> MeasuringProbe =
				SNew(WebToUE::Benchmark::Private::SBenchmarkProbe)
				.Owner(this)[Content];
			TSharedRef<SInvalidationPanel> UncachedRoot =
				SNew(SInvalidationPanel)[MeasuringProbe];
			UncachedRoot->SetCanCache(false);
			UncachedRoot->ForceVolatile(true);
			ProbeWidget = UncachedRoot;
			return UncachedRoot;
		};
	TargetWindow = GEngine->GameViewport->GetWindow();
	TargetWindowPtr = TargetWindow.Pin().Get();
	if (Mode == TEXT("WebToUE"))
	{
		FString HostError;
		if (!PrimaryScreenHost || !PrimaryScreenHost->Attach(BuildMeasuringRoot, HostError))
		{
			FailAndExit(FString::Printf(TEXT("Failed to attach WebToUE Screen Host: %s"),
				*HostError));
			return false;
		}
	}
	else
	{
		ProbeWidget = BuildMeasuringRoot(BuiltTargetWidget.ToSharedRef());
		GEngine->GameViewport->AddViewportWidgetContent(ProbeWidget.ToSharedRef(), 1000);
	}
	UiSetupCompleteCycles = FPlatformTime::Cycles64();
	ColdAttachMs = WebToUE::Benchmark::Private::ElapsedMilliseconds(
		AttachStartCycles, UiSetupCompleteCycles);
	ColdSetupTotalMs = WebToUE::Benchmark::Private::ElapsedMilliseconds(
		UiSetupCycles, UiSetupCompleteCycles);
	UE_LOG(LogTemp, Display, TEXT("WTUE_BENCHMARK_UI_ATTACHED window=%p probe=%p"),
		TargetWindowPtr, ProbeWidget.Get());
	return true;
}

FWebToUEBenchmarkRunner::FMemoryPoint FWebToUEBenchmarkRunner::CaptureMemoryPoint() const
{
	using namespace WebToUE::Benchmark::Private;
	const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
	FMemoryPoint Result;
	Result.RssMiB = Memory.UsedPhysical * BytesToMiB;
	Result.LlmMiB = GetLlmMiB();
	return Result;
}

bool FWebToUEBenchmarkRunner::CaptureSecondViewEvidence()
{
	if (bSecondViewCreated)
	{
		return true;
	}
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("WTUE_BENCHMARK_SECOND_VIEW missing game world"));
		return false;
	}
#if WITH_DEV_AUTOMATION_TESTS
	// Compare equivalent mature view states. The primary view has completed its real
	// renderer-backed warmup and measurement paints before the second view is created.
	if (Mode == TEXT("WebToUE"))
	{
		FWebToUERuntimeMemoryCensus Census;
		if (UWebToUEView* View = Cast<UWebToUEView>(PrimaryUiObject.Get());
			View && View->GetRuntimeMemoryCensusForTesting(Census))
		{
			FirstViewCensus.bAvailable = true;
			FirstViewCensus.SharedStyleTemplateBytes =
				Census.SharedStyleTemplateKnownOwnedBytes;
			FirstViewCensus.RuntimeBytes = Census.RuntimeKnownOwnedBytes;
			FirstViewCensus.PresentationBytes = Census.PresentationKnownOwnedBytes;
		}
	}
#endif
	BeforeSecondViewMemory = CaptureMemoryPoint();
	if (PerformanceCapture)
	{
		PerformanceCapture->Reset();
	}
	if (Mode == TEXT("WebToUE"))
	{
		UWebToUEDocument* Document = BenchmarkDocument.Get();
		if (!Document)
		{
			UE_LOG(LogTemp, Error, TEXT("WTUE_BENCHMARK_SECOND_VIEW missing document"));
			return false;
		}
		UWebToUEDemoViewModel* ViewModel = NewObject<UWebToUEDemoViewModel>(GetTransientPackage());
		UGameInstance* GameInstance = GEngine->GameViewport->GetGameInstance();
		ULocalPlayer* LocalPlayer = GameInstance ? GameInstance->GetFirstGamePlayer() : nullptr;
		if (!LocalPlayer)
		{
			UE_LOG(LogTemp, Error,
				TEXT("WTUE_BENCHMARK_SECOND_VIEW missing first LocalPlayer"));
			return false;
		}
		FWebToUEScreenHostCreateParams HostParams;
		HostParams.Document = Document;
		HostParams.DataContext = ViewModel;
		HostParams.SurfaceId = FName(*FString::Printf(
			TEXT("webtoue.benchmark.%s.second-screen"), *Corpus.ToString()));
		FString HostError;
		SecondScreenHost = FWebToUEScreenHost::CreateForLocalPlayer(
			LocalPlayer, HostParams, HostError);
		if (!SecondScreenHost || !SecondScreenHost->BuildContent(HostError))
		{
			UE_LOG(LogTemp, Error,
				TEXT("WTUE_BENCHMARK_SECOND_VIEW Screen Host failed: %s"), *HostError);
			return false;
		}
		UWebToUEView* View = SecondScreenHost->GetView();
		SecondUiObject.Reset(View);
		SecondDataContextObject.Reset(ViewModel);
		SecondTargetWidget = SecondScreenHost->GetContentWidget();
		if (Corpus == TEXT("ResourceMaterialParameterSmoke") ||
			Corpus == TEXT("CompositingSmoke"))
		{
			FWebToUEMaterialParameterSubmission Submission;
			Submission.Target = View->FindElementById(TEXT("dynamic-material"));
			Submission.Address = FWebToUEPropertyAddress::Material(
				TEXT("Tint"), EWebToUEMaterialParameterType::Vector);
			Submission.Value = FWebToUEMaterialParameterValue::MakeVector(
				FLinearColor(0.05f, 0.9f, 0.2f, 1.0f));
			const FWebToUEMaterialParameterSubmitOutcome Outcome =
				View->SubmitMaterialParameter(Submission);
			bDynamicMaterialSecondViewApplied =
				Outcome.Result == EWebToUEMaterialParameterSubmitResult::Committed;
			if (!bDynamicMaterialSecondViewApplied)
			{
				UE_LOG(LogTemp, Error,
					TEXT("WTUE_BENCHMARK_SECOND_VIEW dynamic Material submission failed: %s"),
					*Outcome.Diagnostic);
				return false;
			}
		}
	}
	else
	{
		UWebToUEBenchmarkUserWidget* Widget = CreateWidget<UWebToUEBenchmarkUserWidget>(
			GEngine->GameViewport->GetWorld(), UWebToUEBenchmarkUserWidget::StaticClass());
		if (!Widget || !Widget->Configure(Corpus))
		{
			UE_LOG(LogTemp, Error,
				TEXT("WTUE_BENCHMARK_SECOND_VIEW failed to build UMG counterpart"));
			return false;
		}
		SecondUiObject.Reset(Widget);
		SecondTargetWidget = Widget->TakeWidget();
	}
	if (!SecondTargetWidget.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("WTUE_BENCHMARK_SECOND_VIEW missing Slate widget"));
		return false;
	}
	SecondTargetWidget->SlatePrepass(1.0f);
	if (Mode == TEXT("WebToUE") &&
		(Corpus == TEXT("ResourceTextureSmoke") ||
		 Corpus == TEXT("ResourceMaterialSmoke") ||
		 Corpus == TEXT("ResourceMaterialParameterSmoke") ||
		 Corpus == TEXT("CompositingSmoke") ||
		 Corpus == TEXT("TransformClipSmoke") ||
		 Corpus == TEXT("TransitionSmoke")))
	{
		FHittestGrid HittestGrid;
		FSlateWindowElementList DrawElements(
			GEngine->GameViewport->GetWindow());
		const FGeometry Geometry = FGeometry::MakeRoot(
			FVector2D(1920.0, 1080.0), FSlateLayoutTransform());
		const FPaintArgs PaintArgs(
			nullptr, HittestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
		SecondTargetWidget->Paint(PaintArgs, Geometry,
			FSlateRect(0.0f, 0.0f, 1920.0f, 1080.0f), DrawElements, 0,
			FWidgetStyle(), true);
	}
#if WITH_DEV_AUTOMATION_TESTS
	if (Mode == TEXT("WebToUE"))
	{
		FWebToUERuntimeMemoryCensus Census;
		if (UWebToUEView* View = Cast<UWebToUEView>(SecondUiObject.Get());
			View && View->GetRuntimeMemoryCensusForTesting(Census))
		{
			SecondViewCensus.bAvailable = true;
			SecondViewCensus.SharedStyleTemplateBytes =
				Census.SharedStyleTemplateKnownOwnedBytes;
			SecondViewCensus.RuntimeBytes = Census.RuntimeKnownOwnedBytes;
			SecondViewCensus.PresentationBytes = Census.PresentationKnownOwnedBytes;
		}
	}
#endif
	if (PerformanceCapture)
	{
		SecondViewWorkload = PerformanceCapture->GetSnapshot();
	}
	AfterSecondViewMemory = CaptureMemoryPoint();
	bSecondViewCreated = true;
	UE_LOG(LogTemp, Display,
		TEXT("WTUE_BENCHMARK_SECOND_VIEW mode=%s corpus=%s rss_delta_mib=%.3f llm_delta_mib=%.3f"),
		*Mode, *Corpus.ToString(),
		AfterSecondViewMemory.RssMiB - BeforeSecondViewMemory.RssMiB,
		AfterSecondViewMemory.LlmMiB - BeforeSecondViewMemory.LlmMiB);
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
		if ((Corpus == TEXT("TransformClipSmoke") ||
			 Corpus == TEXT("CompositingSmoke") ||
			 Corpus == TEXT("TransitionSmoke")) && Mode == TEXT("WebToUE"))
		{
			if ((TrajectoryStep & 1) == 0)
			{
				return Geometry.LocalToAbsolute(FVector2D(24.0, 24.0));
			}
			if (const UWebToUEView* View = Cast<UWebToUEView>(PrimaryUiObject.Get()))
			{
				const FString TargetId = Corpus == TEXT("TransitionSmoke")
					? TEXT("transition-target")
					: Corpus == TEXT("CompositingSmoke")
					? TEXT("compositing-hit") : TEXT("transform-target");
				TArray<FWebToUESemanticNode> Semantics;
				View->GetSemanticNodes(Semantics);
				if (const FWebToUESemanticNode* Target = Semantics.FindByPredicate(
					[&TargetId](const FWebToUESemanticNode& Node)
					{
						return Node.ElementId == TargetId;
					}))
				{
					return Geometry.LocalToAbsolute(FVector2D(
						(Target->Bounds.Left + Target->Bounds.Right) * 0.5,
						(Target->Bounds.Top + Target->Bounds.Bottom) * 0.5));
				}
			}
		}
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
	if (Phase == EPhase::Measuring)
	{
		++MeasurementTrajectorySteps;
	}
	PendingInputCycles = FPlatformTime::Cycles64();
	PendingBackBufferInputCycles.Store(PendingInputCycles);
	UEngine::SetInputSampleLatencyMarker(GFrameCounter);
	if (Corpus == TEXT("ResourceMaterialParameterSmoke") ||
		Corpus == TEXT("CompositingSmoke"))
	{
		bool& bApplied = Phase == EPhase::Measuring
			? bDynamicMaterialMeasurementApplied
			: bDynamicMaterialWarmupApplied;
		if (!bApplied)
		{
			if (UWebToUEView* View = Cast<UWebToUEView>(PrimaryUiObject.Get()))
			{
				FWebToUEMaterialParameterSubmission Submission;
				Submission.Target = View->FindElementById(TEXT("dynamic-material"));
				Submission.Address = FWebToUEPropertyAddress::Material(
					TEXT("Tint"), EWebToUEMaterialParameterType::Vector);
				Submission.Value = FWebToUEMaterialParameterValue::MakeVector(
					Phase == EPhase::Measuring
						? FLinearColor(1.0f, 0.08f, 0.18f, 1.0f)
						: FLinearColor(0.08f, 0.28f, 1.0f, 1.0f));
				const FWebToUEMaterialParameterSubmitOutcome Outcome =
					View->SubmitMaterialParameter(Submission);
				bApplied = Outcome.Result ==
					EWebToUEMaterialParameterSubmitResult::Committed;
				if (!bApplied)
				{
					UE_LOG(LogTemp, Error,
						TEXT("WTUE_BENCHMARK_TRAJECTORY dynamic Material submission failed: %s"),
						*Outcome.Diagnostic);
				}
			}
		}
		if (Corpus != TEXT("CompositingSmoke")) return;
	}
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
	Slate.SetCursorPos(Position);
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
	const bool bDirectWebToUEInput = Mode == TEXT("WebToUE") &&
		InputTargetWidget.IsValid();
	if (!bDirectWebToUEInput && InputTargetWidget.IsValid())
	{
		GeneratePointerPath(InputTargetWidget.ToSharedRef(), InputPath);
	}
	const FPointerEvent MoveEvent(0, Position, LastPointerPosition, TSet<FKey>(),
		FKey(), 0.0f, FModifierKeysState());
	if (bDirectWebToUEInput)
	{
		InputTargetWidget->OnMouseMove(
			InputTargetWidget->GetPaintSpaceGeometry(), MoveEvent);
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
		if (bDirectWebToUEInput)
		{
			InputTargetWidget->OnMouseWheel(
				InputTargetWidget->GetPaintSpaceGeometry(), WheelEvent);
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
		if (bDirectWebToUEInput)
		{
			InputTargetWidget->OnMouseButtonDown(
				InputTargetWidget->GetPaintSpaceGeometry(), DownEvent);
		}
		else
		{
			Slate.RoutePointerDownEvent(InputPath, DownEvent);
		}
		const FPointerEvent UpEvent(0, Position, Position, TSet<FKey>(),
			EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
		if (bDirectWebToUEInput)
		{
			InputTargetWidget->OnMouseButtonUp(
				InputTargetWidget->GetPaintSpaceGeometry(), UpEvent);
		}
		else
		{
			Slate.RoutePointerUpEvent(InputPath, UpEvent);
		}
	}
}

int32 FWebToUEBenchmarkRunner::GetTransitionActiveTrackCount() const
{
	if (Corpus != TEXT("TransitionSmoke")) return 0;
	const UWebToUEView* View = Cast<UWebToUEView>(PrimaryUiObject.Get());
	const TSharedPtr<FWebToUESession> Session = View ? View->GetSession() : nullptr;
	return Session ? Session->GetAnimationCoordinator()->GetActiveTrackCount() : 0;
}

void FWebToUEBenchmarkRunner::ObserveTransitionState()
{
	if (Corpus != TEXT("TransitionSmoke")) return;
	const int32 ActiveTracks = GetTransitionActiveTrackCount();
	MaxTransitionActiveTracks = FMath::Max(MaxTransitionActiveTracks, ActiveTracks);
	if (ActiveTracks > 0) ++TransitionActiveObservationFrames;
	if (bInitialTransitionSemanticFound) return;
	if (const UWebToUEView* View = Cast<UWebToUEView>(PrimaryUiObject.Get()))
	{
		TArray<FWebToUESemanticNode> Semantics;
		View->GetSemanticNodes(Semantics);
		if (const FWebToUESemanticNode* Target = Semantics.FindByPredicate(
			[](const FWebToUESemanticNode& Node)
			{
				return Node.ElementId == TEXT("transition-target");
			}))
		{
			InitialTransitionSemanticBounds = Target->Bounds;
			bInitialTransitionSemanticFound = Target->bVisible && Target->bEnabled &&
				Target->Bounds.IsValid() && !Target->Bounds.IsEmpty();
		}
	}
}

bool FWebToUEBenchmarkRunner::PrepareTransitionScreenshot()
{
	if (TransitionScreenshotPhase == 0)
	{
		if ((TrajectoryStep & 1) != 0) ApplyTrajectory();
		TransitionScreenshotPhase = 1;
		return false;
	}
	if (TransitionScreenshotPhase == 1)
	{
		if (GetTransitionActiveTrackCount() > 0) return false;
		ApplyTrajectory();
		TransitionScreenshotPhase = 2;
		return false;
	}
	return GetTransitionActiveTrackCount() == 0;
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
		ColdFirstRenderWaitMs = WebToUE::Benchmark::Private::ElapsedMilliseconds(
			UiSetupCompleteCycles, FirstRenderCycles);
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
	const bool bSecondViewEvidence = CaptureSecondViewEvidence();
	const bool bTextureResourceSmoke = Corpus == TEXT("ResourceTextureSmoke");
	const bool bCompositingSmoke = Corpus == TEXT("CompositingSmoke");
	const bool bDynamicMaterialParameterSmoke =
		Corpus == TEXT("ResourceMaterialParameterSmoke") || bCompositingSmoke;
	const bool bMaterialResourceSmoke = Corpus == TEXT("ResourceMaterialSmoke") ||
		bDynamicMaterialParameterSmoke;
	const bool bResourceSmoke = bTextureResourceSmoke || bMaterialResourceSmoke;
	const bool bVisualTransformSmoke =
		Corpus == TEXT("TransformClipSmoke") || bCompositingSmoke;
	const bool bTransitionSmoke = Corpus == TEXT("TransitionSmoke");
	bool bResourceIdentityValid = !bResourceSmoke;
	const FWebToUECompiledResource* SmokeResource = nullptr;
	if (bResourceSmoke && BenchmarkDocument.IsValid() &&
		BenchmarkDocument->GetResourceManifest().Num() == 1)
	{
		SmokeResource = &BenchmarkDocument->GetResourceManifest()[0];
		if (bTextureResourceSmoke)
		{
			const UTexture2D* Texture = Cast<UTexture2D>(
				SmokeResource->Path.ResolveObject());
			bResourceIdentityValid =
				SmokeResource->Kind == EWebToUEResourceKind::Texture &&
				SmokeResource->ResourceId.StartsWith(TEXT("resource/texture/")) &&
				SmokeResource->Path.ToString().StartsWith(
					TEXT("/Game/WebToUEGenerated/Textures/T_")) &&
				SmokeResource->Provenance.Origin ==
					EWebToUEResourceOrigin::RelativeSource &&
				SmokeResource->Provenance.AuthorReference ==
					TEXT("ResourceTextureSmoke.png") &&
				SmokeResource->Provenance.ResolvedDependencyId.StartsWith(
					TEXT("generated:textures/")) &&
				SmokeResource->IntrinsicSize.X > 0.0f &&
				SmokeResource->IntrinsicSize.Y > 0.0f && Texture &&
				SmokeResource->IntrinsicSize == FVector2f(
					Texture->GetImportedSize().X, Texture->GetImportedSize().Y);
		}
		else
		{
			const FString ExpectedPath = bDynamicMaterialParameterSmoke
				? TEXT("/Game/WebToUEExamples/Materials/M_WTUE_DynamicMaterialBrush.M_WTUE_DynamicMaterialBrush")
				: TEXT("/Game/WebToUEExamples/Materials/MI_WTUE_StaticMaterialBrush.MI_WTUE_StaticMaterialBrush");
			const FString ExpectedDependency = bDynamicMaterialParameterSmoke
				? TEXT("asset/Game/WebToUEExamples/Materials/M_WTUE_DynamicMaterialBrush")
				: TEXT("asset/Game/WebToUEExamples/Materials/MI_WTUE_StaticMaterialBrush");
			const UMaterialInterface* Material = Cast<UMaterialInterface>(
				SmokeResource->Path.ResolveObject());
			bResourceIdentityValid =
				SmokeResource->Kind == EWebToUEResourceKind::Material &&
				SmokeResource->ResourceId ==
					TEXT("resource/material/") + HashUtf8(ExpectedPath) &&
				SmokeResource->Path.ToString() == ExpectedPath &&
				SmokeResource->Provenance.Origin ==
					EWebToUEResourceOrigin::UnrealAsset &&
				SmokeResource->Provenance.AuthorReference == ExpectedPath &&
				SmokeResource->Provenance.ResolvedDependencyId ==
					ExpectedDependency &&
				SmokeResource->Residency == EWebToUEResidencyClass::Critical &&
				SmokeResource->BrushImageSize.X > 0.0f &&
				SmokeResource->BrushImageSize.Y > 0.0f && Material &&
				!Material->IsA<UMaterialInstanceDynamic>();
		}
	}
	const bool bHasWebToUERuntimeWork = Mode != TEXT("WebToUE") ||
		(CompiledNodeCount > 0 &&
			SetupWorkload.GetCounter(EWebToUEPerformanceCounter::HydratedNodes) ==
				static_cast<uint64>(CompiledNodeCount) &&
			WarmupWorkload.GetCounter(EWebToUEPerformanceCounter::DisplayListBuilds) > 0 &&
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::PaintDrawElements) > 0);
	bool bHasTrajectoryEvidence = TrajectoryStep > 0;
	if (bCompositingSmoke)
	{
		bHasTrajectoryEvidence = bDynamicMaterialWarmupApplied &&
			bDynamicMaterialMeasurementApplied &&
			bDynamicMaterialSecondViewApplied &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::HitTestCandidates) > 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::HitTestCommandsVisited) > 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::InverseHitTests) > 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::ExactClipTests) >= 2 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DisplayCommandsPatched) > 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DisplaySpatialIndexPatches) > 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DirtyRectsAdded) > 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::YogaStyleWrites) == 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::YogaNodesDirtied) == 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::YogaLayoutResultsChanged) == 0;
	}
	else if (bDynamicMaterialParameterSmoke)
	{
		bHasTrajectoryEvidence = bDynamicMaterialWarmupApplied &&
			bDynamicMaterialMeasurementApplied &&
			bDynamicMaterialSecondViewApplied;
	}
	else if (bResourceSmoke)
	{
		bHasTrajectoryEvidence = TrajectoryStep > 0;
	}
	else if (Mode == TEXT("UMG"))
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
		else if (bVisualTransformSmoke)
		{
			bHasTrajectoryEvidence &=
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::VisualTransformCommandsResolved) > 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::ClipChainZonesResolved) >= 2 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::InverseHitTests) > 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::ExactClipTests) >= 2 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::DisplaySpatialIndexPatches) > 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::YogaStyleWrites) == 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::YogaNodesDirtied) == 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::YogaLayoutResultsChanged) == 0;
		}
		else if (bTransitionSmoke)
		{
			bHasTrajectoryEvidence &=
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::VisualTransformCommandsResolved) > 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::DisplaySpatialIndexPatches) > 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::YogaStyleWrites) == 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::YogaNodesDirtied) == 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::YogaLayoutResultsChanged) == 0 &&
				MeasurementWorkload.GetCounter(
					EWebToUEPerformanceCounter::ResourceLoadAttempts) == 0;
		}
	}
	FSlateRect TransformSemanticBounds;
	bool bTransformSemanticFound = false;
	if (bVisualTransformSmoke)
	{
		if (const UWebToUEView* View = Cast<UWebToUEView>(PrimaryUiObject.Get()))
		{
			TArray<FWebToUESemanticNode> Semantics;
			View->GetSemanticNodes(Semantics);
			if (const FWebToUESemanticNode* Target = Semantics.FindByPredicate(
				[bCompositingSmoke](const FWebToUESemanticNode& Node)
				{
					return Node.ElementId == (bCompositingSmoke
						? TEXT("compositing-hit") : TEXT("transform-target"));
				}))
			{
				TransformSemanticBounds = Target->Bounds;
				bTransformSemanticFound = Target->bVisible && Target->bEnabled &&
					Target->Bounds.IsValid() && !Target->Bounds.IsEmpty();
			}
		}
	}
	const bool bVisualTransformEvidenceValid = !bVisualTransformSmoke ||
		(bTransformSemanticFound &&
			CompiledResourceCount == (bCompositingSmoke ? 1 : 0) &&
			WarmupWorkload.GetCounter(
				EWebToUEPerformanceCounter::VisualTransformCommandsResolved) >= 3 &&
			WarmupWorkload.GetCounter(
				EWebToUEPerformanceCounter::ClipChainZonesResolved) >= 2 &&
			SecondViewWorkload.GetCounter(
				EWebToUEPerformanceCounter::VisualTransformCommandsResolved) >= 3 &&
			SecondViewWorkload.GetCounter(
				EWebToUEPerformanceCounter::ClipChainZonesResolved) >= 2 &&
			bHasTrajectoryEvidence);
	FSlateRect FinalTransitionSemanticBounds;
	bool bFinalTransitionSemanticFound = false;
	int32 TransitionTraceCount = 0;
	int32 TransitionStartedCount = 0;
	int32 TransitionRetargetedCount = 0;
	int32 TransitionSampledCount = 0;
	int32 TransitionCompletedCount = 0;
	int32 TransitionTransactionCount = 0;
	int32 TransitionEvaluationCount = 0;
	int32 TransitionMutationCount = 0;
	uint64 TransitionTickerInvocations = 0;
	bool bTransitionTransactionsCommitted = false;
	bool bTransitionTickerReleased = false;
	bool bTransitionIrValid = !bTransitionSmoke;
	if (bTransitionSmoke)
	{
		if (const UWebToUEView* View = Cast<UWebToUEView>(PrimaryUiObject.Get()))
		{
			TArray<FWebToUESemanticNode> Semantics;
			View->GetSemanticNodes(Semantics);
			if (const FWebToUESemanticNode* Target = Semantics.FindByPredicate(
				[](const FWebToUESemanticNode& Node)
				{
					return Node.ElementId == TEXT("transition-target");
				}))
			{
				FinalTransitionSemanticBounds = Target->Bounds;
				bFinalTransitionSemanticFound = Target->bVisible && Target->bEnabled &&
					Target->Bounds.IsValid() && !Target->Bounds.IsEmpty();
			}
			if (const TSharedPtr<FWebToUESession> Session = View->GetSession())
			{
				const TSharedRef<FWebToUEAnimationCoordinator, ESPMode::ThreadSafe>
					Animation = Session->GetAnimationCoordinator();
				TransitionTraceCount = Animation->GetTrace().Num();
				TransitionTickerInvocations = Animation->GetTickerInvocationCount();
				for (const FWebToUEAnimationTrace& Trace : Animation->GetTrace())
				{
					switch (Trace.Outcome)
					{
					case EWebToUEAnimationTraceOutcome::Started:
						++TransitionStartedCount;
						break;
					case EWebToUEAnimationTraceOutcome::Retargeted:
						++TransitionRetargetedCount;
						break;
					case EWebToUEAnimationTraceOutcome::Sampled:
						++TransitionSampledCount;
						break;
					case EWebToUEAnimationTraceOutcome::Completed:
						++TransitionCompletedCount;
						break;
					default:
						break;
					}
				}
				const TConstArrayView<FWebToUEUpdateTrace> UpdateTrace =
					Session->GetUpdateCoordinator()->GetTrace();
				TransitionTransactionCount = UpdateTrace.Num();
				bTransitionTransactionsCommitted = !UpdateTrace.IsEmpty();
				for (const FWebToUEUpdateTrace& Trace : UpdateTrace)
				{
					bTransitionTransactionsCommitted &=
						Trace.Outcome == EWebToUEUpdateOutcome::Committed;
					TransitionEvaluationCount += Trace.EvaluationCount;
					TransitionMutationCount += Trace.StateMutationCount;
				}
				bTransitionTickerReleased =
					Animation->GetActiveTrackCount() == 0 &&
					!Animation->IsTickerRegistered();
			}
		}
		if (BenchmarkDocument.IsValid())
		{
			const FWebToUECompiledAnimationIR& IR =
				BenchmarkDocument->GetCompiledAnimationIR();
			TSet<EWebToUECompiledAnimationTargetKind> TargetKinds;
			for (const FWebToUECompiledTransition& Transition : IR.Transitions)
			{
				TargetKinds.Add(Transition.Target.Kind);
			}
			bTransitionIrValid =
				IR.Version.Major == FWebToUECompiledAnimationIR::CurrentMajor &&
				IR.Version.Minor == FWebToUECompiledAnimationIR::CurrentMinor &&
				IR.Transitions.Num() == 5 &&
				TargetKinds.Contains(EWebToUECompiledAnimationTargetKind::Opacity) &&
				TargetKinds.Contains(EWebToUECompiledAnimationTargetKind::Color) &&
				TargetKinds.Contains(
					EWebToUECompiledAnimationTargetKind::BackgroundColor) &&
				TargetKinds.Contains(
					EWebToUECompiledAnimationTargetKind::BorderColor) &&
				TargetKinds.Contains(
					EWebToUECompiledAnimationTargetKind::VisualTransform);
		}
	}
	const bool bTransitionBoundsChanged = bInitialTransitionSemanticFound &&
		bFinalTransitionSemanticFound &&
		(!FMath::IsNearlyEqual(InitialTransitionSemanticBounds.Left,
			FinalTransitionSemanticBounds.Left, 0.5f) ||
		 !FMath::IsNearlyEqual(InitialTransitionSemanticBounds.Top,
			FinalTransitionSemanticBounds.Top, 0.5f));
	const bool bTransitionEvidenceValid = !bTransitionSmoke ||
		(bTransitionIrValid && CompiledResourceCount == 0 &&
			MaxTransitionActiveTracks == 5 && TransitionActiveObservationFrames > 0 &&
			TransitionTraceCount <= FWebToUEAnimationBudget().MaxTraceEntries &&
			TransitionSampledCount >= 5 && TransitionCompletedCount >= 5 &&
			TransitionTickerInvocations > 0 && bTransitionTickerReleased &&
			bTransitionTransactionsCommitted && TransitionEvaluationCount > 0 &&
			TransitionMutationCount > 0 && bTransitionBoundsChanged &&
			bHasTrajectoryEvidence);
	const uint64 CompositingTier1Decisions =
		SetupWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier1Decisions) +
		WarmupWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier1Decisions);
	const uint64 CompositingTier2Decisions =
		SetupWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier2Decisions) +
		WarmupWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier2Decisions) +
		MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier2Decisions) +
		SecondViewWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier2Decisions);
	const uint64 CompositingTier3Decisions =
		SetupWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier3Decisions) +
		WarmupWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier3Decisions) +
		MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier3Decisions) +
		SecondViewWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingTier3Decisions);
	const uint64 CompositingPlanRejections =
		SetupWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingPlanRejections) +
		WarmupWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingPlanRejections) +
		MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingPlanRejections) +
		SecondViewWorkload.GetCounter(EWebToUEPerformanceCounter::CompositingPlanRejections);
	const bool bCompositingEvidenceValid = !bCompositingSmoke ||
		(bResourceIdentityValid && bVisualTransformEvidenceValid &&
			bHasTrajectoryEvidence && CompositingTier1Decisions > 0 &&
			SecondViewWorkload.GetCounter(
				EWebToUEPerformanceCounter::CompositingTier1Decisions) > 0 &&
			CompositingTier2Decisions == 0 && CompositingTier3Decisions == 0 &&
			CompositingPlanRejections == 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::CompositingRedraws) > 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::CompositingPasses) > 0 &&
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::CompositingCommands) > 0);
	TArray<FString> ProductPolicyFailures;
	bool bProductPolicyPass = true;
	if (Mode == TEXT("WebToUE"))
	{
		FWebToUEPackagedBenchmarkEvidence Evidence;
		Evidence.CompiledNodeCount = CompiledNodeCount;
		Evidence.CompiledResourceCount = CompiledResourceCount;
		Evidence.MeasurementTrajectorySteps = MeasurementTrajectorySteps;
		Evidence.SetupHydratedNodes = SetupWorkload.GetCounter(
			EWebToUEPerformanceCounter::HydratedNodes);
		Evidence.MeasurementHydratedNodes = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::HydratedNodes);
		Evidence.MeasurementStyleNodeVisits = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::StyleNodeVisits);
		Evidence.MeasurementSelectorEvaluations = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::SelectorEvaluations);
		Evidence.MeasurementBindingNodesUpdated = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::BindingNodesUpdated);
		Evidence.SetupResourceLoadAttempts = SetupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceLoadAttempts);
		Evidence.SetupResourceAsyncRequests = SetupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceAsyncRequests);
		Evidence.SetupResourceCacheHits = SetupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceCacheHits);
		Evidence.SetupResourceFailures = SetupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceFailures);
		Evidence.SetupResourceCancellations = SetupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceCancellations);
		Evidence.SetupBrushBuilds = SetupWorkload.GetCounter(
			EWebToUEPerformanceCounter::BrushBuilds);
		Evidence.WarmupResourceLoadAttempts = WarmupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceLoadAttempts);
		Evidence.WarmupResourceAsyncRequests = WarmupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceAsyncRequests);
		Evidence.WarmupResourceCacheHits = WarmupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceCacheHits);
		Evidence.WarmupResourceFailures = WarmupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceFailures);
		Evidence.WarmupResourceCancellations = WarmupWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceCancellations);
		Evidence.WarmupBrushBuilds = WarmupWorkload.GetCounter(
			EWebToUEPerformanceCounter::BrushBuilds);
		Evidence.MeasurementResourceLoadAttempts = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceLoadAttempts);
		Evidence.MeasurementResourceAsyncRequests = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceAsyncRequests);
		Evidence.MeasurementResourceFailures = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceFailures);
		Evidence.MeasurementResourceCancellations = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceCancellations);
		Evidence.SecondViewHydratedNodes = SecondViewWorkload.GetCounter(
			EWebToUEPerformanceCounter::HydratedNodes);
		Evidence.SecondViewResourceLoadAttempts = SecondViewWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceLoadAttempts);
		Evidence.SecondViewResourceAsyncRequests = SecondViewWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceAsyncRequests);
		Evidence.SecondViewResourceCacheHits = SecondViewWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceCacheHits);
		Evidence.SecondViewResourceFailures = SecondViewWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceFailures);
		Evidence.SecondViewResourceCancellations = SecondViewWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceCancellations);
		Evidence.bDynamicMaterialParameterSmoke = bDynamicMaterialParameterSmoke;
		Evidence.bCompositingSmoke = bCompositingSmoke;
		Evidence.WarmupMaterialParameterLookups = WarmupWorkload.GetCounter(
			EWebToUEPerformanceCounter::MaterialParameterLookups);
		Evidence.WarmupMaterialParameterEvaluations = WarmupWorkload.GetCounter(
			EWebToUEPerformanceCounter::MaterialParameterEvaluations);
		Evidence.WarmupMaterialInstancesCreated = WarmupWorkload.GetCounter(
			EWebToUEPerformanceCounter::MaterialInstancesCreated);
		Evidence.MeasurementMaterialParameterLookups = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::MaterialParameterLookups);
		Evidence.MeasurementMaterialParameterEvaluations =
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::MaterialParameterEvaluations);
		Evidence.MeasurementMaterialInstancesReused = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::MaterialInstancesReused);
		Evidence.MeasurementMaterialBrushPatches = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::MaterialBrushPatches);
		Evidence.MeasurementDisplayCommandsPatched = MeasurementWorkload.GetCounter(
			EWebToUEPerformanceCounter::DisplayCommandsPatched);
		Evidence.SecondViewMaterialInstancesCreated = SecondViewWorkload.GetCounter(
			EWebToUEPerformanceCounter::MaterialInstancesCreated);
		Evidence.SecondViewRssDeltaMiB = AfterSecondViewMemory.RssMiB -
			BeforeSecondViewMemory.RssMiB;
		Evidence.SecondViewLlmDeltaMiB = AfterSecondViewMemory.LlmMiB -
			BeforeSecondViewMemory.LlmMiB;
		Evidence.bLlmAvailable = IsLlmEnabled();
		Evidence.bKnownOwnedCensusAvailable = FirstViewCensus.bAvailable &&
			SecondViewCensus.bAvailable;
		Evidence.FirstViewKnownOwnedBytes = FirstViewCensus.GetViewBytes();
		Evidence.SecondViewKnownOwnedBytes = SecondViewCensus.GetViewBytes();
		Evidence.FirstViewSharedTemplateBytes =
			FirstViewCensus.SharedStyleTemplateBytes;
		Evidence.SecondViewSharedTemplateBytes =
			SecondViewCensus.SharedStyleTemplateBytes;
		bProductPolicyPass = bResourceSmoke
			? FWebToUEPackagedBenchmarkPolicy::ValidateResourceSmokeEvidence(
				Evidence, ProductPolicyFailures)
			: FWebToUEPackagedBenchmarkPolicy::ValidateWebToUEEvidence(
				Evidence, ProductPolicyFailures);
	}
	const double KnownSetupMs = ColdAssetLoadMs + ColdUiObjectConstructionMs +
		ColdTakeWidgetMs + ColdPrepassMs + ColdAttachMs;
	const double OtherSetupMs = FMath::Max(0.0, ColdSetupTotalMs - KnownSetupMs);
	const bool bColdAttributionComplete = ColdFirstFrameMs > 0.0 &&
		ColdSetupTotalMs + 0.5 >= KnownSetupMs &&
		FMath::Abs(ColdFirstFrameMs -
			(ColdSetupTotalMs + ColdFirstRenderWaitMs)) <= 1.0;
	const bool bSuccess = Samples.Num() == RequestedSamples && bScreenshotExists &&
		bHasRendererEvidence && bHasInputToDisplay && bHasWebToUERuntimeWork &&
		bHasTrajectoryEvidence && bSecondViewEvidence && bColdAttributionComplete &&
		bProductPolicyPass && bResourceIdentityValid && bVisualTransformEvidenceValid &&
		bTransitionEvidenceValid && bCompositingEvidenceValid;

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
	Root->SetNumberField(TEXT("schema_version"),
		FWebToUEPackagedBenchmarkPolicy::ResultSchemaVersion);
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
		: bDynamicMaterialParameterSmoke
			? TEXT("one typed Vector parameter update after MID warmup")
		: bVisualTransformSmoke
			? TEXT("pointer alternates across a transformed target to patch hover transform")
		: bTransitionSmoke
			? TEXT("pointer alternates across a five-property Transition target, then settles in the hovered completion state")
		: bResourceSmoke ? TEXT("packaged visible-resource observation")
		: TEXT("pointer hover plus bidirectional wheel scroll"));
	TSharedRef<FJsonObject> TrajectoryEvidence = MakeShared<FJsonObject>();
	TrajectoryEvidence->SetNumberField(TEXT("steps_dispatched"), TrajectoryStep);
	TrajectoryEvidence->SetNumberField(TEXT("measurement_steps"),
		MeasurementTrajectorySteps);
	TrajectoryEvidence->SetBoolField(TEXT("ui_effect_observed"), bHasTrajectoryEvidence);
	TrajectoryEvidence->SetStringField(TEXT("contract"), Mode == TEXT("UMG")
		? TEXT("UMG counterpart reports the resulting hover, scroll-offset, or HUD text/visibility state change.")
		: bDynamicMaterialParameterSmoke
			? TEXT("Warmup creates one View-owned MID; measurement commits one typed Vector update and patches only its Material brush/display command; the second View creates an isolated MID.")
		: bVisualTransformSmoke
			? TEXT("Measurement alternates pointer entry/exit over transformed semantic bounds and records inverse hit, exact nested-clip tests, transform/display/spatial patches, dirty regions, and zero Yoga mutation.")
		: bTransitionSmoke
			? TEXT("Measurement drives the persisted Transition IR through the Session-owned coordinator, observes five concurrent typed Tracks, waits for deterministic completion, and records Paint/spatial/dirty work with zero Yoga or resource load.")
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
	if (bResourceSmoke)
	{
		TSharedRef<FJsonObject> ResourceEvidence = MakeShared<FJsonObject>();
		ResourceEvidence->SetBoolField(TEXT("evaluated"), true);
		ResourceEvidence->SetBoolField(TEXT("passed"), bResourceIdentityValid);
		if (SmokeResource)
		{
			ResourceEvidence->SetStringField(TEXT("resource_id"),
				SmokeResource->ResourceId);
			ResourceEvidence->SetStringField(TEXT("path"),
				SmokeResource->Path.ToString());
			ResourceEvidence->SetStringField(TEXT("origin"),
				StaticEnum<EWebToUEResourceOrigin>()->GetNameStringByValue(
					static_cast<int64>(SmokeResource->Provenance.Origin)));
			ResourceEvidence->SetStringField(TEXT("author_reference"),
				SmokeResource->Provenance.AuthorReference);
			ResourceEvidence->SetStringField(TEXT("resolved_dependency_id"),
				SmokeResource->Provenance.ResolvedDependencyId);
			if (bTextureResourceSmoke)
			{
				ResourceEvidence->SetNumberField(TEXT("intrinsic_width"),
					SmokeResource->IntrinsicSize.X);
				ResourceEvidence->SetNumberField(TEXT("intrinsic_height"),
					SmokeResource->IntrinsicSize.Y);
			}
			else
			{
				ResourceEvidence->SetStringField(TEXT("residency"),
					StaticEnum<EWebToUEResidencyClass>()->GetNameStringByValue(
						static_cast<int64>(SmokeResource->Residency)));
				ResourceEvidence->SetNumberField(TEXT("brush_width"),
					SmokeResource->BrushImageSize.X);
				ResourceEvidence->SetNumberField(TEXT("brush_height"),
					SmokeResource->BrushImageSize.Y);
				const UMaterialInterface* Material = Cast<UMaterialInterface>(
					SmokeResource->Path.ResolveObject());
				ResourceEvidence->SetBoolField(TEXT("is_material_interface"),
					Material != nullptr);
				ResourceEvidence->SetBoolField(TEXT("is_dynamic_instance"),
					Material && Material->IsA<UMaterialInstanceDynamic>());
			}
		}
		DocumentContract->SetObjectField(
			bTextureResourceSmoke ? TEXT("texture_resource") : TEXT("material_resource"),
			ResourceEvidence);
	}
	Root->SetObjectField(TEXT("compiled_document"), DocumentContract);
	if (bVisualTransformSmoke)
	{
		TSharedRef<FJsonObject> TransformEvidence = MakeShared<FJsonObject>();
		TransformEvidence->SetBoolField(TEXT("evaluated"), true);
		TransformEvidence->SetBoolField(TEXT("passed"),
			bVisualTransformEvidenceValid);
		TransformEvidence->SetBoolField(TEXT("semantic_target_found"),
			bTransformSemanticFound);
		TSharedRef<FJsonObject> SemanticBounds = MakeShared<FJsonObject>();
		SemanticBounds->SetNumberField(TEXT("left"), TransformSemanticBounds.Left);
		SemanticBounds->SetNumberField(TEXT("top"), TransformSemanticBounds.Top);
		SemanticBounds->SetNumberField(TEXT("right"), TransformSemanticBounds.Right);
		SemanticBounds->SetNumberField(TEXT("bottom"), TransformSemanticBounds.Bottom);
		TransformEvidence->SetObjectField(TEXT("semantic_bounds"), SemanticBounds);
		TransformEvidence->SetNumberField(TEXT("warmup_transform_commands"),
			WarmupWorkload.GetCounter(
				EWebToUEPerformanceCounter::VisualTransformCommandsResolved));
		TransformEvidence->SetNumberField(TEXT("warmup_clip_zones"),
			WarmupWorkload.GetCounter(
				EWebToUEPerformanceCounter::ClipChainZonesResolved));
		TransformEvidence->SetNumberField(TEXT("measurement_transform_commands"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::VisualTransformCommandsResolved));
		TransformEvidence->SetNumberField(TEXT("measurement_clip_zones"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::ClipChainZonesResolved));
		TransformEvidence->SetNumberField(TEXT("measurement_inverse_hit_tests"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::InverseHitTests));
		TransformEvidence->SetNumberField(TEXT("measurement_exact_clip_tests"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::ExactClipTests));
		TransformEvidence->SetNumberField(TEXT("measurement_display_patches"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DisplayCommandsPatched));
		TransformEvidence->SetNumberField(TEXT("measurement_spatial_patches"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DisplaySpatialIndexPatches));
		TransformEvidence->SetNumberField(TEXT("measurement_dirty_rects"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DirtyRectsAdded));
		TransformEvidence->SetNumberField(TEXT("measurement_yoga_style_writes"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::YogaStyleWrites));
		TransformEvidence->SetNumberField(TEXT("measurement_yoga_nodes_dirtied"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::YogaNodesDirtied));
		TransformEvidence->SetNumberField(TEXT("measurement_yoga_results_changed"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::YogaLayoutResultsChanged));
		TransformEvidence->SetNumberField(TEXT("second_view_transform_commands"),
			SecondViewWorkload.GetCounter(
				EWebToUEPerformanceCounter::VisualTransformCommandsResolved));
		TransformEvidence->SetNumberField(TEXT("second_view_clip_zones"),
			SecondViewWorkload.GetCounter(
				EWebToUEPerformanceCounter::ClipChainZonesResolved));
		TransformEvidence->SetStringField(TEXT("contract"),
			TEXT("Semantic bounds consume the transformed full border-box AABB so clipped descendants remain navigable; the 128px broad-phase spatial index consumes transformed/clipped AABBs. Hit testing then evaluates every clip quad and inverse-transforms into the local border box. K=1 hover changes remain paint/hit-only and do not write or dirty Yoga."));
		Root->SetObjectField(TEXT("visual_transform"), TransformEvidence);
	}
	if (bCompositingSmoke)
	{
		const auto CounterTotal = [&](EWebToUEPerformanceCounter Counter)
		{
			return SetupWorkload.GetCounter(Counter) + WarmupWorkload.GetCounter(Counter) +
				MeasurementWorkload.GetCounter(Counter) +
				SecondViewWorkload.GetCounter(Counter);
		};
		TSharedRef<FJsonObject> CompositingEvidence = MakeShared<FJsonObject>();
		CompositingEvidence->SetNumberField(TEXT("evidence_schema_version"), 1);
		CompositingEvidence->SetBoolField(TEXT("evaluated"), true);
		CompositingEvidence->SetBoolField(TEXT("passed"), bCompositingEvidenceValid);
		CompositingEvidence->SetStringField(TEXT("policy"),
			TEXT("Tier selection consumes sealed requirements during Display rebuild; no frame heuristic or silent fallback is permitted."));
		CompositingEvidence->SetNumberField(TEXT("tier_decisions"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingTierDecisions));
		CompositingEvidence->SetNumberField(TEXT("tier0_decisions"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingTier0Decisions));
		CompositingEvidence->SetNumberField(TEXT("tier1_decisions"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingTier1Decisions));
		CompositingEvidence->SetNumberField(TEXT("plan_rejections"),
			CompositingPlanRejections);
		CompositingEvidence->SetNumberField(TEXT("cache_allocated"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingCacheAllocated));
		CompositingEvidence->SetNumberField(TEXT("cache_reused"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingCacheReused));
		CompositingEvidence->SetNumberField(TEXT("cache_released"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingCacheReleased));
		CompositingEvidence->SetNumberField(TEXT("cache_evicted"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingCacheEvicted));
		CompositingEvidence->SetNumberField(TEXT("active_layers"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingActiveLayers));
		CompositingEvidence->SetNumberField(TEXT("active_surfaces"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingActiveSurfaces));
		CompositingEvidence->SetNumberField(TEXT("allocated_pixels"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingAllocatedPixels));
		CompositingEvidence->SetNumberField(TEXT("allocated_bytes"),
			CounterTotal(EWebToUEPerformanceCounter::CompositingAllocatedBytes));
		CompositingEvidence->SetNumberField(TEXT("measurement_redraws"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::CompositingRedraws));
		CompositingEvidence->SetNumberField(TEXT("measurement_passes"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::CompositingPasses));
		CompositingEvidence->SetNumberField(TEXT("measurement_commands"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::CompositingCommands));
		CompositingEvidence->SetNumberField(TEXT("measurement_display_patches"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DisplayCommandsPatched));
		CompositingEvidence->SetNumberField(TEXT("measurement_spatial_patches"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DisplaySpatialIndexPatches));
		CompositingEvidence->SetNumberField(TEXT("measurement_dirty_rects"),
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::DirtyRectsAdded));
		CompositingEvidence->SetNumberField(TEXT("measurement_yoga_style_writes"),
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::YogaStyleWrites));
		CompositingEvidence->SetNumberField(TEXT("measurement_yoga_nodes_dirtied"),
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::YogaNodesDirtied));
		CompositingEvidence->SetNumberField(TEXT("measurement_yoga_results_changed"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::YogaLayoutResultsChanged));
		CompositingEvidence->SetBoolField(TEXT("shared_parent_material"),
			bResourceIdentityValid);
		CompositingEvidence->SetBoolField(TEXT("primary_mid_committed"),
			bDynamicMaterialMeasurementApplied);
		CompositingEvidence->SetBoolField(TEXT("second_view_mid_committed"),
			bDynamicMaterialSecondViewApplied);
		TSharedRef<FJsonObject> Tier2 = MakeShared<FJsonObject>();
		Tier2->SetStringField(TEXT("status"), TEXT("N/A"));
		Tier2->SetBoolField(TEXT("runtime_evaluated"), false);
		Tier2->SetNumberField(TEXT("decisions"), CompositingTier2Decisions);
		Tier2->SetStringField(TEXT("reason"),
			TEXT("The sealed product fixture contains no overlapping descendants under group opacity; the adversarial Automation prototype proves deterministic Tier 2 classification and stable unavailable-backend refusal."));
		CompositingEvidence->SetObjectField(TEXT("tier2_subtree_layer"), Tier2);
		TSharedRef<FJsonObject> Tier3 = MakeShared<FJsonObject>();
		Tier3->SetStringField(TEXT("status"), TEXT("N/A"));
		Tier3->SetBoolField(TEXT("runtime_evaluated"), false);
		Tier3->SetNumberField(TEXT("decisions"), CompositingTier3Decisions);
		Tier3->SetStringField(TEXT("reason"),
			TEXT("No sealed node samples a composited subtree or requests an independent Surface; reporting zero as measured Render Target cost would be invalid."));
		CompositingEvidence->SetObjectField(TEXT("tier3_render_target"), Tier3);
		CompositingEvidence->SetStringField(TEXT("contract"),
			TEXT("K=1 measurement reports local hit/display/spatial/dirty work, no Yoga mutation, View-isolated MIDs over one parent Material, and renderer-backed redraw/pass/command counts. Active layer/surface pixel and byte fields describe only real offscreen allocations."));
		Root->SetObjectField(TEXT("compositing"), CompositingEvidence);
	}
	if (bTransitionSmoke)
	{
		TSharedRef<FJsonObject> TransitionEvidence = MakeShared<FJsonObject>();
		TransitionEvidence->SetNumberField(TEXT("evidence_schema_version"), 1);
		TransitionEvidence->SetBoolField(TEXT("evaluated"), true);
		TransitionEvidence->SetBoolField(TEXT("passed"), bTransitionEvidenceValid);
		TransitionEvidence->SetBoolField(TEXT("compiled_ir_valid"), bTransitionIrValid);
		TransitionEvidence->SetNumberField(TEXT("compiled_transition_count"),
			BenchmarkDocument.IsValid()
				? BenchmarkDocument->GetCompiledAnimationIR().Transitions.Num() : 0);
		TransitionEvidence->SetNumberField(TEXT("maximum_active_tracks"),
			MaxTransitionActiveTracks);
		TransitionEvidence->SetNumberField(TEXT("active_observation_frames"),
			TransitionActiveObservationFrames);
		TransitionEvidence->SetNumberField(TEXT("trace_count"), TransitionTraceCount);
		TransitionEvidence->SetNumberField(TEXT("trace_budget"),
			FWebToUEAnimationBudget().MaxTraceEntries);
		TransitionEvidence->SetNumberField(TEXT("started_count"),
			TransitionStartedCount);
		TransitionEvidence->SetNumberField(TEXT("retargeted_count"),
			TransitionRetargetedCount);
		TransitionEvidence->SetNumberField(TEXT("sampled_count"),
			TransitionSampledCount);
		TransitionEvidence->SetNumberField(TEXT("completed_count"),
			TransitionCompletedCount);
		TransitionEvidence->SetNumberField(TEXT("ticker_invocations"),
			static_cast<double>(TransitionTickerInvocations));
		TransitionEvidence->SetBoolField(TEXT("active_tracks_and_ticker_released"),
			bTransitionTickerReleased);
		TransitionEvidence->SetNumberField(TEXT("transaction_count"),
			TransitionTransactionCount);
		TransitionEvidence->SetNumberField(TEXT("property_evaluation_count"),
			TransitionEvaluationCount);
		TransitionEvidence->SetNumberField(TEXT("state_mutation_count"),
			TransitionMutationCount);
		TransitionEvidence->SetBoolField(TEXT("all_transactions_committed"),
			bTransitionTransactionsCommitted);
		TransitionEvidence->SetBoolField(TEXT("initial_semantic_target_found"),
			bInitialTransitionSemanticFound);
		TransitionEvidence->SetBoolField(TEXT("final_semantic_target_found"),
			bFinalTransitionSemanticFound);
		TransitionEvidence->SetBoolField(TEXT("semantic_bounds_changed"),
			bTransitionBoundsChanged);
		const auto BoundsObject = [](const FSlateRect& Bounds)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("left"), Bounds.Left);
			Result->SetNumberField(TEXT("top"), Bounds.Top);
			Result->SetNumberField(TEXT("right"), Bounds.Right);
			Result->SetNumberField(TEXT("bottom"), Bounds.Bottom);
			return Result;
		};
		TransitionEvidence->SetObjectField(TEXT("initial_semantic_bounds"),
			BoundsObject(InitialTransitionSemanticBounds));
		TransitionEvidence->SetObjectField(TEXT("final_semantic_bounds"),
			BoundsObject(FinalTransitionSemanticBounds));
		TransitionEvidence->SetNumberField(TEXT("display_commands_patched"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DisplayCommandsPatched));
		TransitionEvidence->SetNumberField(TEXT("spatial_index_patches"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::DisplaySpatialIndexPatches));
		TransitionEvidence->SetNumberField(TEXT("dirty_rects_added"),
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::DirtyRectsAdded));
		TransitionEvidence->SetNumberField(TEXT("yoga_style_writes"),
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::YogaStyleWrites));
		TransitionEvidence->SetNumberField(TEXT("yoga_nodes_dirtied"),
			MeasurementWorkload.GetCounter(EWebToUEPerformanceCounter::YogaNodesDirtied));
		TransitionEvidence->SetNumberField(TEXT("yoga_results_changed"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::YogaLayoutResultsChanged));
		TransitionEvidence->SetNumberField(TEXT("resource_load_attempts"),
			MeasurementWorkload.GetCounter(
				EWebToUEPerformanceCounter::ResourceLoadAttempts));
		TransitionEvidence->SetStringField(TEXT("contract"),
			TEXT("Numeric Track/Clock/transaction evidence, sampled Paint/spatial/dirty workload, renderer-backed frame distributions, and the screenshot are independent layers. The screenshot is requested only after a forced exit completion followed by a forced hover completion. K=5 is one legal five-address set on one target; the default sample budget 256 covers it in one Pump. This does not prove Portal/compositing, input-to-pixel causality, or WTUE-to-UMG performance equivalence."));
		Root->SetObjectField(TEXT("transition"), TransitionEvidence);
	}
	if (bDynamicMaterialParameterSmoke)
	{
		TSharedRef<FJsonObject> ParameterEvidence = MakeShared<FJsonObject>();
		ParameterEvidence->SetBoolField(TEXT("evaluated"), true);
		ParameterEvidence->SetBoolField(TEXT("passed"), bHasTrajectoryEvidence);
		ParameterEvidence->SetStringField(TEXT("address"), TEXT("material.vector.Tint"));
		ParameterEvidence->SetStringField(TEXT("durable_owner"), TEXT("Binding"));
		ParameterEvidence->SetBoolField(TEXT("warmup_committed"),
			bDynamicMaterialWarmupApplied);
		ParameterEvidence->SetBoolField(TEXT("measurement_committed"),
			bDynamicMaterialMeasurementApplied);
		ParameterEvidence->SetBoolField(TEXT("second_view_committed"),
			bDynamicMaterialSecondViewApplied);
		ParameterEvidence->SetStringField(TEXT("warmup_value"),
			TEXT("(0.08,0.28,1.0,1.0)"));
		ParameterEvidence->SetStringField(TEXT("measurement_value"),
			TEXT("(1.0,0.08,0.18,1.0)"));
		ParameterEvidence->SetStringField(TEXT("second_view_value"),
			TEXT("(0.05,0.9,0.2,1.0)"));
		Root->SetObjectField(TEXT("material_parameter"), ParameterEvidence);
	}
	Root->SetObjectField(TEXT("setup_workload"), WorkloadObject(SetupWorkload));
	Root->SetObjectField(TEXT("warmup_workload"), WorkloadObject(WarmupWorkload));
	Root->SetObjectField(TEXT("measurement_workload"), WorkloadObject(MeasurementWorkload));
	Root->SetObjectField(TEXT("second_view_workload"), WorkloadObject(SecondViewWorkload));
	Root->SetStringField(TEXT("workload_contract"),
		TEXT("Setup captures load/hydrate/prepass for primary K=1; warmup captures first layout/display-list construction; measurement resets after warmup and reports aggregate local-update work; second_view captures an independent K=1 hydrate/prepass of the same immutable document revision after measurement."));
	Root->SetNumberField(TEXT("cold_first_frame_ms"), ColdFirstFrameMs);
	TSharedRef<FJsonObject> ColdAttribution = MakeShared<FJsonObject>();
	ColdAttribution->SetBoolField(TEXT("complete"), bColdAttributionComplete);
	ColdAttribution->SetNumberField(TEXT("asset_load_ms"), ColdAssetLoadMs);
	ColdAttribution->SetNumberField(TEXT("ui_object_construction_ms"),
		ColdUiObjectConstructionMs);
	ColdAttribution->SetNumberField(TEXT("take_widget_ms"), ColdTakeWidgetMs);
	ColdAttribution->SetNumberField(TEXT("prepass_ms"), ColdPrepassMs);
	ColdAttribution->SetNumberField(TEXT("attach_ms"), ColdAttachMs);
	ColdAttribution->SetNumberField(TEXT("other_setup_ms"), OtherSetupMs);
	ColdAttribution->SetNumberField(TEXT("setup_total_ms"), ColdSetupTotalMs);
	ColdAttribution->SetNumberField(TEXT("first_renderer_wait_ms"),
		ColdFirstRenderWaitMs);
	ColdAttribution->SetNumberField(TEXT("ui_setup_to_first_renderer_ms"),
		ColdFirstFrameMs);
	ColdAttribution->SetStringField(TEXT("contract"),
		TEXT("Cold time begins immediately before document load/UI construction and ends at the first matching Slate renderer callback. setup_total plus first_renderer_wait reconciles to the total; other_setup is measured setup overhead outside the named stages."));
	Root->SetObjectField(TEXT("cold_start_attribution"), ColdAttribution);

	auto MemoryPointObject = [](const FMemoryPoint& Point)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("rss_mib"), Point.RssMiB);
		Result->SetNumberField(TEXT("llm_mib"), Point.LlmMiB);
		return Result;
	};
	auto CensusObject = [](const FKnownOwnedCensus& Census)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("available"), Census.bAvailable);
		Result->SetNumberField(TEXT("shared_style_template_bytes"),
			static_cast<double>(Census.SharedStyleTemplateBytes));
		Result->SetNumberField(TEXT("runtime_bytes"),
			static_cast<double>(Census.RuntimeBytes));
		Result->SetNumberField(TEXT("presentation_bytes"),
			static_cast<double>(Census.PresentationBytes));
		Result->SetNumberField(TEXT("view_total_bytes"),
			static_cast<double>(Census.GetViewBytes()));
		return Result;
	};
	TSharedRef<FJsonObject> MemoryEvidence = MakeShared<FJsonObject>();
	MemoryEvidence->SetBoolField(TEXT("second_view_created"), bSecondViewCreated);
	MemoryEvidence->SetObjectField(TEXT("before_first_view"),
		MemoryPointObject(BeforeFirstViewMemory));
	MemoryEvidence->SetObjectField(TEXT("after_first_view_prepass"),
		MemoryPointObject(AfterFirstViewMemory));
	MemoryEvidence->SetObjectField(TEXT("before_second_view"),
		MemoryPointObject(BeforeSecondViewMemory));
	MemoryEvidence->SetObjectField(TEXT("after_second_view_prepass"),
		MemoryPointObject(AfterSecondViewMemory));
	MemoryEvidence->SetNumberField(TEXT("first_view_rss_delta_mib"),
		AfterFirstViewMemory.RssMiB - BeforeFirstViewMemory.RssMiB);
	MemoryEvidence->SetNumberField(TEXT("first_view_llm_delta_mib"),
		AfterFirstViewMemory.LlmMiB - BeforeFirstViewMemory.LlmMiB);
	MemoryEvidence->SetNumberField(TEXT("second_view_rss_delta_mib"),
		AfterSecondViewMemory.RssMiB - BeforeSecondViewMemory.RssMiB);
	MemoryEvidence->SetNumberField(TEXT("second_view_llm_delta_mib"),
		AfterSecondViewMemory.LlmMiB - BeforeSecondViewMemory.LlmMiB);
	MemoryEvidence->SetObjectField(TEXT("first_view_known_owned"),
		CensusObject(FirstViewCensus));
	MemoryEvidence->SetObjectField(TEXT("second_view_known_owned"),
		CensusObject(SecondViewCensus));
	MemoryEvidence->SetStringField(TEXT("contract"),
		TEXT("Process RSS/LLM points are raw Packaged observations; positive second-view deltas are bounded. Development additionally records exact WTUE known-owned Runtime/Presentation capacity while shared Style Template bytes are reported separately. Shipping without compiled LLM or census reports availability rather than zero-as-proof."));
	Root->SetObjectField(TEXT("memory_evidence"), MemoryEvidence);

	TSharedRef<FJsonObject> ProductPolicy = MakeShared<FJsonObject>();
	ProductPolicy->SetBoolField(TEXT("evaluated"), Mode == TEXT("WebToUE"));
	ProductPolicy->SetBoolField(TEXT("passed"), bProductPolicyPass);
	ProductPolicy->SetNumberField(TEXT("maximum_compiled_resources"),
		bResourceSmoke
			? FWebToUEPackagedBenchmarkPolicy::ResourceSmokeExpectedCompiledResources
			: FWebToUEPackagedBenchmarkPolicy::FrozenCorpusMaximumCompiledResources);
	ProductPolicy->SetNumberField(TEXT("maximum_style_node_visits_per_trajectory"),
		static_cast<double>(FWebToUEPackagedBenchmarkPolicy::MaximumStyleNodeVisitsPerTrajectory));
	ProductPolicy->SetNumberField(TEXT("maximum_selector_evaluations_per_trajectory"),
		static_cast<double>(FWebToUEPackagedBenchmarkPolicy::MaximumSelectorEvaluationsPerTrajectory));
	ProductPolicy->SetNumberField(TEXT("maximum_binding_nodes_updated_per_trajectory"),
		static_cast<double>(FWebToUEPackagedBenchmarkPolicy::MaximumBindingNodesUpdatedPerTrajectory));
	ProductPolicy->SetNumberField(TEXT("maximum_second_view_process_memory_delta_mib"),
		FWebToUEPackagedBenchmarkPolicy::MaximumSecondViewProcessMemoryDeltaMiB);
	ProductPolicy->SetNumberField(TEXT("maximum_second_view_known_owned_ratio"),
		FWebToUEPackagedBenchmarkPolicy::MaximumSecondViewKnownOwnedRatio);
	TArray<TSharedPtr<FJsonValue>> PolicyFailureValues;
	for (const FString& Failure : ProductPolicyFailures)
	{
		PolicyFailureValues.Add(MakeShared<FJsonValueString>(Failure));
	}
	ProductPolicy->SetArrayField(TEXT("failures"), PolicyFailureValues);
	Root->SetObjectField(TEXT("product_policy"), ProductPolicy);
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
		if (!bSecondViewEvidence)
			Failures.Add(MakeShared<FJsonValueString>(TEXT("second view evidence unavailable")));
		if (!bColdAttributionComplete)
			Failures.Add(MakeShared<FJsonValueString>(TEXT("cold-start attribution incomplete")));
		if (!bVisualTransformEvidenceValid)
			Failures.Add(MakeShared<FJsonValueString>(
				TEXT("visual transform/clip evidence incomplete")));
		if (!bTransitionEvidenceValid)
			Failures.Add(MakeShared<FJsonValueString>(
				TEXT("compiled Transition/Track/Paint evidence incomplete")));
		if (!bCompositingEvidenceValid)
			Failures.Add(MakeShared<FJsonValueString>(
				TEXT("deterministic compositing evidence incomplete")));
		for (const FString& Failure : ProductPolicyFailures)
		{
			Failures.Add(MakeShared<FJsonValueString>(Failure));
		}
		Root->SetArrayField(TEXT("failures"), Failures);
	}

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	FFileHelper::SaveStringToFile(Json, *JsonPath);
	UE_LOG(LogTemp, Display, TEXT("WTUE_BENCHMARK_COMPLETE success=%s json=%s"),
		bSuccess ? TEXT("true") : TEXT("false"), *JsonPath);
	ShutdownUi();
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
	ShutdownUi();
	FPlatformMisc::RequestExit(true);
}
