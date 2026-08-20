#pragma once

#include "CoreMinimal.h"
#include "WebToUECoreTypes.h"

enum class EWebToUEPropertyTargetKind : uint8
{
	Invalid,
	NodeText,
	Visibility,
	Enabled,
	CssProperty,
	VisualTransform,
	MaterialParameter
};

enum class EWebToUEMaterialParameterType : uint8
{
	None,
	Scalar,
	Vector,
	Texture
};

/**
 * A canonical property address. Material parameters are deliberately named and typed;
 * they never alias CSS properties by string spelling.
 */
struct WEBTOUECORE_API FWebToUEPropertyAddress
{
	EWebToUEPropertyTargetKind Kind = EWebToUEPropertyTargetKind::Invalid;
	EWebToUECssProperty CssProperty = EWebToUECssProperty::Invalid;
	FName MaterialParameter;
	EWebToUEMaterialParameterType MaterialParameterType =
		EWebToUEMaterialParameterType::None;

	static FWebToUEPropertyAddress NodeText();
	static FWebToUEPropertyAddress Visibility();
	static FWebToUEPropertyAddress Enabled();
	static FWebToUEPropertyAddress Css(EWebToUECssProperty Property);
	static FWebToUEPropertyAddress VisualTransform();
	static FWebToUEPropertyAddress Material(
		FName Parameter, EWebToUEMaterialParameterType Type);

	bool IsValid() const;
	FString ToString() const;
};

/** Writer domains, not individual writes or animation tracks. */
enum class EWebToUEPropertyWriter : uint8
{
	Source,
	Css,
	CssPseudo,
	Binding,
	Behavior,
	Animation,
	Count
};

struct WEBTOUECORE_API FWebToUEPropertyOwnershipClaim
{
	EWebToUEPropertyWriter Writer = EWebToUEPropertyWriter::Source;
	FString Source;
};

enum class EWebToUEPropertyOwnershipDiagnosticSeverity : uint8
{
	Warning,
	Error
};

struct WEBTOUECORE_API FWebToUEPropertyOwnershipDiagnostic
{
	FName Code;
	EWebToUEPropertyOwnershipDiagnosticSeverity Severity =
		EWebToUEPropertyOwnershipDiagnosticSeverity::Error;
	FString Message;
};

enum class EWebToUEPropertyComposition : uint8
{
	LayeredOverride,
	RestrictiveGate
};

/**
 * Resolution records the layers that may coexist. CSS and pseudo selectors remain one
 * baseline cascade; Binding and Behavior are mutually exclusive durable owners; Animation
 * is a transient overlay and must reveal the latest underlying value when released.
 */
struct WEBTOUECORE_API FWebToUEPropertyOwnershipDecision
{
	bool bAccepted = false;
	EWebToUEPropertyComposition Composition =
		EWebToUEPropertyComposition::LayeredOverride;
	bool bHasSourceBaseline = false;
	bool bHasCssCascadeBaseline = false;
	EWebToUEPropertyWriter DurableOwner = EWebToUEPropertyWriter::Source;
	bool bHasDurableOwner = false;
	bool bHasAnimationOverlay = false;
	TArray<FWebToUEPropertyOwnershipDiagnostic> Diagnostics;

	FString DescribePrecedence() const;
};

class WEBTOUECORE_API FWebToUEPropertyOwnershipPolicy
{
public:
	static FWebToUEPropertyOwnershipDecision Resolve(
		const FWebToUEPropertyAddress& Address,
		TConstArrayView<FWebToUEPropertyOwnershipClaim> Claims);

	static bool IsAnimationTarget(const FWebToUEPropertyAddress& Address);
	static const TCHAR* LexToString(EWebToUEPropertyWriter Writer);
};
