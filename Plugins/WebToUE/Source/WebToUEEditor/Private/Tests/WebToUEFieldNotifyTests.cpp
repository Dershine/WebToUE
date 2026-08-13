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
		"<body><span id='first' data-ue-bind-text='BenchmarkLabel' "
		"data-ue-bind-visible='BenchmarkVisible'>First</span>"
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
