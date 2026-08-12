#pragma once

#include "WebToUECoreTypes.h"

namespace WebToUE::Private
{
	bool IsKnownCssProperty(const FString& Name);
	bool IsValidCssValue(const FString& Name, const FString& Value);
	void ApplyProperties(const TMap<FString, FString>& Properties, FWebToUEComputedStyle& Style);
}
