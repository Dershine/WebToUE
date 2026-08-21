#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WebToUECoreTypes.h"
#include "WebToUEResourceContract.h"
#include "WebToUEDocument.generated.h"

class UAssetImportData;
class UWebToUEDocument;

DECLARE_DELEGATE_RetVal_TwoParams(bool, FWebToUECookFreshnessValidator,
	const UWebToUEDocument&, TArray<FWebToUEResourceContractDiagnostic>&);

UENUM()
enum class EWebToUEResourceKind : uint8
{
	Texture,
	Material,
	Font,
	StringTable
};

USTRUCT()
struct WEBTOUERUNTIME_API FWebToUECompiledResource
{
	GENERATED_BODY()
	UPROPERTY() EWebToUEResourceKind Kind = EWebToUEResourceKind::Texture;
	UPROPERTY() FSoftObjectPath Path;
	UPROPERTY() FString ResourceId;
	UPROPERTY() FWebToUEResourceProvenance Provenance;
	UPROPERTY() FString GroupId;
	UPROPERTY() EWebToUEResidencyClass Residency = EWebToUEResidencyClass::Invalid;
	UPROPERTY() FVector2f IntrinsicSize = FVector2f::ZeroVector;
	UPROPERTY() FVector2f BrushImageSize = FVector2f::ZeroVector;

	bool operator==(const FWebToUECompiledResource& Other) const
	{
		return Kind == Other.Kind && Path == Other.Path;
	}
};

USTRUCT()
struct FWebToUECompiledAttribute
{
	GENERATED_BODY()
	UPROPERTY() FString Name;
	UPROPERTY() FString Value;
};

USTRUCT()
struct FWebToUECompiledDeclaration
{
	GENERATED_BODY()
	UPROPERTY() EWebToUECssProperty Property = EWebToUECssProperty::Invalid;
	UPROPERTY() FWebToUEStyleValue TypedValue;
	// Version 3 compatibility payload. Version 4 writers leave these empty.
	UPROPERTY() FString Name;
	UPROPERTY() FString Value;
};

UENUM()
enum class EWebToUEBindingKind : uint8
{
	Text,
	Visible,
	Enabled
};

USTRUCT()
struct FWebToUECompiledBindingOp
{
	GENERATED_BODY()
	UPROPERTY() FName RootField;
	UPROPERTY() EWebToUEBindingKind Kind = EWebToUEBindingKind::Text;
	UPROPERTY() int32 TargetNodeIndex = INDEX_NONE;
	UPROPERTY() bool bRichText = false;
};

USTRUCT()
struct FWebToUECompiledNode
{
	GENERATED_BODY()
	UPROPERTY() uint8 Type = 0;
	UPROPERTY() FString Tag;
	UPROPERTY() FString ResourceId;
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
	UPROPERTY() TArray<FWebToUECompiledDeclaration> InlineStyleDeclarations;
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
struct FWebToUECompiledRule
{
	GENERATED_BODY()
	UPROPERTY() TArray<FWebToUECompiledSelectorSegment> Selector;
	UPROPERTY() TArray<FWebToUECompiledDeclaration> Declarations;
	UPROPERTY() int32 Specificity = 0;
	UPROPERTY() int32 SourceOrder = 0;
};

struct WEBTOUERUNTIME_API FWebToUECompiledDocumentData
{
	FString LocalizationNamespace;
	TArray<FWebToUECompiledNode> Nodes;
	TArray<FWebToUECompiledRule> Rules;
	TArray<FWebToUECompiledBindingOp> BindingOps;
	int32 RootNodeIndex = INDEX_NONE;
	TArray<FWebToUECompiledResource> ResourceManifest;
	TArray<FWebToUEResourceDependency> SealedResourceDependencies;
	FWebToUECookFreshnessStamp ResourceFreshness;
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
	const FString& GetLocalizationNamespace() const { return LocalizationNamespace; }
	const TArray<FWebToUECompiledNode>& GetCompiledNodes() const { return CompiledNodes; }
	const TArray<FWebToUECompiledRule>& GetCompiledRules() const { return CompiledRules; }
	const TArray<FWebToUECompiledBindingOp>& GetCompiledBindingOps() const { return CompiledBindingOps; }
	TSharedPtr<const FWebToUERuntimeStyleTemplate> GetOrCreateRuntimeStyleTemplate() const;
	int32 GetRootNodeIndex() const { return RootNodeIndex; }
	const TArray<FWebToUECompiledResource>& GetResourceManifest() const { return ResourceManifest; }
	const TArray<FWebToUEResourceDependency>& GetSealedResourceDependencies() const
	{
		return SealedResourceDependencies;
	}
	const FWebToUECookFreshnessStamp& GetResourceFreshness() const { return ResourceFreshness; }
	bool ValidateResourceContract(
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics) const;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category="WebToUE", meta=(MultiLine=true))
	FString CompiledHtml;

	UPROPERTY(VisibleAnywhere, Category="WebToUE", meta=(MultiLine=true))
	FString CompiledCss;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="WebToUE")
	TArray<FWebToUEAssetDiagnostic> Diagnostics;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Instanced, Category="Import Settings")
	TObjectPtr<UAssetImportData> AssetImportData;

	UPROPERTY(VisibleAnywhere, Category="Import Settings")
	TArray<FString> DependencyFiles;
#endif

	bool HasCompileErrors() const;
	void NotifyDocumentChanged();
	virtual void Serialize(FArchive& Ar) override;
	virtual void PreSave(FObjectPreSaveContext SaveContext) override;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentChanged, UWebToUEDocument*);
	static FOnDocumentChanged& OnDocumentChanged();
	static FWebToUECookFreshnessValidator& CookFreshnessValidator();

#if WITH_EDITOR
	bool NeedsRecompile() const { return bNeedsRecompile; }
	void MarkRecompiled() { bNeedsRecompile = false; }
	void CommitCompiledDocument(FWebToUECompiledDocumentData&& CompiledDocument);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnDocumentNeedsRecompile, UWebToUEDocument*);
	static FOnDocumentNeedsRecompile& OnDocumentNeedsRecompile();

	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

private:
	bool bNeedsRecompile = false;
#endif

private:
	UPROPERTY()
	FString LocalizationNamespace;

	UPROPERTY()
	TArray<FWebToUECompiledNode> CompiledNodes;

	UPROPERTY()
	TArray<FWebToUECompiledRule> CompiledRules;

	UPROPERTY()
	TArray<FWebToUECompiledBindingOp> CompiledBindingOps;

	UPROPERTY()
	int32 RootNodeIndex = INDEX_NONE;

	UPROPERTY()
	TArray<FWebToUECompiledResource> ResourceManifest;

	UPROPERTY()
	TArray<FWebToUEResourceDependency> SealedResourceDependencies;

	UPROPERTY()
	FWebToUECookFreshnessStamp ResourceFreshness;

	mutable TSharedPtr<const FWebToUERuntimeStyleTemplate> RuntimeStyleTemplate;
};
