#pragma once

#include "CoreMinimal.h"

struct WEBTOUERUNTIME_API FWebToUEAssetVersion
{
	enum Type : int32
	{
		BeforeCustomVersionWasAdded = 0,
		InitialCompiledDocument = 1,

		LatestVersion = InitialCompiledDocument
	};

	static const FGuid GUID;

	static bool RequiresRecompile(int32 SerializedVersion)
	{
		return SerializedVersion < LatestVersion;
	}
};
