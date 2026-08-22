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
		TypedStyleDeclarations = 4,
		CompiledBindingOps = 5,
		CompiledResourceManifest = 6,
		CssSrgbColors = 7,
		ResourceContractManifest = 8,
		ResourceConsumerContract = 9,
		RelativeTextureSources = 10,
		StaticMaterialBrushes = 11,
		VisualTransformAndClip = 12,
		AnimationIR = 13,
		TransitionLowering = 14,

		LatestVersion = TransitionLowering
	};

	static const FGuid GUID;

	static bool RequiresRecompile(int32 SerializedVersion)
	{
		return SerializedVersion < LatestVersion;
	}
};
