#include "WebToUEFeedbackProfile.h"

#include "Hash/Blake3.h"
#include "Misc/PackageName.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWebToUEFeedbackProfile, Log, All);

namespace WebToUE::FeedbackProfile::Private
{
	static constexpr int32 MaximumCueCount = 256;
	static constexpr int32 MaximumVariantsPerCue = 8;

	static FString HashBuffer(const void* Data, uint64 Size)
	{
		return LexToString(FBlake3::HashBuffer(Data, Size)).ToLower();
	}

	static FString HashUtf8(const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		return HashBuffer(Utf8.Get(), Utf8.Length());
	}

	static bool IsNamespacedName(FName Name)
	{
		if (Name.IsNone())
		{
			return false;
		}
		const FString Value = Name.ToString();
		if (!Value.Contains(TEXT(".")) || Value.TrimStartAndEnd() != Value)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (FChar::IsWhitespace(Character) || Character == TEXT('/') ||
				Character == TEXT('\\'))
			{
				return false;
			}
		}
		return true;
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

	static FString MakeProfileSourceId(FName ProfileId)
	{
		return TEXT("profile/") + ProfileId.ToString();
	}

	static FString MakeAssetDependencyId(const FSoftObjectPath& Path)
	{
		FString PackageName = FPackageName::ObjectPathToPackageName(Path.ToString());
		if (PackageName.StartsWith(TEXT("/")))
		{
			PackageName.RightChopInline(1);
		}
		return TEXT("asset/") + PackageName;
	}

	static FString MakeResourceId(const FSoftObjectPath& Path)
	{
		return TEXT("resource/feedback/") + HashUtf8(Path.ToString());
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

	static FString BuildCanonicalProfile(const UWebToUEFeedbackProfile& Profile)
	{
		TArray<const FWebToUEFeedbackCueProfile*> Sorted;
		Sorted.Reserve(Profile.Cues.Num());
		for (const FWebToUEFeedbackCueProfile& Cue : Profile.Cues)
		{
			Sorted.Add(&Cue);
		}
		Sorted.Sort([](const FWebToUEFeedbackCueProfile& A,
			const FWebToUEFeedbackCueProfile& B)
		{
			return A.CueId.LexicalLess(B.CueId);
		});

		FString Canonical = FString::Printf(TEXT("profile=%s\nschema=%u.%u\n"),
			*Profile.ProfileId.ToString(), Profile.SchemaMajor, Profile.SchemaMinor);
		for (const FWebToUEFeedbackCueProfile* Cue : Sorted)
		{
			Canonical += FString::Printf(
				TEXT("cue=%s\nresidency=%u\nvolume=%.9g\npitch=%.9g\n")
				TEXT("cooldown=%.17g\ndedupe=%s\ndedupe_window=%.17g\n")
				TEXT("throttle=%s\nthrottle_max=%d\nthrottle_window=%.17g\n")
				TEXT("world=%u\nproject_route=%s\nconcurrency=%s\n"),
				*Cue->CueId.ToString(), static_cast<uint8>(Cue->Residency),
				Cue->VolumeMultiplier, Cue->PitchMultiplier, Cue->CooldownSeconds,
				*Cue->DeduplicationGroup.ToString(), Cue->DeduplicationWindowSeconds,
				*Cue->ThrottleGroup.ToString(), Cue->ThrottleMaximum,
				Cue->ThrottleWindowSeconds, static_cast<uint8>(Cue->WorldPolicy),
				*Cue->ProjectRouteId.ToString(), *Cue->Concurrency.ToSoftObjectPath().ToString());
			for (int32 Index = 0; Index < Cue->Variants.Num(); ++Index)
			{
				Canonical += FString::Printf(TEXT("variant[%d]=%s\n"), Index,
					*Cue->Variants[Index].ToSoftObjectPath().ToString());
			}
		}
		return Canonical;
	}

	static void AddResourcesToDescriptor(const UWebToUEFeedbackProfile& Profile,
		FWebToUEResourceContractDescriptor& Descriptor)
	{
		struct FResourceUse
		{
			FSoftObjectPath Path;
			EWebToUEResidencyClass Residency = EWebToUEResidencyClass::Lazy;
		};
		TMap<FString, FResourceUse> Uses;
		for (const FWebToUEFeedbackCueProfile& Cue : Profile.Cues)
		{
			const auto AddUse = [&Uses, &Cue](const FSoftObjectPath& Path)
			{
				if (Path.IsNull()) return;
				FResourceUse& Use = Uses.FindOrAdd(Path.ToString());
				Use.Path = Path;
				if (ResidencyRank(Cue.Residency) < ResidencyRank(Use.Residency))
				{
					Use.Residency = Cue.Residency;
				}
			};
			for (const TSoftObjectPtr<USoundBase>& Variant : Cue.Variants)
			{
				AddUse(Variant.ToSoftObjectPath());
			}
			AddUse(Cue.Concurrency.ToSoftObjectPath());
		}

		TArray<FString> Paths;
		Uses.GetKeys(Paths);
		Paths.Sort();
		const FString SourceId = MakeProfileSourceId(Profile.ProfileId);
		for (const FString& PathString : Paths)
		{
			const FResourceUse& Use = Uses.FindChecked(PathString);
			const FString ResourceId = MakeResourceId(Use.Path);
			Descriptor.Resources.Add({ ResourceId, {
				EWebToUEResourceOrigin::UnrealAsset, SourceId, PathString,
				MakeAssetDependencyId(Use.Path) } });
			Descriptor.ResidencyAssignments.Add({ ResourceId, FString(),
				TEXT("feedback/profile"), Use.Residency });
		}
	}

	static FWebToUEResourceContractDescriptor BuildDescriptor(
		const UWebToUEFeedbackProfile& Profile,
		const TArray<FWebToUEResourceDependency>& Dependencies)
	{
		FWebToUEResourceContractDescriptor Descriptor;
		Descriptor.ContractVersion = { 1, 0 };
		Descriptor.DocumentId = TEXT("feedback-profile/") +
			HashUtf8(Profile.ProfileId.ToString());
		Descriptor.CompilerFingerprintBlake3 = HashUtf8(
			TEXT("WebToUE.FeedbackProfile/1;Resource-IR/1.3"));
		Descriptor.ArtifactVersions.UiIr = { 1, 0 };
		Descriptor.ArtifactVersions.ResourceIr = {
			1, UWebToUEFeedbackProfile::ResourceIrMinor };
		Descriptor.Dependencies = Dependencies;

		const FString SourceId = MakeProfileSourceId(Profile.ProfileId);
		const FString SourceHash = HashUtf8(BuildCanonicalProfile(Profile));
		if (FWebToUEResourceDependency* Existing =
			Descriptor.Dependencies.FindByPredicate([&SourceId](
				const FWebToUEResourceDependency& Dependency)
			{
				return Dependency.LogicalId == SourceId;
			}))
		{
			Existing->Kind = EWebToUEResourceDependencyKind::GeneratedInput;
			Existing->ContentHashBlake3 = SourceHash;
		}
		else
		{
			Descriptor.Dependencies.Add({ SourceId,
				EWebToUEResourceDependencyKind::GeneratedInput, SourceHash });
		}
		AddResourcesToDescriptor(Profile, Descriptor);
		return Descriptor;
	}

#if WITH_EDITOR
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

	static bool AddPackageClosure(const FSoftObjectPath& RootPath,
		TArray<FWebToUEResourceDependency>& OutDependencies,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
	{
		const FString RootPackage =
			FPackageName::ObjectPathToPackageName(RootPath.ToString());
		if (!FPackageName::IsValidLongPackageName(RootPackage))
		{
			OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-002"), TEXT("resources"),
				TEXT("Feedback assets require a valid /Game or /Engine package.") });
			return false;
		}

		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FName> Pending { FName(*RootPackage) };
		TSet<FName> Visited;
		bool bValid = true;
		while (!Pending.IsEmpty())
		{
			const FName PackageName = Pending.Pop(EAllowShrinking::No);
			if (Visited.Contains(PackageName)) continue;
			Visited.Add(PackageName);
			const FString PackageString = PackageName.ToString();
			if (!PackageString.StartsWith(TEXT("/Game/")) &&
				!PackageString.StartsWith(TEXT("/Engine/")))
			{
				continue;
			}
			FString Filename;
			FString Hash;
			if (!FPackageName::DoesPackageExist(PackageString, &Filename) ||
				!HashFile(Filename, Hash))
			{
				OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-002"),
					TEXT("dependencies/") + PackageString,
					TEXT("Feedback asset package is missing or has no saved content fingerprint.") });
				bValid = false;
			}
			else
			{
				FString LogicalId = PackageString;
				LogicalId.RightChopInline(1);
				LogicalId = TEXT("asset/") + LogicalId;
				if (!OutDependencies.ContainsByPredicate([&LogicalId](
					const FWebToUEResourceDependency& Dependency)
				{
					return Dependency.LogicalId == LogicalId;
				}))
				{
					OutDependencies.Add({ LogicalId,
						EWebToUEResourceDependencyKind::Resource, MoveTemp(Hash) });
				}
			}

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

	static bool BuildCurrentSnapshot(const UWebToUEFeedbackProfile& Profile,
		FWebToUEResourceContractSnapshot& OutSnapshot,
		TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
	{
		OutDiagnostics.Reset();
		TArray<FWebToUEResourceContractDiagnostic> ProfileDiagnostics;
		if (!Profile.ValidateProfile(ProfileDiagnostics))
		{
			OutDiagnostics = MoveTemp(ProfileDiagnostics);
			return false;
		}

		TArray<FWebToUEResourceDependency> Dependencies;
		TSet<FString> RootPaths;
		for (const FWebToUEFeedbackCueProfile& Cue : Profile.Cues)
		{
			for (const TSoftObjectPtr<USoundBase>& Variant : Cue.Variants)
			{
				RootPaths.Add(Variant.ToSoftObjectPath().ToString());
			}
			if (!Cue.Concurrency.IsNull())
			{
				RootPaths.Add(Cue.Concurrency.ToSoftObjectPath().ToString());
			}
		}
		TArray<FString> SortedPaths = RootPaths.Array();
		SortedPaths.Sort();
		bool bValid = true;
		for (const FString& Path : SortedPaths)
		{
			const FSoftObjectPath ObjectPath(Path);
			UObject* Object = ObjectPath.ResolveObject();
			if (!Object)
			{
				Object = ObjectPath.TryLoad();
			}
			const bool bExpectedClass = Profile.Cues.ContainsByPredicate(
				[&ObjectPath, Object](const FWebToUEFeedbackCueProfile& Cue)
				{
					if (Cue.Concurrency.ToSoftObjectPath() == ObjectPath)
					{
						return Object && Object->IsA<USoundConcurrency>();
					}
					return Cue.Variants.ContainsByPredicate(
						[&ObjectPath, Object](const TSoftObjectPtr<USoundBase>& Variant)
						{
							return Variant.ToSoftObjectPath() == ObjectPath &&
								Object && Object->IsA<USoundBase>();
						});
				});
			if (!bExpectedClass)
			{
				OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-002"),
					TEXT("resources/") + Path,
					TEXT("Feedback resource is missing or has the wrong Unreal asset class.") });
				bValid = false;
				continue;
			}
			bValid &= AddPackageClosure(FSoftObjectPath(Path), Dependencies,
				OutDiagnostics);
		}
		if (!bValid)
		{
			SortDiagnostics(OutDiagnostics);
			return false;
		}
		return FWebToUEResourceContractPolicy::BuildSnapshot(
			BuildDescriptor(Profile, Dependencies), OutSnapshot, OutDiagnostics);
	}
#endif
}

const FWebToUEFeedbackCueProfile* UWebToUEFeedbackProfile::FindCue(FName CueId) const
{
	return Cues.FindByPredicate([CueId](const FWebToUEFeedbackCueProfile& Cue)
	{
		return Cue.CueId == CueId;
	});
}

bool UWebToUEFeedbackProfile::ValidateProfile(
	TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics) const
{
	using namespace WebToUE::FeedbackProfile::Private;
	OutDiagnostics.Reset();
	if (SchemaMajor != SupportedSchemaMajor || SchemaMinor > SupportedSchemaMinor)
	{
		OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-001"), TEXT("schema-version"),
			TEXT("Feedback Profile schema version is unsupported.") });
	}
	if (!IsNamespacedName(ProfileId))
	{
		OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-001"), TEXT("profile-id"),
			TEXT("Feedback ProfileId must be a namespaced stable identifier.") });
	}
	if (Cues.IsEmpty() || Cues.Num() > MaximumCueCount)
	{
		OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-001"), TEXT("cues"),
			TEXT("Feedback Profile requires a bounded non-empty Cue set.") });
	}

	TSet<FName> CueIds;
	for (int32 CueIndex = 0; CueIndex < Cues.Num(); ++CueIndex)
	{
		const FWebToUEFeedbackCueProfile& Cue = Cues[CueIndex];
		const FString Path = FString::Printf(TEXT("cues/%d"), CueIndex);
		if (!IsNamespacedName(Cue.CueId) || CueIds.Contains(Cue.CueId))
		{
			OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-001"), Path + TEXT("/cue-id"),
				TEXT("Cue IDs must be unique namespaced stable identifiers.") });
		}
		CueIds.Add(Cue.CueId);
		if (Cue.Variants.IsEmpty() || Cue.Variants.Num() > MaximumVariantsPerCue)
		{
			OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-002"), Path + TEXT("/variants"),
				TEXT("A Cue requires one to eight Sound variants.") });
		}
		TSet<FSoftObjectPath> VariantPaths;
		for (const TSoftObjectPtr<USoundBase>& Variant : Cue.Variants)
		{
			const FSoftObjectPath VariantPath = Variant.ToSoftObjectPath();
			const FString Value = VariantPath.ToString();
			if (VariantPath.IsNull() ||
				(!Value.StartsWith(TEXT("/Game/")) && !Value.StartsWith(TEXT("/Engine/"))) ||
				VariantPaths.Contains(VariantPath))
			{
				OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-002"), Path + TEXT("/variants"),
					TEXT("Sound variants must be unique sealed /Game or /Engine assets.") });
			}
			VariantPaths.Add(VariantPath);
		}
		if (!Cue.Concurrency.IsNull())
		{
			const FString Value = Cue.Concurrency.ToSoftObjectPath().ToString();
			if (!Value.StartsWith(TEXT("/Game/")) && !Value.StartsWith(TEXT("/Engine/")))
			{
				OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-002"), Path + TEXT("/concurrency"),
					TEXT("Concurrency must be a sealed /Game or /Engine asset.") });
			}
		}
		if (ResidencyRank(Cue.Residency) == MAX_int32)
		{
			OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-002"), Path + TEXT("/residency"),
				TEXT("Feedback resources require Critical, Visible or Lazy residency.") });
		}
		if (!FMath::IsFinite(Cue.VolumeMultiplier) || Cue.VolumeMultiplier < 0.0f ||
			!FMath::IsFinite(Cue.PitchMultiplier) || Cue.PitchMultiplier <= 0.0f ||
			!FMath::IsFinite(Cue.CooldownSeconds) || Cue.CooldownSeconds < 0.0 ||
			!FMath::IsFinite(Cue.DeduplicationWindowSeconds) ||
			Cue.DeduplicationWindowSeconds < 0.0 ||
			!FMath::IsFinite(Cue.ThrottleWindowSeconds) || Cue.ThrottleWindowSeconds < 0.0)
		{
			OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-003"), Path + TEXT("/policy"),
				TEXT("Feedback volume, pitch and timing values must be finite and non-negative.") });
		}
		if ((Cue.DeduplicationGroup.IsNone() !=
				(Cue.DeduplicationWindowSeconds <= 0.0)) ||
			(Cue.ThrottleGroup.IsNone() !=
				(Cue.ThrottleMaximum <= 0 && Cue.ThrottleWindowSeconds <= 0.0)) ||
			(!Cue.ThrottleGroup.IsNone() &&
				(Cue.ThrottleMaximum <= 0 || Cue.ThrottleMaximum > 64 ||
				 Cue.ThrottleWindowSeconds <= 0.0)))
		{
			OutDiagnostics.Add({ TEXT("WTUE-FEEDBACK-003"), Path + TEXT("/rate-policy"),
				TEXT("Dedupe and throttle groups require complete bounded window settings.") });
		}
	}
	SortDiagnostics(OutDiagnostics);
	return OutDiagnostics.IsEmpty();
}

bool UWebToUEFeedbackProfile::ValidateResourceContract(
	TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics) const
{
	using namespace WebToUE::FeedbackProfile::Private;
	OutDiagnostics.Reset();
	TArray<FWebToUEResourceContractDiagnostic> ProfileDiagnostics;
	if (!ValidateProfile(ProfileDiagnostics))
	{
		OutDiagnostics = MoveTemp(ProfileDiagnostics);
		return false;
	}

	FWebToUEResourceContractSnapshot Snapshot;
	if (!FWebToUEResourceContractPolicy::BuildSnapshot(
		BuildDescriptor(*this, SealedResourceDependencies), Snapshot, OutDiagnostics))
	{
		return false;
	}
	FWebToUEArtifactVersionSet Supported;
	Supported.UiIr = { 1, 0 };
	Supported.ResourceIr = { 1, ResourceIrMinor };
	TArray<FWebToUEResourceContractDiagnostic> CompatibilityDiagnostics;
	FWebToUEResourceContractPolicy::IsRuntimeCompatible(
		Snapshot.Freshness.ArtifactVersions, Supported, CompatibilityDiagnostics);
	OutDiagnostics.Append(MoveTemp(CompatibilityDiagnostics));
	TArray<FWebToUEResourceContractDiagnostic> FreshnessDiagnostics;
	FWebToUEResourceContractPolicy::IsCookFresh(
		Snapshot.Freshness, ResourceFreshness, FreshnessDiagnostics);
	OutDiagnostics.Append(MoveTemp(FreshnessDiagnostics));
	SortDiagnostics(OutDiagnostics);
	return OutDiagnostics.IsEmpty();
}

#if WITH_EDITOR
bool UWebToUEFeedbackProfile::RebuildResourceSeal()
{
	using namespace WebToUE::FeedbackProfile::Private;
	FWebToUEResourceContractSnapshot Snapshot;
	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	if (!BuildCurrentSnapshot(*this, Snapshot, Diagnostics))
	{
		for (const FWebToUEResourceContractDiagnostic& Diagnostic : Diagnostics)
		{
			UE_LOG(LogWebToUEFeedbackProfile, Error, TEXT("%s %s: %s Asset=%s"),
				*Diagnostic.Code, *Diagnostic.Path, *Diagnostic.Detail, *GetPathName());
		}
		return false;
	}
	SealedResourceDependencies = MoveTemp(Snapshot.Dependencies);
	ResourceFreshness = MoveTemp(Snapshot.Freshness);
	MarkPackageDirty();
	return true;
}

bool UWebToUEFeedbackProfile::ValidateCurrentCookFreshness(
	const UWebToUEFeedbackProfile& Profile,
	TArray<FWebToUEResourceContractDiagnostic>& OutDiagnostics)
{
	using namespace WebToUE::FeedbackProfile::Private;
	FWebToUEResourceContractSnapshot Current;
	if (!BuildCurrentSnapshot(Profile, Current, OutDiagnostics))
	{
		return false;
	}
	return FWebToUEResourceContractPolicy::IsCookFresh(
		Current.Freshness, Profile.GetResourceFreshness(), OutDiagnostics);
}
#endif

FPrimaryAssetId UWebToUEFeedbackProfile::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("WebToUEFeedbackProfile"), GetFName());
}

FWebToUEFeedbackCookFreshnessValidator& UWebToUEFeedbackProfile::CookFreshnessValidator()
{
	static FWebToUEFeedbackCookFreshnessValidator Validator;
	return Validator;
}

void UWebToUEFeedbackProfile::PreSave(FObjectPreSaveContext SaveContext)
{
	Super::PreSave(SaveContext);
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject)) return;
	if (!SaveContext.IsCooking())
	{
		RebuildResourceSeal();
		return;
	}

	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	bool bFresh = ValidateResourceContract(Diagnostics);
	if (bFresh)
	{
		FWebToUEFeedbackCookFreshnessValidator& Validator = CookFreshnessValidator();
		if (Validator.IsBound())
		{
			bFresh = Validator.Execute(*this, Diagnostics);
		}
		else
		{
			bFresh = false;
			Diagnostics.Add({ TEXT("WTUE-RES-004"), TEXT("cook-validator"),
				TEXT("Cook cannot prove Feedback Profile freshness because the Editor validator is unavailable.") });
		}
	}
	if (!bFresh)
	{
		for (const FWebToUEResourceContractDiagnostic& Diagnostic : Diagnostics)
		{
			UE_LOG(LogWebToUEFeedbackProfile, Error, TEXT("%s %s: %s Asset=%s"),
				*Diagnostic.Code, *Diagnostic.Path, *Diagnostic.Detail, *GetPathName());
		}
	}
#endif
}
