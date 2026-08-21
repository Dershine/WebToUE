#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "Hash/Blake3.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "WebToUEDocument.h"
#include "WebToUEFactory.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERelativeTextureSourceTest,
	"WebToUE.Editor.RelativeTextureSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERelativeTextureFailuresTest,
	"WebToUE.Editor.RelativeTextureFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERelativeTextureWatcherTest,
	"WebToUE.Editor.RelativeTextureWatcher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUERelativeTextureCookFreshnessTest,
	"WebToUE.Editor.RelativeTextureCookFreshness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::RelativeTextureSource::Tests
{
	static FString HashUtf8(const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		return LexToString(FBlake3::HashBuffer(Utf8.Get(), Utf8.Length())).ToLower();
	}

	static FString MakeLogicalSourceId(const FString& Filename)
	{
		FString Relative = FPaths::ConvertRelativePathToFull(Filename);
		FPaths::NormalizeFilename(Relative);
		check(FPaths::MakePathRelativeTo(Relative, *FPaths::ProjectDir()));
		FPaths::NormalizeFilename(Relative);
		return TEXT("source/") + Relative;
	}

	static bool WriteBmp(const FString& Filename, int32 Width, int32 Height, uint8 Blue)
	{
		const int32 RowBytes = Align(Width * 3, 4);
		const int32 PixelBytes = RowBytes * Height;
		TArray<uint8> Bytes;
		Bytes.Init(0, 54 + PixelBytes);
		auto Write16 = [&Bytes](int32 Offset, uint16 Value)
		{
			Bytes[Offset] = static_cast<uint8>(Value & 0xff);
			Bytes[Offset + 1] = static_cast<uint8>(Value >> 8);
		};
		auto Write32 = [&Bytes](int32 Offset, uint32 Value)
		{
			for (int32 Byte = 0; Byte < 4; ++Byte)
			{
				Bytes[Offset + Byte] = static_cast<uint8>((Value >> (Byte * 8)) & 0xff);
			}
		};
		Bytes[0] = 'B';
		Bytes[1] = 'M';
		Write32(2, Bytes.Num());
		Write32(10, 54);
		Write32(14, 40);
		Write32(18, Width);
		Write32(22, Height);
		Write16(26, 1);
		Write16(28, 24);
		Write32(34, PixelBytes);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Offset = 54 + Y * RowBytes + X * 3;
				Bytes[Offset] = Blue;
				Bytes[Offset + 1] = static_cast<uint8>(32 + X * 16);
				Bytes[Offset + 2] = static_cast<uint8>(64 + Y * 16);
			}
		}
		return FFileHelper::SaveArrayToFile(Bytes, *Filename);
	}
}

bool FWebToUERelativeTextureSourceTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::RelativeTextureSource::Tests;
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation/RelativeTextureSource"));
	const FString HtmlFilename = FPaths::Combine(TestDirectory, TEXT("Document.html"));
	const FString ImageFilename = FPaths::Combine(TestDirectory, TEXT("Fixture.bmp"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*HtmlFilename, false, true);
		IFileManager::Get().Delete(*ImageFilename, false, true);
	};

	const FString SourceIdentity = MakeLogicalSourceId(ImageFilename);
	const FString StableHash = HashUtf8(SourceIdentity);
	const FString GeneratedId = TEXT("generated:textures/") + StableHash;
	const FString ExpectedPackage = TEXT("/Game/WebToUEGenerated/Textures/T_") + StableHash;
	const FString ExpectedObject = ExpectedPackage + TEXT(".T_") + StableHash;
	TestTrue(TEXT("The relative texture fixture is written"),
		WriteBmp(ImageFilename, 4, 2, 192));
	TestTrue(TEXT("The relative author document is written"),
		FFileHelper::SaveStringToFile(
			TEXT("<body><img src='./Fixture.bmp'><img src='Fixture.bmp'></body>"),
			*HtmlFilename));
	UWebToUEDocument* RelativeDocument =
		NewObject<UWebToUEDocument>(GetTransientPackage());
	AddExpectedError(TEXT("WTUE-RES-001"), EAutomationExpectedErrorFlags::Contains, -1);
	AddExpectedError(TEXT("WTUE-RES-003"), EAutomationExpectedErrorFlags::Contains, -1);
	TestTrue(TEXT("A project-relative texture source imports"),
		UWebToUEFactory::ImportIntoDocument(*RelativeDocument, HtmlFilename, false));

	const TArray<FWebToUECompiledResource>& RelativeManifest =
		RelativeDocument->GetResourceManifest();
	TestEqual(TEXT("Equivalent relative references share one manifest entry"),
		RelativeManifest.Num(), 1);
	if (RelativeManifest.Num() == 1)
	{
		const FWebToUECompiledResource& Resource = RelativeManifest[0];
		TestEqual(TEXT("The generated texture has a stable object identity"),
			Resource.Path.ToString(), ExpectedObject);
		TestEqual(TEXT("The author provenance remains RelativeSource"),
			Resource.Provenance.Origin, EWebToUEResourceOrigin::RelativeSource);
		TestEqual(TEXT("The author reference is canonicalized"),
			Resource.Provenance.AuthorReference, FString(TEXT("Fixture.bmp")));
		TestEqual(TEXT("The generated dependency identity is sealed"),
			Resource.Provenance.ResolvedDependencyId, GeneratedId);
		TestEqual(TEXT("The intrinsic size is sealed at import"),
			Resource.IntrinsicSize, FVector2f(4.0f, 2.0f));
	}

	const FString FirstResourceId = RelativeManifest.Num() == 1
		? RelativeManifest[0].ResourceId : FString();
	const FWebToUECookFreshnessStamp FirstStamp =
		RelativeDocument->GetResourceFreshness();
	TestTrue(TEXT("Changed relative texture bytes are written"),
		WriteBmp(ImageFilename, 8, 4, 64));
	TestTrue(TEXT("A changed relative texture reimports in place"),
		UWebToUEFactory::ImportIntoDocument(*RelativeDocument, HtmlFilename, true));
	if (RelativeDocument->GetResourceManifest().Num() == 1)
	{
		TestEqual(TEXT("Content changes preserve ResourceId"),
			RelativeDocument->GetResourceManifest()[0].ResourceId, FirstResourceId);
		TestEqual(TEXT("Content changes preserve generated object path"),
			RelativeDocument->GetResourceManifest()[0].Path.ToString(), ExpectedObject);
		TestEqual(TEXT("Reimport updates the sealed intrinsic size"),
			RelativeDocument->GetResourceManifest()[0].IntrinsicSize,
			FVector2f(8.0f, 4.0f));
	}
	TestTrue(TEXT("Content changes replace the sealed dependency stamp"),
		RelativeDocument->GetResourceFreshness() != FirstStamp);

	UWebToUEDocument* GeneratedDocument =
		NewObject<UWebToUEDocument>(GetTransientPackage());
	const FString GeneratedHtml = FString::Printf(
		TEXT("<body><img src='%s'></body>"), *GeneratedId);
	TestTrue(TEXT("The generated author document is written"),
		FFileHelper::SaveStringToFile(GeneratedHtml, *HtmlFilename));
	TestTrue(TEXT("A sealed generated texture identity resolves"),
		UWebToUEFactory::ImportIntoDocument(*GeneratedDocument, HtmlFilename, false));
	if (GeneratedDocument->GetResourceManifest().Num() == 1)
	{
		const FWebToUECompiledResource& Resource =
			GeneratedDocument->GetResourceManifest()[0];
		TestEqual(TEXT("Generated provenance is explicit"),
			Resource.Provenance.Origin, EWebToUEResourceOrigin::Generated);
		TestEqual(TEXT("Generated references reuse the stable object path"),
			Resource.Path.ToString(), ExpectedObject);
		TestEqual(TEXT("Relative and generated references share ResourceId"),
			Resource.ResourceId, FirstResourceId);
	}

	RelativeDocument = nullptr;
	GeneratedDocument = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	AddCommand(new FDelayedFunctionLatentCommand([this, ExpectedPackage]
	{
		if (UEditorAssetSubsystem* Assets =
			GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
			Assets && Assets->DoesAssetExist(ExpectedPackage))
		{
			TestTrue(TEXT("The generated test texture is removed"),
				Assets->DeleteAsset(ExpectedPackage));
		}
	}, 0.25f));
	return true;
}

bool FWebToUERelativeTextureCookFreshnessTest::RunTest(const FString& Parameters)
{
	UWebToUEDocument* Document = LoadObject<UWebToUEDocument>(nullptr,
		TEXT("/Game/WebToUEExamples/ResourceTextureSmoke.ResourceTextureSmoke"));
	if (!TestNotNull(TEXT("The persisted relative texture fixture loads"), Document))
	{
		return false;
	}
	TArray<FWebToUEResourceContractDiagnostic> Diagnostics;
	const bool bFresh = UWebToUEFactory::ValidateCookFreshness(*Document, Diagnostics);
	for (const FWebToUEResourceContractDiagnostic& Diagnostic : Diagnostics)
	{
		AddError(FString::Printf(TEXT("%s %s: %s"), *Diagnostic.Code,
			*Diagnostic.Path, *Diagnostic.Detail));
	}
	TestTrue(TEXT("The persisted relative texture fixture is Cook-fresh across processes"),
		bFresh);
	return true;
}

bool FWebToUERelativeTextureFailuresTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::RelativeTextureSource::Tests;
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation/RelativeTextureFailures"));
	const FString HtmlFilename = FPaths::Combine(TestDirectory, TEXT("Document.html"));
	const FString ImageFilename = FPaths::Combine(TestDirectory, TEXT("Fixture.bmp"));
	const FString UnsupportedFilename = FPaths::Combine(TestDirectory, TEXT("Fixture.gif"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*HtmlFilename, false, true);
		IFileManager::Get().Delete(*ImageFilename, false, true);
		IFileManager::Get().Delete(*UnsupportedFilename, false, true);
	};
	TestTrue(TEXT("The rejection fixture is written"),
		WriteBmp(ImageFilename, 2, 2, 32));
	TestTrue(TEXT("The unsupported-extension fixture is written"),
		WriteBmp(UnsupportedFilename, 2, 2, 64));

	const TArray<FString> RejectedReferences = {
		TEXT("https://example.invalid/texture.png"),
		ImageFilename,
		TEXT("../../../../Outside.png"),
		TEXT("."),
		TEXT("Fixture.gif"),
		TEXT("generated:textures/not-a-canonical-blake3"),
		TEXT("sub\\Fixture.bmp")
	};
	AddExpectedError(TEXT("WTUE-RES-001"),
		EAutomationExpectedErrorFlags::Contains, RejectedReferences.Num());
	for (const FString& Reference : RejectedReferences)
	{
		const FString Html = FString::Printf(
			TEXT("<body><img src='%s'></body>"), *Reference);
		TestTrue(TEXT("The rejected-reference document is written"),
			FFileHelper::SaveStringToFile(Html, *HtmlFilename));
		UWebToUEDocument* Document =
			NewObject<UWebToUEDocument>(GetTransientPackage());
		TestFalse(*FString::Printf(TEXT("Texture reference fails closed: %s"), *Reference),
			UWebToUEFactory::ImportIntoDocument(*Document, HtmlFilename, false));
		TestTrue(TEXT("A rejected reference exposes no partial manifest"),
			Document->GetResourceManifest().IsEmpty());
	}
	return true;
}

bool FWebToUERelativeTextureWatcherTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::RelativeTextureSource::Tests;
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation/RelativeTextureWatcher"));
	const FString HtmlFilename = FPaths::Combine(TestDirectory, TEXT("Document.html"));
	const FString ImageFilename = FPaths::Combine(TestDirectory, TEXT("Fixture.bmp"));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	TestTrue(TEXT("The watcher texture fixture is written"),
		WriteBmp(ImageFilename, 4, 2, 96));
	TestTrue(TEXT("The watcher author document is written"),
		FFileHelper::SaveStringToFile(
			TEXT("<body><img src='Fixture.bmp'></body>"), *HtmlFilename));

	const FString StableHash = HashUtf8(MakeLogicalSourceId(ImageFilename));
	const FString GeneratedPackage =
		TEXT("/Game/WebToUEGenerated/Textures/T_") + StableHash;
	UPackage* WatcherPackage = CreatePackage(
		TEXT("/Game/WebToUEAutomation/WTUE_RelativeTextureWatcher"));
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(WatcherPackage,
		TEXT("WatcherDocument"), RF_Public | RF_Standalone);
	Document->AddToRoot();
	TestTrue(TEXT("The watcher document imports its initial texture"),
		UWebToUEFactory::ImportIntoDocument(*Document, HtmlFilename, false));
	TestEqual(TEXT("The watcher document starts with one resource"),
		Document->GetResourceManifest().Num(), 1);
	const FString ResourceId = Document->GetResourceManifest().Num() == 1
		? Document->GetResourceManifest()[0].ResourceId : FString();
	TestTrue(TEXT("Changed image bytes are written for the watcher"),
		WriteBmp(ImageFilename, 8, 4, 128));
	AddExpectedError(TEXT("WTUE-RES-001"),
		EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Failed to read file"),
		EAutomationExpectedErrorFlags::Contains, 1);

	AddCommand(new FDelayedFunctionLatentCommand(
		[this, Document, ImageFilename, ResourceId]
		{
			TestEqual(TEXT("Watcher reimport retains one manifest entry"),
				Document->GetResourceManifest().Num(), 1);
			if (Document->GetResourceManifest().Num() == 1)
			{
				TestEqual(TEXT("The image watcher reimports changed intrinsic size"),
					Document->GetResourceManifest()[0].IntrinsicSize,
					FVector2f(8.0f, 4.0f));
				TestEqual(TEXT("Watcher reimport preserves ResourceId"),
					Document->GetResourceManifest()[0].ResourceId, ResourceId);
			}
			TestTrue(TEXT("The watched image is removed for the negative path"),
				IFileManager::Get().Delete(*ImageFilename, false, true));
		}, 0.75f));

	AddCommand(new FDelayedFunctionLatentCommand(
		[this, Document, ImageFilename, ResourceId]
		{
			TestEqual(TEXT("Failed watcher reimport retains one manifest entry"),
				Document->GetResourceManifest().Num(), 1);
			if (Document->GetResourceManifest().Num() == 1)
			{
				TestEqual(TEXT("A missing watched image preserves last-good intrinsic size"),
					Document->GetResourceManifest()[0].IntrinsicSize,
					FVector2f(8.0f, 4.0f));
				TestEqual(TEXT("A failed watcher reimport preserves ResourceId"),
					Document->GetResourceManifest()[0].ResourceId, ResourceId);
			}
			TArray<FWebToUEResourceContractDiagnostic> CookDiagnostics;
			TestFalse(TEXT("Last-good relative content cannot Cook with a missing source"),
				UWebToUEFactory::ValidateCookFreshness(*Document, CookDiagnostics));
			TestTrue(TEXT("A missing relative source uses WTUE-RES-004"),
				CookDiagnostics.ContainsByPredicate([](
					const FWebToUEResourceContractDiagnostic& Diagnostic)
				{
					return Diagnostic.Code == TEXT("WTUE-RES-004");
				}));
			TestTrue(TEXT("The watched image is restored for recovery"),
				WriteBmp(ImageFilename, 16, 8, 160));
		}, 0.75f));

	AddCommand(new FDelayedFunctionLatentCommand(
		[this, Document, WatcherPackage, HtmlFilename, ImageFilename, ResourceId]
		{
			TestEqual(TEXT("Watcher recovery retains one manifest entry"),
				Document->GetResourceManifest().Num(), 1);
			if (Document->GetResourceManifest().Num() == 1)
			{
				TestEqual(TEXT("Restored image bytes trigger watcher recovery"),
					Document->GetResourceManifest()[0].IntrinsicSize,
					FVector2f(16.0f, 8.0f));
				TestEqual(TEXT("Watcher recovery keeps the original ResourceId"),
					Document->GetResourceManifest()[0].ResourceId, ResourceId);
			}
			TArray<FWebToUEResourceContractDiagnostic> CookDiagnostics;
			TestTrue(TEXT("Recovered watcher content is Cook-fresh"),
				UWebToUEFactory::ValidateCookFreshness(*Document, CookDiagnostics));
			Document->RemoveFromRoot();
			Document->ClearFlags(RF_Public | RF_Standalone);
			Document->MarkAsGarbage();
			WatcherPackage->SetDirtyFlag(false);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			IFileManager::Get().Delete(*HtmlFilename, false, true);
			IFileManager::Get().Delete(*ImageFilename, false, true);
		}, 0.75f));

	AddCommand(new FDelayedFunctionLatentCommand(
		[this, GeneratedPackage]
		{
			if (UEditorAssetSubsystem* Assets =
				GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
				Assets && Assets->DoesAssetExist(GeneratedPackage))
			{
				TestTrue(TEXT("The watcher-generated test texture is removed"),
					Assets->DeleteAsset(GeneratedPackage));
			}
		}, 0.25f));
	return true;
}

#endif
