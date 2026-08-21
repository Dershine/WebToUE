#include "WebToUEDocument.h"

#include "WebToUEAssetVersion.h"
#include "WebToUEStyleProperties.h"

#include "Serialization/Archive.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITOR
#include "EditorFramework/AssetImportData.h"
#include "UObject/AssetRegistryTagsContext.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWebToUEDocument, Log, All);

TSharedPtr<const FWebToUERuntimeStyleTemplate>
UWebToUEDocument::GetOrCreateRuntimeStyleTemplate() const
{
	if (RuntimeStyleTemplate)
	{
		return RuntimeStyleTemplate;
	}

	TSharedRef<FWebToUERuntimeStyleTemplate> NewTemplate = MakeShared<FWebToUERuntimeStyleTemplate>();
	NewTemplate->Rules.Reserve(CompiledRules.Num());
	for (const FWebToUECompiledRule& SourceRule : CompiledRules)
	{
		FWebToUEStyleRule Rule;
		Rule.Specificity = SourceRule.Specificity;
		Rule.SourceOrder = SourceRule.SourceOrder;
		Rule.Selector.Reserve(SourceRule.Selector.Num());
		for (const FWebToUECompiledSelectorSegment& SourceSegment : SourceRule.Selector)
		{
			FWebToUESelectorSegment& Segment = Rule.Selector.AddDefaulted_GetRef();
			Segment.Type = SourceSegment.Type;
			Segment.Id = SourceSegment.Id;
			Segment.Classes = SourceSegment.Classes;
			Segment.RequiredState = static_cast<EWebToUEPseudoState>(SourceSegment.RequiredState);
			Segment.RelationToPrevious =
				static_cast<EWebToUECombinator>(SourceSegment.RelationToPrevious);
		}
		Rule.Declarations.Reserve(SourceRule.Declarations.Num());
		for (const FWebToUECompiledDeclaration& SourceDeclaration : SourceRule.Declarations)
		{
			FWebToUEStyleDeclaration& Declaration = Rule.Declarations.AddDefaulted_GetRef();
			if (SourceDeclaration.Property != EWebToUECssProperty::Invalid &&
				SourceDeclaration.TypedValue.Type != EWebToUEStyleValueType::Invalid)
			{
				Declaration.Property = SourceDeclaration.Property;
				Declaration.TypedValue = SourceDeclaration.TypedValue;
			}
			else if (!WebToUE::Private::TryParseCssDeclaration(
				SourceDeclaration.Name, SourceDeclaration.Value, Declaration))
			{
				UE_LOG(LogWebToUEDocument, Error,
					TEXT("Failed to prepare legacy or typed style declaration '%s'."),
					*SourceDeclaration.Name);
				return nullptr;
			}
		}
		NewTemplate->Rules.Add(MoveTemp(Rule));
	}
	NewTemplate->SelectorIndex.Initialize(NewTemplate->Rules);
	NewTemplate->CompilePseudoInvalidationDependencies();
	RuntimeStyleTemplate = NewTemplate;
	return RuntimeStyleTemplate;
}

bool UWebToUEDocument::HasCompileErrors() const
{
	return Diagnostics.ContainsByPredicate([](const FWebToUEAssetDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EWebToUEAssetDiagnosticSeverity::Error;
	});
}

bool UWebToUEDocument::ValidateResourceContract(
	TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics) const
{
	OutDiagnostics.Reset();
	const bool bHasContractResource = ResourceManifest.ContainsByPredicate(
		[](const FWebToUECompiledResource& Resource)
		{
			return Resource.Kind == EWebToUEResourceKind::Texture ||
				Resource.Kind == EWebToUEResourceKind::Material;
		});
	const bool bHasContract = ResourceFreshness.ContractVersion.IsPresent();
	if (!bHasContractResource && !bHasContract)
	{
		return true;
	}

	FWebToUEResourceContractDescriptor Descriptor;
	Descriptor.ContractVersion = ResourceFreshness.ContractVersion;
	Descriptor.DocumentId = ResourceFreshness.DocumentId;
	Descriptor.CompilerFingerprintBlake3 = ResourceFreshness.CompilerFingerprintBlake3;
	Descriptor.ArtifactVersions = ResourceFreshness.ArtifactVersions;
	Descriptor.Dependencies = SealedResourceDependencies;

	TMap<FString, const FWebToUECompiledResource*> ResourcesById;
	for (const FWebToUECompiledResource& Resource : ResourceManifest)
	{
		if (Resource.Kind != EWebToUEResourceKind::Texture &&
			Resource.Kind != EWebToUEResourceKind::Material)
		{
			continue;
		}
		Descriptor.Resources.Add({ Resource.ResourceId, Resource.Provenance });
		Descriptor.ResidencyAssignments.Add({ Resource.ResourceId, FString(),
			Resource.GroupId, Resource.Residency });
		if (Resource.Path.IsNull() || ResourcesById.Contains(Resource.ResourceId))
		{
			OutDiagnostics.Add({ TEXT("WTUE-RES-002"),
				TEXT("manifest/") + Resource.ResourceId,
				TEXT("Contract resource paths and ResourceIds must be present and unique.") });
		}
		else
		{
			ResourcesById.Add(Resource.ResourceId, &Resource);
		}
		if (ResourceFreshness.ArtifactVersions.ResourceIr.Major == 1 &&
			ResourceFreshness.ArtifactVersions.ResourceIr.Minor >= 1 &&
			Resource.Kind == EWebToUEResourceKind::Texture &&
			(Resource.IntrinsicSize.X <= 0.0f || Resource.IntrinsicSize.Y <= 0.0f))
		{
			OutDiagnostics.Add({ TEXT("WTUE-RES-003"),
				TEXT("manifest/") + Resource.ResourceId + TEXT("/intrinsic-size"),
				TEXT("Resource IR 1.1 texture entries require a positive sealed intrinsic size.") });
		}
		if (ResourceFreshness.ArtifactVersions.ResourceIr.Major == 1 &&
			ResourceFreshness.ArtifactVersions.ResourceIr.Minor >= 2 &&
			Resource.Kind == EWebToUEResourceKind::Material &&
			(Resource.BrushImageSize.X <= 0.0f || Resource.BrushImageSize.Y <= 0.0f))
		{
			OutDiagnostics.Add({ TEXT("WTUE-RES-003"),
				TEXT("manifest/") + Resource.ResourceId + TEXT("/brush-image-size"),
				TEXT("Resource IR 1.2 Material entries require a positive sealed Slate brush image size.") });
		}
	}

	for (int32 NodeIndex = 0; NodeIndex < CompiledNodes.Num(); ++NodeIndex)
	{
		const FWebToUECompiledNode& Node = CompiledNodes[NodeIndex];
		const bool bTextureNode = Node.Tag == TEXT("img");
		const FWebToUECompiledAttribute* MaterialAttribute =
			Node.Attributes.FindByPredicate([](const FWebToUECompiledAttribute& Attribute)
			{
				return Attribute.Name == TEXT("data-ue-material");
			});
		if (!bTextureNode && !MaterialAttribute)
		{
			continue;
		}
		const FWebToUECompiledResource* const* Resource =
			ResourcesById.Find(Node.ResourceId);
		const FWebToUECompiledAttribute* SourceAttribute =
			Node.Attributes.FindByPredicate([](const FWebToUECompiledAttribute& Attribute)
			{
				return Attribute.Name == TEXT("src");
			});
		const bool bLegacyUnrealBinding =
			ResourceFreshness.ArtifactVersions.ResourceIr.Major == 1 &&
			ResourceFreshness.ArtifactVersions.ResourceIr.Minor == 0;
		const FWebToUECompiledAttribute* AuthorAttribute = bTextureNode
			? SourceAttribute : MaterialAttribute;
		const EWebToUEResourceKind ExpectedKind = bTextureNode
			? EWebToUEResourceKind::Texture : EWebToUEResourceKind::Material;
		if (!Resource || (*Resource)->Kind != ExpectedKind || !AuthorAttribute ||
			(*Resource)->Provenance.AuthorReference != AuthorAttribute->Value ||
			(bTextureNode && bLegacyUnrealBinding &&
				(*Resource)->Path != FSoftObjectPath(AuthorAttribute->Value)))
		{
			OutDiagnostics.Add({ TEXT("WTUE-RES-003"),
				FString::Printf(TEXT("nodes/%d/resource-id"), NodeIndex),
				bTextureNode
					? TEXT("Image nodes must bind by ResourceId to their sealed Unreal texture manifest entry.")
					: TEXT("Material nodes must bind by ResourceId to their sealed static Material manifest entry.") });
		}
	}

	FWebToUEResourceContractSnapshot Snapshot;
	TArray<FWebToUEResourceContractDiagnostic> SnapshotDiagnostics;
	if (!FWebToUEResourceContractPolicy::BuildSnapshot(
		Descriptor, Snapshot, SnapshotDiagnostics))
	{
		OutDiagnostics.Append(MoveTemp(SnapshotDiagnostics));
	}
	else
	{
		FWebToUEArtifactVersionSet SupportedVersions;
		SupportedVersions.UiIr = { 1, 0 };
		SupportedVersions.ResourceIr = { 1, 2 };
		TArray<FWebToUEResourceContractDiagnostic> CompatibilityDiagnostics;
		FWebToUEResourceContractPolicy::IsRuntimeCompatible(
			Snapshot.Freshness.ArtifactVersions, SupportedVersions,
			CompatibilityDiagnostics);
		OutDiagnostics.Append(MoveTemp(CompatibilityDiagnostics));
		TArray<FWebToUEResourceContractDiagnostic> FreshnessDiagnostics;
		FWebToUEResourceContractPolicy::IsCookFresh(
			Snapshot.Freshness, ResourceFreshness, FreshnessDiagnostics);
		OutDiagnostics.Append(MoveTemp(FreshnessDiagnostics));
	}

	OutDiagnostics.Sort([](const FWebToUEResourceContractDiagnostic& A,
		const FWebToUEResourceContractDiagnostic& B)
	{
		if (A.Code != B.Code) return A.Code < B.Code;
		if (A.Path != B.Path) return A.Path < B.Path;
		return A.Detail < B.Detail;
	});
	return OutDiagnostics.IsEmpty();
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

FWebToUECookFreshnessValidator& UWebToUEDocument::CookFreshnessValidator()
{
	static FWebToUECookFreshnessValidator Validator;
	return Validator;
}

void UWebToUEDocument::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
#if WITH_EDITOR
	if (!SaveContext.IsCooking() || HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	TArray<FWebToUEResourceContractDiagnostic> FreshnessDiagnostics;
	bool bFresh = ValidateResourceContract(FreshnessDiagnostics);
	if (bFresh)
	{
		FWebToUECookFreshnessValidator& Validator = CookFreshnessValidator();
		if (Validator.IsBound())
		{
			bFresh = Validator.Execute(*this, FreshnessDiagnostics);
		}
		else
		{
			bFresh = false;
			FreshnessDiagnostics.Add({ TEXT("WTUE-RES-004"),
				TEXT("cook-validator"),
				TEXT("Cook cannot prove Resource freshness because the Editor validator is unavailable.") });
		}
	}
	if (!bFresh)
	{
		for (const FWebToUEResourceContractDiagnostic& Diagnostic : FreshnessDiagnostics)
		{
			UE_LOG(LogWebToUEDocument, Error, TEXT("%s %s: %s Asset=%s"),
				*Diagnostic.Code, *Diagnostic.Path, *Diagnostic.Detail, *GetPathName());
		}
	}
#endif
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

void UWebToUEDocument::CommitCompiledDocument(FWebToUECompiledDocumentData&& CompiledDocument)
{
	RuntimeStyleTemplate.Reset();
	LocalizationNamespace = MoveTemp(CompiledDocument.LocalizationNamespace);
	CompiledNodes = MoveTemp(CompiledDocument.Nodes);
	CompiledRules = MoveTemp(CompiledDocument.Rules);
	CompiledBindingOps = MoveTemp(CompiledDocument.BindingOps);
	RootNodeIndex = CompiledDocument.RootNodeIndex;
	ResourceManifest = MoveTemp(CompiledDocument.ResourceManifest);
	SealedResourceDependencies = MoveTemp(CompiledDocument.SealedResourceDependencies);
	ResourceFreshness = MoveTemp(CompiledDocument.ResourceFreshness);
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
