#include "WebToUEResourceContract.h"

#include "Hash/Blake3.h"

namespace WebToUE::ResourceContract::Private
{
	static constexpr uint16 SupportedContractMajor = 1;
	static constexpr uint16 SupportedContractMinor = 0;

	static void AddDiagnostic(
		TArray<FWebToUEResourceContractDiagnostic>& Diagnostics,
		const TCHAR* Code,
		FString Path,
		FString Detail)
	{
		Diagnostics.Add({ Code, MoveTemp(Path), MoveTemp(Detail) });
	}

	static void SortDiagnostics(TArray<FWebToUEResourceContractDiagnostic>& Diagnostics)
	{
		Diagnostics.Sort([](const FWebToUEResourceContractDiagnostic& A,
			const FWebToUEResourceContractDiagnostic& B)
		{
			if (A.Code != B.Code) return A.Code < B.Code;
			if (A.Path != B.Path) return A.Path < B.Path;
			return A.Detail < B.Detail;
		});
	}

	static bool IsCanonicalLogicalId(const FString& Value, bool bAllowEmpty = false)
	{
		if (Value.IsEmpty()) return bAllowEmpty;
		if (Value.TrimStartAndEnd() != Value || Value.StartsWith(TEXT("/")) ||
			Value.EndsWith(TEXT("/")) || Value.Contains(TEXT("\\")) ||
			Value.Contains(TEXT("://")) || Value.Contains(TEXT("//")) ||
			(Value.Len() >= 2 && FChar::IsAlpha(Value[0]) && Value[1] == TEXT(':')))
		{
			return false;
		}
		TArray<FString> Segments;
		Value.ParseIntoArray(Segments, TEXT("/"), false);
		return Segments.Num() > 0 && !Segments.ContainsByPredicate([](const FString& Segment)
		{
			return Segment.IsEmpty() || Segment == TEXT(".") || Segment == TEXT("..");
		});
	}

	static bool IsCanonicalBlake3(const FString& Value)
	{
		if (Value.Len() != 64) return false;
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

	static bool IsSafeAuthorReference(const FString& Value)
	{
		return !Value.IsEmpty() && Value.TrimStartAndEnd() == Value &&
			!Value.Contains(TEXT("\\")) && !Value.Contains(TEXT("://")) &&
			!(Value.Len() >= 2 && FChar::IsAlpha(Value[0]) && Value[1] == TEXT(':'));
	}

	static bool IsValidOriginReference(
		EWebToUEResourceOrigin Origin,
		const FString& Reference)
	{
		switch (Origin)
		{
		case EWebToUEResourceOrigin::UnrealAsset:
			return Reference.StartsWith(TEXT("/Game/")) ||
				Reference.StartsWith(TEXT("/Engine/"));
		case EWebToUEResourceOrigin::RelativeSource:
			return IsCanonicalLogicalId(Reference);
		case EWebToUEResourceOrigin::Generated:
			return Reference.StartsWith(TEXT("generated:")) &&
				IsCanonicalLogicalId(Reference.RightChop(10));
		default:
			return false;
		}
	}

	static bool IsValidLayerVersion(
		const FWebToUEArtifactLayerVersion& Version,
		bool bRequired)
	{
		return Version.IsPresent() || (!bRequired && Version.IsCanonicalAbsent());
	}

	static bool ValidateVersions(
		const FWebToUEArtifactVersionSet& Versions,
		TArray<FWebToUEResourceContractDiagnostic>& Diagnostics)
	{
		const struct
		{
			const TCHAR* Name;
			const FWebToUEArtifactLayerVersion* Version;
			bool bRequired;
		} Layers[] = {
			{ TEXT("ui-ir"), &Versions.UiIr, true },
			{ TEXT("resource-ir"), &Versions.ResourceIr, true },
			{ TEXT("behavior-ir"), &Versions.BehaviorIr, false },
			{ TEXT("animation-ir"), &Versions.AnimationIr, false },
			{ TEXT("interop-schema"), &Versions.InteropSchema, false },
		};
		for (const auto& Layer : Layers)
		{
			if (!IsValidLayerVersion(*Layer.Version, Layer.bRequired))
			{
				AddDiagnostic(Diagnostics, TEXT("WTUE-RES-005"),
					FString::Printf(TEXT("versions.%s"), Layer.Name),
					TEXT("Required layers need a non-zero major; absent optional layers must be exactly 0.0."));
			}
		}
		return Diagnostics.IsEmpty();
	}

	static void AppendField(FString& Canonical, const TCHAR* Name, const FString& Value)
	{
		Canonical += FString::Printf(TEXT("%s=%d:%s\n"), Name, Value.Len(), *Value);
	}

	static void AppendField(FString& Canonical, const TCHAR* Name, uint64 Value)
	{
		Canonical += FString::Printf(TEXT("%s=%llu\n"), Name,
			static_cast<unsigned long long>(Value));
	}

	static void AppendVersion(
		FString& Canonical,
		const TCHAR* Name,
		const FWebToUEArtifactLayerVersion& Version)
	{
		AppendField(Canonical, *FString::Printf(TEXT("%s.major"), Name), Version.Major);
		AppendField(Canonical, *FString::Printf(TEXT("%s.minor"), Name), Version.Minor);
	}

	static void AppendVersions(FString& Canonical, const FWebToUEArtifactVersionSet& Versions)
	{
		AppendVersion(Canonical, TEXT("ui"), Versions.UiIr);
		AppendVersion(Canonical, TEXT("resource"), Versions.ResourceIr);
		AppendVersion(Canonical, TEXT("behavior"), Versions.BehaviorIr);
		AppendVersion(Canonical, TEXT("animation"), Versions.AnimationIr);
		AppendVersion(Canonical, TEXT("schema"), Versions.InteropSchema);
	}

	static FString HashCanonicalUtf8(const FString& Canonical)
	{
		const FTCHARToUTF8 Utf8(*Canonical);
		return LexToString(FBlake3::HashBuffer(Utf8.Get(), Utf8.Length())).ToLower();
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

	static bool IsSourceKind(EWebToUEResourceDependencyKind Kind)
	{
		return Kind == EWebToUEResourceDependencyKind::UiSource ||
			Kind == EWebToUEResourceDependencyKind::StyleSource ||
			Kind == EWebToUEResourceDependencyKind::BehaviorSource ||
			Kind == EWebToUEResourceDependencyKind::GeneratedInput;
	}

	static bool IsResolvedResourceKind(EWebToUEResourceDependencyKind Kind)
	{
		return Kind == EWebToUEResourceDependencyKind::Resource ||
			Kind == EWebToUEResourceDependencyKind::GeneratedInput;
	}

	static void CompareFreshnessField(
		bool bEqual,
		const TCHAR* Path,
		TArray<FWebToUEResourceContractDiagnostic>& Diagnostics)
	{
		if (!bEqual)
		{
			AddDiagnostic(Diagnostics, TEXT("WTUE-RES-004"), Path,
				TEXT("Compiled artifact is stale for the current sealed Cook inputs."));
		}
	}

	static bool CheckLayerCompatibility(
		const TCHAR* Name,
		const FWebToUEArtifactLayerVersion& Produced,
		const FWebToUEArtifactLayerVersion& Supported,
		TArray<FWebToUEResourceContractDiagnostic>& Diagnostics)
	{
		if (!Produced.IsPresent()) return true;
		if (!Supported.IsPresent() || Produced.Major != Supported.Major ||
			Produced.Minor > Supported.Minor)
		{
			AddDiagnostic(Diagnostics, TEXT("WTUE-RES-005"),
				FString::Printf(TEXT("versions.%s"), Name),
				FString::Printf(TEXT("Producer %u.%u is not supported by consumer %u.%u."),
					Produced.Major, Produced.Minor, Supported.Major, Supported.Minor));
			return false;
		}
		return true;
	}
}

void FWebToUEResourceContractSnapshot::Reset()
{
	Dependencies.Reset();
	Resources.Reset();
	ResidencyAssignments.Reset();
	Freshness = {};
}

bool FWebToUEResourceContractPolicy::BuildSnapshot(
	const FWebToUEResourceContractDescriptor& Descriptor,
	FWebToUEResourceContractSnapshot& OutSnapshot,
	TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
{
	using namespace WebToUE::ResourceContract::Private;
	OutSnapshot.Reset();
	OutDiagnostics.Reset();

	if (Descriptor.ContractVersion.Major != SupportedContractMajor ||
		Descriptor.ContractVersion.Minor > SupportedContractMinor)
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-005"), TEXT("contract-version"),
			TEXT("Resource contract policy version is unsupported."));
	}
	if (!IsCanonicalLogicalId(Descriptor.DocumentId))
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-001"), TEXT("document-id"),
			TEXT("Document identity must be a canonical logical identifier."));
	}
	if (!IsCanonicalBlake3(Descriptor.CompilerFingerprintBlake3))
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-001"), TEXT("compiler-fingerprint"),
			TEXT("Compiler fingerprint must be lowercase BLAKE3-256."));
	}
	ValidateVersions(Descriptor.ArtifactVersions, OutDiagnostics);

	OutSnapshot.Dependencies = Descriptor.Dependencies;
	OutSnapshot.Dependencies.Sort([](const FWebToUEResourceDependency& A,
		const FWebToUEResourceDependency& B)
	{
		if (A.LogicalId != B.LogicalId) return A.LogicalId < B.LogicalId;
		return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind);
	});
	TMap<FString, EWebToUEResourceDependencyKind> DependencyKinds;
	for (const FWebToUEResourceDependency& Dependency : OutSnapshot.Dependencies)
	{
		if (!IsCanonicalLogicalId(Dependency.LogicalId) ||
			Dependency.Kind == EWebToUEResourceDependencyKind::Invalid ||
			!IsCanonicalBlake3(Dependency.ContentHashBlake3))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-001"),
				TEXT("dependencies/") + Dependency.LogicalId,
				TEXT("Dependency requires a canonical logical identity, kind and lowercase BLAKE3-256."));
			continue;
		}
		if (DependencyKinds.Contains(Dependency.LogicalId))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-002"),
				TEXT("dependencies/") + Dependency.LogicalId,
				TEXT("Dependency identity is duplicated."));
			continue;
		}
		DependencyKinds.Add(Dependency.LogicalId, Dependency.Kind);
	}
	if (OutSnapshot.Dependencies.IsEmpty())
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-001"), TEXT("dependencies"),
			TEXT("A sealed document requires at least one hashed dependency."));
	}

	OutSnapshot.Resources = Descriptor.Resources;
	OutSnapshot.Resources.Sort([](const FWebToUEResourceDescriptor& A,
		const FWebToUEResourceDescriptor& B)
	{
		return A.ResourceId < B.ResourceId;
	});
	TSet<FString> ResourceIds;
	for (const FWebToUEResourceDescriptor& Resource : OutSnapshot.Resources)
	{
		const FString Path = TEXT("resources/") + Resource.ResourceId;
		if (!IsCanonicalLogicalId(Resource.ResourceId) ||
			Resource.Provenance.Origin == EWebToUEResourceOrigin::Invalid ||
			!IsCanonicalLogicalId(Resource.Provenance.SourceUnit) ||
			!IsSafeAuthorReference(Resource.Provenance.AuthorReference) ||
			!IsValidOriginReference(Resource.Provenance.Origin,
				Resource.Provenance.AuthorReference) ||
			!IsCanonicalLogicalId(Resource.Provenance.ResolvedDependencyId))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-001"), Path,
				TEXT("Resource identity and provenance must be logical, complete and non-networked."));
			continue;
		}
		if (ResourceIds.Contains(Resource.ResourceId))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-002"), Path,
				TEXT("Resource identity is duplicated inside the document."));
			continue;
		}
		ResourceIds.Add(Resource.ResourceId);
		const EWebToUEResourceDependencyKind* SourceKind =
			DependencyKinds.Find(Resource.Provenance.SourceUnit);
		const EWebToUEResourceDependencyKind* ResolvedKind =
			DependencyKinds.Find(Resource.Provenance.ResolvedDependencyId);
		if (!SourceKind || !IsSourceKind(*SourceKind) ||
			!ResolvedKind || !IsResolvedResourceKind(*ResolvedKind))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-001"), Path,
				TEXT("Provenance must reference a sealed source dependency and resolved resource input."));
		}
	}

	OutSnapshot.ResidencyAssignments = Descriptor.ResidencyAssignments;
	OutSnapshot.ResidencyAssignments.Sort([](const FWebToUEResidencyAssignment& A,
		const FWebToUEResidencyAssignment& B)
	{
		if (A.RouteId != B.RouteId) return A.RouteId < B.RouteId;
		if (A.ResourceId != B.ResourceId) return A.ResourceId < B.ResourceId;
		if (A.GroupId != B.GroupId) return A.GroupId < B.GroupId;
		return static_cast<uint8>(A.Residency) < static_cast<uint8>(B.Residency);
	});
	TSet<FString> AssignmentKeys;
	TMap<FString, EWebToUEResidencyClass> DocumentResidency;
	TSet<FString> AssignedResources;
	for (const FWebToUEResidencyAssignment& Assignment : OutSnapshot.ResidencyAssignments)
	{
		const FString Path = FString::Printf(TEXT("residency/%s/%s"),
			Assignment.RouteId.IsEmpty() ? TEXT("<document>") : *Assignment.RouteId,
			*Assignment.ResourceId);
		if (!ResourceIds.Contains(Assignment.ResourceId) ||
			!IsCanonicalLogicalId(Assignment.RouteId, true) ||
			!IsCanonicalLogicalId(Assignment.GroupId) ||
			ResidencyRank(Assignment.Residency) == MAX_int32)
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-003"), Path,
				TEXT("Residency requires a known resource, canonical scope/group and valid class."));
			continue;
		}
		const FString Key = Assignment.RouteId + TEXT("\n") + Assignment.ResourceId;
		if (AssignmentKeys.Contains(Key))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-002"), Path,
				TEXT("A resource has multiple assignments in the same scope."));
			continue;
		}
		AssignmentKeys.Add(Key);
		AssignedResources.Add(Assignment.ResourceId);
		if (Assignment.RouteId.IsEmpty())
		{
			DocumentResidency.Add(Assignment.ResourceId, Assignment.Residency);
		}
		else if (const EWebToUEResidencyClass* Fallback =
			DocumentResidency.Find(Assignment.ResourceId))
		{
			if (ResidencyRank(Assignment.Residency) > ResidencyRank(*Fallback))
			{
				AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-003"), Path,
					TEXT("A route may promote but cannot demote document residency."));
			}
		}
	}
	for (const FString& ResourceId : ResourceIds)
	{
		if (!AssignedResources.Contains(ResourceId))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-003"),
				TEXT("residency/") + ResourceId,
				TEXT("Every resource needs an explicit document or route residency assignment."));
		}
	}

	SortDiagnostics(OutDiagnostics);
	if (!OutDiagnostics.IsEmpty())
	{
		OutSnapshot.Reset();
		return false;
	}

	FString DependencyCanonical;
	for (const FWebToUEResourceDependency& Dependency : OutSnapshot.Dependencies)
	{
		AppendField(DependencyCanonical, TEXT("id"), Dependency.LogicalId);
		AppendField(DependencyCanonical, TEXT("kind"), static_cast<uint8>(Dependency.Kind));
		AppendField(DependencyCanonical, TEXT("blake3"), Dependency.ContentHashBlake3);
	}

	FString ManifestCanonical;
	AppendField(ManifestCanonical, TEXT("document"), Descriptor.DocumentId);
	AppendVersion(ManifestCanonical, TEXT("contract"), Descriptor.ContractVersion);
	AppendVersions(ManifestCanonical, Descriptor.ArtifactVersions);
	for (const FWebToUEResourceDescriptor& Resource : OutSnapshot.Resources)
	{
		AppendField(ManifestCanonical, TEXT("resource"), Resource.ResourceId);
		AppendField(ManifestCanonical, TEXT("origin"), static_cast<uint8>(Resource.Provenance.Origin));
		AppendField(ManifestCanonical, TEXT("source"), Resource.Provenance.SourceUnit);
		AppendField(ManifestCanonical, TEXT("reference"), Resource.Provenance.AuthorReference);
		AppendField(ManifestCanonical, TEXT("resolved"), Resource.Provenance.ResolvedDependencyId);
	}
	for (const FWebToUEResidencyAssignment& Assignment : OutSnapshot.ResidencyAssignments)
	{
		AppendField(ManifestCanonical, TEXT("assignment.resource"), Assignment.ResourceId);
		AppendField(ManifestCanonical, TEXT("assignment.route"), Assignment.RouteId);
		AppendField(ManifestCanonical, TEXT("assignment.group"), Assignment.GroupId);
		AppendField(ManifestCanonical, TEXT("assignment.class"),
			static_cast<uint8>(Assignment.Residency));
	}

	OutSnapshot.Freshness.ContractVersion = Descriptor.ContractVersion;
	OutSnapshot.Freshness.DocumentId = Descriptor.DocumentId;
	OutSnapshot.Freshness.CompilerFingerprintBlake3 = Descriptor.CompilerFingerprintBlake3;
	OutSnapshot.Freshness.DependencyClosureBlake3 = HashCanonicalUtf8(DependencyCanonical);
	OutSnapshot.Freshness.ResourceManifestBlake3 = HashCanonicalUtf8(ManifestCanonical);
	OutSnapshot.Freshness.ArtifactVersions = Descriptor.ArtifactVersions;
	if (!IsCanonicalBlake3(OutSnapshot.Freshness.DependencyClosureBlake3) ||
		!IsCanonicalBlake3(OutSnapshot.Freshness.ResourceManifestBlake3))
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-RES-001"), TEXT("freshness"),
			TEXT("BLAKE3-256 generation failed."));
		OutSnapshot.Reset();
		return false;
	}
	return true;
}

bool FWebToUEResourceContractPolicy::IsCookFresh(
	const FWebToUECookFreshnessStamp& Expected,
	const FWebToUECookFreshnessStamp& Compiled,
	TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
{
	using namespace WebToUE::ResourceContract::Private;
	OutDiagnostics.Reset();
	CompareFreshnessField(Expected.ContractVersion == Compiled.ContractVersion,
		TEXT("contract-version"), OutDiagnostics);
	CompareFreshnessField(Expected.DocumentId == Compiled.DocumentId,
		TEXT("document-id"), OutDiagnostics);
	CompareFreshnessField(Expected.CompilerFingerprintBlake3 == Compiled.CompilerFingerprintBlake3,
		TEXT("compiler-fingerprint"), OutDiagnostics);
	CompareFreshnessField(Expected.DependencyClosureBlake3 == Compiled.DependencyClosureBlake3,
		TEXT("dependency-closure"), OutDiagnostics);
	CompareFreshnessField(Expected.ResourceManifestBlake3 == Compiled.ResourceManifestBlake3,
		TEXT("resource-manifest"), OutDiagnostics);
	CompareFreshnessField(Expected.ArtifactVersions == Compiled.ArtifactVersions,
		TEXT("artifact-versions"), OutDiagnostics);
	SortDiagnostics(OutDiagnostics);
	return OutDiagnostics.IsEmpty();
}

bool FWebToUEResourceContractPolicy::IsRuntimeCompatible(
	const FWebToUEArtifactVersionSet& Produced,
	const FWebToUEArtifactVersionSet& Supported,
	TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
{
	using namespace WebToUE::ResourceContract::Private;
	OutDiagnostics.Reset();
	CheckLayerCompatibility(TEXT("ui-ir"), Produced.UiIr, Supported.UiIr, OutDiagnostics);
	CheckLayerCompatibility(TEXT("resource-ir"), Produced.ResourceIr, Supported.ResourceIr, OutDiagnostics);
	CheckLayerCompatibility(TEXT("behavior-ir"), Produced.BehaviorIr, Supported.BehaviorIr, OutDiagnostics);
	CheckLayerCompatibility(TEXT("animation-ir"), Produced.AnimationIr, Supported.AnimationIr, OutDiagnostics);
	CheckLayerCompatibility(TEXT("interop-schema"), Produced.InteropSchema, Supported.InteropSchema,
		OutDiagnostics);
	SortDiagnostics(OutDiagnostics);
	return OutDiagnostics.IsEmpty();
}
