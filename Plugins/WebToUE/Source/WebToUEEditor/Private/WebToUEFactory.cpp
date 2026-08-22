#include "WebToUEFactory.h"

#include "WebToUECompiler.h"
#include "WebToUEDocument.h"
#include "WebToUEResourceContract.h"
#include "WebToUESettings.h"

#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Factories/TextureFactory.h"
#include "Internationalization/StringTable.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "HAL/FileManager.h"
#include "Hash/Blake3.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogWebToUEImport, Log, All);

namespace WebToUE::ResourceImport::Private
{
	static constexpr const TCHAR* GeneratedTextureRoot =
		TEXT("/Game/WebToUEGenerated/Textures");

	struct FResolvedTextureReference
	{
		FSoftObjectPath Path;
		FString ResourceId;
		FWebToUEResourceProvenance Provenance;
		FString SourceFilename;
		FVector2f IntrinsicSize = FVector2f::ZeroVector;
	};

	struct FResolvedMaterialReference
	{
		FSoftObjectPath Path;
		FString ResourceId;
		FWebToUEResourceProvenance Provenance;
		FVector2f BrushImageSize = FVector2f(1.0f, 1.0f);
	};

	static FString HashBuffer(const void* Data, uint64 Size)
	{
		return LexToString(FBlake3::HashBuffer(Data, Size)).ToLower();
	}

	static FString HashUtf8(const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		return HashBuffer(Utf8.Get(), Utf8.Length());
	}

	static bool MakeLogicalSourceId(const FString& Filename, FString& OutLogicalId)
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(Filename);
		FPaths::NormalizeFilename(FullPath);
		FString RelativePath = FullPath;
		if (!FPaths::MakePathRelativeTo(RelativePath, *FPaths::ProjectDir()) ||
			RelativePath.StartsWith(TEXT("../")))
		{
			return false;
		}
		FPaths::NormalizeFilename(RelativePath);
		OutLogicalId = TEXT("source/") + RelativePath;
		return true;
	}

	static FString MakeAssetDependencyId(const FSoftObjectPath& Path)
	{
		const FString PackageName = FPackageName::ObjectPathToPackageName(Path.ToString());
		return PackageName.StartsWith(TEXT("/"))
			? TEXT("asset/") + PackageName.RightChop(1)
			: TEXT("asset/") + PackageName;
	}

	static bool IsCanonicalBlake3(const FString& Value)
	{
		if (Value.Len() != 64)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (!((Character >= TEXT('0') && Character <= TEXT('9')) ||
				(Character >= TEXT('a') && Character <= TEXT('f'))))
			{
				return false;
			}
		}
		return true;
	}

	static FString MakeGeneratedTextureId(const FString& StableHash)
	{
		return TEXT("generated:textures/") + StableHash;
	}

	static FSoftObjectPath MakeGeneratedTexturePath(const FString& StableHash)
	{
		const FString Name = TEXT("T_") + StableHash;
		return FSoftObjectPath(FString::Printf(TEXT("%s/%s.%s"),
			GeneratedTextureRoot, *Name, *Name));
	}

	static bool ParseGeneratedTextureReference(const FString& Reference,
		FString& OutStableHash)
	{
		const FString Prefix = TEXT("generated:textures/");
		if (!Reference.StartsWith(Prefix))
		{
			return false;
		}
		OutStableHash = Reference.RightChop(Prefix.Len());
		return IsCanonicalBlake3(OutStableHash);
	}

	static FVector2f GetTextureIntrinsicSize(const UTexture2D& Texture)
	{
		const FIntPoint ImportedSize = Texture.GetImportedSize();
		return FVector2f(ImportedSize.X, ImportedSize.Y);
	}

	static bool IsSupportedRelativeTextureExtension(const FString& Filename)
	{
		static const TSet<FString> Supported = {
			TEXT("bmp"), TEXT("png"), TEXT("jpg"), TEXT("jpeg"),
			TEXT("tga"), TEXT("psd"), TEXT("dds"), TEXT("exr"), TEXT("hdr")
		};
		return Supported.Contains(FPaths::GetExtension(Filename).ToLower());
	}

	static bool ResolveRelativeTexturePath(const FString& DocumentFilename,
		const FString& Reference, FString& OutFilename,
		FString& OutCanonicalReference, FString& OutLogicalSourceId)
	{
		if (Reference.IsEmpty() || Reference.TrimStartAndEnd() != Reference ||
			Reference.Contains(TEXT("\\")) || Reference.Contains(TEXT("://")) ||
			!FPaths::IsRelative(Reference))
		{
			return false;
		}
		OutFilename = FPaths::ConvertRelativePathToFull(
			FPaths::GetPath(DocumentFilename), Reference);
		FPaths::NormalizeFilename(OutFilename);
		FString ProjectRelative = OutFilename;
		if (!FPaths::MakePathRelativeTo(ProjectRelative, *FPaths::ProjectDir()) ||
			ProjectRelative.StartsWith(TEXT("../")) ||
			!IFileManager::Get().FileExists(*OutFilename) ||
			!IsSupportedRelativeTextureExtension(OutFilename) ||
			!MakeLogicalSourceId(OutFilename, OutLogicalSourceId))
		{
			return false;
		}
		OutCanonicalReference = OutFilename;
		const FString DocumentDirectory =
			FPaths::GetPath(DocumentFilename) + TEXT("/");
		if (!FPaths::MakePathRelativeTo(OutCanonicalReference,
			*DocumentDirectory))
		{
			return false;
		}
		FPaths::NormalizeFilename(OutCanonicalReference);
		while (OutCanonicalReference.StartsWith(TEXT("./")))
		{
			OutCanonicalReference.RightChopInline(2);
		}
		return !OutCanonicalReference.IsEmpty() &&
			!OutCanonicalReference.StartsWith(TEXT("../"));
	}

	static UTexture2D* ImportGeneratedTexture(const FString& SourceFilename,
		const FString& StableHash)
	{
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = SourceFilename;
		Task->DestinationPath = GeneratedTextureRoot;
		Task->DestinationName = TEXT("T_") + StableHash;
		Task->bReplaceExisting = true;
		Task->bReplaceExistingSettings = false;
		Task->bAutomated = true;
		Task->bSave = true;
		Task->bAsync = false;
		Task->Factory = NewObject<UTextureFactory>();
		TArray<UAssetImportTask*> Tasks = { Task };
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
		const FSoftObjectPath ExpectedPath = MakeGeneratedTexturePath(StableHash);
		for (UObject* Object : Task->GetObjects())
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Object);
				Texture && FSoftObjectPath(Texture) == ExpectedPath)
			{
				return Texture;
			}
		}
		return Cast<UTexture2D>(ExpectedPath.ResolveObject());
	}

	static bool ResolveTextureReference(const FString& DocumentFilename,
		const FString& SourceUnit, const FString& Reference,
		FString& OutIdentity, FResolvedTextureReference& OutResolved)
	{
		if (Reference.StartsWith(TEXT("/Game/")) ||
			Reference.StartsWith(TEXT("/Engine/")))
		{
			OutResolved.Path = FSoftObjectPath(Reference);
			UTexture2D* Texture = Cast<UTexture2D>(OutResolved.Path.TryLoad());
			if (!OutResolved.Path.IsValid() || !Texture)
			{
				return false;
			}
			OutIdentity = Reference;
			OutResolved.ResourceId = TEXT("resource/texture/") + HashUtf8(OutIdentity);
			OutResolved.Provenance = { EWebToUEResourceOrigin::UnrealAsset,
				SourceUnit, Reference, MakeAssetDependencyId(OutResolved.Path) };
			OutResolved.IntrinsicSize = GetTextureIntrinsicSize(*Texture);
			return true;
		}

		FString StableHash;
		if (Reference.StartsWith(TEXT("generated:")))
		{
			if (!ParseGeneratedTextureReference(Reference, StableHash))
			{
				return false;
			}
			OutIdentity = MakeGeneratedTextureId(StableHash);
			OutResolved.Path = MakeGeneratedTexturePath(StableHash);
			UTexture2D* Texture = Cast<UTexture2D>(OutResolved.Path.TryLoad());
			if (!Texture)
			{
				return false;
			}
			OutResolved.ResourceId = TEXT("resource/texture/") + HashUtf8(OutIdentity);
			OutResolved.Provenance = { EWebToUEResourceOrigin::Generated,
				SourceUnit, OutIdentity, OutIdentity };
			OutResolved.IntrinsicSize = GetTextureIntrinsicSize(*Texture);
			return true;
		}

		FString CanonicalReference;
		FString LogicalSourceId;
		if (!ResolveRelativeTexturePath(DocumentFilename, Reference,
			OutResolved.SourceFilename, CanonicalReference, LogicalSourceId))
		{
			return false;
		}
		StableHash = HashUtf8(LogicalSourceId);
		OutIdentity = LogicalSourceId;
		const FString GeneratedId = MakeGeneratedTextureId(StableHash);
		UTexture2D* Texture = ImportGeneratedTexture(
			OutResolved.SourceFilename, StableHash);
		if (!Texture || FSoftObjectPath(Texture) != MakeGeneratedTexturePath(StableHash))
		{
			return false;
		}
		OutResolved.Path = FSoftObjectPath(Texture);
		OutResolved.ResourceId = TEXT("resource/texture/") + HashUtf8(GeneratedId);
		OutResolved.Provenance = { EWebToUEResourceOrigin::RelativeSource,
			SourceUnit, CanonicalReference, GeneratedId };
		OutResolved.IntrinsicSize = GetTextureIntrinsicSize(*Texture);
		return true;
	}

	static bool ResolveMaterialReference(const FString& SourceUnit,
		const FString& Reference, FResolvedMaterialReference& OutResolved)
	{
		if (!Reference.StartsWith(TEXT("/Game/")) &&
			!Reference.StartsWith(TEXT("/Engine/")))
		{
			return false;
		}
		OutResolved.Path = FSoftObjectPath(Reference);
		UMaterialInterface* Material =
			Cast<UMaterialInterface>(OutResolved.Path.TryLoad());
		if (!OutResolved.Path.IsValid() || !Material ||
			FSoftObjectPath(Material) != OutResolved.Path)
		{
			return false;
		}
		OutResolved.ResourceId =
			TEXT("resource/material/") + HashUtf8(Reference);
		OutResolved.Provenance = { EWebToUEResourceOrigin::UnrealAsset,
			SourceUnit, Reference, MakeAssetDependencyId(OutResolved.Path) };
		return true;
	}

	static int32 ResidencyRank(EWebToUEResidencyClass Residency)
	{
		switch (Residency)
		{
		case EWebToUEResidencyClass::Critical: return 0;
		case EWebToUEResidencyClass::Visible: return 1;
		case EWebToUEResidencyClass::Lazy: return 2;
		default: return MAX_int32;
		}
	}

	static EWebToUEResidencyClass ParseImageResidency(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Equals(TEXT("visible"), ESearchCase::IgnoreCase))
		{
			return EWebToUEResidencyClass::Visible;
		}
		if (Value.Equals(TEXT("critical"), ESearchCase::IgnoreCase))
		{
			return EWebToUEResidencyClass::Critical;
		}
		if (Value.Equals(TEXT("lazy"), ESearchCase::IgnoreCase))
		{
			return EWebToUEResidencyClass::Lazy;
		}
		return EWebToUEResidencyClass::Invalid;
	}

	static bool HashFile(const FString& Filename, FString& OutHash)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Filename))
		{
			return false;
		}
		OutHash = HashBuffer(Bytes.GetData(), Bytes.Num());
		return true;
	}

	static bool HashAssetPackage(const FSoftObjectPath& Path, FString& OutHash)
	{
		const FString PackageNameString = FPackageName::ObjectPathToPackageName(Path.ToString());
		if (!FPackageName::IsValidLongPackageName(PackageNameString))
		{
			return false;
		}
		FString PackageFilename;
		if (!FPackageName::DoesPackageExist(PackageNameString, &PackageFilename))
		{
			return false;
		}
		return HashFile(PackageFilename, OutHash);
	}

	static bool AddSealedPackageDependency(const FName PackageName,
		TArray<FWebToUEResourceDependency>& OutDependencies,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
	{
		const FString PackageString = PackageName.ToString();
		if (!PackageString.StartsWith(TEXT("/Game/")) &&
			!PackageString.StartsWith(TEXT("/Engine/")))
		{
			return true;
		}
		FString PackageFilename;
		FString PackageHash;
		if (!FPackageName::DoesPackageExist(PackageString, &PackageFilename) ||
			!HashFile(PackageFilename, PackageHash))
		{
			OutDiagnostics.Add({ TEXT("WTUE-RES-001"),
				TEXT("dependencies.material-package"),
				FString::Printf(TEXT("Referenced Material package is missing or has no saved content fingerprint: %s"),
					*PackageString) });
			return false;
		}
		const FString LogicalId = PackageString.StartsWith(TEXT("/"))
			? TEXT("asset/") + PackageString.RightChop(1)
			: TEXT("asset/") + PackageString;
		if (!OutDependencies.ContainsByPredicate(
			[&LogicalId](const FWebToUEResourceDependency& Dependency)
			{
				return Dependency.LogicalId == LogicalId;
			}))
		{
			OutDependencies.Add({ LogicalId,
				EWebToUEResourceDependencyKind::Resource, MoveTemp(PackageHash) });
		}
		return true;
	}

	static bool AddMaterialDependencyClosure(const FSoftObjectPath& MaterialPath,
		TArray<FWebToUEResourceDependency>& OutDependencies,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
	{
		const FString RootPackage =
			FPackageName::ObjectPathToPackageName(MaterialPath.ToString());
		if (!FPackageName::IsValidLongPackageName(RootPackage))
		{
			return false;
		}
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FName> Pending = { FName(*RootPackage) };
		TSet<FName> Visited;
		bool bValid = true;
		while (!Pending.IsEmpty())
		{
			const FName PackageName = Pending.Pop(EAllowShrinking::No);
			if (Visited.Contains(PackageName))
			{
				continue;
			}
			Visited.Add(PackageName);
			bValid &= AddSealedPackageDependency(
				PackageName, OutDependencies, OutDiagnostics);
			TArray<FName> ReferencedPackages;
			AssetRegistry.GetDependencies(PackageName, ReferencedPackages,
				UE::AssetRegistry::EDependencyCategory::Package);
			ReferencedPackages.Sort(FNameLexicalLess());
			for (const FName ReferencedPackage : ReferencedPackages)
			{
				const FString ReferencedString = ReferencedPackage.ToString();
				if (!Visited.Contains(ReferencedPackage) &&
					(ReferencedString.StartsWith(TEXT("/Game/")) ||
					 ReferencedString.StartsWith(TEXT("/Engine/"))))
				{
					Pending.Add(ReferencedPackage);
				}
			}
		}
		return bValid;
	}

	static FWebToUEArtifactVersionSet CurrentArtifactVersions()
	{
		FWebToUEArtifactVersionSet Versions;
		Versions.UiIr = { 1, 0 };
		Versions.ResourceIr = { 1, 2 };
		Versions.AnimationIr = {
			FWebToUECompiledAnimationIR::CurrentMajor,
			FWebToUECompiledAnimationIR::CurrentMinor };
		return Versions;
	}

	static FString CompilerFingerprint()
	{
		return HashUtf8(TEXT("WebToUE.Editor.ResourceImporter/5;UI-IR/1.0;Resource-IR/1.2;Animation-IR/1.1;StaticMaterialBrush/1;TransitionLowering/1"));
	}

	static bool ResolveDocumentTextures(const FWebToUEDocument& Source,
		const FString& DocumentFilename, const FString& SourceUnit,
		TMap<FString, FResolvedTextureReference>& OutResolvedByIdentity,
		TMap<FString, FString>& OutIdentityByAuthorReference,
		TArray<FString>& InOutDependencies,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
	{
		bool bValid = true;
		Source.ForEachNode([&](FWebToUENode& Node)
		{
			if (Node.Tag != TEXT("img"))
			{
				return;
			}
			const FString Reference = Node.GetAttribute(TEXT("src"));
			if (Reference.IsEmpty() || OutIdentityByAuthorReference.Contains(Reference))
			{
				return;
			}

			FString Identity;
			FString RelativeFilename;
			FString CanonicalReference;
			if (!Reference.StartsWith(TEXT("/Game/")) &&
				!Reference.StartsWith(TEXT("/Engine/")) &&
				!Reference.StartsWith(TEXT("generated:")))
			{
				if (!ResolveRelativeTexturePath(DocumentFilename, Reference,
					RelativeFilename, CanonicalReference, Identity))
				{
					OutDiagnostics.Add({ TEXT("WTUE-RES-001"), TEXT("resources.texture"),
						FString::Printf(TEXT("Relative texture is missing, outside the project, or unsupported: %s"),
							*Reference) });
					bValid = false;
					return;
				}
				InOutDependencies.AddUnique(RelativeFilename);
				if (OutResolvedByIdentity.Contains(Identity))
				{
					OutIdentityByAuthorReference.Add(Reference, Identity);
					return;
				}
			}

			FResolvedTextureReference Resolved;
			if (!ResolveTextureReference(DocumentFilename, SourceUnit,
				Reference, Identity, Resolved))
			{
				OutDiagnostics.Add({ TEXT("WTUE-RES-001"), TEXT("resources.texture"),
					FString::Printf(TEXT("Texture reference is invalid, missing, or not a supported Texture2D: %s"),
						*Reference) });
				bValid = false;
				return;
			}
			if (!Resolved.SourceFilename.IsEmpty())
			{
				InOutDependencies.AddUnique(Resolved.SourceFilename);
			}
			OutIdentityByAuthorReference.Add(Reference, Identity);
			OutResolvedByIdentity.Add(Identity, MoveTemp(Resolved));
		});
		return bValid;
	}

	static bool ResolveDocumentMaterials(const FWebToUEDocument& Source,
		const FString& SourceUnit,
		TMap<FString, FResolvedMaterialReference>& OutResolvedByAuthorReference,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
	{
		bool bValid = true;
		Source.ForEachNode([&](FWebToUENode& Node)
		{
			const FString Reference = Node.GetAttribute(TEXT("data-ue-material"));
			if (Reference.IsEmpty() ||
				OutResolvedByAuthorReference.Contains(Reference))
			{
				return;
			}
			FResolvedMaterialReference Resolved;
			if (!ResolveMaterialReference(SourceUnit, Reference, Resolved))
			{
				const bool bUnsupportedSource =
					Reference.StartsWith(TEXT("generated:")) ||
					(!Reference.StartsWith(TEXT("/Game/")) &&
					 !Reference.StartsWith(TEXT("/Engine/")));
				OutDiagnostics.Add({ TEXT("WTUE-RES-001"),
					TEXT("resources.material"),
					bUnsupportedSource
						? FString::Printf(TEXT("Static Material references only support sealed /Game or /Engine MaterialInterface assets; relative/generated/HTTP/absolute-disk sources are unsupported: %s"),
							*Reference)
						: FString::Printf(TEXT("Material reference is missing or is not a MaterialInterface asset: %s"),
							*Reference) });
				bValid = false;
				return;
			}
			OutResolvedByAuthorReference.Add(Reference, MoveTemp(Resolved));
		});
		return bValid;
	}
}

UWebToUEFactory::UWebToUEFactory()
{
	SupportedClass = UWebToUEDocument::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bText = true;
	Formats.Add(TEXT("html;WebToUE HTML Document"));
}

bool UWebToUEFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("html"), ESearchCase::IgnoreCase);
}

static FWebToUEAssetDiagnostic ConvertDiagnostic(const FWebToUEDiagnostic& Source)
{
	FWebToUEAssetDiagnostic Result;
	Result.Severity = Source.Severity == EWebToUEDiagnosticSeverity::Error ? EWebToUEAssetDiagnosticSeverity::Error :
		Source.Severity == EWebToUEDiagnosticSeverity::Warning ? EWebToUEAssetDiagnosticSeverity::Warning : EWebToUEAssetDiagnosticSeverity::Info;
	Result.File = Source.File;
	Result.Line = Source.Line;
	Result.Column = Source.Column;
	Result.Message = Source.Message;
	return Result;
}

static FWebToUECompiledDocumentData BuildCompiledDocument(const FWebToUEDocument& Source,
	const UWebToUEDocument& Target, const FString& SourceUnit,
	const TMap<FString, WebToUE::ResourceImport::Private::FResolvedTextureReference>&
		ResolvedTextures,
	const TMap<FString, FString>& TextureIdentities,
	const TMap<FString, WebToUE::ResourceImport::Private::FResolvedMaterialReference>&
		ResolvedMaterials)
{
	FWebToUECompiledDocumentData Result;
	TMap<FString, FString> PreviousAutoKeys;
	for (const FWebToUECompiledNode& Existing : Target.GetCompiledNodes())
	{
		if (Existing.bAutoLocalizationKey && !Existing.TextIdentity.IsEmpty() && !Existing.LocalizationKey.IsEmpty())
		{
			PreviousAutoKeys.Add(Existing.TextIdentity, Existing.LocalizationKey);
		}
	}
	Result.LocalizationNamespace = Target.GetLocalizationNamespace();
	if (Result.LocalizationNamespace.IsEmpty())
	{
		Result.LocalizationNamespace = TEXT("WebToUE_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}
	const auto FindResolvedTexture = [&ResolvedTextures, &TextureIdentities](
		const FString& AuthorReference)
		-> const WebToUE::ResourceImport::Private::FResolvedTextureReference*
	{
		const FString* Identity = TextureIdentities.Find(AuthorReference);
		return Identity ? ResolvedTextures.Find(*Identity) : nullptr;
	};
	const auto FindResolvedMaterial = [&ResolvedMaterials](
		const FString& AuthorReference)
		-> const WebToUE::ResourceImport::Private::FResolvedMaterialReference*
	{
		return ResolvedMaterials.Find(AuthorReference);
	};
	TMap<const FWebToUENode*, int32> CompiledNodeIndices;

	TFunction<int32(const TSharedPtr<FWebToUENode>&, int32, const FString&, int32)> AddNode =
		[&](const TSharedPtr<FWebToUENode>& Node, int32 ParentIndex, const FString& ParentIdentity, int32 SiblingOrdinal)
	{
		FWebToUECompiledNode Serialized;
		Serialized.Type = static_cast<uint8>(Node->Type);
		Serialized.Tag = Node->Tag;
		const WebToUE::ResourceImport::Private::FResolvedTextureReference*
			ResolvedTexture = nullptr;
		if (Node->Tag == TEXT("img"))
		{
			const FString AuthorReference = Node->GetAttribute(TEXT("src"));
			ResolvedTexture = FindResolvedTexture(AuthorReference);
			if (ResolvedTexture)
			{
				Serialized.ResourceId = ResolvedTexture->ResourceId;
			}
		}
		const FString MaterialReference =
			Node->GetAttribute(TEXT("data-ue-material"));
		if (!MaterialReference.IsEmpty())
		{
			if (const WebToUE::ResourceImport::Private::FResolvedMaterialReference*
				ResolvedMaterial = FindResolvedMaterial(MaterialReference))
			{
				Serialized.ResourceId = ResolvedMaterial->ResourceId;
			}
		}
		Serialized.Text = Node->Text;
		Serialized.bRichText = Node->bRichText;
		Serialized.ParentIndex = ParentIndex;
		const FString ElementId = Node->Type == EWebToUENodeType::Element ? Node->GetAttribute(TEXT("id")) : FString();
		Serialized.TextIdentity = !ElementId.IsEmpty()
			? TEXT("#") + ElementId
			: FString::Printf(TEXT("%s/%s[%d]"), *ParentIdentity,
				Node->Type == EWebToUENodeType::Text ? TEXT("text") : *Node->Tag, SiblingOrdinal);
		for (const TPair<FString, FString>& Attribute : Node->Attributes)
		{
			FWebToUECompiledAttribute& OutAttribute = Serialized.Attributes.AddDefaulted_GetRef();
			OutAttribute.Name = Attribute.Key;
			OutAttribute.Value = ResolvedTexture && Attribute.Key == TEXT("src")
				? ResolvedTexture->Provenance.AuthorReference : Attribute.Value;
		}
		for (const FWebToUEStyleDeclaration& Declaration : Node->InlineStyleDeclarations)
		{
			FWebToUECompiledDeclaration& OutDeclaration = Serialized.InlineStyleDeclarations.AddDefaulted_GetRef();
			OutDeclaration.Property = Declaration.Property;
			OutDeclaration.TypedValue = Declaration.TypedValue;
		}
		if (Node->Type == EWebToUENodeType::Text)
		{
			const FWebToUENode* Owner = Node->Parent;
			const FString TableId = Owner ? Owner->GetAttribute(TEXT("data-ue-string-table")) : FString();
			const FString TableKey = Owner ? Owner->GetAttribute(TEXT("data-ue-string-key")) : FString();
			if (!TableId.IsEmpty() && !TableKey.IsEmpty())
			{
				Serialized.StringTableId = FName(*TableId);
				Serialized.StringTableKey = TableKey;
				Serialized.LocalizedText = FText::FromStringTable(Serialized.StringTableId, TableKey);
			}
			else
			{
				Serialized.LocalizationNamespace = Owner ? Owner->GetAttribute(TEXT("data-ue-loc-namespace")) : FString();
				if (Serialized.LocalizationNamespace.IsEmpty()) Serialized.LocalizationNamespace = Result.LocalizationNamespace;
				Serialized.LocalizationKey = Owner ? Owner->GetAttribute(TEXT("data-ue-loc-key")) : FString();
				Serialized.bAutoLocalizationKey = Serialized.LocalizationKey.IsEmpty();
				if (Serialized.bAutoLocalizationKey)
				{
					if (const FString* ExistingKey = PreviousAutoKeys.Find(Serialized.TextIdentity))
					{
						Serialized.LocalizationKey = *ExistingKey;
					}
					else
					{
						Serialized.LocalizationKey = FGuid::NewGuid().ToString(EGuidFormats::Digits);
					}
				}
				Serialized.LocalizedText = FText::ChangeKey(Serialized.LocalizationNamespace,
					Serialized.LocalizationKey, FText::FromString(Node->Text));
			}
		}
		const int32 NodeIndex = Result.Nodes.Add(MoveTemp(Serialized));
		CompiledNodeIndices.Add(Node.Get(), NodeIndex);
		const auto AddBindingOp = [&](const TCHAR* AttributeName, EWebToUEBindingKind Kind)
		{
			const FString RootField = Node->GetAttribute(AttributeName);
			if (RootField.IsEmpty()) return;
			FWebToUECompiledBindingOp& BindingOp = Result.BindingOps.AddDefaulted_GetRef();
			BindingOp.RootField = FName(*RootField);
			BindingOp.Kind = Kind;
			BindingOp.TargetNodeIndex = NodeIndex;
			BindingOp.bRichText = Kind == EWebToUEBindingKind::Text &&
				Node->GetAttribute(TEXT("data-ue-rich-text")).Equals(
					TEXT("true"), ESearchCase::IgnoreCase);
		};
		AddBindingOp(TEXT("data-ue-bind-text"), EWebToUEBindingKind::Text);
		AddBindingOp(TEXT("data-ue-bind-visible"), EWebToUEBindingKind::Visible);
		AddBindingOp(TEXT("data-ue-bind-enabled"), EWebToUEBindingKind::Enabled);
		TMap<FString, int32> ChildOrdinals;
		for (const TSharedPtr<FWebToUENode>& Child : Node->Children)
		{
			const FString Kind = Child->Type == EWebToUENodeType::Text ? TEXT("text") : Child->Tag;
			const int32 ChildOrdinal = ChildOrdinals.FindOrAdd(Kind)++;
			AddNode(Child, NodeIndex, Result.Nodes[NodeIndex].TextIdentity, ChildOrdinal);
		}
		return NodeIndex;
	};
	if (Source.Root) Result.RootNodeIndex = AddNode(Source.Root, INDEX_NONE, TEXT("document"), 0);

	for (const FWebToUEStyleRule& Rule : Source.Rules)
	{
		FWebToUECompiledRule& SerializedRule = Result.Rules.AddDefaulted_GetRef();
		SerializedRule.Specificity = Rule.Specificity;
		SerializedRule.SourceOrder = Rule.SourceOrder;
		for (const FWebToUESelectorSegment& Segment : Rule.Selector)
		{
			FWebToUECompiledSelectorSegment& SerializedSegment = SerializedRule.Selector.AddDefaulted_GetRef();
			SerializedSegment.Type = Segment.Type;
			SerializedSegment.Id = Segment.Id;
			SerializedSegment.Classes = Segment.Classes;
			SerializedSegment.RequiredState = static_cast<uint8>(Segment.RequiredState);
			SerializedSegment.RelationToPrevious = static_cast<uint8>(Segment.RelationToPrevious);
		}
		for (const FWebToUEStyleDeclaration& Declaration : Rule.Declarations)
		{
			FWebToUECompiledDeclaration& SerializedDeclaration = SerializedRule.Declarations.AddDefaulted_GetRef();
			SerializedDeclaration.Property = Declaration.Property;
			SerializedDeclaration.TypedValue = Declaration.TypedValue;
		}
	}

	const auto ToTransitionTargetKind = [](EWebToUECssProperty Property)
	{
		switch (Property)
		{
		case EWebToUECssProperty::Opacity:
			return EWebToUECompiledAnimationTargetKind::Opacity;
		case EWebToUECssProperty::Color:
			return EWebToUECompiledAnimationTargetKind::Color;
		case EWebToUECssProperty::BackgroundColor:
			return EWebToUECompiledAnimationTargetKind::BackgroundColor;
		case EWebToUECssProperty::BorderColor:
			return EWebToUECompiledAnimationTargetKind::BorderColor;
		case EWebToUECssProperty::Transform:
			return EWebToUECompiledAnimationTargetKind::VisualTransform;
		default:
			return EWebToUECompiledAnimationTargetKind::Invalid;
		}
	};
	Source.ForEachNode([&](FWebToUENode& Node)
	{
		const int32* NodeIndex = CompiledNodeIndices.Find(&Node);
		if (!NodeIndex) return;
		for (const FWebToUETransitionItem& Item :
			Source.GetComputedStyle(Node).Transition.Items)
		{
			const EWebToUECompiledAnimationTargetKind Kind =
				ToTransitionTargetKind(Item.Property);
			if (Kind == EWebToUECompiledAnimationTargetKind::Invalid) continue;
			FWebToUECompiledTransition& Transition =
				Result.AnimationIR.Transitions.AddDefaulted_GetRef();
			Transition.TransitionId = FName(*FString::Printf(
				TEXT("transition.%08d.%s"), *NodeIndex,
				WebToUE::Private::LexToString(Item.Property)));
			Transition.Target.TargetNodeIndex = *NodeIndex;
			Transition.Target.Kind = Kind;
			Transition.DurationSeconds = Item.DurationSeconds;
			Transition.DelaySeconds = Item.DelaySeconds;
			Transition.Easing = Item.Easing;
		}
	});
	Result.AnimationIR.Transitions.Sort([](
		const FWebToUECompiledTransition& A,
		const FWebToUECompiledTransition& B)
	{
		if (A.Target.TargetNodeIndex != B.Target.TargetNodeIndex)
		{
			return A.Target.TargetNodeIndex < B.Target.TargetNodeIndex;
		}
		return static_cast<uint8>(A.Target.Kind) <
			static_cast<uint8>(B.Target.Kind);
	});

	const auto AddResource = [&Result, &FindResolvedTexture, &FindResolvedMaterial](
		EWebToUEResourceKind Kind,
		const FString& AuthorReference, EWebToUEResidencyClass Residency)
	{
		if (Kind == EWebToUEResourceKind::Texture)
		{
			const WebToUE::ResourceImport::Private::FResolvedTextureReference*
				Resolved = FindResolvedTexture(AuthorReference);
			if (!Resolved)
			{
				return;
			}
			if (FWebToUECompiledResource* Existing =
				Result.ResourceManifest.FindByPredicate(
					[Resolved](const FWebToUECompiledResource& Resource)
					{
						return Resource.Kind == EWebToUEResourceKind::Texture &&
							Resource.ResourceId == Resolved->ResourceId;
					}))
			{
				if (WebToUE::ResourceImport::Private::ResidencyRank(Residency) <
					WebToUE::ResourceImport::Private::ResidencyRank(Existing->Residency))
				{
					Existing->Residency = Residency;
				}
				return;
			}
			FWebToUECompiledResource Resource;
			Resource.Kind = Kind;
			Resource.Path = Resolved->Path;
			Resource.ResourceId = Resolved->ResourceId;
			Resource.Provenance = Resolved->Provenance;
			Resource.GroupId = TEXT("document/images");
			Resource.Residency = Residency;
			Resource.IntrinsicSize = Resolved->IntrinsicSize;
			Result.ResourceManifest.Add(MoveTemp(Resource));
			return;
		}
		if (Kind == EWebToUEResourceKind::Material)
		{
			const WebToUE::ResourceImport::Private::FResolvedMaterialReference*
				Resolved = FindResolvedMaterial(AuthorReference);
			if (!Resolved)
			{
				return;
			}
			if (FWebToUECompiledResource* Existing =
				Result.ResourceManifest.FindByPredicate(
					[Resolved](const FWebToUECompiledResource& Resource)
					{
						return Resource.Kind == EWebToUEResourceKind::Material &&
							Resource.ResourceId == Resolved->ResourceId;
					}))
			{
				if (WebToUE::ResourceImport::Private::ResidencyRank(Residency) <
					WebToUE::ResourceImport::Private::ResidencyRank(Existing->Residency))
				{
					Existing->Residency = Residency;
				}
				return;
			}
			FWebToUECompiledResource Resource;
			Resource.Kind = Kind;
			Resource.Path = Resolved->Path;
			Resource.ResourceId = Resolved->ResourceId;
			Resource.Provenance = Resolved->Provenance;
			Resource.GroupId = TEXT("document/materials");
			Resource.Residency = Residency;
			Resource.BrushImageSize = Resolved->BrushImageSize;
			Result.ResourceManifest.Add(MoveTemp(Resource));
			return;
		}

		const FSoftObjectPath Path(AuthorReference);
		if (!Path.IsValid()) return;
		if (FWebToUECompiledResource* Existing = Result.ResourceManifest.FindByPredicate(
			[Kind, &Path](const FWebToUECompiledResource& Resource)
			{
				return Resource.Kind == Kind && Resource.Path == Path;
			}))
		{
			return;
		}
		FWebToUECompiledResource Resource;
		Resource.Kind = Kind;
		Resource.Path = Path;
		Result.ResourceManifest.Add(MoveTemp(Resource));
	};
	const UWebToUESettings* Settings = GetDefault<UWebToUESettings>();
	Source.ForEachNode([&Source, &AddResource, Settings](FWebToUENode& Node)
	{
		if (Node.Tag == TEXT("img"))
		{
			AddResource(EWebToUEResourceKind::Texture,
				Node.GetAttribute(TEXT("src")),
				WebToUE::ResourceImport::Private::ParseImageResidency(
					Node.GetAttribute(TEXT("data-ue-residency"))));
		}
		const FString Material = Node.GetAttribute(TEXT("data-ue-material"));
		if (!Material.IsEmpty())
		{
			AddResource(EWebToUEResourceKind::Material, Material,
				WebToUE::ResourceImport::Private::ParseImageResidency(
					Node.GetAttribute(TEXT("data-ue-residency"))));
		}
		const FString StringTable = Node.GetAttribute(TEXT("data-ue-string-table"));
		if (!StringTable.IsEmpty())
		{
			AddResource(EWebToUEResourceKind::StringTable, StringTable,
				EWebToUEResidencyClass::Invalid);
		}
		AddResource(EWebToUEResourceKind::Font,
			Settings->FindFontObjectPath(Source.GetComputedStyle(Node).FontFamily).ToString(),
			EWebToUEResidencyClass::Invalid);
	});
	return Result;
}

static bool BuildResourceContract(const TArray<FString>& DependencyFiles,
	FWebToUECompiledDocumentData& InOutDocument,
	TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
{
	using namespace WebToUE::ResourceImport::Private;
	OutDiagnostics.Reset();
	FWebToUEResourceContractDescriptor Descriptor;
	Descriptor.ContractVersion = { 1, 0 };
	Descriptor.CompilerFingerprintBlake3 = CompilerFingerprint();
	Descriptor.ArtifactVersions = CurrentArtifactVersions();

	for (int32 Index = 0; Index < DependencyFiles.Num(); ++Index)
	{
		FString LogicalId;
		FString ContentHash;
		if (!MakeLogicalSourceId(DependencyFiles[Index], LogicalId) ||
			!HashFile(DependencyFiles[Index], ContentHash))
		{
			OutDiagnostics.Add({ TEXT("WTUE-RES-001"), TEXT("dependencies.source"),
				FString::Printf(TEXT("Source dependency is outside the project or unreadable: %s"),
					*DependencyFiles[Index]) });
			continue;
		}
		if (Index == 0)
		{
			Descriptor.DocumentId = TEXT("document/") + HashUtf8(LogicalId);
		}
		const EWebToUEResourceDependencyKind Kind = Index == 0
			? EWebToUEResourceDependencyKind::UiSource
			: FPaths::GetExtension(DependencyFiles[Index]).Equals(
				TEXT("css"), ESearchCase::IgnoreCase)
				? EWebToUEResourceDependencyKind::StyleSource
				: EWebToUEResourceDependencyKind::Resource;
		Descriptor.Dependencies.Add({ MoveTemp(LogicalId), Kind,
			MoveTemp(ContentHash) });
	}

	for (const FWebToUECompiledResource& Resource : InOutDocument.ResourceManifest)
	{
		if (Resource.Kind != EWebToUEResourceKind::Texture &&
			Resource.Kind != EWebToUEResourceKind::Material)
		{
			continue;
		}
		if (Resource.Kind == EWebToUEResourceKind::Material)
		{
			AddMaterialDependencyClosure(Resource.Path,
				Descriptor.Dependencies, OutDiagnostics);
		}
		else
		{
			FString AssetHash;
			if (!HashAssetPackage(Resource.Path, AssetHash))
			{
				OutDiagnostics.Add({ TEXT("WTUE-RES-001"),
					FString::Printf(TEXT("resources.%s"), *Resource.ResourceId),
					FString::Printf(TEXT("Unreal texture package is missing or has no saved content fingerprint: %s"),
						*Resource.Path.ToString()) });
			}
			const FString PackageDependencyId =
				Resource.Provenance.Origin == EWebToUEResourceOrigin::UnrealAsset
					? MakeAssetDependencyId(Resource.Path)
					: Resource.Provenance.ResolvedDependencyId;
			const EWebToUEResourceDependencyKind PackageDependencyKind =
				Resource.Provenance.Origin == EWebToUEResourceOrigin::UnrealAsset
					? EWebToUEResourceDependencyKind::Resource
					: EWebToUEResourceDependencyKind::GeneratedInput;
			if (!AssetHash.IsEmpty() && !Descriptor.Dependencies.ContainsByPredicate(
				[&PackageDependencyId](const FWebToUEResourceDependency& Dependency)
				{
					return Dependency.LogicalId == PackageDependencyId;
				}))
			{
				Descriptor.Dependencies.Add({ PackageDependencyId,
					PackageDependencyKind, MoveTemp(AssetHash) });
			}
		}
		Descriptor.Resources.Add({ Resource.ResourceId, Resource.Provenance });
		Descriptor.ResidencyAssignments.Add({ Resource.ResourceId, FString(),
			Resource.GroupId, Resource.Residency });
	}

	FWebToUEResourceContractSnapshot Snapshot;
	TArray<FWebToUEResourceContractDiagnostic> PolicyDiagnostics;
	const bool bPolicyValid = FWebToUEResourceContractPolicy::BuildSnapshot(
		Descriptor, Snapshot, PolicyDiagnostics);
	OutDiagnostics.Append(MoveTemp(PolicyDiagnostics));
	OutDiagnostics.Sort([](const FWebToUEResourceContractDiagnostic& A,
		const FWebToUEResourceContractDiagnostic& B)
	{
		if (A.Code != B.Code) return A.Code < B.Code;
		if (A.Path != B.Path) return A.Path < B.Path;
		return A.Detail < B.Detail;
	});
	if (!bPolicyValid || !OutDiagnostics.IsEmpty())
	{
		return false;
	}
	InOutDocument.SealedResourceDependencies = MoveTemp(Snapshot.Dependencies);
	InOutDocument.ResourceFreshness = MoveTemp(Snapshot.Freshness);
	return true;
}

bool UWebToUEFactory::ValidateCookFreshness(const UWebToUEDocument& Document,
	TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
{
#if WITH_EDITORONLY_DATA
	FWebToUECompiledDocumentData ExpectedDocument;
	ExpectedDocument.ResourceManifest = Document.GetResourceManifest();
	if (!BuildResourceContract(
		Document.DependencyFiles, ExpectedDocument, OutDiagnostics))
	{
		OutDiagnostics.Add({ TEXT("WTUE-RES-004"), TEXT("cook-validator"),
			TEXT("Cook cannot prove Resource freshness because the current dependency closure cannot be rebuilt.") });
		return false;
	}
	TArray<FWebToUEResourceContractDiagnostic> FreshnessDiagnostics;
	const bool bFresh = FWebToUEResourceContractPolicy::IsCookFresh(
		ExpectedDocument.ResourceFreshness, Document.GetResourceFreshness(),
		FreshnessDiagnostics);
	OutDiagnostics.Append(MoveTemp(FreshnessDiagnostics));
	OutDiagnostics.Sort([](const FWebToUEResourceContractDiagnostic& A,
		const FWebToUEResourceContractDiagnostic& B)
	{
		if (A.Code != B.Code) return A.Code < B.Code;
		if (A.Path != B.Path) return A.Path < B.Path;
		return A.Detail < B.Detail;
	});
	return bFresh && OutDiagnostics.IsEmpty();
#else
	OutDiagnostics = { { TEXT("WTUE-RES-004"), TEXT("cook-validator"),
		TEXT("Editor-only source inputs are unavailable for Cook freshness validation.") } };
	return false;
#endif
}

bool UWebToUEFactory::ImportIntoDocument(UWebToUEDocument& Document, const FString& Filename, bool bPreserveLastGood)
{
	FString Html;
	if (!FFileHelper::LoadFileToString(Html, *Filename))
	{
		const FString Message = FString::Printf(TEXT("Could not read WebToUE document '%s'."), *Filename);
		UE_LOG(LogWebToUEImport, Error, TEXT("%s"), *Message);
		Document.Diagnostics.Reset();
		FWebToUEAssetDiagnostic& Diagnostic = Document.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Severity = EWebToUEAssetDiagnosticSeverity::Error;
		Diagnostic.File = Filename;
		Diagnostic.Line = 1;
		Diagnostic.Column = 1;
		Diagnostic.Message = Message;
		if (!bPreserveLastGood || Document.GetCompiledNodes().IsEmpty())
		{
			Document.CompiledHtml.Reset();
			Document.CompiledCss.Reset();
			FWebToUECompiledDocumentData EmptyDocument;
			EmptyDocument.LocalizationNamespace = Document.GetLocalizationNamespace();
			Document.CommitCompiledDocument(MoveTemp(EmptyDocument));
		}
		Document.MarkPackageDirty();
		Document.NotifyDocumentChanged();
		return false;
	}

	const TSharedRef<FWebToUEDocument> LinkScan = FWebToUECompiler::Compile(Html, FString(), Filename);
	FString CombinedCss;
	TArray<FWebToUEStyleSheetSource> ExternalStyleSheets;
	TArray<FString> Dependencies;
	Dependencies.Add(FPaths::ConvertRelativePathToFull(Filename));
	TArray<FWebToUEDiagnostic> LinkErrors;
	for (const FString& RelativeHref : LinkScan->LinkedStylesheets)
	{
		FString CssPath = RelativeHref;
		if (FPaths::IsRelative(CssPath)) CssPath = FPaths::Combine(FPaths::GetPath(Filename), CssPath);
		CssPath = FPaths::ConvertRelativePathToFull(CssPath);
		FPaths::NormalizeFilename(CssPath);
		FString Css;
		if (FFileHelper::LoadFileToString(Css, *CssPath))
		{
			CombinedCss += FString::Printf(TEXT("\n/* %s */\n%s\n"), *CssPath, *Css);
			ExternalStyleSheets.Add({ MoveTemp(Css), CssPath, 1, 1 });
			Dependencies.AddUnique(CssPath);
		}
		else
		{
			LinkErrors.Add({ EWebToUEDiagnosticSeverity::Error, CssPath, 1, 1,
				FString::Printf(TEXT("Could not read linked stylesheet '%s'."), *RelativeHref) });
		}
	}

	const TSharedRef<FWebToUEDocument> Compiled = FWebToUECompiler::Compile(Html, ExternalStyleSheets, Filename);
	Compiled->Diagnostics.Append(LinkErrors);
	Document.Diagnostics.Reset();
	for (const FWebToUEDiagnostic& Diagnostic : Compiled->Diagnostics)
	{
		Document.Diagnostics.Add(ConvertDiagnostic(Diagnostic));
		if (Diagnostic.Severity == EWebToUEDiagnosticSeverity::Error)
		{
			UE_LOG(LogWebToUEImport, Error, TEXT("%s(%d,%d): %s"), *Diagnostic.File, Diagnostic.Line, Diagnostic.Column, *Diagnostic.Message);
		}
		else if (Diagnostic.Severity == EWebToUEDiagnosticSeverity::Warning)
		{
			UE_LOG(LogWebToUEImport, Warning, TEXT("%s(%d,%d): %s"), *Diagnostic.File, Diagnostic.Line, Diagnostic.Column, *Diagnostic.Message);
		}
	}

	bool bHasErrors = Compiled->HasErrors();
	FWebToUECompiledDocumentData CompiledDocument;
	if (!bHasErrors)
	{
		FString SourceUnit;
		WebToUE::ResourceImport::Private::MakeLogicalSourceId(Filename, SourceUnit);
		TArray<FWebToUEResourceContractDiagnostic> ResourceDiagnostics;
		TMap<FString, WebToUE::ResourceImport::Private::FResolvedTextureReference>
			ResolvedTextures;
		TMap<FString, FString> TextureIdentities;
		TMap<FString, WebToUE::ResourceImport::Private::FResolvedMaterialReference>
			ResolvedMaterials;
		const bool bTexturesResolved =
			WebToUE::ResourceImport::Private::ResolveDocumentTextures(
				*Compiled, Filename, SourceUnit, ResolvedTextures,
				TextureIdentities, Dependencies, ResourceDiagnostics);
		const bool bMaterialsResolved =
			WebToUE::ResourceImport::Private::ResolveDocumentMaterials(
				*Compiled, SourceUnit, ResolvedMaterials, ResourceDiagnostics);
		if (bTexturesResolved && bMaterialsResolved)
		{
			CompiledDocument = BuildCompiledDocument(*Compiled, Document, SourceUnit,
				ResolvedTextures, TextureIdentities, ResolvedMaterials);
		}
		if (!bTexturesResolved || !bMaterialsResolved || !BuildResourceContract(
			Dependencies, CompiledDocument, ResourceDiagnostics))
		{
			bHasErrors = true;
		}
		for (const FWebToUEResourceContractDiagnostic& ResourceDiagnostic : ResourceDiagnostics)
		{
			FWebToUEAssetDiagnostic& Diagnostic = Document.Diagnostics.AddDefaulted_GetRef();
			Diagnostic.Severity = EWebToUEAssetDiagnosticSeverity::Error;
			Diagnostic.File = Filename;
			Diagnostic.Line = 1;
			Diagnostic.Column = 1;
			Diagnostic.Message = FString::Printf(TEXT("%s %s: %s"),
				*ResourceDiagnostic.Code, *ResourceDiagnostic.Path, *ResourceDiagnostic.Detail);
			UE_LOG(LogWebToUEImport, Error, TEXT("%s(1,1): %s"),
				*Filename, *Diagnostic.Message);
		}
	}
	if (!bHasErrors || !bPreserveLastGood || Document.GetCompiledNodes().IsEmpty())
	{
		Document.CompiledHtml = bHasErrors ? FString() : Html;
		Document.CompiledCss = bHasErrors ? FString() : CombinedCss;
		if (bHasErrors)
		{
			FWebToUECompiledDocumentData EmptyDocument;
			EmptyDocument.LocalizationNamespace = Document.GetLocalizationNamespace();
			Document.CommitCompiledDocument(MoveTemp(EmptyDocument));
		}
		else
		{
			Document.CommitCompiledDocument(MoveTemp(CompiledDocument));
			Document.MarkRecompiled();
		}
	}

#if WITH_EDITORONLY_DATA
	if (!Document.AssetImportData) Document.AssetImportData = NewObject<UAssetImportData>(&Document, TEXT("AssetImportData"));
	Document.AssetImportData->Update(Filename);
	if (!bHasErrors || !bPreserveLastGood || Document.GetCompiledNodes().IsEmpty())
	{
		Document.DependencyFiles = MoveTemp(Dependencies);
	}
#endif
	Document.MarkPackageDirty();
	Document.NotifyDocumentChanged();
	return !bHasErrors;
}

UObject* UWebToUEFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(InParent, InClass, InName, Flags);
	ImportIntoDocument(*Document, Filename, false);
	return Document;
}

bool UWebToUEFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	if (const UWebToUEDocument* Document = Cast<UWebToUEDocument>(Obj))
	{
#if WITH_EDITORONLY_DATA
		if (Document->AssetImportData)
		{
			Document->AssetImportData->ExtractFilenames(OutFilenames);
			return OutFilenames.Num() > 0;
		}
#endif
	}
	return false;
}

void UWebToUEFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
#if WITH_EDITORONLY_DATA
	if (UWebToUEDocument* Document = Cast<UWebToUEDocument>(Obj); Document && Document->AssetImportData && NewReimportPaths.Num() == 1)
	{
		Document->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
#endif
}

EReimportResult::Type UWebToUEFactory::Reimport(UObject* Obj)
{
	UWebToUEDocument* Document = Cast<UWebToUEDocument>(Obj);
	if (!Document) return EReimportResult::Failed;
#if WITH_EDITORONLY_DATA
	if (!Document->AssetImportData) return EReimportResult::Failed;
	const FString Filename = Document->AssetImportData->GetFirstFilename();
	Document->PreEditChange(nullptr);
	const bool bSucceeded = ImportIntoDocument(*Document, Filename, true);
	Document->PostEditChange();
	return bSucceeded ? EReimportResult::Succeeded : EReimportResult::Failed;
#else
	return EReimportResult::Failed;
#endif
}
