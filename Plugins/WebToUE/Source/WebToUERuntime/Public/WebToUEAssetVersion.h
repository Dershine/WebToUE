#pragma once

#include "CoreMinimal.h"

struct WEBTOUERUNTIME_API FWebToUEAssetVersion
{
	enum Type : int32
	{
		BeforeCustomVersionWasAdded = 0,
		InitialCompiledDocument = 1,
		LocalizedRichText = 2,
		OrderedDeclarations = 3,

		LatestVersion = OrderedDeclarations
	};

	static const FGuid GUID;

	static bool RequiresRecompile(int32 SerializedVersion)
	{
		return SerializedVersion < LatestVersion;
	}
};
