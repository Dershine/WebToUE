#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEStaticMaterialSourceTest,
	"WebToUE.Editor.StaticMaterialSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEStaticMaterialFailuresTest,
	"WebToUE.Editor.StaticMaterialFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEStaticMaterialCookFreshnessTest,
	"WebToUE.Editor.StaticMaterialCookFreshness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::StaticMaterialSource::Tests
{
	static FString MakeTestFilename(const TCHAR* Leaf)
	{
		const FString Directory = FPaths::Combine(
			FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation/StaticMaterial"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		return FPaths::Combine(Directory, Leaf);
	}
}

bool FWebToUEStaticMaterialSourceTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::StaticMaterialSource::Tests;
	const FString Filename = MakeTestFilename(TEXT("Source.html"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Filename, false, true);
	};
	const FString MaterialPath =
		TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");
	const FString Html = FString::Printf(
		TEXT("<body><div id='first' data-ue-material='%s' ")
		TEXT("data-ue-residency='critical'></div>")
		TEXT("<div id='second' data-ue-material='%s'></div></body>"),
		*MaterialPath, *MaterialPath);
	TestTrue(TEXT("The static Material source is written"),
		FFileHelper::SaveStringToFile(Html, *Filename));

	UWebToUEDocument* Document =
		NewObject<UWebToUEDocument>(GetTransientPackage());
	TestTrue(TEXT("A static MaterialInterface author reference imports"),
		UWebToUEFactory::ImportIntoDocument(*Document, Filename, false));
	const TArray<FWebToUECompiledResource>& Manifest =
		Document->GetResourceManifest();
	TestEqual(TEXT("Duplicate static Material references share one manifest entry"),
		Manifest.Num(), 1);
	if (Manifest.Num() == 1)
	{
		const FWebToUECompiledResource& Resource = Manifest[0];
		TestEqual(TEXT("Static Material uses its own Resource kind"),
			Resource.Kind, EWebToUEResourceKind::Material);
		TestEqual(TEXT("The Material manifest path is the sealed author asset"),
			Resource.Path.ToString(), MaterialPath);
		TestTrue(TEXT("The Material has a deterministic logical ResourceId"),
			Resource.ResourceId.StartsWith(TEXT("resource/material/")) &&
			Resource.ResourceId.Len() == 82);
		TestEqual(TEXT("The Material uses Unreal Asset provenance"),
			Resource.Provenance.Origin, EWebToUEResourceOrigin::UnrealAsset);
		TestEqual(TEXT("Duplicate references promote residency to Critical"),
			Resource.Residency, EWebToUEResidencyClass::Critical);
		TestEqual(TEXT("The minimal Slate brush image size is sealed"),
			Resource.BrushImageSize, FVector2f(1.0f, 1.0f));
		int32 BoundNodeCount = 0;
		for (const FWebToUECompiledNode& Node : Document->GetCompiledNodes())
		{
			if (!Node.ResourceId.IsEmpty())
			{
				++BoundNodeCount;
				TestEqual(TEXT("Material nodes bind the shared logical ResourceId"),
					Node.ResourceId, Resource.ResourceId);
			}
		}
		TestEqual(TEXT("Both Material nodes consume the sealed entry"),
			BoundNodeCount, 2);
	}
	TestTrue(TEXT("The direct Material package enters the sealed closure"),
		Document->GetSealedResourceDependencies().ContainsByPredicate(
			[](const FWebToUEResourceDependency& Dependency)
			{
				return Dependency.LogicalId ==
					TEXT("asset/Engine/EngineMaterials/DefaultMaterial");
			}));
	TestEqual(TEXT("The imported artifact declares Resource IR 1.2"),
		Document->GetResourceFreshness().ArtifactVersions.ResourceIr.Minor,
		uint16(2));
	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	TestTrue(TEXT("The static Material document satisfies the runtime contract"),
		Document->ValidateResourceContract(Diagnostics));

	const FWebToUECookFreshnessStamp LastGoodStamp =
		Document->GetResourceFreshness();
	TestTrue(TEXT("A wrong-class Material reimport source is written"),
		FFileHelper::SaveStringToFile(
			TEXT("<body><div data-ue-material='/Engine/EngineResources/DefaultTexture.DefaultTexture'></div></body>"),
			*Filename));
	AddExpectedError(TEXT("WTUE-RES-001"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Wrong-class reimport fails closed"),
		UWebToUEFactory::ImportIntoDocument(*Document, Filename, true));
	TestTrue(TEXT("Failed Material reimport preserves last-good IR"),
		Document->GetResourceFreshness() == LastGoodStamp);
	Diagnostics.Reset();
	TestFalse(TEXT("Last-good Material cannot Cook after author Source drifts"),
		UWebToUEFactory::ValidateCookFreshness(*Document, Diagnostics));
	TestTrue(TEXT("Stale Material Source uses WTUE-RES-004"),
		Diagnostics.ContainsByPredicate([](
			const FWebToUEResourceContractDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == TEXT("WTUE-RES-004");
		}));
	return true;
}

bool FWebToUEStaticMaterialFailuresTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::StaticMaterialSource::Tests;
	const FString Filename = MakeTestFilename(TEXT("Failures.html"));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*Filename, false, true);
	};
	const TArray<FString> RejectedReferences = {
		TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"),
		TEXT("/Engine/EngineMaterials/WTUE_Missing.WTUE_Missing"),
		TEXT("https://example.invalid/material"),
		TEXT("C:/Project/Content/M_Invalid.uasset"),
		TEXT("Materials/M_Relative"),
		TEXT("generated:materials/not-supported")
	};
	AddExpectedError(TEXT("WTUE-RES-001"),
		EAutomationExpectedErrorFlags::Contains, RejectedReferences.Num());
	for (const FString& Reference : RejectedReferences)
	{
		const FString Html = FString::Printf(
			TEXT("<body><div data-ue-material='%s'></div></body>"),
			*Reference);
		TestTrue(TEXT("The rejected Material source is written"),
			FFileHelper::SaveStringToFile(Html, *Filename));
		UWebToUEDocument* Document =
			NewObject<UWebToUEDocument>(GetTransientPackage());
		TestFalse(*FString::Printf(TEXT("Material reference fails closed: %s"),
			*Reference), UWebToUEFactory::ImportIntoDocument(
				*Document, Filename, false));
		TestTrue(TEXT("A rejected Material reference exposes no partial manifest"),
			Document->GetResourceManifest().IsEmpty());
	}
	return true;
}

bool FWebToUEStaticMaterialCookFreshnessTest::RunTest(const FString& Parameters)
{
	UWebToUEDocument* Document = LoadObject<UWebToUEDocument>(nullptr,
		TEXT("/Game/WebToUEExamples/ResourceMaterialSmoke.ResourceMaterialSmoke"));
	if (!TestNotNull(TEXT("The persisted static Material fixture loads"), Document))
	{
		return false;
	}
	const TArray<FWebToUECompiledResource>& Manifest =
		Document->GetResourceManifest();
	TestEqual(TEXT("The fixture seals exactly one static Material resource"),
		Manifest.Num(), 1);
	if (Manifest.Num() == 1)
	{
		TestEqual(TEXT("The fixture resource is a Material"),
			Manifest[0].Kind, EWebToUEResourceKind::Material);
		TestEqual(TEXT("The fixture references the static MI"),
			Manifest[0].Path.ToString(),
			FString(TEXT("/Game/WebToUEExamples/Materials/MI_WTUE_StaticMaterialBrush.MI_WTUE_StaticMaterialBrush")));
	}
	const auto HasDependency = [Document](const TCHAR* LogicalId)
	{
		return Document->GetSealedResourceDependencies().ContainsByPredicate(
			[LogicalId](const FWebToUEResourceDependency& Dependency)
			{
				return Dependency.LogicalId == LogicalId;
			});
	};
	TestTrue(TEXT("The closure seals the direct MI package"),
		HasDependency(TEXT("asset/Game/WebToUEExamples/Materials/MI_WTUE_StaticMaterialBrush")));
	TestTrue(TEXT("The closure seals the parent Material package"),
		HasDependency(TEXT("asset/Game/WebToUEExamples/Materials/M_WTUE_StaticMaterialBrush")));
	TestTrue(TEXT("The closure seals the texture package used by the parent graph"),
		HasDependency(TEXT("asset/Game/WebToUEGenerated/Textures/T_907ecc87eeefa847248baa28942a34fc15f6ad17e65458ef0af9440080e1ed58")));

	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	const bool bFresh = UWebToUEFactory::ValidateCookFreshness(
		*Document, Diagnostics);
	for (const FWebToUEResourceContractDiagnostic& Diagnostic : Diagnostics)
	{
		AddError(FString::Printf(TEXT("%s %s: %s"), *Diagnostic.Code,
			*Diagnostic.Path, *Diagnostic.Detail));
	}
	TestTrue(TEXT("The persisted Material fixture is Cook-fresh across processes"),
		bFresh);
	return true;
}

#endif
