#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SWebToUEView.h"
#include "WebToUEDocument.h"
#include "WebToUECompositing.h"
#include "WebToUEPerformance.h"
#include "WebToUEResourceContractTestUtils.h"
#include "WebToUESettings.h"
#include "WebToUEStyleProperties.h"

#include "Engine/Texture2D.h"
#include "Input/HittestGrid.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEResourceIntrinsicSizeTest,
	"WebToUE.Runtime.ResourceIntrinsicSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEStaticMaterialLifecycleTest,
	"WebToUE.Runtime.StaticMaterialLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEDynamicMaterialParameterLifecycleTest,
	"WebToUE.Runtime.DynamicMaterialParameterLifecycle",
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

bool FWebToUEStaticMaterialLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::ResourceLifecycle::Tests;
	const FSoftObjectPath MaterialPath(
		TEXT("/Game/WebToUEExamples/Materials/MI_WTUE_StaticMaterialBrush.MI_WTUE_StaticMaterialBrush"));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(
		nullptr, *MaterialPath.ToString());
	if (!TestNotNull(TEXT("The static Material fixture is resident"), Material))
	{
		return false;
	}

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;
	FWebToUECompiledNode& Root = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Root.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Root.Tag = TEXT("body");
	for (const TCHAR* Id : { TEXT("material-a"), TEXT("material-b") })
	{
		FWebToUECompiledNode& Node = CompiledDocument.Nodes.AddDefaulted_GetRef();
		Node.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Node.Tag = TEXT("div");
		Node.ParentIndex = 0;
		AddAttribute(Node, TEXT("id"), Id);
		AddAttribute(Node, TEXT("data-ue-material"), *MaterialPath.ToString());
		AddInlineDeclaration(Node, TEXT("width"), TEXT("160px"));
		AddInlineDeclaration(Node, TEXT("height"), TEXT("90px"));
	}
	FWebToUECompiledResource& Resource =
		CompiledDocument.ResourceManifest.AddDefaulted_GetRef();
	Resource.Kind = EWebToUEResourceKind::Material;
	Resource.Path = MaterialPath;
	Resource.Residency = EWebToUEResidencyClass::Critical;
	Resource.BrushImageSize = FVector2f(1.0f, 1.0f);
	WebToUE::Tests::SealResourceContractForTesting(
		CompiledDocument, TEXT("document/static-material"), 2);
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));

	const TSharedRef<SWebToUEView> FirstView = SNew(SWebToUEView);
	FWebToUEPerformanceSnapshot ColdSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		FirstView->SetDocument(Document);
		PaintView(FirstView);
		ColdSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("One static Material slot is consumed once"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceManifestEntries),
		uint64(1));
	TestEqual(TEXT("The resident static Material is one cache hit"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceCacheHits),
		uint64(1));
	TestEqual(TEXT("Static Material setup has no async request"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceAsyncRequests),
		uint64(0));
	TestEqual(TEXT("Static Material setup has no synchronous load"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceLoadAttempts),
		uint64(0));
	TestEqual(TEXT("Static Material setup has no resource failure"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::ResourceFailures),
		uint64(0));
	TestEqual(TEXT("Two nodes build two lightweight Slate brushes"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::BrushBuilds),
		uint64(3));
	int32 Tier1Entries = 0;
	for (const FWebToUECompositingPlanEntry& Entry :
		FirstView->GetCompositingPlanForTesting().GetEntries())
	{
		if (Entry.Decision.Tier == EWebToUECompositingTier::MaterialBrush)
		{
			++Tier1Entries;
		}
	}
	TestTrue(TEXT("The static Material fixture has an accepted compositing plan"),
		FirstView->GetCompositingPlanForTesting().IsAccepted());
	TestEqual(TEXT("Both static Material nodes classify as Tier 1"), Tier1Entries, 2);
	TestEqual(TEXT("Telemetry records both Tier 1 decisions"),
		ColdSnapshot.GetCounter(EWebToUEPerformanceCounter::CompositingTier1Decisions),
		uint64(2));

	FWebToUENode* MaterialA =
		FirstView->FindRuntimeNodeByIdForTesting(TEXT("material-a"));
	FWebToUENode* MaterialB =
		FirstView->FindRuntimeNodeByIdForTesting(TEXT("material-b"));
	TestNotNull(TEXT("The first static Material node has a brush"),
		MaterialA ? FirstView->GetPresentationBrushIdentityForTesting(*MaterialA) : nullptr);
	TestNotNull(TEXT("The second static Material node has a brush"),
		MaterialB ? FirstView->GetPresentationBrushIdentityForTesting(*MaterialB) : nullptr);
	const int32 MaterialHandle = FirstView->FindPresentationResourceHandleForTesting(
		EWebToUEResourceKind::Material, MaterialPath);
	const UObject* FirstObject =
		FirstView->GetPresentationResourceObjectForTesting(MaterialHandle);
	TestEqual(TEXT("The View strongly retains the engine-owned static Material"),
		FirstObject, static_cast<const UObject*>(Material));
	TestFalse(TEXT("The static path never creates a MID"),
		FirstObject && FirstObject->IsA<UMaterialInstanceDynamic>());

	const TSharedRef<SWebToUEView> SecondView = SNew(SWebToUEView);
	SecondView->SetDocument(Document);
	SecondView->LayoutForTesting(FVector2f(640.0f, 360.0f));
	const int32 SecondHandle = SecondView->FindPresentationResourceHandleForTesting(
		EWebToUEResourceKind::Material, MaterialPath);
	TestEqual(TEXT("Static Material handles are stable across Views"),
		SecondHandle, MaterialHandle);
	TestEqual(TEXT("Views share one engine-owned MaterialInterface"),
		SecondView->GetPresentationResourceObjectForTesting(SecondHandle), FirstObject);

	UWebToUEDocument* WrongClass =
		NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData WrongData;
	WrongData.RootNodeIndex = 0;
	FWebToUECompiledNode& WrongRoot = WrongData.Nodes.AddDefaulted_GetRef();
	WrongRoot.Type = static_cast<uint8>(EWebToUENodeType::Element);
	WrongRoot.Tag = TEXT("body");
	FWebToUECompiledNode& WrongNode = WrongData.Nodes.AddDefaulted_GetRef();
	WrongNode.Type = static_cast<uint8>(EWebToUENodeType::Element);
	WrongNode.Tag = TEXT("div");
	WrongNode.ParentIndex = 0;
	AddAttribute(WrongNode, TEXT("id"), TEXT("wrong-material"));
	AddAttribute(WrongNode, TEXT("data-ue-material"),
		TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	FWebToUECompiledResource& WrongResource =
		WrongData.ResourceManifest.AddDefaulted_GetRef();
	WrongResource.Kind = EWebToUEResourceKind::Material;
	WrongResource.Path = FSoftObjectPath(
		TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	WrongResource.Residency = EWebToUEResidencyClass::Critical;
	WrongResource.BrushImageSize = FVector2f(1.0f, 1.0f);
	WebToUE::Tests::SealResourceContractForTesting(
		WrongData, TEXT("document/static-material-wrong-class"), 2);
	WrongClass->CommitCompiledDocument(MoveTemp(WrongData));
	const TSharedRef<SWebToUEView> WrongView = SNew(SWebToUEView);
	WrongView->SetDocument(WrongClass);
	FWebToUENode* WrongRuntimeNode =
		WrongView->FindRuntimeNodeByIdForTesting(TEXT("wrong-material"));
	TestEqual(TEXT("Wrong-class Material fails once"),
		WrongView->GetPresentationResourceFailuresForTesting(), uint64(1));
	TestNull(TEXT("Wrong-class Material uses deterministic no-brush fallback"),
		WrongRuntimeNode
			? WrongView->GetPresentationBrushIdentityForTesting(*WrongRuntimeNode)
			: nullptr);

	UWebToUEDocument* Pending = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData PendingData;
	PendingData.RootNodeIndex = 0;
	FWebToUECompiledNode& PendingRoot = PendingData.Nodes.AddDefaulted_GetRef();
	PendingRoot.Type = static_cast<uint8>(EWebToUENodeType::Element);
	PendingRoot.Tag = TEXT("body");
	FWebToUECompiledResource& PendingResource =
		PendingData.ResourceManifest.AddDefaulted_GetRef();
	PendingResource.Kind = EWebToUEResourceKind::Material;
	PendingResource.Path = FSoftObjectPath(
		TEXT("/Game/WebToUEAutomation/M_Pending.M_Pending"));
	PendingResource.Residency = EWebToUEResidencyClass::Critical;
	PendingResource.BrushImageSize = FVector2f(1.0f, 1.0f);
	WebToUE::Tests::SealResourceContractForTesting(
		PendingData, TEXT("document/static-material-pending"), 2);
	Pending->CommitCompiledDocument(MoveTemp(PendingData));
	const TSharedRef<SWebToUEView> PendingView = SNew(SWebToUEView);
	AddExpectedError(TEXT("/Game/WebToUEAutomation/M_Pending"),
		EAutomationExpectedErrorFlags::Contains, -1);
	PendingView->SetDocument(Pending);
	TestEqual(TEXT("An unresolved Material requests asynchronously"),
		PendingView->GetPresentationResourceAsyncRequestsForTesting(), uint64(1));
	FWebToUEPerformanceSnapshot MaterialCancellationSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		PendingView->SetDocument(nullptr);
		MaterialCancellationSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("View reset cancels the pending Material request"),
		MaterialCancellationSnapshot.GetCounter(
			EWebToUEPerformanceCounter::ResourceCancellations),
		uint64(1));
	return true;
}

bool FWebToUEDynamicMaterialParameterLifecycleTest::RunTest(
	const FString& Parameters)
{
	using namespace WebToUE::ResourceLifecycle::Tests;
	const FSoftObjectPath MaterialPath(
		TEXT("/Game/WebToUEExamples/Materials/M_WTUE_DynamicMaterialBrush.M_WTUE_DynamicMaterialBrush"));
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(
		nullptr, *MaterialPath.ToString());
	if (!TestNotNull(TEXT("The dynamic Material fixture is resident"), Material))
	{
		return false;
	}

	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;
	FWebToUECompiledNode& Root = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Root.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Root.Tag = TEXT("body");
	FWebToUECompiledNode& Node = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Node.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Node.Tag = TEXT("div");
	Node.ParentIndex = 0;
	AddAttribute(Node, TEXT("id"), TEXT("dynamic-material"));
	AddAttribute(Node, TEXT("data-ue-material"), *MaterialPath.ToString());
	AddInlineDeclaration(Node, TEXT("width"), TEXT("160px"));
	AddInlineDeclaration(Node, TEXT("height"), TEXT("90px"));
	FWebToUECompiledResource& Resource =
		CompiledDocument.ResourceManifest.AddDefaulted_GetRef();
	Resource.Kind = EWebToUEResourceKind::Material;
	Resource.Path = MaterialPath;
	Resource.Residency = EWebToUEResidencyClass::Critical;
	Resource.BrushImageSize = FVector2f(1.0f, 1.0f);
	WebToUE::Tests::SealResourceContractForTesting(
		CompiledDocument, TEXT("document/dynamic-material"), 2);
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));

	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetDocument(Document);
	View->LayoutForTesting(FVector2f(640.0f, 360.0f));
	PaintView(View);
	FWebToUENode* RuntimeNode =
		View->FindRuntimeNodeByIdForTesting(TEXT("dynamic-material"));
	if (!TestNotNull(TEXT("The dynamic Material node hydrates"), RuntimeNode))
	{
		return false;
	}
	FWebToUEMaterialParameterSubmission Submission;
	Submission.Target = View->GetInstanceHandleForTesting(*RuntimeNode);
	Submission.Address = FWebToUEPropertyAddress::Material(
		TEXT("Tint"), EWebToUEMaterialParameterType::Vector);
	Submission.Value = FWebToUEMaterialParameterValue::MakeVector(
		FLinearColor(1.0f, 0.05f, 0.1f, 1.0f));
	TestEqual(TEXT("A static node creates no MID before a parameter diverges"),
		View->GetDynamicMaterialCountForTesting(), 0);
	FWebToUEPerformanceSnapshot FirstParameterSnapshot;
	FWebToUEMaterialParameterSubmitOutcome Outcome;
	{
		FWebToUEPerformanceCapture Capture;
		Outcome = View->SubmitMaterialParameter(Submission);
		FirstParameterSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("A canonical Vector parameter commits"), Outcome.Result,
		EWebToUEMaterialParameterSubmitResult::Committed);
	TestEqual(TEXT("The first divergent node creates exactly one View-owned MID"),
		View->GetDynamicMaterialCountForTesting(), 1);
	UMaterialInstanceDynamic* FirstMid =
		View->GetDynamicMaterialForTesting(Submission.Target);
	TestNotNull(TEXT("The divergent node resolves its MID"), FirstMid);
	if (!FirstMid)
	{
		return false;
	}
	TestEqual(TEXT("The MID receives the typed Vector value"),
		FirstMid->K2_GetVectorParameterValue(TEXT("Tint")), Submission.Value.Vector);
	TestEqual(TEXT("K=1 validates one typed parameter"),
		FirstParameterSnapshot.GetCounter(
			EWebToUEPerformanceCounter::MaterialParameterLookups), uint64(1));
	TestEqual(TEXT("K=1 evaluates one typed parameter"),
		FirstParameterSnapshot.GetCounter(
			EWebToUEPerformanceCounter::MaterialParameterEvaluations), uint64(1));
	TestEqual(TEXT("K=1 creates one MID"),
		FirstParameterSnapshot.GetCounter(
			EWebToUEPerformanceCounter::MaterialInstancesCreated), uint64(1));
	TestEqual(TEXT("K=1 patches one Material brush"),
		FirstParameterSnapshot.GetCounter(
			EWebToUEPerformanceCounter::MaterialBrushPatches), uint64(1));
	TestEqual(TEXT("K=1 patches only the affected Display command"),
		FirstParameterSnapshot.GetCounter(
			EWebToUEPerformanceCounter::DisplayCommandsPatched), uint64(1));
	TestEqual(TEXT("A parameter write performs no synchronous resource load"),
		FirstParameterSnapshot.GetCounter(
			EWebToUEPerformanceCounter::ResourceLoadAttempts), uint64(0));
	TestEqual(TEXT("A parameter write performs no asynchronous resource request"),
		FirstParameterSnapshot.GetCounter(
			EWebToUEPerformanceCounter::ResourceAsyncRequests), uint64(0));

	FWebToUEPerformanceSnapshot UnchangedSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		Outcome = View->SubmitMaterialParameter(Submission);
		UnchangedSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("An identical typed value is unchanged"), Outcome.Result,
		EWebToUEMaterialParameterSubmitResult::Unchanged);
	TestEqual(TEXT("An unchanged value creates no MID"),
		UnchangedSnapshot.GetCounter(
			EWebToUEPerformanceCounter::MaterialInstancesCreated), uint64(0));
	TestEqual(TEXT("An unchanged value patches no brush"),
		UnchangedSnapshot.GetCounter(
			EWebToUEPerformanceCounter::MaterialBrushPatches), uint64(0));

	FWebToUEMaterialParameterSubmission ScalarSubmission;
	ScalarSubmission.Target = Submission.Target;
	ScalarSubmission.Address = FWebToUEPropertyAddress::Material(
		TEXT("Strength"), EWebToUEMaterialParameterType::Scalar);
	ScalarSubmission.Value = FWebToUEMaterialParameterValue::MakeScalar(0.5f);
	FWebToUEPerformanceSnapshot ReuseSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		Outcome = View->SubmitMaterialParameter(ScalarSubmission);
		ReuseSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("A second typed address commits"), Outcome.Result,
		EWebToUEMaterialParameterSubmitResult::Committed);
	TestEqual(TEXT("The second address reuses the node MID"),
		View->GetDynamicMaterialForTesting(Submission.Target), FirstMid);
	TestEqual(TEXT("The second address records one MID reuse"),
		ReuseSnapshot.GetCounter(
			EWebToUEPerformanceCounter::MaterialInstancesReused), uint64(1));
	TestEqual(TEXT("The MID receives the typed Scalar value"),
		FirstMid->K2_GetScalarParameterValue(TEXT("Strength")), 0.5f);

	FWebToUEMaterialParameterSubmission InvalidSubmission = ScalarSubmission;
	InvalidSubmission.Address = FWebToUEPropertyAddress::Material(
		TEXT("Missing"), EWebToUEMaterialParameterType::Scalar);
	Outcome = View->SubmitMaterialParameter(InvalidSubmission);
	TestEqual(TEXT("Unknown parameters fail closed"), Outcome.Result,
		EWebToUEMaterialParameterSubmitResult::RejectedParameter);
	InvalidSubmission.Address = FWebToUEPropertyAddress::Material(
		TEXT("Tint"), EWebToUEMaterialParameterType::Texture);
	InvalidSubmission.Value.Type = EWebToUEMaterialParameterType::Texture;
	Outcome = View->SubmitMaterialParameter(InvalidSubmission);
	TestEqual(TEXT("Texture parameter writes are outside the M4.3b contract"),
		Outcome.Result,
		EWebToUEMaterialParameterSubmitResult::RejectedInvalidAddress);

	FWebToUEMaterialParameterSubmission ConflictSubmission = Submission;
	ConflictSubmission.DurableOwner = EWebToUEPropertyWriter::Behavior;
	ConflictSubmission.Value = FWebToUEMaterialParameterValue::MakeVector(
		FLinearColor(0.1f, 1.0f, 0.1f, 1.0f));
	Outcome = View->SubmitMaterialParameter(ConflictSubmission);
	TestEqual(TEXT("Binding and Behavior cannot claim the same durable address"),
		Outcome.Result, EWebToUEMaterialParameterSubmitResult::RejectedOwnership);

	const TSharedRef<SWebToUEView> SecondView = SNew(SWebToUEView);
	SecondView->SetDocument(Document);
	SecondView->LayoutForTesting(FVector2f(640.0f, 360.0f));
	FWebToUENode* SecondNode =
		SecondView->FindRuntimeNodeByIdForTesting(TEXT("dynamic-material"));
	TestNotNull(TEXT("The second View hydrates an isolated node"), SecondNode);
	if (!SecondNode)
	{
		return false;
	}
	FWebToUEMaterialParameterSubmission SecondSubmission = Submission;
	SecondSubmission.Target = SecondView->GetInstanceHandleForTesting(*SecondNode);
	SecondSubmission.Value = FWebToUEMaterialParameterValue::MakeVector(
		FLinearColor(0.05f, 1.0f, 0.2f, 1.0f));
	Outcome = SecondView->SubmitMaterialParameter(SecondSubmission);
	TestEqual(TEXT("The second View commits its own parameter state"), Outcome.Result,
		EWebToUEMaterialParameterSubmitResult::Committed);
	TestNotEqual(TEXT("Views never share a MID"),
		SecondView->GetDynamicMaterialForTesting(SecondSubmission.Target), FirstMid);
	const int32 FirstResourceHandle = View->FindPresentationResourceHandleForTesting(
		EWebToUEResourceKind::Material, MaterialPath);
	const int32 SecondResourceHandle = SecondView->FindPresentationResourceHandleForTesting(
		EWebToUEResourceKind::Material, MaterialPath);
	TestEqual(TEXT("Views still share the sealed parent Material resource"),
		SecondView->GetPresentationResourceObjectForTesting(SecondResourceHandle),
		View->GetPresentationResourceObjectForTesting(FirstResourceHandle));

	TWeakObjectPtr<UMaterialInstanceDynamic> WeakFirstMid(FirstMid);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(TEXT("The View strongly owns its MID across GC"), WeakFirstMid.IsValid());
	FWebToUEPerformanceSnapshot ResetSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetDocument(nullptr);
		ResetSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("View reset releases one MID ownership slot"),
		ResetSnapshot.GetCounter(
			EWebToUEPerformanceCounter::MaterialInstancesReleased), uint64(1));
	TestEqual(TEXT("View reset removes every MID slot"),
		View->GetDynamicMaterialCountForTesting(), 0);
	Outcome = View->SubmitMaterialParameter(Submission);
	TestEqual(TEXT("The old-generation handle is rejected after reset"), Outcome.Result,
		EWebToUEMaterialParameterSubmitResult::RejectedInvalidTarget);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(TEXT("The released MID becomes collectible"), WeakFirstMid.IsValid());
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

bool FWebToUEResourceIntrinsicSizeTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::ResourceLifecycle::Tests;
	const auto MakeImageDocument = [](const FSoftObjectPath& Path,
		const FVector2f IntrinsicSize, EWebToUEResidencyClass Residency,
		const FString& DocumentId)
	{
		UWebToUEDocument* Document =
			NewObject<UWebToUEDocument>(GetTransientPackage());
		FWebToUECompiledDocumentData CompiledDocument;
		CompiledDocument.RootNodeIndex = 0;
		FWebToUECompiledNode& Root =
			CompiledDocument.Nodes.AddDefaulted_GetRef();
		Root.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Root.Tag = TEXT("body");
		FWebToUECompiledNode& Image =
			CompiledDocument.Nodes.AddDefaulted_GetRef();
		Image.Type = static_cast<uint8>(EWebToUENodeType::Element);
		Image.Tag = TEXT("img");
		Image.ParentIndex = 0;
		AddAttribute(Image, TEXT("id"), TEXT("intrinsic-image"));
		AddAttribute(Image, TEXT("src"), *Path.ToString());
		FWebToUECompiledResource& Resource =
			CompiledDocument.ResourceManifest.AddDefaulted_GetRef();
		Resource.Kind = EWebToUEResourceKind::Texture;
		Resource.Path = Path;
		Resource.Residency = Residency;
		Resource.IntrinsicSize = IntrinsicSize;
		WebToUE::Tests::SealResourceContractForTesting(
			CompiledDocument, DocumentId, 1);
		Document->CommitCompiledDocument(MoveTemp(CompiledDocument));
		return Document;
	};

	const FVector2f SealedLazySize(37.0f, 19.0f);
	const FSoftObjectPath MissingPath(
		TEXT("/Game/WebToUEAutomation/T_UnresidentIntrinsic.T_UnresidentIntrinsic"));
	UWebToUEDocument* LazyDocument = MakeImageDocument(MissingPath,
		SealedLazySize, EWebToUEResidencyClass::Lazy,
		TEXT("document/intrinsic-lazy"));
	const TSharedRef<SWebToUEView> LazyView = SNew(SWebToUEView);
	LazyView->SetDocument(LazyDocument);
	LazyView->LayoutForTesting(FVector2f(640.0f, 360.0f));
	FWebToUENode* LazyImage =
		LazyView->FindRuntimeNodeByIdForTesting(TEXT("intrinsic-image"));
	TestNotNull(TEXT("The unresident intrinsic image hydrates"), LazyImage);
	if (LazyImage)
	{
		TestEqual(TEXT("First measure uses sealed intrinsic pixels"),
			LazyView->MeasurePresentationNodeForTesting(*LazyImage), SealedLazySize);
		TestNull(TEXT("First measure does not require a resident texture"),
			LazyView->GetPresentationBrushIdentityForTesting(*LazyImage));
	}
	TestEqual(TEXT("Lazy first measure performs no async request"),
		LazyView->GetPresentationResourceAsyncRequestsForTesting(), uint64(0));

	const FSoftObjectPath ResidentPath(
		TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	UTexture2D* ResidentTexture =
		LoadObject<UTexture2D>(nullptr, *ResidentPath.ToString());
	TestNotNull(TEXT("The intrinsic-size resident fixture loads"), ResidentTexture);
	if (!ResidentTexture) return false;
	const FIntPoint ResidentImportedSize = ResidentTexture->GetImportedSize();
	const FVector2f ResidentSize(
		ResidentImportedSize.X, ResidentImportedSize.Y);
	UWebToUEDocument* ResidentDocument = MakeImageDocument(ResidentPath,
		ResidentSize, EWebToUEResidencyClass::Critical,
		TEXT("document/intrinsic-resident"));
	const TSharedRef<SWebToUEView> ResidentView = SNew(SWebToUEView);
	ResidentView->SetDocument(ResidentDocument);
	ResidentView->LayoutForTesting(FVector2f(640.0f, 360.0f));
	FWebToUENode* ResidentImage =
		ResidentView->FindRuntimeNodeByIdForTesting(TEXT("intrinsic-image"));
	TestNotNull(TEXT("The resident intrinsic image hydrates"), ResidentImage);
	if (ResidentImage)
	{
		TestNotNull(TEXT("Matching sealed dimensions build one image brush"),
			ResidentView->GetPresentationBrushIdentityForTesting(*ResidentImage));
		TestEqual(TEXT("Resident brush measures at the sealed pixel size"),
			ResidentView->MeasurePresentationNodeForTesting(*ResidentImage), ResidentSize);
	}

	UWebToUEDocument* DriftDocument = MakeImageDocument(ResidentPath,
		ResidentSize + FVector2f(1.0f, 1.0f), EWebToUEResidencyClass::Critical,
		TEXT("document/intrinsic-drift"));
	const TSharedRef<SWebToUEView> DriftView = SNew(SWebToUEView);
	DriftView->SetDocument(DriftDocument);
	FWebToUENode* DriftImage =
		DriftView->FindRuntimeNodeByIdForTesting(TEXT("intrinsic-image"));
	TestEqual(TEXT("Runtime dimension drift fails the Resource contract once"),
		DriftView->GetPresentationResourceFailuresForTesting(), uint64(1));
	TestNull(TEXT("A dimension-mismatched texture is not retained"),
		DriftView->GetPresentationResourceObjectForTesting(0));
	TestNull(TEXT("A dimension-mismatched texture exposes no brush"),
		DriftImage ? DriftView->GetPresentationBrushIdentityForTesting(*DriftImage) : nullptr);
	return true;
}

#endif
