#include "WebToUEAssetVersion.h"

#include "Serialization/CustomVersion.h"

const FGuid FWebToUEAssetVersion::GUID(0xC31A0308, 0xD3214603, 0x994B5ED2, 0xF7D5CC8B);

static FCustomVersionRegistration GRegisterWebToUEAssetVersion(
	FWebToUEAssetVersion::GUID,
	FWebToUEAssetVersion::LatestVersion,
	TEXT("WebToUEAssetVersion"));
