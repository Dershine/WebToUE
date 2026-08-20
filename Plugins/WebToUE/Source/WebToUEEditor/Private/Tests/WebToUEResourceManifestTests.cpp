#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Internationalization/StringTableRegistry.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "UObject/ObjectSaveContext.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"
#include "WebToUESettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEResourceManifestTest,
	"WebToUE.Editor.ResourceManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEResourceManifestTest::RunTest(const FString& Parameters)
{
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	const FString TestFilename = FPaths::Combine(
		TestDirectory, TEXT("ResourceManifest.html"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	UWebToUESettings* Settings = GetMutableDefault<UWebToUESettings>();
	const TArray<FWebToUEFontFamily> PreviousFamilies = Settings->FontFamilies;
	const FName TableId(TEXT("/WebToUETests/ST_Manifest.ST_Manifest"));
	FStringTableRegistry::Get().Internal_NewLocTable(TableId, TEXT("WebToUETests"));
	FStringTableRegistry::Get().Internal_SetLocTableEntry(TableId, TEXT("Label"), TEXT("Label"));
	ON_SCOPE_EXIT
	{
		FStringTableRegistry::Get().UnregisterStringTable(TableId);
		Settings->FontFamilies = PreviousFamilies;
		IFileManager::Get().Delete(*TestFilename, false, true);
	};
	FWebToUEFontFamily& Family = Settings->FontFamilies.AddDefaulted_GetRef();
	Family.CssFamily = TEXT("ManifestFont");
	Family.FontObject = TSoftObjectPtr<UObject>(
		FSoftObjectPath(TEXT("/Engine/EngineFonts/Roboto.Roboto")));

	const FString Html = TEXT(
		"<body><img src='/Engine/EngineResources/DefaultTexture.DefaultTexture'>"
		"<img src='/Engine/EngineResources/DefaultTexture.DefaultTexture' "
		"data-ue-residency='critical'>"
		"<p style='font-family: ManifestFont' "
		"data-ue-string-table='/WebToUETests/ST_Manifest.ST_Manifest' "
		"data-ue-string-key='Label'>Label</p></body>");
	TestTrue(TEXT("The manifest source is written"),
		FFileHelper::SaveStringToFile(Html, *TestFilename));
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	TestTrue(TEXT("The manifest source imports"),
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, false));

	const TArray<FWebToUECompiledResource>& Manifest = Document->GetResourceManifest();
	TestEqual(TEXT("Texture, font, and String Table produce three unique resources"),
		Manifest.Num(), 3);
	const auto CountKind = [&Manifest](EWebToUEResourceKind Kind)
	{
		int32 Count = 0;
		for (const FWebToUECompiledResource& Resource : Manifest)
		{
			Count += Resource.Kind == Kind ? 1 : 0;
		}
		return Count;
	};
	TestEqual(TEXT("Duplicate image nodes share one texture manifest entry"),
		CountKind(EWebToUEResourceKind::Texture), 1);
	TestEqual(TEXT("The computed font family produces one font entry"),
		CountKind(EWebToUEResourceKind::Font), 1);
	TestEqual(TEXT("The localized node produces one String Table entry"),
		CountKind(EWebToUEResourceKind::StringTable), 1);
	const FWebToUECompiledResource* TextureResource = Manifest.FindByPredicate(
		[](const FWebToUECompiledResource& Resource)
		{
			return Resource.Kind == EWebToUEResourceKind::Texture;
		});
	TestNotNull(TEXT("The imported texture has a contract entry"), TextureResource);
	if (TextureResource)
	{
		TestTrue(TEXT("The imported texture has a deterministic logical identity"),
			TextureResource->ResourceId.StartsWith(TEXT("resource/texture/")) &&
			TextureResource->ResourceId.Len() == 81);
		TestEqual(TEXT("The imported texture records Unreal Asset provenance"),
			TextureResource->Provenance.Origin, EWebToUEResourceOrigin::UnrealAsset);
		TestEqual(TEXT("Duplicate references promote document residency to Critical"),
			TextureResource->Residency, EWebToUEResidencyClass::Critical);
		int32 BoundImageCount = 0;
		for (const FWebToUECompiledNode& Node : Document->GetCompiledNodes())
		{
			if (Node.Tag == TEXT("img"))
			{
				++BoundImageCount;
				TestEqual(TEXT("Image nodes consume the shared logical ResourceId"),
					Node.ResourceId, TextureResource->ResourceId);
			}
		}
		TestEqual(TEXT("Both duplicate image nodes carry the contract identity"),
			BoundImageCount, 2);
	}
	TestEqual(TEXT("The sealed contract contains UI Source and Texture package inputs"),
		Document->GetSealedResourceDependencies().Num(), 2);
	TestEqual(TEXT("The imported artifact declares Resource IR 1.0"),
		Document->GetResourceFreshness().ArtifactVersions.ResourceIr.Major, uint16(1));
	TestEqual(TEXT("The imported artifact has a BLAKE3 dependency closure"),
		Document->GetResourceFreshness().DependencyClosureBlake3.Len(), 64);
	const FWebToUECookFreshnessStamp LastGoodStamp = Document->GetResourceFreshness();
	AddExpectedError(TEXT("WTUE-RES-001"), EAutomationExpectedErrorFlags::Contains, -1);
	AddExpectedError(TEXT("WTUE-RES-003"), EAutomationExpectedErrorFlags::Contains, -1);
	const FString InvalidHtml = TEXT("<body><img src='https://example.com/image.png'></body>");
	TestTrue(TEXT("The invalid reimport source is written"),
		FFileHelper::SaveStringToFile(InvalidHtml, *TestFilename));
	TestFalse(TEXT("HTTP texture provenance fails closed"),
		UWebToUEFactory::ImportIntoDocument(*Document, TestFilename, true));
	TestTrue(TEXT("A failed reimport preserves the last-good sealed artifact"),
		Document->GetResourceFreshness() == LastGoodStamp);
	TArray<FWebToUEResourceContractDiagnostic> CookDiagnostics;
	TestFalse(TEXT("Last-good content cannot Cook after its source drifts"),
		UWebToUEFactory::ValidateCookFreshness(*Document, CookDiagnostics));
	TestTrue(TEXT("Stale Source uses the stable Cook freshness diagnostic"),
		CookDiagnostics.ContainsByPredicate([](
			const FWebToUEResourceContractDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == TEXT("WTUE-RES-004");
		}));
	ITargetPlatform* WindowsTarget =
		GetTargetPlatformManagerRef().FindTargetPlatform(TEXT("Windows"));
	TestNotNull(TEXT("The Win64 Cook target is available to the PreSave test"),
		WindowsTarget);
	if (WindowsTarget)
	{
		FObjectSaveContextData SaveData(Document->GetPackage(), WindowsTarget,
			TEXT("WebToUEAutomation/ResourceManifest.uasset"), SAVE_None);
		SaveData.ObjectSaveContextPhase = EObjectSaveContextPhase::PreSave;
		AddExpectedError(TEXT("WTUE-RES-004"),
			EAutomationExpectedErrorFlags::Contains, -1);
		Document->PreSave(FObjectPreSaveContext(SaveData));
	}
	return true;
}

#endif
