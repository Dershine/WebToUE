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

		FString SourceUnit = TEXT("source/runtime-test.html");
		for (const FWebToUECompiledResource& Resource : Document.ResourceManifest)
		{
			if (Resource.Kind == EWebToUEResourceKind::Texture &&
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
			if (Resource.Kind != EWebToUEResourceKind::Texture)
			{
				continue;
			}
			if (Resource.ResourceId.IsEmpty())
			{
				Resource.ResourceId = FString::Printf(
					TEXT("resource/texture/fixture-%d"), Index);
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
				Resource.GroupId = TEXT("document/images");
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
			if (Node.Tag != TEXT("img") || !Node.ResourceId.IsEmpty())
			{
				continue;
			}
			const FWebToUECompiledAttribute* Source = Node.Attributes.FindByPredicate(
				[](const FWebToUECompiledAttribute& Attribute)
				{
					return Attribute.Name == TEXT("src");
				});
			if (!Source)
			{
				continue;
			}
			const FWebToUECompiledResource* Resource =
				Document.ResourceManifest.FindByPredicate(
					[&Source](const FWebToUECompiledResource& Candidate)
					{
						return Candidate.Kind == EWebToUEResourceKind::Texture &&
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
