#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Internationalization/StringTableRegistry.h"
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
		"<img src='/Engine/EngineResources/DefaultTexture.DefaultTexture'>"
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
	return true;
}

#endif
