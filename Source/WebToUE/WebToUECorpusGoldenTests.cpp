#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEDemoViewModel.h"

#include "Dom/JsonObject.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Slate/WidgetRenderer.h"
#include "WebToUECoreTypes.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUESemantics.h"
#include "WebToUEView.h"

namespace WebToUECorpusGolden
{
	constexpr int32 LogicalWidth = 1280;
	constexpr int32 LogicalHeight = 720;
	constexpr int32 GridWidth = 32;
	constexpr int32 GridHeight = 18;
	constexpr int32 SignatureChannels = 4;

	struct FDiff
	{
		double MeanAbsolute = 0.0;
		double LargeDifferenceRatio = 0.0;
	};

	static TArray<uint8> BuildSignature(
		const TArray<FColor>& Pixels, int32 Width, int32 Height)
	{
		TArray<uint8> Result;
		Result.Reserve(GridWidth * GridHeight * SignatureChannels);
		for (int32 GridY = 0; GridY < GridHeight; ++GridY)
		{
			const int32 MinY = GridY * Height / GridHeight;
			const int32 MaxY = (GridY + 1) * Height / GridHeight;
			for (int32 GridX = 0; GridX < GridWidth; ++GridX)
			{
				const int32 MinX = GridX * Width / GridWidth;
				const int32 MaxX = (GridX + 1) * Width / GridWidth;
				uint64 Red = 0;
				uint64 Green = 0;
				uint64 Blue = 0;
				uint64 Alpha = 0;
				uint64 Count = 0;
				for (int32 Y = MinY; Y < MaxY; ++Y)
				{
					for (int32 X = MinX; X < MaxX; ++X)
					{
						const FColor& Pixel = Pixels[Y * Width + X];
						Red += Pixel.R;
						Green += Pixel.G;
						Blue += Pixel.B;
						Alpha += Pixel.A;
						++Count;
					}
				}
				const auto Quantize = [Count](uint64 Sum)
				{
					const uint8 Average = static_cast<uint8>(Sum / FMath::Max<uint64>(Count, 1));
					return static_cast<uint8>(FMath::Min(248, ((Average + 4) / 8) * 8));
				};
				Result.Add(Quantize(Red));
				Result.Add(Quantize(Green));
				Result.Add(Quantize(Blue));
				Result.Add(Quantize(Alpha));
			}
		}
		return Result;
	}

	static FString EncodeSignature(TConstArrayView<uint8> Signature)
	{
		FString Result;
		Result.Reserve(Signature.Num() * 2);
		for (const uint8 Value : Signature)
		{
			Result += FString::Printf(TEXT("%02x"), Value);
		}
		return Result;
	}

	static bool DecodeSignature(const FString& Encoded, TArray<uint8>& Out)
	{
		const int32 ExpectedCharacters =
			GridWidth * GridHeight * SignatureChannels * 2;
		if (Encoded.Len() != ExpectedCharacters) return false;
		Out.Reset(ExpectedCharacters / 2);
		for (int32 Index = 0; Index < Encoded.Len(); Index += 2)
		{
			const int32 High = FParse::HexDigit(Encoded[Index]);
			const int32 Low = FParse::HexDigit(Encoded[Index + 1]);
			if (High < 0 || Low < 0) return false;
			Out.Add(static_cast<uint8>((High << 4) | Low));
		}
		return true;
	}

	static FDiff Compare(
		TConstArrayView<uint8> Actual, TConstArrayView<uint8> Expected)
	{
		FDiff Result;
		if (Actual.Num() != Expected.Num() || Actual.IsEmpty())
		{
			Result.MeanAbsolute = TNumericLimits<double>::Max();
			Result.LargeDifferenceRatio = 1.0;
			return Result;
		}
		uint64 Difference = 0;
		int32 LargeDifferences = 0;
		for (int32 Index = 0; Index < Actual.Num(); ++Index)
		{
			const int32 Delta = FMath::Abs(
				static_cast<int32>(Actual[Index]) - Expected[Index]);
			Difference += Delta;
			if (Delta > 24) ++LargeDifferences;
		}
		Result.MeanAbsolute = static_cast<double>(Difference) / Actual.Num();
		Result.LargeDifferenceRatio =
			static_cast<double>(LargeDifferences) / Actual.Num();
		return Result;
	}

	static bool ReadPixels(UTextureRenderTarget2D& RenderTarget, TArray<FColor>& Out)
	{
		FlushRenderingCommands();
		FTextureRenderTargetResource* Resource =
			RenderTarget.GameThread_GetRenderTargetResource();
		return Resource && Resource->ReadPixels(Out);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUECorpusDpiGoldenTest,
	"WebToUE.Benchmark.CorpusDpiGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUECorpusOptionalInputContractTest,
	"WebToUE.Benchmark.CorpusOptionalInputContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUECorpusOptionalInputContractTest::RunTest(const FString& Parameters)
{
	const FName Corpora[] = {
		TEXT("MainMenu"), TEXT("HUD"), TEXT("ScrollableSettings")
	};
	for (const FName Corpus : Corpora)
	{
		const FString CorpusName = Corpus.ToString();
		const FString SourceDirectory = FPaths::Combine(
			FPaths::ProjectDir(), TEXT("WebUI/Examples"));
		FString Html;
		FString Css;
		TestTrue(*FString::Printf(TEXT("%s frozen HTML source is readable"),
			*CorpusName), FFileHelper::LoadFileToString(Html,
				*FPaths::Combine(SourceDirectory, CorpusName + TEXT(".html"))));
		TestTrue(*FString::Printf(TEXT("%s frozen CSS source is readable"),
			*CorpusName), FFileHelper::LoadFileToString(Css,
				*FPaths::Combine(SourceDirectory, CorpusName + TEXT(".css"))));
		const FString Contract = (Html + TEXT("\n") + Css).ToLower();
		TestFalse(*FString::Printf(TEXT("%s does not declare a visible scrollbar"),
			*CorpusName), Contract.Contains(TEXT("scrollbar")));
		TestFalse(*FString::Printf(TEXT("%s does not declare a drag contract"),
			*CorpusName), Contract.Contains(TEXT("draggable")) ||
			Contract.Contains(TEXT("data-ue-on-drag")) ||
			Contract.Contains(TEXT("drag-handle")));
		TestFalse(*FString::Printf(TEXT("%s does not declare touch or inertia"),
			*CorpusName), Contract.Contains(TEXT("data-ue-on-touch")) ||
			Contract.Contains(TEXT("inertia")));
		TestFalse(*FString::Printf(TEXT("%s has no horizontal overflow contract"),
			*CorpusName), Contract.Contains(TEXT("overflow-x")));
	}

	FString SettingsHtml;
	FString SettingsCss;
	const FString SourceDirectory = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("WebUI/Examples"));
	FFileHelper::LoadFileToString(SettingsHtml,
		*FPaths::Combine(SourceDirectory, TEXT("ScrollableSettings.html")));
	FFileHelper::LoadFileToString(SettingsCss,
		*FPaths::Combine(SourceDirectory, TEXT("ScrollableSettings.css")));
	TestTrue(TEXT("The only scrolling Corpus explicitly instructs wheel input"),
		SettingsHtml.Contains(TEXT("use the wheel to scroll")));
	TestTrue(TEXT("The only scrolling Corpus uses immediate overflow scrolling"),
		SettingsCss.Replace(TEXT(" "), TEXT("")).Contains(TEXT("overflow:auto")));
	AddInfo(TEXT("P0_5_IF_USED scrollbar_drag=N/A corpus=MainMenu,HUD,ScrollableSettings reason=wheel-only-no-visible-scrollbar-or-drag-contract"));
	return true;
}

bool FWebToUECorpusDpiGoldenTest::RunTest(const FString& Parameters)
{
	using namespace WebToUECorpusGolden;
	if (!TestTrue(TEXT("Slate is initialized for framebuffer Golden rendering"),
		FSlateApplication::IsInitialized()))
	{
		return false;
	}
	FSlateApplication& Slate = FSlateApplication::Get();
	const bool bHadCustomSafeZone = Slate.IsCustomSafeZoneSet();
	const FMargin PreviousSafeZone = Slate.GetCustomSafeZone();
	ON_SCOPE_EXIT
	{
		if (bHadCustomSafeZone) Slate.SetCustomSafeZone(PreviousSafeZone);
		else Slate.ResetCustomSafeZone();
		Slate.OnDebugSafeZoneChanged.Broadcast(
			bHadCustomSafeZone ? PreviousSafeZone : FMargin(), true);
	};
	Slate.SetCustomSafeZone(FMargin(0.05f));
	Slate.OnDebugSafeZoneChanged.Broadcast(FMargin(0.05f), true);

	const FString BaselinePath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("WebUI/Golden/M2_8_CorpusDpiGolden.json"));
	TSharedPtr<FJsonObject> BaselineRoot;
	FString BaselineText;
	const bool bHasBaseline = FFileHelper::LoadFileToString(
		BaselineText, *BaselinePath) &&
		FJsonSerializer::Deserialize(
			TJsonReaderFactory<>::Create(BaselineText), BaselineRoot) &&
		BaselineRoot.IsValid();
	const TSharedPtr<FJsonObject>* BaselineCorpora = nullptr;
	if (bHasBaseline)
	{
		BaselineRoot->TryGetObjectField(TEXT("corpora"), BaselineCorpora);
	}

	const FString OutputDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation/CorpusDpiGolden"));
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	TSharedRef<FJsonObject> CandidateRoot = MakeShared<FJsonObject>();
	CandidateRoot->SetNumberField(TEXT("schema"), 1);
	CandidateRoot->SetNumberField(TEXT("logical_width"), LogicalWidth);
	CandidateRoot->SetNumberField(TEXT("logical_height"), LogicalHeight);
	CandidateRoot->SetNumberField(TEXT("grid_width"), GridWidth);
	CandidateRoot->SetNumberField(TEXT("grid_height"), GridHeight);
	TSharedRef<FJsonObject> CandidateCorpora = MakeShared<FJsonObject>();
	CandidateRoot->SetObjectField(TEXT("corpora"), CandidateCorpora);

	const FName Corpora[] = {
		TEXT("MainMenu"), TEXT("HUD"), TEXT("ScrollableSettings")
	};
	for (const FName Corpus : Corpora)
	{
		const FString CorpusName = Corpus.ToString();
		const FString AssetPath = FString::Printf(
			TEXT("/Game/WebToUEExamples/%s.%s"), *CorpusName, *CorpusName);
		UWebToUEDocument* Document = LoadObject<UWebToUEDocument>(nullptr, *AssetPath);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads for DPI Golden"),
			*CorpusName), Document))
		{
			continue;
		}

		UWebToUEDemoViewModel* ViewModel =
			NewObject<UWebToUEDemoViewModel>(GetTransientPackage());
		ViewModel->SetPlayerName(FText::FromString(TEXT("Golden Player")));
		ViewModel->SetHealth(18, 100);
		UWebToUEView* View = NewObject<UWebToUEView>(GetTransientPackage());
		View->AddToRoot();
		TSharedPtr<SWidget> HostedWidget;
		ON_SCOPE_EXIT
		{
			HostedWidget.Reset();
			View->ReleaseSlateResources(true);
			View->RemoveFromRoot();
		};
		View->SetDocument(Document);
		HostedWidget = View->TakeWidget();
		View->SetDataContext(ViewModel);

		TArray<uint8> DpiOneSignature;
		for (const float DPIScale : { 1.0f, 2.0f })
		{
			if (Corpus == TEXT("MainMenu"))
			{
				ViewModel->SetCanStart(false);
				FWebToUENode* Start =
					View->FindRuntimeNodeByIdForTesting(TEXT("start-button"));
				TestTrue(*FString::Printf(TEXT("%s %.0fx applies disabled binding"),
					*CorpusName, DPIScale),
					Start && !View->GetRuntimeEnabledForTesting(*Start));
				ViewModel->SetCanStart(true);
				TestTrue(*FString::Printf(TEXT("%s %.0fx restores enabled binding"),
					*CorpusName, DPIScale),
					Start && View->GetRuntimeEnabledForTesting(*Start));
			}
			else if (Corpus == TEXT("HUD"))
			{
				ViewModel->SetHealth(24, 100);
				ViewModel->SetHealth(18, 100);
				TestTrue(*FString::Printf(TEXT("%s %.0fx retains visible warning binding"),
					*CorpusName, DPIScale), ViewModel->bShowWarning);
			}

			const FIntPoint PhysicalSize(
				FMath::RoundToInt(LogicalWidth * DPIScale),
				FMath::RoundToInt(LogicalHeight * DPIScale));
			View->SetSafeZoneOverrideForTesting(
				FVector2D(PhysicalSize), DPIScale);
			FWidgetRenderer Renderer(false, true);
			UTextureRenderTarget2D* RenderTarget = FWidgetRenderer::CreateTargetFor(
				FVector2D(PhysicalSize), TF_Bilinear, false);
			if (!TestNotNull(*FString::Printf(TEXT("%s %.0fx creates a render target"),
				*CorpusName, DPIScale), RenderTarget))
			{
				continue;
			}
			Renderer.DrawWidget(RenderTarget, HostedWidget.ToSharedRef(), DPIScale,
				FVector2D(PhysicalSize), 0.0f);
			TArray<FColor> Pixels;
			TestTrue(*FString::Printf(TEXT("%s %.0fx reads the rendered framebuffer"),
				*CorpusName, DPIScale), ReadPixels(*RenderTarget, Pixels));
			TestEqual(*FString::Printf(TEXT("%s %.0fx returns every framebuffer pixel"),
				*CorpusName, DPIScale), Pixels.Num(), PhysicalSize.X * PhysicalSize.Y);
			if (Pixels.Num() != PhysicalSize.X * PhysicalSize.Y) continue;

			FBufferArchive PngBytes;
			TestTrue(*FString::Printf(TEXT("%s %.0fx exports a PNG"),
				*CorpusName, DPIScale),
				FImageUtils::ExportRenderTarget2DAsPNG(RenderTarget, PngBytes));
			const FString PngPath = FPaths::Combine(OutputDirectory,
				FString::Printf(TEXT("%s_%.0fx.png"), *CorpusName, DPIScale));
			TestTrue(*FString::Printf(TEXT("%s %.0fx writes visual evidence"),
				*CorpusName, DPIScale),
				FFileHelper::SaveArrayToFile(PngBytes, *PngPath));
			TestTrue(*FString::Printf(TEXT("%s %.0fx PNG is non-empty"),
				*CorpusName, DPIScale), IFileManager::Get().FileSize(*PngPath) > 1024);
			AddInfo(FString::Printf(TEXT("CORPUS_DPI_GOLDEN=%s"), *PngPath));

			const TArray<uint8> Signature = BuildSignature(
				Pixels, PhysicalSize.X, PhysicalSize.Y);
			if (DPIScale == 1.0f)
			{
				DpiOneSignature = Signature;
				CandidateCorpora->SetStringField(
					CorpusName, EncodeSignature(Signature));
			}
			else
			{
				const FDiff CrossDpi = Compare(Signature, DpiOneSignature);
				AddInfo(FString::Printf(
					TEXT("CROSS_DPI_DIFF corpus=%s mean=%.4f large_ratio=%.6f"),
					*CorpusName, CrossDpi.MeanAbsolute,
					CrossDpi.LargeDifferenceRatio));
				TestTrue(*FString::Printf(TEXT("%s 2x normalized image matches 1x"),
					*CorpusName), CrossDpi.MeanAbsolute <= 4.0 &&
					CrossDpi.LargeDifferenceRatio <= 0.03);
			}

			TArray<FWebToUESemanticNode> SemanticNodes;
			View->GetSemanticNodes(SemanticNodes);
			if (!SemanticNodes.IsEmpty())
			{
				const FMargin SafeMargin =
					View->GetSafeZoneMarginForTesting(DPIScale);
				const FVector2D ContentSize(
					LogicalWidth - SafeMargin.GetTotalSpaceAlong<Orient_Horizontal>(),
					LogicalHeight - SafeMargin.GetTotalSpaceAlong<Orient_Vertical>());
				const FGeometry LeafGeometry = FGeometry::MakeRoot(
					ContentSize, FSlateLayoutTransform(DPIScale));
				const FSlateRect Bounds = SemanticNodes[0].Bounds;
				const FVector2D LocalCenter(
					(Bounds.Left + Bounds.Right) * 0.5f,
					(Bounds.Top + Bounds.Bottom) * 0.5f);
				const FVector2D ScreenCenter =
					LeafGeometry.LocalToAbsolute(LocalCenter);
				const FPointerEvent MoveEvent(0, ScreenCenter,
					FVector2D::ZeroVector, TSet<FKey>(), FKey(), 0.0f,
					FModifierKeysState());
				FWebToUEPerformanceCapture InputCapture;
				View->OnMouseMoveForTesting(LeafGeometry, MoveEvent);
				const FWebToUEPerformanceSnapshot Input = InputCapture.GetSnapshot();
				TestTrue(*FString::Printf(TEXT("%s %.0fx input reaches a hit candidate"),
					*CorpusName, DPIScale), Input.GetCounter(
						EWebToUEPerformanceCounter::HitTestCandidates) > 0);
				TestTrue(*FString::Printf(TEXT("%s %.0fx input changes semantic hover"),
					*CorpusName, DPIScale), Input.GetCounter(
						EWebToUEPerformanceCounter::PseudoStateNodesChanged) > 0);
				View->SetHoveredNodeForTesting(nullptr);
			}
		}

		if (BaselineCorpora && *BaselineCorpora)
		{
			FString EncodedBaseline;
			TArray<uint8> BaselineSignature;
			const bool bDecoded = (*BaselineCorpora)->TryGetStringField(
				CorpusName, EncodedBaseline) &&
				DecodeSignature(EncodedBaseline, BaselineSignature);
			TestTrue(*FString::Printf(TEXT("%s owns a valid source-controlled Golden"),
				*CorpusName), bDecoded);
			if (bDecoded)
			{
				const FDiff BaselineDiff = Compare(DpiOneSignature, BaselineSignature);
				AddInfo(FString::Printf(
					TEXT("BASELINE_DIFF corpus=%s mean=%.4f large_ratio=%.6f"),
					*CorpusName, BaselineDiff.MeanAbsolute,
					BaselineDiff.LargeDifferenceRatio));
				TestTrue(*FString::Printf(TEXT("%s matches its screenshot Golden"),
					*CorpusName), BaselineDiff.MeanAbsolute <= 2.0 &&
					BaselineDiff.LargeDifferenceRatio <= 0.01);
			}
		}
	}

	FString CandidateText;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&CandidateText);
	FJsonSerializer::Serialize(CandidateRoot, Writer);
	const FString CandidatePath = FPaths::Combine(
		OutputDirectory, TEXT("M2_8_CorpusDpiGolden.candidate.json"));
	TestTrue(TEXT("The reproducible Golden candidate is written"),
		FFileHelper::SaveStringToFile(CandidateText, *CandidatePath));
	AddInfo(FString::Printf(TEXT("CORPUS_DPI_GOLDEN_CANDIDATE=%s"), *CandidatePath));
	if (!bHasBaseline || !BaselineCorpora || !*BaselineCorpora)
	{
		AddError(FString::Printf(
			TEXT("Source-controlled DPI Golden is missing or invalid: %s"),
			*BaselinePath));
	}
	return true;
}

#endif
