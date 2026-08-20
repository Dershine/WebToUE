#include "WebToUEDocument.h"

#include "WebToUEAssetVersion.h"
#include "WebToUEStyleProperties.h"

#include "Serialization/Archive.h"

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
