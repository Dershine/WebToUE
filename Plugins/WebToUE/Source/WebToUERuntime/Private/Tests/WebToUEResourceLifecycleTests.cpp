#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SWebToUEView.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUEResourceContractTestUtils.h"
#include "WebToUESettings.h"
#include "WebToUEStyleProperties.h"

#include "Engine/Texture2D.h"
#include "Input/HittestGrid.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEResourceLifecycleTest,
	"WebToUE.Runtime.ResourceLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEResourceResidencyTest,
	"WebToUE.Runtime.ResourceResidency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::ResourceLifecycle::Tests
{
	static void AddAttribute(FWebToUECompiledNode& Node, const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUECompiledAttribute& Attribute = Node.Attributes.AddDefaulted_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
	}

	static void AddInlineDeclaration(FWebToUECompiledNode& Node,
		const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUEStyleDeclaration Parsed;
		check(WebToUE::Private::TryParseCssDeclaration(Name, Value, Parsed));
		FWebToUECompiledDeclaration& Declaration =
			Node.InlineStyleDeclarations.AddDefaulted_GetRef();
		Declaration.Property = Parsed.Property;
		Declaration.TypedValue = Parsed.TypedValue;
	}

	static void PaintView(const TSharedRef<SWebToUEView>& View)
	{
		FHittestGrid HittestGrid;
		FSlateWindowElementList DrawElements(nullptr);
		const FGeometry Geometry = FGeometry::MakeRoot(
			FVector2D(640.0, 360.0), FSlateLayoutTransform());
		const FPaintArgs PaintArgs(
			&View.Get(), HittestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
		View->OnPaint(PaintArgs, Geometry, FSlateRect(0.0f, 0.0f, 640.0f, 360.0f),
			DrawElements, 0, FWidgetStyle(), true);
	}
}

bool FWebToUEResourceLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::ResourceLifecycle::Tests;
	const FSoftObjectPath TexturePath(
		TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	const FSoftObjectPath FontPath(TEXT("/Engine/EngineFonts/Roboto.Roboto"));
	TestNotNull(TEXT("The fixture texture is resident before the runtime boundary"),
		LoadObject<UTexture2D>(nullptr, *TexturePath.ToString()));
	TestNotNull(TEXT("The fixture font is resident before the runtime boundary"),
		LoadObject<UObject>(nullptr, *FontPath.ToString()));

	UWebToUESettings* Settings = GetMutableDefault<UWebToUESettings>();
	const TArray<FWebToUEFontFamily> PreviousFamilies = Settings->FontFamilies;
	ON_SCOPE_EXIT { Settings->FontFamilies = PreviousFamilies; };
	FWebToUEFontFamily& Family = Settings->FontFamilies.AddDefaulted_GetRef();
	Family.CssFamily = TEXT("LifecycleFont");
	Family.FontObject = TSoftObjectPtr<UObject>(FontPath);

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;
	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	for (const TCHAR* Id : { TEXT("image-a"), TEXT("image-b") })
	{
		FWebToUECompiledNode& Image = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Image.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Image.Tag = TEXT("img");
		Image.ParentIndex = 0;
		AddAttribute(Image, TEXT("id"), Id);
		AddAttribute(Image, TEXT("src"), *TexturePath.ToString());
	}
	FWebToUECompiledNode& InvalidImage = CompiledDocument.Nodes.AddDefaulted_GetRef();
	InvalidImage.Type = static_cast<uint8>(EWebToUENodeType::Element);
	InvalidImage.Tag = TEXT("img");
	InvalidImage.ParentIndex = 0;
	AddAttribute(InvalidImage, TEXT("id"), TEXT("invalid-image"));
	AddAttribute(InvalidImage, TEXT("src"), *FontPath.ToString());
	FWebToUECompiledNode& Text = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Text.Type = static_cast<uint8>(EWebToUENodeType::Text);
	Text.Tag = TEXT("#text");
	Text.Text = TEXT("Warm glyphs");
	Text.LocalizedText = FText::FromString(Text.Text);
	Text.ParentIndex = 0;
	AddAttribute(Text, TEXT("id"), TEXT("glyph-text"));
	AddInlineDeclaration(Text, TEXT("font-family"), TEXT("LifecycleFont"));
	CompiledDocument.ResourceManifest.Add(
		{ EWebToUEResourceKind::Texture, TexturePath });
	CompiledDocument.ResourceManifest.Add(
		{ EWebToUEResourceKind::Font, FontPath });
	CompiledDocument.ResourceManifest.Add(
		{ EWebToUEResourceKind::Texture, FontPath });
	WebToUE::Tests::SealResourceContractForTesting(CompiledDocument);
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));

	const TSharedRef<SWebToUEView> FirstView = SNew(SWebToUEView);
	FWebToUEPerformanceSnapshot ColdSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		FirstView->SetDocument(Document);
		PaintView(FirstView);
		ColdSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Cold view setup consumes all three manifest slots"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceManifestEntries), uint64(3));
	TestEqual(TEXT("Resident texture and font are cache hits"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceCacheHits), uint64(2));
	TestEqual(TEXT("A resource kind mismatch is diagnosed once"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceFailures), uint64(1));
	TestEqual(TEXT("Cold view setup performs no synchronous resource load"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceLoadAttempts), uint64(0));
	TestTrue(TEXT("Cold view setup reports known-owned stable handle storage"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceKnownOwnedBytes) > 0);

	const int32 TextureHandle = FirstView->FindPresentationResourceHandleForTesting(
		EWebToUEResourceKind::Texture, TexturePath);
	const int32 FontHandle = FirstView->FindPresentationResourceHandleForTesting(
		EWebToUEResourceKind::Font, FontPath);
	TestEqual(TEXT("The texture retains its compiled stable handle"), TextureHandle, 0);
	TestEqual(TEXT("The font retains its compiled stable handle"), FontHandle, 1);
	TestNotNull(TEXT("The texture handle resolves without a hot-path load"),
		FirstView->GetPresentationResourceObjectForTesting(TextureHandle));
	TestNotNull(TEXT("The font handle resolves without a hot-path load"),
		FirstView->GetPresentationResourceObjectForTesting(FontHandle));
	FWebToUENode* ImageA = FirstView->FindRuntimeNodeByIdForTesting(TEXT("image-a"));
	FWebToUENode* ImageB = FirstView->FindRuntimeNodeByIdForTesting(TEXT("image-b"));
	FWebToUENode* Invalid = FirstView->FindRuntimeNodeByIdForTesting(TEXT("invalid-image"));
	TestNotNull(TEXT("The first duplicate image resolves a brush"),
		ImageA ? FirstView->GetPresentationBrushIdentityForTesting(*ImageA) : nullptr);
	TestNotNull(TEXT("The second duplicate image resolves a brush"),
		ImageB ? FirstView->GetPresentationBrushIdentityForTesting(*ImageB) : nullptr);
	TestNull(TEXT("The mismatched image falls back without a brush"),
		Invalid ? FirstView->GetPresentationBrushIdentityForTesting(*Invalid) : nullptr);

	const TSharedRef<SWebToUEView> SecondView = SNew(SWebToUEView);
	SecondView->SetDocument(Document);
	SecondView->LayoutForTesting(FVector2f(640.0f, 360.0f));
	const int32 SecondTextureHandle = SecondView->FindPresentationResourceHandleForTesting(
		EWebToUEResourceKind::Texture, TexturePath);
	TestEqual(TEXT("Manifest handles are stable across views"), SecondTextureHandle, TextureHandle);
	TestEqual(TEXT("Views share the engine-owned resolved texture object"),
		SecondView->GetPresentationResourceObjectForTesting(SecondTextureHandle),
		FirstView->GetPresentationResourceObjectForTesting(TextureHandle));

	FWebToUEPerformanceSnapshot WarmSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		PaintView(FirstView);
		WarmSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Warm layout performs no text recompute"),
		WarmSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes), uint64(0));
	TestEqual(TEXT("Warm layout performs no resource request"),
		WarmSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceAsyncRequests), uint64(0));
	TestEqual(TEXT("Warm layout performs no synchronous resource load"),
		WarmSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceLoadAttempts), uint64(0));

	FWebToUENode* GlyphText = FirstView->FindRuntimeNodeByIdForTesting(TEXT("glyph-text"));
	TestNotNull(TEXT("The glyph test text hydrates"), GlyphText);
	if (!GlyphText) return false;
	FWebToUEPerformanceSnapshot GlyphSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		TestTrue(TEXT("The new glyph string invalidates its measured text dependency"),
			FirstView->ApplyBoundTextChangeForTesting(
				*GlyphText, FText::FromString(TEXT("首次字形")), false));
		FirstView->LayoutForTesting(FVector2f(640.0f, 360.0f));
		GlyphSnapshot = Capture.GetSnapshot();
	}
	TestTrue(TEXT("The first new glyph string recomputes its text layout"),
		GlyphSnapshot.GetCounter(EWebToUEPerformanceCounter::TextLayoutComputes) > 0);
	TestEqual(TEXT("The first new glyph string performs no synchronous font load"),
		GlyphSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceLoadAttempts), uint64(0));
	TestEqual(TEXT("The first new glyph string performs no new async request"),
		GlyphSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceAsyncRequests), uint64(0));

	UWebToUEDocument* PendingDocument = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData PendingCompiled;
	PendingCompiled.RootNodeIndex = 0;
	FWebToUECompiledNode& PendingRoot = PendingCompiled.Nodes.AddDefaulted_GetRef();
	PendingRoot.Type = static_cast<uint8>(EWebToUENodeType::Element);
	PendingRoot.Tag = TEXT("body");
	PendingCompiled.ResourceManifest.Add({ EWebToUEResourceKind::Texture,
		FSoftObjectPath(TEXT("/Game/WebToUEAutomation/T_Pending.T_Pending")) });
	PendingCompiled.ResourceManifest[0].Residency = EWebToUEResidencyClass::Critical;
	WebToUE::Tests::SealResourceContractForTesting(
		PendingCompiled, TEXT("document/pending-test"));
	PendingDocument->CommitCompiledDocument(MoveTemp(PendingCompiled));
	const TSharedRef<SWebToUEView> PendingView = SNew(SWebToUEView);
	AddExpectedError(TEXT("/Game/WebToUEAutomation/T_Pending"),
		EAutomationExpectedErrorFlags::Contains, -1);
	FWebToUEPerformanceSnapshot RequestSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		PendingView->SetDocument(PendingDocument);
		RequestSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("An unresolved manifest path is requested asynchronously"),
		RequestSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceAsyncRequests), uint64(1));
	FWebToUEPerformanceSnapshot CancellationSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		PendingView->SetDocument(nullptr);
		CancellationSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Reset cancels the unresolved view-owned request"),
		CancellationSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceCancellations),
		uint64(1));
	PendingView->SetDocument(Document);
	PendingView->LayoutForTesting(FVector2f(640.0f, 360.0f));
	const int32 RecoveredTextureHandle = PendingView->FindPresentationResourceHandleForTesting(
		EWebToUEResourceKind::Texture, TexturePath);
	TestNotNull(TEXT("A view can recover with a valid document after cancelling a request"),
		PendingView->GetPresentationResourceObjectForTesting(RecoveredTextureHandle));
	return true;
}

bool FWebToUEResourceResidencyTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::ResourceLifecycle::Tests;
	const FSoftObjectPath CriticalPath(
		TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	const FSoftObjectPath VisiblePath(
		TEXT("/Engine/EngineResources/Black.Black"));
	const FSoftObjectPath LazyPath(
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	for (const FSoftObjectPath& Path : { CriticalPath, VisiblePath, LazyPath })
	{
		TestNotNull(TEXT("Residency fixture texture is resident"),
			LoadObject<UTexture2D>(nullptr, *Path.ToString()));
	}

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;
	FWebToUECompiledNode& Root = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Root.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Root.Tag = TEXT("body");
	const TArray<FSoftObjectPath> Paths{ CriticalPath, VisiblePath, LazyPath };
	for (int32 Index = 0; Index < Paths.Num(); ++Index)
	{
		FWebToUECompiledNode& Image = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Image.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Image.Tag = TEXT("img");
		Image.ParentIndex = 0;
		AddAttribute(Image, TEXT("id"),
			*FString::Printf(TEXT("residency-%d"), Index));
		AddAttribute(Image, TEXT("src"), *Paths[Index].ToString());
		FWebToUECompiledResource& Resource =
			CompiledDocument.ResourceManifest.AddDefaulted_GetRef();
		Resource.Kind = EWebToUEResourceKind::Texture;
		Resource.Path = Paths[Index];
		Resource.ResourceId = FString::Printf(
			TEXT("resource/texture/residency-%d"), Index);
		Resource.Residency = Index == 0 ? EWebToUEResidencyClass::Critical :
			Index == 1 ? EWebToUEResidencyClass::Visible :
			EWebToUEResidencyClass::Lazy;
	}
	WebToUE::Tests::SealResourceContractForTesting(
		CompiledDocument, TEXT("document/residency-test"));
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));

	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetDocument(Document);
	const int32 CriticalHandle = View->FindPresentationResourceHandleByIdForTesting(
		TEXT("resource/texture/residency-0"));
	const int32 VisibleHandle = View->FindPresentationResourceHandleByIdForTesting(
		TEXT("resource/texture/residency-1"));
	const int32 LazyHandle = View->FindPresentationResourceHandleByIdForTesting(
		TEXT("resource/texture/residency-2"));
	TestTrue(TEXT("Critical residency is satisfied during Document activation"),
		View->ArePresentationCriticalResourcesReadyForTesting());
	TestNotNull(TEXT("Critical texture is resident before interaction"),
		View->GetPresentationResourceObjectForTesting(CriticalHandle));
	TestNull(TEXT("Visible texture is not requested before its node is evaluated"),
		View->GetPresentationResourceObjectForTesting(VisibleHandle));
	TestNull(TEXT("Lazy texture is not part of Document activation"),
		View->GetPresentationResourceObjectForTesting(LazyHandle));

	View->LayoutForTesting(FVector2f(640.0f, 360.0f));
	TestNotNull(TEXT("Displayed Visible texture is requested by the visibility boundary"),
		View->GetPresentationResourceObjectForTesting(VisibleHandle));
	TestNull(TEXT("Layout does not implicitly request Lazy texture"),
		View->GetPresentationResourceObjectForTesting(LazyHandle));
	TestTrue(TEXT("Explicit ResourceId consumption requests a Lazy texture"),
		View->RequestLazyResource(TEXT("resource/texture/residency-2")));
	TestNotNull(TEXT("Explicit Lazy request retains the resolved texture strongly"),
		View->GetPresentationResourceObjectForTesting(LazyHandle));
	TestFalse(TEXT("A resolved Lazy resource is not requested twice"),
		View->RequestLazyResource(TEXT("resource/texture/residency-2")));
	TestFalse(TEXT("Unknown ResourceIds fail without path fallback"),
		View->RequestLazyResource(TEXT("resource/texture/unknown")));
	return true;
}

#endif
