#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETextWrappingTest, "WebToUE.Runtime.TextWrapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUETextWrappingTest::RunTest(const FString& Parameters)
{
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	const FString Text = TEXT("A localized paragraph needs enough words to wrap across several lines in a narrow menu panel.");
	const FVector2f Wide = View->MeasureTextForTesting(Text, 1000.0f, true);
	const FVector2f Narrow = View->MeasureTextForTesting(Text, 120.0f, true);
	const FVector2f NoWrap = View->MeasureTextForTesting(Text, 120.0f, false);

	TestTrue(TEXT("Narrow text wraps to a greater height"), Narrow.Y > Wide.Y);
	TestTrue(TEXT("Wrapping reduces the desired width"), Narrow.X < NoWrap.X);
	TestTrue(TEXT("Nowrap keeps the single-line height"), FMath::IsNearlyEqual(NoWrap.Y, Wide.Y, 1.0f));
	TestTrue(TEXT("Nowrap text can exceed the available width"), NoWrap.X > 120.0f);
	return true;
}

#endif
