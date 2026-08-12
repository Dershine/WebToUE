#pragma once

#include "WebToUECoreTypes.h"

namespace WebToUE::Private
{
	WEBTOUECORE_API bool TryGetCssProperty(const FString& Name, EWebToUECssProperty& OutProperty);
	WEBTOUECORE_API bool TryParseCssValue(EWebToUECssProperty Property, const FString& Value,
		FWebToUEStyleValue& OutValue);
	WEBTOUECORE_API bool TryParseCssDeclaration(const FString& Name, const FString& Value,
		FWebToUEStyleDeclaration& OutDeclaration);
	WEBTOUECORE_API const TCHAR* LexToString(EWebToUECssProperty Property);
	WEBTOUECORE_API void ApplyProperties(
		const TMap<EWebToUECssProperty, FWebToUEStyleValue>& Properties,
		FWebToUEComputedStyle& Style);
}
