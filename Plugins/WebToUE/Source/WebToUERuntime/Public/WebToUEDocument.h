#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WebToUEDocument.generated.h"

class UAssetImportData;
class UStringTable;
class UTexture2D;

USTRUCT()
struct FWebToUECompiledAttribute
{
	GENERATED_BODY()
	UPROPERTY() FString Name;
	UPROPERTY() FString Value;
};

USTRUCT()
struct FWebToUECompiledNode
{
	GENERATED_BODY()
	UPROPERTY() uint8 Type = 0;
	UPROPERTY() FString Tag;
	UPROPERTY() FString Text;
	UPROPERTY() FText LocalizedText;
	UPROPERTY() bool bRichText = false;
	UPROPERTY() FString TextIdentity;
	UPROPERTY() FString LocalizationNamespace;
	UPROPERTY() FString LocalizationKey;
	UPROPERTY() bool bAutoLocalizationKey = false;
	UPROPERTY() FName StringTableId;
	UPROPERTY() FString StringTableKey;
	UPROPERTY() TArray<FWebToUECompiledAttribute> Attributes;
	UPROPERTY() int32 ParentIndex = INDEX_NONE;
};

USTRUCT()
struct FWebToUECompiledSelectorSegment
{
	GENERATED_BODY()
	UPROPERTY() FString Type;
	UPROPERTY() FString Id;
	UPROPERTY() TArray<FString> Classes;
	UPROPERTY() uint8 RequiredState = 0;
	UPROPERTY() uint8 RelationToPrevious = 0;
};

USTRUCT()
struct FWebToUECompiledDeclaration
{
	GENERATED_BODY()
	UPROPERTY() FString Name;
	UPROPERTY() FString Value;
};

USTRUCT()
struct FWebToUECompiledRule
{
	GENERATED_BODY()
	UPROPERTY() TArray<FWebToUECompiledSelectorSegment> Selector;
	UPROPERTY() TArray<FWebToUECompiledDeclaration> Declarations;
	UPROPERTY() int32 Specificity = 0;
	UPROPERTY() int32 SourceOrder = 0;
};

UENUM(BlueprintType)
enum class EWebToUEAssetDiagnosticSeverity : uint8
{
	Info,
	Warning,
	Error
};

USTRUCT(BlueprintType)
struct WEBTOUERUNTIME_API FWebToUEAssetDiagnostic
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WebToUE")
	EWebToUEAssetDiagnosticSeverity Severity = EWebToUEAssetDiagnosticSeverity::Info;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WebToUE")
	FString File;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WebToUE")
	int32 Line = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WebToUE")
	int32 Column = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WebToUE")
	FString Message;
};

UCLASS(BlueprintType)
class WEBTOUERUNTIME_API UWebToUEDocument : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString LocalizationNamespace;

	UPROPERTY()
	TArray<FWebToUECompiledNode> CompiledNodes;

	UPROPERTY()
	TArray<FWebToUECompiledRule> CompiledRules;

	UPROPERTY()
	int32 RootNodeIndex = INDEX_NONE;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category="WebToUE", meta=(MultiLine=true))
	FString CompiledHtml;

	UPROPERTY(VisibleAnywhere, Category="WebToUE", meta=(MultiLine=true))
	FString CompiledCss;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WebToUE")
	TArray<FWebToUEAssetDiagnostic> Diagnostics;

	UPROPERTY()
	TArray<TSoftObjectPtr<UTexture2D>> ReferencedTextures;

	UPROPERTY()
	TArray<TSoftObjectPtr<UStringTable>> ReferencedStringTables;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Instanced, Category="Import Settings")
	TObjectPtr<UAssetImportData> AssetImportData;

	UPROPERTY(VisibleAnywhere, Category="Import Settings")
	TArray<FString> DependencyFiles;
#endif

	bool HasCompileErrors() const;
	void NotifyDocumentChanged();
	virtual void Serialize(FArchive& Ar) override;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentChanged, UWebToUEDocument*);
	static FOnDocumentChanged& OnDocumentChanged();

#if WITH_EDITOR
	bool NeedsRecompile() const { return bNeedsRecompile; }
	void MarkRecompiled() { bNeedsRecompile = false; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentNeedsRecompile, UWebToUEDocument*);
	static FOnDocumentNeedsRecompile& OnDocumentNeedsRecompile();

	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

private:
	bool bNeedsRecompile = false;
#endif
};
