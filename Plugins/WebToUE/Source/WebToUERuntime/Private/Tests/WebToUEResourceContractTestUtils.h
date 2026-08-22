#pragma once

#include "WebToUEDocument.h"

namespace WebToUE::Tests
{
	inline void SealResourceContractForTesting(
		FWebToUECompiledDocumentData& Document,
		const FString& DocumentId = TEXT("document/runtime-test"),
		uint16 ResourceIrMinor = 0)
	{
		FWebToUEResourceContractDescriptor Descriptor;
		Descriptor.ContractVersion = { 1, 0 };
		Descriptor.DocumentId = DocumentId;
		Descriptor.CompilerFingerprintBlake3 = FString::ChrN(64, TEXT('d'));
		Descriptor.ArtifactVersions.UiIr = { 1, 0 };
		Descriptor.ArtifactVersions.ResourceIr = { 1, ResourceIrMinor };
		Descriptor.ArtifactVersions.AnimationIr = {
			FWebToUECompiledAnimationIR::CurrentMajor,
			FWebToUECompiledAnimationIR::CurrentMinor };

		FString SourceUnit = TEXT("source/runtime-test.html");
		for (const FWebToUECompiledResource& Resource : Document.ResourceManifest)
		{
			if ((Resource.Kind == EWebToUEResourceKind::Texture ||
				 Resource.Kind == EWebToUEResourceKind::Material) &&
				!Resource.Provenance.SourceUnit.IsEmpty())
			{
				SourceUnit = Resource.Provenance.SourceUnit;
				break;
			}
		}
		Descriptor.Dependencies.Add({ SourceUnit,
			EWebToUEResourceDependencyKind::UiSource,
			FString::ChrN(64, TEXT('a')) });

		for (int32 Index = 0; Index < Document.ResourceManifest.Num(); ++Index)
		{
			FWebToUECompiledResource& Resource = Document.ResourceManifest[Index];
			if (Resource.Kind != EWebToUEResourceKind::Texture &&
				Resource.Kind != EWebToUEResourceKind::Material)
			{
				continue;
			}
			if (Resource.ResourceId.IsEmpty())
			{
				Resource.ResourceId = Resource.Kind == EWebToUEResourceKind::Texture
					? FString::Printf(TEXT("resource/texture/fixture-%d"), Index)
					: FString::Printf(TEXT("resource/material/fixture-%d"), Index);
			}
			Resource.Provenance.Origin = EWebToUEResourceOrigin::UnrealAsset;
			Resource.Provenance.SourceUnit = SourceUnit;
			Resource.Provenance.AuthorReference = Resource.Path.ToString();
			if (Resource.Provenance.ResolvedDependencyId.IsEmpty())
			{
				Resource.Provenance.ResolvedDependencyId = FString::Printf(
					TEXT("asset/runtime-test/fixture-%d"), Index);
			}
			if (Resource.GroupId.IsEmpty())
			{
				Resource.GroupId = Resource.Kind == EWebToUEResourceKind::Texture
					? TEXT("document/images") : TEXT("document/materials");
			}
			if (Resource.Residency == EWebToUEResidencyClass::Invalid)
			{
				Resource.Residency = EWebToUEResidencyClass::Visible;
			}
			Descriptor.Dependencies.Add({ Resource.Provenance.ResolvedDependencyId,
				EWebToUEResourceDependencyKind::Resource,
				FString::ChrN(64, static_cast<TCHAR>(TEXT('b') + Index % 4)) });
			Descriptor.Resources.Add({ Resource.ResourceId, Resource.Provenance });
			Descriptor.ResidencyAssignments.Add({ Resource.ResourceId, FString(),
				Resource.GroupId, Resource.Residency });
		}

		for (FWebToUECompiledNode& Node : Document.Nodes)
		{
			if (!Node.ResourceId.IsEmpty())
			{
				continue;
			}
			const bool bTextureNode = Node.Tag == TEXT("img");
			const FWebToUECompiledAttribute* Source = Node.Attributes.FindByPredicate(
				[bTextureNode](const FWebToUECompiledAttribute& Attribute)
				{
					return Attribute.Name == (bTextureNode
						? TEXT("src") : TEXT("data-ue-material"));
				});
			if (!Source)
			{
				continue;
			}
			const FWebToUECompiledResource* Resource =
				Document.ResourceManifest.FindByPredicate(
					[Source, bTextureNode](const FWebToUECompiledResource& Candidate)
					{
						return Candidate.Kind == (bTextureNode
							? EWebToUEResourceKind::Texture
							: EWebToUEResourceKind::Material) &&
							Candidate.Path == FSoftObjectPath(Source->Value);
					});
			if (Resource)
			{
				Node.ResourceId = Resource->ResourceId;
			}
		}

		FWebToUEResourceContractSnapshot Snapshot;
		TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
		check(FWebToUEResourceContractPolicy::BuildSnapshot(
			Descriptor, Snapshot, Diagnostics));
		Document.SealedResourceDependencies = MoveTemp(Snapshot.Dependencies);
		Document.ResourceFreshness = MoveTemp(Snapshot.Freshness);
	}
}
