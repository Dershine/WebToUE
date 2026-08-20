#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WebToUEPropertyOwnership.h"

namespace
{
	FWebToUEPropertyOwnershipClaim Claim(EWebToUEPropertyWriter Writer,
		const TCHAR* Source = TEXT("test"))
	{
		return { Writer, Source };
	}

	const FWebToUEPropertyOwnershipDiagnostic* FindDiagnostic(
		const FWebToUEPropertyOwnershipDecision& Decision, const TCHAR* Code)
	{
		return Decision.Diagnostics.FindByPredicate([Code](
			const FWebToUEPropertyOwnershipDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == FName(Code);
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPropertyOwnershipLayersTest,
	"WebToUE.Core.PropertyOwnershipLayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEPropertyOwnershipLayersTest::RunTest(const FString& Parameters)
{
	const TArray<FWebToUEPropertyOwnershipClaim> Claims = {
		Claim(EWebToUEPropertyWriter::Css),
		Claim(EWebToUEPropertyWriter::CssPseudo),
		Claim(EWebToUEPropertyWriter::Binding),
		Claim(EWebToUEPropertyWriter::Animation)
	};
	const FWebToUEPropertyOwnershipDecision Decision =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::Css(EWebToUECssProperty::Opacity), Claims);
	TestTrue(TEXT("CSS, Pseudo, one durable owner and Animation may coexist"),
		Decision.bAccepted);
	TestTrue(TEXT("CSS and Pseudo form one baseline cascade"),
		Decision.bHasCssCascadeBaseline);
	TestTrue(TEXT("Binding is the single durable owner"),
		Decision.bHasDurableOwner &&
		Decision.DurableOwner == EWebToUEPropertyWriter::Binding);
	TestTrue(TEXT("Animation is an active-only transient overlay"),
		Decision.bHasAnimationOverlay);
	TestEqual(TEXT("Precedence is explicit and does not use call order"),
		Decision.DescribePrecedence(),
		FString(TEXT("Animation(active) > Binding > CSS cascade(CSS+Pseudo)")));

	const TArray<FWebToUEPropertyOwnershipClaim> TextClaims = {
		Claim(EWebToUEPropertyWriter::Source),
		Claim(EWebToUEPropertyWriter::Behavior)
	};
	const FWebToUEPropertyOwnershipDecision TextDecision =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::NodeText(), TextClaims);
	TestTrue(TEXT("Behavior may durably override source text"), TextDecision.bAccepted);
	TestEqual(TEXT("Releasing Behavior reveals source text"),
		TextDecision.DescribePrecedence(), FString(TEXT("Behavior > Source")));

	const TArray<FWebToUEPropertyOwnershipClaim> VisibilityClaims = {
		Claim(EWebToUEPropertyWriter::Source),
		Claim(EWebToUEPropertyWriter::CssPseudo),
		Claim(EWebToUEPropertyWriter::Binding)
	};
	const FWebToUEPropertyOwnershipDecision VisibilityDecision =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::Css(EWebToUECssProperty::Visibility),
			VisibilityClaims);
	TestTrue(TEXT("CSS visibility and a runtime binding gate coexist"),
		VisibilityDecision.bAccepted);
	TestEqual(TEXT("Visibility uses restrictive composition rather than override order"),
		VisibilityDecision.Composition,
		EWebToUEPropertyComposition::RestrictiveGate);
	TestEqual(TEXT("Every visibility gate must allow the node"),
		VisibilityDecision.DescribePrecedence(),
		FString(TEXT("Binding & CSS cascade(CSS+Pseudo) & Source (restrictive)")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPropertyOwnershipConflictTest,
	"WebToUE.Core.PropertyOwnershipConflicts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEPropertyOwnershipConflictTest::RunTest(const FString& Parameters)
{
	const TArray<FWebToUEPropertyOwnershipClaim> ConflictingClaims = {
		Claim(EWebToUEPropertyWriter::Behavior, TEXT("behavior.ts:4")),
		Claim(EWebToUEPropertyWriter::Binding, TEXT("menu.html:7"))
	};
	const FWebToUEPropertyOwnershipDecision Conflict =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::Visibility(), ConflictingClaims);
	TestFalse(TEXT("Binding and Behavior cannot silently compete"), Conflict.bAccepted);
	const FWebToUEPropertyOwnershipDiagnostic* DurableConflict =
		FindDiagnostic(Conflict, TEXT("WTUE-OWN-003"));
	TestNotNull(TEXT("The durable conflict has a stable diagnostic code"), DurableConflict);
	if (DurableConflict)
	{
		TestTrue(TEXT("The diagnostic names the canonical target"),
			DurableConflict->Message.Contains(TEXT("node.visibility")));
		TestTrue(TEXT("The diagnostic retains both source locations"),
			DurableConflict->Message.Contains(TEXT("menu.html:7")) &&
			DurableConflict->Message.Contains(TEXT("behavior.ts:4")));
		TestTrue(TEXT("The diagnostic rejects write-order arbitration"),
			DurableConflict->Message.Contains(TEXT("write order")));
	}
	const TArray<FWebToUEPropertyOwnershipClaim> ReverseClaims = {
		Claim(EWebToUEPropertyWriter::Binding, TEXT("menu.html:7")),
		Claim(EWebToUEPropertyWriter::Behavior, TEXT("behavior.ts:4"))
	};
	const FWebToUEPropertyOwnershipDecision ReverseConflict =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::Visibility(), ReverseClaims);
	TestEqual(TEXT("Conflict diagnostics are independent of claim order"),
		Conflict.Diagnostics[0].Message, ReverseConflict.Diagnostics[0].Message);

	const TArray<FWebToUEPropertyOwnershipClaim> LayoutAnimation = {
		Claim(EWebToUEPropertyWriter::Css), Claim(EWebToUEPropertyWriter::Animation)
	};
	const FWebToUEPropertyOwnershipDecision InvalidAnimation =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::Css(EWebToUECssProperty::Width), LayoutAnimation);
	TestFalse(TEXT("Layout properties are not accepted as visual animation overlays"),
		InvalidAnimation.bAccepted);
	TestNotNull(TEXT("Unsupported writers have a stable diagnostic code"),
		FindDiagnostic(InvalidAnimation, TEXT("WTUE-OWN-002")));

	const TArray<FWebToUEPropertyOwnershipClaim> ShorthandClaim = {
		Claim(EWebToUEPropertyWriter::Css)
	};
	const FWebToUEPropertyOwnershipDecision InvalidShorthand =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::Css(EWebToUECssProperty::Margin), ShorthandClaim);
	TestFalse(TEXT("A shorthand cannot bypass canonical longhand ownership slots"),
		InvalidShorthand.bAccepted);
	TestNotNull(TEXT("The shorthand target fails with the stable invalid-target code"),
		FindDiagnostic(InvalidShorthand, TEXT("WTUE-OWN-001")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPropertyOwnershipMaterialTest,
	"WebToUE.Core.PropertyOwnershipMaterialParameters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEPropertyOwnershipMaterialTest::RunTest(const FString& Parameters)
{
	const TArray<FWebToUEPropertyOwnershipClaim> MaterialClaims = {
		Claim(EWebToUEPropertyWriter::Source),
		Claim(EWebToUEPropertyWriter::Behavior),
		Claim(EWebToUEPropertyWriter::Animation)
	};
	const FWebToUEPropertyOwnershipDecision Scalar =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::Material(
				FName(TEXT("Progress")), EWebToUEMaterialParameterType::Scalar),
			MaterialClaims);
	TestTrue(TEXT("A typed scalar parameter supports source, durable and transient layers"),
		Scalar.bAccepted);
	TestEqual(TEXT("Material parameter precedence is explicit"),
		Scalar.DescribePrecedence(),
		FString(TEXT("Animation(active) > Behavior > Source")));

	const FWebToUEPropertyOwnershipDecision Untyped =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::Material(
				FName(TEXT("Progress")), EWebToUEMaterialParameterType::None),
			MaterialClaims);
	TestFalse(TEXT("An untyped material parameter address is rejected"), Untyped.bAccepted);
	TestNotNull(TEXT("The invalid parameter has a stable diagnostic code"),
		FindDiagnostic(Untyped, TEXT("WTUE-OWN-001")));

	const TArray<FWebToUEPropertyOwnershipClaim> CssMaterialClaim = {
		Claim(EWebToUEPropertyWriter::Css)
	};
	const FWebToUEPropertyOwnershipDecision CssCannotAliasMaterial =
		FWebToUEPropertyOwnershipPolicy::Resolve(
			FWebToUEPropertyAddress::Material(
				FName(TEXT("Tint")), EWebToUEMaterialParameterType::Vector),
			CssMaterialClaim);
	TestFalse(TEXT("CSS spelling cannot implicitly alias a material parameter"),
		CssCannotAliasMaterial.bAccepted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEPropertyOwnershipDeterminismTest,
	"WebToUE.Core.PropertyOwnershipDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEPropertyOwnershipDeterminismTest::RunTest(const FString& Parameters)
{
	const TArray<FWebToUEPropertyOwnershipClaim> Forward = {
		Claim(EWebToUEPropertyWriter::Css),
		Claim(EWebToUEPropertyWriter::Binding),
		Claim(EWebToUEPropertyWriter::Animation),
		Claim(EWebToUEPropertyWriter::CssPseudo)
	};
	const TArray<FWebToUEPropertyOwnershipClaim> Reverse = {
		Claim(EWebToUEPropertyWriter::CssPseudo),
		Claim(EWebToUEPropertyWriter::Animation),
		Claim(EWebToUEPropertyWriter::Binding),
		Claim(EWebToUEPropertyWriter::Css)
	};
	const FWebToUEPropertyAddress Address =
		FWebToUEPropertyAddress::Css(EWebToUECssProperty::BackgroundColor);
	const FWebToUEPropertyOwnershipDecision A =
		FWebToUEPropertyOwnershipPolicy::Resolve(Address, Forward);
	const FWebToUEPropertyOwnershipDecision B =
		FWebToUEPropertyOwnershipPolicy::Resolve(Address, Reverse);
	TestEqual(TEXT("Claim ordering does not change acceptance"), A.bAccepted, B.bAccepted);
	TestEqual(TEXT("Claim ordering does not change precedence"),
		A.DescribePrecedence(), B.DescribePrecedence());
	TestEqual(TEXT("Claim ordering does not change diagnostics"),
		A.Diagnostics.Num(), B.Diagnostics.Num());
	return true;
}

#endif
