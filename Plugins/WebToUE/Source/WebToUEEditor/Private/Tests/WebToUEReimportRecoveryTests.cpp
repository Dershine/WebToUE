#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WebToUECoreTypes.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"
#include "WebToUERuntimeBenchmarkViewModel.h"
#include "WebToUEView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEReimportRecoveryTest,
	"WebToUE.Editor.ReimportRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEReimportRecoveryTest::RunTest(const FString& Parameters)
{
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation/ReimportRecovery"));
	const FString HtmlFilename = FPaths::Combine(
		TestDirectory, TEXT("ProductionHost.html"));
	const FString CssFilename = FPaths::Combine(
		TestDirectory, TEXT("ProductionHost.css"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*HtmlFilename, false, true);
		IFileManager::Get().Delete(*CssFilename, false, true);
	};

	const FString InitialHtml = TEXT(
		"<html><head><link rel='stylesheet' href='ProductionHost.css'></head>"
		"<body><button id='target' data-ue-bind-text='BenchmarkLabel' "
		"data-ue-on-click='Commit'>Initial</button></body></html>");
	const FString InitialCss = TEXT(
		"body { width: 640px; height: 360px; } "
		"button { width: 220px; height: 64px; background-color: #204060ff; }");
	TestTrue(TEXT("The initial reimport HTML source is written"),
		FFileHelper::SaveStringToFile(InitialHtml, *HtmlFilename));
	TestTrue(TEXT("The initial linked CSS source is written"),
		FFileHelper::SaveStringToFile(InitialCss, *CssFilename));

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	UWebToUEFactory* Factory = NewObject<UWebToUEFactory>(GetTransientPackage());
	TestTrue(TEXT("The initial source imports into a transient WTUE document"),
		UWebToUEFactory::ImportIntoDocument(*Document, HtmlFilename, false));
	TArray<FString> ReimportFilenames;
	TestTrue(TEXT("The imported document exposes its reimport source"),
		Factory->CanReimport(Document, ReimportFilenames));
	TestEqual(TEXT("The document has exactly one primary reimport source"),
		ReimportFilenames.Num(), 1);

	FString NormalizedHtml = FPaths::ConvertRelativePathToFull(HtmlFilename);
	FString NormalizedCss = FPaths::ConvertRelativePathToFull(CssFilename);
	FPaths::NormalizeFilename(NormalizedHtml);
	FPaths::NormalizeFilename(NormalizedCss);
	TestTrue(TEXT("The dependency graph retains the primary HTML source"),
		Document->DependencyFiles.Contains(NormalizedHtml));
	TestTrue(TEXT("The dependency graph retains the linked CSS source"),
		Document->DependencyFiles.Contains(NormalizedCss));

	UWebToUERuntimeBenchmarkViewModel* ViewModel =
		NewObject<UWebToUERuntimeBenchmarkViewModel>(GetTransientPackage());
	const FText InitialLabel = FText::FromString(TEXT("Initial binding"));
	const FText UpdatedLabel = FText::FromString(TEXT("Updated binding"));
	const FText RecoveredLabel = FText::FromString(TEXT("Recovered binding"));
	TestTrue(TEXT("The initial FieldNotify value is set"),
		ViewModel->SetBenchmarkLabel(InitialLabel));
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
	FWebToUENode* Target = View->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The initial View hydrates the reimport target"), Target);
	if (!Target || Target->Children.IsEmpty()) return false;
	TestTrue(TEXT("The initial binding reaches the hosted View"),
		View->GetDisplayTextForTesting(*Target->Children[0]).EqualTo(InitialLabel));
	const FWebToUEInstanceHandle InitialHandle =
		View->GetInstanceHandleForTesting(*Target);

	TestTrue(TEXT("FieldNotify updates before reimport"),
		ViewModel->SetBenchmarkLabel(UpdatedLabel));
	TestTrue(TEXT("The direct binding remains live before reimport"),
		View->GetDisplayTextForTesting(*Target->Children[0]).EqualTo(UpdatedLabel));

	const FString UpdatedCss = TEXT(
		"body { width: 640px; height: 360px; } "
		"button { width: 220px; height: 64px; background-color: #804020ff; }");
	TestTrue(TEXT("The linked dependency is changed to a valid revision"),
		FFileHelper::SaveStringToFile(UpdatedCss, *CssFilename));
	TestEqual(TEXT("A linked CSS change reimports successfully"),
		Factory->Reimport(Document), EReimportResult::Succeeded);
	Target = View->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The hosted View rehydrates after dependency reimport"), Target);
	TestNull(TEXT("The pre-reimport Instance Handle cannot resolve"),
		View->ResolveInstanceHandleForTesting(InitialHandle));
	if (!Target || Target->Children.IsEmpty()) return false;
	const FLinearColor LastGoodColor =
		FLinearColor::FromSRGBColor(FColor(0x80, 0x40, 0x20, 0xff));
	TestEqual(TEXT("The valid dependency revision reaches computed style"),
		View->GetComputedStyleForTesting(*Target).BackgroundColor, LastGoodColor);
	TestTrue(TEXT("Binding is restored after successful reimport"),
		View->GetDisplayTextForTesting(*Target->Children[0]).EqualTo(UpdatedLabel));
	const int32 LastGoodNodeCount = Document->GetCompiledNodes().Num();

	TestTrue(TEXT("The linked stylesheet is removed for the negative path"),
		IFileManager::Get().Delete(*CssFilename, false, true));
	AddExpectedError(TEXT("Could not read linked stylesheet"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("A missing dependency reports failed reimport"),
		Factory->Reimport(Document), EReimportResult::Failed);
	Target = View->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("Failed reimport preserves the last good hosted tree"), Target);
	TestEqual(TEXT("Failed reimport preserves the last good compiled nodes"),
		Document->GetCompiledNodes().Num(), LastGoodNodeCount);
	if (!Target || Target->Children.IsEmpty()) return false;
	TestEqual(TEXT("Failed reimport preserves the last good computed style"),
		View->GetComputedStyleForTesting(*Target).BackgroundColor, LastGoodColor);

	const FString RecoveredHtml = TEXT(
		"<html><head><link rel='stylesheet' href='ProductionHost.css'></head>"
		"<body><button id='target' data-ue-bind-text='BenchmarkLabel' "
		"data-ue-on-click='Commit'>Recovered</button>"
		"<button id='added'>Added after recovery</button></body></html>");
	TestTrue(TEXT("The dependency is restored after the negative path"),
		FFileHelper::SaveStringToFile(InitialCss, *CssFilename));
	TestTrue(TEXT("The primary source is advanced after the negative path"),
		FFileHelper::SaveStringToFile(RecoveredHtml, *HtmlFilename));
	TestEqual(TEXT("A corrected source recovers on the next reimport"),
		Factory->Reimport(Document), EReimportResult::Succeeded);
	TestNotNull(TEXT("Recovery hydrates the newly added element"),
		View->FindRuntimeNodeByIdForTesting(TEXT("added")));
	Target = View->FindRuntimeNodeByIdForTesting(TEXT("target"));
	if (!Target || Target->Children.IsEmpty()) return false;
	TestTrue(TEXT("FieldNotify broadcasts after recovery"),
		ViewModel->SetBenchmarkLabel(RecoveredLabel));
	TestTrue(TEXT("Recovered binding updates the rehydrated target"),
		View->GetDisplayTextForTesting(*Target->Children[0]).EqualTo(RecoveredLabel));
	return true;
}

#endif
