#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEBenchmarkUserWidget.h"
#include "WebToUEBenchmarkRunner.h"
#include "WebToUEPackagedBenchmarkPolicy.h"

#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUEView.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEBenchmarkCorpusContractTest,
	"WebToUE.Benchmark.CorpusContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEBenchmarkCorpusSlateOutputTest,
	"WebToUE.Benchmark.CorpusSlateOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPackagedExitPolicyTest,
	"WebToUE.Benchmark.PackagedExitPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEBenchmarkCorpusContractTest::RunTest(const FString& Parameters)
{
	const FName Corpora[] = {
		TEXT("MainMenu"), TEXT("HUD"), TEXT("ScrollableSettings")
	};
	for (const FName Corpus : Corpora)
	{
		const FString AssetPath = FString::Printf(
			TEXT("/Game/WebToUEExamples/%s.%s"),
			*Corpus.ToString(), *Corpus.ToString());
		UWebToUEDocument* Document = LoadObject<UWebToUEDocument>(nullptr, *AssetPath);
		TestNotNull(*FString::Printf(TEXT("%s has a cooked WTUE document counterpart"),
			*Corpus.ToString()), Document);
		if (Document)
		{
			TestTrue(*FString::Printf(TEXT("%s contains compiled runtime nodes"),
				*Corpus.ToString()), !Document->GetCompiledNodes().IsEmpty());
		}

		UWebToUEBenchmarkUserWidget* UmgWidget =
			NewObject<UWebToUEBenchmarkUserWidget>(GetTransientPackage());
		TestNotNull(*FString::Printf(TEXT("%s creates a UMG counterpart"),
			*Corpus.ToString()), UmgWidget);
		if (!UmgWidget) continue;
		TestTrue(*FString::Printf(TEXT("%s builds its UMG WidgetTree"),
			*Corpus.ToString()), UmgWidget->Configure(Corpus));
		TArray<UWidget*> AllWidgets;
		UmgWidget->WidgetTree->GetAllWidgets(AllWidgets);
		if (Corpus == TEXT("MainMenu") || Corpus == TEXT("ScrollableSettings"))
		{
			const float ExpectedWrapWidth =
				Corpus == TEXT("MainMenu") ? 536.0f : 616.0f;
			const FString WrappedText = Corpus == TEXT("MainMenu")
				? TEXT("Native UI, frontend workflow.")
				: TEXT("Move the mouse over the list and use the wheel to scroll.");
			const UTextBlock* WrappedBlock = nullptr;
			for (const UWidget* Candidate : AllWidgets)
			{
				const UTextBlock* TextBlock = Cast<UTextBlock>(Candidate);
				if (TextBlock && TextBlock->GetText().ToString() == WrappedText)
				{
					WrappedBlock = TextBlock;
					break;
				}
			}
			TestNotNull(*FString::Printf(TEXT("%s identifies its wrapping text contract"),
				*Corpus.ToString()), WrappedBlock);
			if (WrappedBlock)
			{
				TestTrue(*FString::Printf(TEXT("%s enables text wrapping"),
					*Corpus.ToString()), WrappedBlock->GetAutoWrapText());
				TestEqual(*FString::Printf(TEXT("%s freezes the CSS content width"),
					*Corpus.ToString()), WrappedBlock->GetWrapTextAt(), ExpectedWrapWidth);
			}
		}
		const float ExpectedButtonContentHeight =
			Corpus == TEXT("ScrollableSettings") ? 52.0f : 28.0f;
		int32 ButtonCount = 0;
		for (const UWidget* Candidate : AllWidgets)
		{
			const UButton* Button = Cast<UButton>(Candidate);
			if (!Button) continue;
			++ButtonCount;
			const USizeBox* ContentSize = Cast<USizeBox>(Button->GetContent());
			TestNotNull(*FString::Printf(TEXT("%s button owns a fixed CSS content box"),
				*Corpus.ToString()), ContentSize);
			if (ContentSize)
			{
				TestEqual(*FString::Printf(TEXT("%s button height includes Slate padding once"),
					*Corpus.ToString()), ContentSize->GetHeightOverride(),
					ExpectedButtonContentHeight);
			}
			const UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->GetContentSlot());
			TestTrue(*FString::Printf(TEXT("%s button content fills for centered text"),
				*Corpus.ToString()), ContentSlot &&
				ContentSlot->GetHorizontalAlignment() == HAlign_Fill);
		}
		if (Corpus == TEXT("MainMenu"))
		{
			TestEqual(TEXT("MainMenu UMG counterpart owns three actions"), ButtonCount, 3);
		}
		else if (Corpus == TEXT("ScrollableSettings"))
		{
			TestEqual(TEXT("ScrollableSettings UMG counterpart owns seven rows"),
				ButtonCount, 7);
			const UScrollBox* Scroll = nullptr;
			for (const UWidget* Candidate : AllWidgets)
			{
				if (const UScrollBox* Found = Cast<UScrollBox>(Candidate))
				{
					Scroll = Found;
					break;
				}
			}
			TestNotNull(TEXT("ScrollableSettings owns its scroll container"), Scroll);
			if (Scroll)
			{
				TestEqual(TEXT("UMG wheel quantum matches WebToUE"),
					Scroll->GetWheelScrollMultiplier(), 1.5f);
				TestFalse(TEXT("UMG trajectory uses immediate CSS-like wheel scrolling"),
					Scroll->IsAnimateWheelScrolling());
			}
		}
		if (Corpus == TEXT("HUD"))
		{
			TestTrue(TEXT("HUD UMG counterpart applies and observes the benchmark trajectory"),
				UmgWidget->ApplyTrajectory(1));
		}
		TestNotNull(*FString::Printf(TEXT("%s owns a UMG root widget"),
			*Corpus.ToString()), UmgWidget->WidgetTree
				? UmgWidget->WidgetTree->RootWidget.Get() : nullptr);
		const TSharedRef<SWidget> SlateWidget = UmgWidget->TakeWidget();
		SlateWidget->SlatePrepass(1.0f);
		TestTrue(*FString::Printf(TEXT("%s produces non-empty Slate content"),
			*Corpus.ToString()), SlateWidget->GetDesiredSize().X > 0.0f ||
			SlateWidget->GetDesiredSize().Y > 0.0f);
	}
	return true;
}

bool FWebToUEBenchmarkCorpusSlateOutputTest::RunTest(const FString& Parameters)
{
	const FName Corpora[] = {
		TEXT("MainMenu"), TEXT("HUD"), TEXT("ScrollableSettings")
	};
	for (const FName Corpus : Corpora)
	{
		const FString AssetPath = FString::Printf(
			TEXT("/Game/WebToUEExamples/%s.%s"),
			*Corpus.ToString(), *Corpus.ToString());
		UWebToUEDocument* Document = LoadObject<UWebToUEDocument>(nullptr, *AssetPath);
		if (!TestNotNull(*FString::Printf(TEXT("%s document loads for Slate output"),
			*Corpus.ToString()), Document))
		{
			continue;
		}

		UWebToUEView* View = NewObject<UWebToUEView>(GetTransientPackage());
		View->SetDocument(Document);
		FWebToUEPerformanceCapture Capture;
		const TSharedRef<SWidget> SlateWidget = View->TakeWidget();
		SlateWidget->SlatePrepass(1.0f);

		FHittestGrid HittestGrid;
		FSlateWindowElementList DrawElements(nullptr);
		const FGeometry Geometry = FGeometry::MakeRoot(
			FVector2D(1280.0, 720.0), FSlateLayoutTransform());
		const FPaintArgs PaintArgs(
			nullptr, HittestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
		SlateWidget->Paint(PaintArgs, Geometry,
			FSlateRect(0.0f, 0.0f, 1280.0f, 720.0f), DrawElements, 0,
			FWidgetStyle(), true);
		const FWebToUEPerformanceSnapshot Snapshot = Capture.GetSnapshot();
		TestEqual(*FString::Printf(TEXT("%s hydrates exactly one K=1 document"),
			*Corpus.ToString()),
			Snapshot.GetCounter(EWebToUEPerformanceCounter::HydratedNodes),
			static_cast<uint64>(Document->GetCompiledNodes().Num()));

		const FSlateDrawElementMap& Elements = DrawElements.GetUncachedDrawElements();
		const auto& RoundedBoxElements = Elements.Get<
			static_cast<uint8>(EElementType::ET_RoundedBox)>();
		const int32 RoundedBoxes = RoundedBoxElements.Num();
		const int32 TextRuns = Elements.Get<
			static_cast<uint8>(EElementType::ET_ShapedText)>().Num() +
			Elements.Get<static_cast<uint8>(EElementType::ET_Text)>().Num();
		TestTrue(*FString::Printf(TEXT("%s produces Slate paint elements"),
			*Corpus.ToString()), RoundedBoxes + TextRuns > 0);
		if (Corpus == TEXT("MainMenu"))
		{
			FWebToUENode* StartButton =
				View->FindRuntimeNodeByIdForTesting(TEXT("start-button"));
			TestNotNull(TEXT("MainMenu hydrates its primary button"), StartButton);
			const FLinearColor ExpectedButton =
				FLinearColor::FromSRGBColor(FColor(0x20, 0x3b, 0x61, 0xff));
			if (StartButton)
			{
				TestEqual(TEXT("MainMenu primary button retains its compiled sRGB fill"),
					View->GetComputedStyleForTesting(*StartButton).BackgroundColor,
					ExpectedButton);
			}
			TestTrue(TEXT("MainMenu submits its dark button fill to Slate"),
				RoundedBoxElements.ContainsByPredicate(
					[&ExpectedButton](const auto& Element)
					{
						return Element.GetTint().Equals(ExpectedButton, KINDA_SMALL_NUMBER);
					}));

			if (StartButton)
			{
				const FWebToUERuntimeLayoutResult& ButtonLayout =
					View->GetLayoutResultForTesting(*StartButton);
				const FVector2D LocalCenter(
					ButtonLayout.Position + ButtonLayout.Size * 0.5f);
				const FVector2D ScreenCenter = Geometry.LocalToAbsolute(LocalCenter);
				const FPointerEvent MoveEvent(0, ScreenCenter, FVector2D::ZeroVector,
					TSet<FKey>(), FKey(), 0.0f, FModifierKeysState());
				const FChildren* HostChildren = SlateWidget->GetChildren();
				TestEqual(TEXT("Production SafeZone host owns one WebToUE leaf"),
					HostChildren->Num(), 1);
				const TSharedPtr<SWidget> InputTarget =
					WebToUE::Benchmark::ResolvePointerTarget(SlateWidget);
				TestTrue(TEXT("Benchmark resolves the production WebToUE leaf"),
					HostChildren->Num() == 1 && InputTarget.IsValid() &&
					InputTarget.Get() == &HostChildren->GetChildAt(0).Get());
				const TSharedRef<SWindow> InputWindow = SNew(SWindow)
					.ClientSize(FVector2D(1280.0, 720.0))
					[
						SlateWidget
					];
				TArray<FWidgetAndPointer> InputWidgets;
				InputWidgets.Emplace(FArrangedWidget(InputWindow, Geometry));
				InputWidgets.Emplace(FArrangedWidget(SlateWidget, Geometry));
				if (InputTarget.IsValid())
				{
					InputWidgets.Emplace(FArrangedWidget(InputTarget.ToSharedRef(),
						Geometry));
				}
				const FWidgetPath InputPath(InputWidgets);
				Capture.Reset();
				TestTrue(TEXT("Benchmark routes pointer move through the production leaf path"),
					WebToUE::Benchmark::RoutePointerMove(
						FSlateApplication::Get(), InputPath, MoveEvent));
				const FWebToUEPerformanceSnapshot Interaction = Capture.GetSnapshot();
				TestTrue(TEXT("MainMenu trajectory queries spatial hit candidates"),
					Interaction.GetCounter(
						EWebToUEPerformanceCounter::HitTestCandidates) > 0);
				TestTrue(TEXT("MainMenu trajectory visits a hit command"),
					Interaction.GetCounter(
						EWebToUEPerformanceCounter::HitTestCommandsVisited) > 0);
				TestTrue(TEXT("MainMenu trajectory changes a pseudo state"),
					Interaction.GetCounter(
						EWebToUEPerformanceCounter::PseudoStateNodesChanged) > 0);
				TestTrue(TEXT("MainMenu trajectory patches local display commands"),
					Interaction.GetCounter(
						EWebToUEPerformanceCounter::DisplayCommandsPatched) > 0);
				TestTrue(TEXT("MainMenu trajectory records a dirty rectangle"),
					Interaction.GetCounter(
						EWebToUEPerformanceCounter::DirtyRectsAdded) > 0);
			}
		}
	}
	return true;
}

bool FWebToUEPackagedExitPolicyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Packaged result schema records M2.9 evidence"),
		FWebToUEPackagedBenchmarkPolicy::ResultSchemaVersion, 6);
	TestEqual(TEXT("Frozen production corpus has an explicit resource ceiling"),
		FWebToUEPackagedBenchmarkPolicy::FrozenCorpusMaximumCompiledResources, 0);

	const FName Corpora[] = {
		TEXT("MainMenu"), TEXT("HUD"), TEXT("ScrollableSettings")
	};
	for (const FName Corpus : Corpora)
	{
		const FString AssetPath = FString::Printf(
			TEXT("/Game/WebToUEExamples/%s.%s"),
			*Corpus.ToString(), *Corpus.ToString());
		UWebToUEDocument* Document = LoadObject<UWebToUEDocument>(nullptr, *AssetPath);
		if (!TestNotNull(*FString::Printf(TEXT("%s loads for the exit policy"),
			*Corpus.ToString()), Document))
		{
			continue;
		}

		UWebToUEView* FirstView = NewObject<UWebToUEView>(GetTransientPackage());
		FirstView->SetDocument(Document);
		FWebToUEPerformanceSnapshot FirstWorkload;
		const TSharedRef<SWidget> FirstWidget = [&]()
		{
			FWebToUEPerformanceCapture Capture;
			const TSharedRef<SWidget> Widget = FirstView->TakeWidget();
			Widget->SlatePrepass(1.0f);
			FirstWorkload = Capture.GetSnapshot();
			return Widget;
		}();
		UWebToUEView* SecondView = NewObject<UWebToUEView>(GetTransientPackage());
		SecondView->SetDocument(Document);
		FWebToUEPerformanceSnapshot SecondWorkload;
		const TSharedRef<SWidget> SecondWidget = [&]()
		{
			FWebToUEPerformanceCapture Capture;
			const TSharedRef<SWidget> Widget = SecondView->TakeWidget();
			Widget->SlatePrepass(1.0f);
			SecondWorkload = Capture.GetSnapshot();
			return Widget;
		}();
		FWebToUERuntimeMemoryCensus FirstCensus;
		FWebToUERuntimeMemoryCensus SecondCensus;
		TestTrue(*FString::Printf(TEXT("%s exposes first-view known-owned capacity"),
			*Corpus.ToString()), FirstView->GetRuntimeMemoryCensusForTesting(FirstCensus));
		TestTrue(*FString::Printf(TEXT("%s exposes second-view known-owned capacity"),
			*Corpus.ToString()), SecondView->GetRuntimeMemoryCensusForTesting(SecondCensus));

		FWebToUEPackagedBenchmarkEvidence Evidence;
		Evidence.CompiledNodeCount = Document->GetCompiledNodes().Num();
		Evidence.CompiledResourceCount = Document->GetResourceManifest().Num();
		Evidence.MeasurementTrajectorySteps = 1;
		Evidence.SetupHydratedNodes = FirstWorkload.GetCounter(
			EWebToUEPerformanceCounter::HydratedNodes);
		Evidence.SecondViewHydratedNodes = SecondWorkload.GetCounter(
			EWebToUEPerformanceCounter::HydratedNodes);
		Evidence.SetupResourceLoadAttempts = FirstWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceLoadAttempts);
		Evidence.SetupResourceFailures = FirstWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceFailures);
		Evidence.SecondViewResourceLoadAttempts = SecondWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceLoadAttempts);
		Evidence.SecondViewResourceFailures = SecondWorkload.GetCounter(
			EWebToUEPerformanceCounter::ResourceFailures);
		Evidence.bKnownOwnedCensusAvailable = true;
		Evidence.FirstViewKnownOwnedBytes = FirstCensus.GetTotalKnownOwnedBytes();
		Evidence.SecondViewKnownOwnedBytes = SecondCensus.GetTotalKnownOwnedBytes();
		Evidence.FirstViewSharedTemplateBytes =
			FirstCensus.SharedStyleTemplateKnownOwnedBytes;
		Evidence.SecondViewSharedTemplateBytes =
			SecondCensus.SharedStyleTemplateKnownOwnedBytes;
		TArray<FString> Failures;
		TestTrue(*FString::Printf(TEXT("%s satisfies the packaged exit evidence policy"),
			*Corpus.ToString()),
			FWebToUEPackagedBenchmarkPolicy::ValidateWebToUEEvidence(
				Evidence, Failures));
		if (!Failures.IsEmpty())
		{
			AddError(FString::Join(Failures, TEXT("; ")));
		}
	}

	FWebToUEPackagedBenchmarkEvidence Negative;
	Negative.CompiledNodeCount = 15;
	Negative.CompiledResourceCount = 0;
	Negative.MeasurementTrajectorySteps = 1;
	Negative.SetupHydratedNodes = 15;
	Negative.SecondViewHydratedNodes = 15;
	Negative.bKnownOwnedCensusAvailable = true;
	Negative.FirstViewKnownOwnedBytes = 1000;
	Negative.SecondViewKnownOwnedBytes = 1000;
	Negative.FirstViewSharedTemplateBytes = 100;
	Negative.SecondViewSharedTemplateBytes = 100;
	TArray<FString> Failures;
	TestTrue(TEXT("The exact boundary evidence is accepted"),
		FWebToUEPackagedBenchmarkPolicy::ValidateWebToUEEvidence(Negative, Failures));
	Negative.CompiledResourceCount = 1;
	TestFalse(TEXT("An undeclared frozen-corpus resource fails the gate"),
		FWebToUEPackagedBenchmarkPolicy::ValidateWebToUEEvidence(Negative, Failures));
	Negative.CompiledResourceCount = 0;
	Negative.MeasurementStyleNodeVisits = 5;
	TestFalse(TEXT("O(N)-shaped Style work fails the K=1 constant bound"),
		FWebToUEPackagedBenchmarkPolicy::ValidateWebToUEEvidence(Negative, Failures));
	Negative.MeasurementStyleNodeVisits = 0;
	Negative.SecondViewRssDeltaMiB = 32.01;
	TestFalse(TEXT("An excessive second-view process delta fails the gate"),
		FWebToUEPackagedBenchmarkPolicy::ValidateWebToUEEvidence(Negative, Failures));
	Negative.SecondViewRssDeltaMiB = 0.0;
	Negative.SecondViewKnownOwnedBytes = 1101;
	TestFalse(TEXT("An excessive second-view known-owned census fails the gate"),
		FWebToUEPackagedBenchmarkPolicy::ValidateWebToUEEvidence(Negative, Failures));
	return true;
}

#endif
