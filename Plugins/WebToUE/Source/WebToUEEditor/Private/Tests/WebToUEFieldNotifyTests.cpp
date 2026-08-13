#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WebToUECoreTypes.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"
#include "WebToUEPerformance.h"
#include "WebToUERuntimeBenchmarkViewModel.h"
#include "WebToUEView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEFieldNotifyInvalidationTest,
	"WebToUE.Editor.FieldNotifyInvalidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEFieldNotifyInvalidationTest::RunTest(const FString& Parameters)
{
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(
		TestDirectory, TEXT("FieldNotifyInvalidation.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*TestFilename, false, true);
	};
	const FString Html = TEXT(
		"<style>#first { opacity: 1; } #first:disabled { opacity: 0.25; }</style>"
		"<body><button id='first' data-ue-bind-text='BenchmarkLabel' "
		"data-ue-bind-visible='BenchmarkVisible' "
		"data-ue-bind-enabled='BenchmarkEnabled'>First</button>"
		"<span id='second' data-ue-bind-text='BenchmarkLabel'>Second</span></body>");
	TestTrue(TEXT("The FieldNotify source is written"),
		FFileHelper::SaveStringToFile(Html, *TestFilename));
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	TestTrue(TEXT("The FieldNotify source imports"),
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, true));

	UWebToUERuntimeBenchmarkViewModel* ViewModel =
		NewObject<UWebToUERuntimeBenchmarkViewModel>(GetTransientPackage());
	const FText InitialLabel = FText::FromString(TEXT("Initial"));
	const FText UpdatedLabel = FText::FromString(TEXT("Updated"));
	TestTrue(TEXT("The initial shared label is set"),
		ViewModel->SetBenchmarkLabel(InitialLabel));
	UWebToUEView* View = NewObject<UWebToUEView>(GetTransientPackage());
	View->SetDocument(Document);
	View->TakeWidget();
	View->SetDataContext(ViewModel);

	FWebToUENode* First = View->FindRuntimeNodeByIdForTesting(TEXT("first"));
	FWebToUENode* Second = View->FindRuntimeNodeByIdForTesting(TEXT("second"));
	TestNotNull(TEXT("The first binding target hydrates"), First);
	TestNotNull(TEXT("The second binding target hydrates"), Second);
	if (!First || !Second || First->Children.IsEmpty() || Second->Children.IsEmpty())
	{
		View->ReleaseSlateResources(true);
		return false;
	}
	FWebToUENode& FirstText = *First->Children[0];
	FWebToUENode& SecondText = *Second->Children[0];
	TestTrue(TEXT("Initial refresh updates the first dependent node"),
		View->GetDisplayTextForTesting(FirstText).EqualTo(InitialLabel));
	TestTrue(TEXT("Initial refresh updates the second dependent node"),
		View->GetDisplayTextForTesting(SecondText).EqualTo(InitialLabel));

	FWebToUEPerformanceSnapshot TextSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		TestTrue(TEXT("Changing the shared field broadcasts FieldNotify"),
			ViewModel->SetBenchmarkLabel(UpdatedLabel));
		TextSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("One FieldNotify performs one binding phase"),
		TextSnapshot.Get(EWebToUEPerformancePhase::Binding).CallCount, uint64(1));
	TestEqual(TEXT("One FieldNotify reads only its root field"),
		TextSnapshot.GetCounter(EWebToUEPerformanceCounter::BindingFieldsRead), uint64(1));
	TestEqual(TEXT("One field executes both directly indexed ops"),
		TextSnapshot.GetCounter(EWebToUEPerformanceCounter::BindingOpsExecuted), uint64(2));
	TestEqual(TEXT("Both direct text dependents update"),
		TextSnapshot.GetCounter(EWebToUEPerformanceCounter::BindingNodesUpdated), uint64(2));
	TestEqual(TEXT("A text-only field change does not scan style nodes"),
		TextSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(0));
	TestTrue(TEXT("The first direct dependent receives the new value"),
		View->GetDisplayTextForTesting(FirstText).EqualTo(UpdatedLabel));
	TestTrue(TEXT("The second direct dependent receives the new value"),
		View->GetDisplayTextForTesting(SecondText).EqualTo(UpdatedLabel));

	FWebToUEPerformanceSnapshot VisibleSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		TestTrue(TEXT("Changing the second field broadcasts FieldNotify"),
			ViewModel->SetBenchmarkVisible(false));
		VisibleSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("The second field still reads one root property"),
		VisibleSnapshot.GetCounter(EWebToUEPerformanceCounter::BindingFieldsRead), uint64(1));
	TestEqual(TEXT("The second field executes only its direct op"),
		VisibleSnapshot.GetCounter(EWebToUEPerformanceCounter::BindingOpsExecuted), uint64(1));
	TestEqual(TEXT("Multiple fields can independently update the first node"),
		VisibleSnapshot.GetCounter(EWebToUEPerformanceCounter::BindingNodesUpdated), uint64(1));
	TestFalse(TEXT("The directly bound runtime visibility changes"),
		View->GetRuntimeVisibleForTesting(*First));
	TestEqual(TEXT("Visibility updates only its direct Style target"),
		VisibleSnapshot.GetCounter(EWebToUEPerformanceCounter::StyleNodeVisits), uint64(1));
	TestEqual(TEXT("Visibility has no Measure/Layout Yoga impact"),
		VisibleSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));

	FWebToUEPerformanceSnapshot DisabledSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		TestTrue(TEXT("Disabling the target broadcasts FieldNotify"),
			ViewModel->SetBenchmarkEnabled(false));
		DisabledSnapshot = Capture.GetSnapshot();
	}
	TestFalse(TEXT("The runtime enabled state changes in the same refresh"),
		View->GetRuntimeEnabledForTesting(*First));
	TestTrue(TEXT("The disabled pseudo state is active in the same refresh"),
		EnumHasAnyFlags(View->GetRuntimePseudoStatesForTesting(*First),
			EWebToUEPseudoState::Disabled));
	TestEqual(TEXT(":disabled matches using the new pseudo state in the same refresh"),
		View->GetComputedStyleForTesting(*First).Opacity, 0.25f);
	TestEqual(TEXT("Enabled FieldNotify reads one root property"),
		DisabledSnapshot.GetCounter(EWebToUEPerformanceCounter::BindingFieldsRead), uint64(1));
	TestEqual(TEXT("Enabled FieldNotify executes one direct op"),
		DisabledSnapshot.GetCounter(EWebToUEPerformanceCounter::BindingOpsExecuted), uint64(1));
	TestEqual(TEXT("Enabled FieldNotify updates one direct node"),
		DisabledSnapshot.GetCounter(EWebToUEPerformanceCounter::BindingNodesUpdated), uint64(1));
	TestEqual(TEXT("Paint-only :disabled has no Yoga impact"),
		DisabledSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaNodesBuilt), uint64(0));

	TestTrue(TEXT("Re-enabling the target broadcasts FieldNotify"),
		ViewModel->SetBenchmarkEnabled(true));
	TestTrue(TEXT("The runtime enabled state is restored"),
		View->GetRuntimeEnabledForTesting(*First));
	TestFalse(TEXT("The disabled pseudo state clears in the same refresh"),
		EnumHasAnyFlags(View->GetRuntimePseudoStatesForTesting(*First),
			EWebToUEPseudoState::Disabled));
	TestEqual(TEXT("The base style rematches in the same refresh"),
		View->GetComputedStyleForTesting(*First).Opacity, 1.0f);

	FWebToUEPerformanceSnapshot UnrelatedSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		TestTrue(TEXT("The unrelated FieldNotify value changes"),
			ViewModel->SetUnrelatedLabel(FText::FromString(TEXT("Ignored"))));
		UnrelatedSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Unbound fields are not subscribed and trigger no binding phase"),
		UnrelatedSnapshot.Get(EWebToUEPerformancePhase::Binding).CallCount, uint64(0));
	View->ReleaseSlateResources(true);
	return true;
}

#endif
