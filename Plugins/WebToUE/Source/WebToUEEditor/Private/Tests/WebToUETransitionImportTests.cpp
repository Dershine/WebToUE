#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETransitionImportTest,
	"WebToUE.Editor.TransitionImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUETransitionImportTest::RunTest(const FString& Parameters)
{
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(
		TestDirectory, TEXT("TransitionImport.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	const FString Html = TEXT(R"(
		<body><style>
		#target { opacity: 1; color: white; transform: none;
			transition: transform 400ms ease-in-out 50ms, opacity 200ms ease-out, color 0.3s linear; }
		#target:hover { opacity: 0.25; color: red; transform: translate(20px, 0px); }
		</style><button id='target'>Transition</button></body>)");
	TestTrue(TEXT("The Transition source is written"),
		FFileHelper::SaveStringToFile(Html, *TestFilename));
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	TestTrue(TEXT("The Transition document imports"),
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, true));
	const FWebToUECompiledAnimationIR& IR = Document->GetCompiledAnimationIR();
	const FWebToUEArtifactLayerVersion ExpectedVersion{ 1, 1 };
	TestEqual(TEXT("Transition lowering upgrades Animation IR to 1.1"),
		IR.Version, ExpectedVersion);
	TestEqual(TEXT("Three typed targets lower into Animation IR"),
		IR.Transitions.Num(), 3);
	if (IR.Transitions.Num() == 3)
	{
		TestEqual(TEXT("Lowering sorts Opacity first by stable target kind"),
			IR.Transitions[0].Target.Kind,
			EWebToUECompiledAnimationTargetKind::Opacity);
		TestEqual(TEXT("Lowering sorts Color second by stable target kind"),
			IR.Transitions[1].Target.Kind,
			EWebToUECompiledAnimationTargetKind::Color);
		TestEqual(TEXT("Lowering sorts Transform third by stable target kind"),
			IR.Transitions[2].Target.Kind,
			EWebToUECompiledAnimationTargetKind::VisualTransform);
		TestEqual(TEXT("Opacity duration is typed in seconds"),
			IR.Transitions[0].DurationSeconds, 0.2);
		TestEqual(TEXT("Transform delay is sealed in seconds"),
			IR.Transitions[2].DelaySeconds, 0.05);
		TestEqual(TEXT("Transform easing is sealed as a typed enum"),
			IR.Transitions[2].Easing, EWebToUETransitionEasing::EaseInOut);
		TestEqual(TEXT("Transition uses the controlled reverse contract"),
			IR.Transitions[0].ReverseMode,
			EWebToUETransitionReverseMode::RetargetFromCurrent);
		TestEqual(TEXT("Transition releases to latest underlying after completion"),
			IR.Transitions[0].FillMode,
			EWebToUETransitionFillMode::UnderlyingAfterCompletion);
	}
	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	TestTrue(TEXT("Lowered Transition IR passes the sealed asset boundary"),
		Document->ValidateResourceContract(Diagnostics));
	TestTrue(TEXT("The temporary Transition source is removed"),
		IFileManager::Get().Delete(*TestFilename, false, true));
	return true;
}

#endif
