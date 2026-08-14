#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "WebToUEView.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/Layout/SSafeZone.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEDpiSafeZoneTest,
	"WebToUE.Runtime.DpiSafeZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEDpiSafeZoneTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate is initialized for the production host test"),
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
	Slate.SetCustomSafeZone(FMargin(0.1f));
	Slate.OnDebugSafeZoneChanged.Broadcast(FMargin(0.1f), true);

	UWebToUEView* View = NewObject<UWebToUEView>(GetTransientPackage());
	View->AddToRoot();
	TSharedPtr<SWidget> HostedWidget;
	ON_SCOPE_EXIT
	{
		HostedWidget.Reset();
		View->ReleaseSlateResources(true);
		View->RemoveFromRoot();
	};
	HostedWidget = View->TakeWidget();
	TestTrue(TEXT("The UWidget host returns its safe-zone root"), HostedWidget.IsValid());
	TestTrue(TEXT("The safe-zone root retains exactly one WebToUE Slate leaf"),
		View->GetSafeZoneForTesting().IsValid() &&
		View->GetSlateViewForTesting().IsValid());

	const FVector2D LogicalSize(1280.0, 720.0);
	const auto VerifyDpi = [this, View, LogicalSize](float DPIScale)
	{
		const FVector2D PhysicalSize = LogicalSize * DPIScale;
		View->SetSafeZoneOverrideForTesting(PhysicalSize, DPIScale);
		const FMargin Margin = View->GetSafeZoneMarginForTesting(DPIScale);
		TestEqual(*FString::Printf(TEXT("%.0fx DPI keeps a 64-unit horizontal safe inset"),
			DPIScale), Margin.Left, 64.0f);
		TestEqual(*FString::Printf(TEXT("%.0fx DPI keeps a 36-unit vertical safe inset"),
			DPIScale), Margin.Top, 36.0f);

		const FGeometry RootGeometry = FGeometry::MakeRoot(
			LogicalSize, FSlateLayoutTransform(DPIScale));
		FArrangedChildren Arranged(EVisibility::Visible);
		View->GetSafeZoneForTesting()->OnArrangeChildren(RootGeometry, Arranged);
		TestEqual(*FString::Printf(TEXT("%.0fx DPI arranges one leaf"), DPIScale),
			Arranged.Num(), 1);
		if (Arranged.Num() == 1)
		{
			TestEqual(*FString::Printf(TEXT("%.0fx DPI preserves safe content width"),
				DPIScale), Arranged[0].Geometry.GetLocalSize().X, 1152.0f);
			TestEqual(*FString::Printf(TEXT("%.0fx DPI preserves safe content height"),
				DPIScale), Arranged[0].Geometry.GetLocalSize().Y, 648.0f);
		}
	};
	VerifyDpi(1.0f);
	VerifyDpi(2.0f);

	View->SetRespectSafeZone(false);
	TestEqual(TEXT("The explicit opt-out removes host safe padding"),
		View->GetSafeZoneMarginForTesting(2.0f), FMargin());
	View->SetRespectSafeZone(true);
	TestEqual(TEXT("Re-enabling the host restores the DPI-normalized safe padding"),
		View->GetSafeZoneMarginForTesting(2.0f),
		FMargin(64.0f, 36.0f, 64.0f, 36.0f));
	return true;
}

#endif
