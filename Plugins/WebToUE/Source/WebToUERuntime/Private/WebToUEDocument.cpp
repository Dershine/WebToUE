#include "WebToUEDocument.h"

#include "EditorFramework/AssetImportData.h"
#include "UObject/AssetRegistryTagsContext.h"

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

void UWebToUEDocument::NotifyDocumentChanged()
{
	OnDocumentChanged().Broadcast(this);
}

#if WITH_EDITOR
void UWebToUEDocument::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject) && !AssetImportData)
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
}

void UWebToUEDocument::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);
	Context.AddTag(FAssetRegistryTag(TEXT("WebToUECompileStatus"), HasCompileErrors() ? TEXT("Error") : TEXT("Valid"), FAssetRegistryTag::TT_Alphabetical));
}
#endif
