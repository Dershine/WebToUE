#pragma once

#include "CoreMinimal.h"

struct FWebToUEInteropSchemaSnapshot;

/** Editor/build-time projection from a validated C++ schema snapshot to authoring declarations. */
class WEBTOUEEDITOR_API FWebToUESchemaTypeScriptEmitter final
{
public:
	/** Pure in-memory projection. The caller owns freshness policy and any file write. */
	static FString Emit(const FWebToUEInteropSchemaSnapshot& Snapshot);
};
