#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WebToUECoreTypes.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"
#include "WebToUERuntimeBenchmarkViewModel.h"
#include "WebToUEView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPropertyOwnershipIntegrationTest,
	"WebToUE.Editor.PropertyOwnershipIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEPropertyOwnershipIntegrationTest::RunTest(const FString& Parameters)
{
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(
		TestDirectory, TEXT("PropertyOwnershipIntegration.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*TestFilename, false, true);
	};
	const FString Html = TEXT(
		"<style>#target { opacity: 1; visibility: visible; } "
		"#target:disabled { opacity: 0.25; visibility: hidden; }</style>"
		"<body><button id='target' data-ue-bind-text='BenchmarkLabel' "
		"data-ue-bind-visible='BenchmarkVisible' "
		"data-ue-bind-enabled='BenchmarkEnabled'>Source label</button></body>");
	TestTrue(TEXT("The ownership integration source is written"),
		FFileHelper::SaveStringToFile(Html, *TestFilename));

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	TestTrue(TEXT("The ownership integration source imports"),
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, true));
	UWebToUERuntimeBenchmarkViewModel* ViewModel =
		NewObject<UWebToUERuntimeBenchmarkViewModel>(GetTransientPackage());
	const FText BoundLabel = FText::FromString(TEXT("Bound label"));
	TestTrue(TEXT("The binding baseline is initialized"),
		ViewModel->SetBenchmarkLabel(BoundLabel));

	UWebToUEView* View = NewObject<UWebToUEView>(GetTransientPackage());
	View->SetDocument(Document);
	View->TakeWidget();
	View->SetDataContext(ViewModel);
	FWebToUENode* Target = View->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The ownership target hydrates"), Target);
	if (!Target || Target->Children.IsEmpty())
	{
		View->ReleaseSlateResources(true);
		return false;
	}

	FWebToUENode& Text = *Target->Children[0];
	TestTrue(TEXT("Binding durably overrides source text"),
		View->GetDisplayTextForTesting(Text).EqualTo(BoundLabel));
	TestTrue(TEXT("The initial Binding visibility gate allows the node"),
		View->GetRuntimeVisibleForTesting(*Target));
	TestTrue(TEXT("The initial CSS visibility gate allows the node"),
		View->GetComputedStyleForTesting(*Target).bVisible);

	TestTrue(TEXT("Disabling the node updates the Binding enabled owner"),
		ViewModel->SetBenchmarkEnabled(false));
	TestTrue(TEXT("The disabled Pseudo becomes active"),
		EnumHasAnyFlags(View->GetRuntimePseudoStatesForTesting(*Target),
			EWebToUEPseudoState::Disabled));
	TestFalse(TEXT("The Pseudo visibility gate can hide an otherwise Binding-visible node"),
		View->GetComputedStyleForTesting(*Target).bVisible);
	TestTrue(TEXT("Pseudo visibility does not overwrite the Binding visibility gate"),
		View->GetRuntimeVisibleForTesting(*Target));
	TestEqual(TEXT("The same Pseudo cascade controls another canonical style slot"),
		View->GetComputedStyleForTesting(*Target).Opacity, 0.25f);

	TestTrue(TEXT("Re-enabling releases the disabled Pseudo gate"),
		ViewModel->SetBenchmarkEnabled(true));
	TestTrue(TEXT("The base CSS visibility is restored"),
		View->GetComputedStyleForTesting(*Target).bVisible);
	TestEqual(TEXT("The base CSS opacity is restored"),
		View->GetComputedStyleForTesting(*Target).Opacity, 1.0f);

	TestTrue(TEXT("The independent Binding visibility owner can hide the node"),
		ViewModel->SetBenchmarkVisible(false));
	TestFalse(TEXT("The Binding visibility gate records its own value"),
		View->GetRuntimeVisibleForTesting(*Target));
	TestFalse(TEXT("Effective visibility remains restrictive"),
		View->GetComputedStyleForTesting(*Target).bVisible);
	View->ReleaseSlateResources(true);
	return true;
}

#endif
