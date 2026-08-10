#include "WebToUEDocument.h"

#include "WebToUEAssetVersion.h"

#include "Serialization/Archive.h"

#if WITH_EDITOR
#include "EditorFramework/AssetImportData.h"
#include "UObject/AssetRegistryTagsContext.h"
#endif

bool UWebToUEDocument::HasCompileErrors() const
{
	return Diagnostics.ContainsByPredicate([](const FWebToUEAssetDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EWebToUEAssetDiagnosticSeverity::Error;
	});
}

UWebToUEDocument::FOnDocumentChanged& UWebToUEDocument::OnDocumentChanged()
{
	static FOnDocumentChanged Delegate;
	return Delegate;
}

void UWebToUEDocument::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FWebToUEAssetVersion::GUID);
}

void UWebToUEDocument::NotifyDocumentChanged()
{
	OnDocumentChanged().Broadcast(this);
}

#if WITH_EDITOR
UWebToUEDocument::FOnDocumentNeedsRecompile& UWebToUEDocument::OnDocumentNeedsRecompile()
{
	static FOnDocumentNeedsRecompile Delegate;
	return Delegate;
}

void UWebToUEDocument::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject) && !AssetImportData)
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
}

void UWebToUEDocument::PostLoad()
{
	Super::PostLoad();
	bNeedsRecompile = FWebToUEAssetVersion::RequiresRecompile(GetLinkerCustomVersion(FWebToUEAssetVersion::GUID));
	if (bNeedsRecompile)
	{
		OnDocumentNeedsRecompile().Broadcast(this);
	}
}

void UWebToUEDocument::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);
	Context.AddTag(FAssetRegistryTag(TEXT("WebToUECompileStatus"), HasCompileErrors() ? TEXT("Error") : TEXT("Valid"), FAssetRegistryTag::TT_Alphabetical));
}
#endif
