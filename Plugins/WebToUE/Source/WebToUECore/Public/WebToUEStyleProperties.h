#pragma once

#include "WebToUECoreTypes.h"

enum class EWebToUEStyleImpact : uint8
{
	None = 0,
	Style = 1 << 0,
	Measure = 1 << 1,
	Layout = 1 << 2,
	Paint = 1 << 3,
	HitTest = 1 << 4,
	Resource = 1 << 5
};
ENUM_CLASS_FLAGS(EWebToUEStyleImpact)

struct FWebToUECssPropertyMetadata
{
	EWebToUECssProperty Property = EWebToUECssProperty::Invalid;
	const TCHAR* Name = TEXT("invalid");
	bool bInherited = false;
	EWebToUEStyleImpact Impacts = EWebToUEStyleImpact::None;
};

namespace WebToUE::Private
{
	WEBTOUECORE_API const FWebToUECssPropertyMetadata& GetCssPropertyMetadata(
		EWebToUECssProperty Property);
	WEBTOUECORE_API TConstArrayView<FWebToUECssPropertyMetadata> GetAllCssPropertyMetadata();
	WEBTOUECORE_API bool TryGetCssProperty(const FString& Name, EWebToUECssProperty& OutProperty);
	WEBTOUECORE_API bool TryParseCssValue(EWebToUECssProperty Property, const FString& Value,
		FWebToUEStyleValue& OutValue);
	WEBTOUECORE_API bool TryParseCssDeclaration(const FString& Name, const FString& Value,
		FWebToUEStyleDeclaration& OutDeclaration);
	WEBTOUECORE_API const TCHAR* LexToString(EWebToUECssProperty Property);
	WEBTOUECORE_API void ApplyInheritedProperties(
		const FWebToUEComputedStyle& ParentStyle, FWebToUEComputedStyle& Style);
	WEBTOUECORE_API void ApplyCascadedProperty(
		EWebToUECssProperty PropertySlot, EWebToUECssProperty SourceProperty,
		const FWebToUEStyleValue& Value,
		FWebToUEComputedStyle& Style);
	WEBTOUECORE_API bool AreComputedStylePropertyValuesEqual(
		EWebToUECssProperty Property,
		const FWebToUEComputedStyle& A,
		const FWebToUEComputedStyle& B);
	WEBTOUECORE_API bool IsCanonicalComputedStyleProperty(EWebToUECssProperty Property);
}
