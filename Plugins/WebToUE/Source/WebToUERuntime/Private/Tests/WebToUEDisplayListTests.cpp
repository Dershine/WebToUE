#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"
#include "WebToUEPerformance.h"
#include "WebToUERuntimePresentation.h"

#include "Input/HittestGrid.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Rendering/DrawElements.h"
#include "RenderingThread.h"
#include "Serialization/BufferArchive.h"
#include "Slate/WidgetRenderer.h"
#include "Types/PaintArgs.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEDisplayListOwnershipTest,
	"WebToUE.Runtime.DisplayListOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUESpatialPaintHitWorkloadTest,
	"WebToUE.Runtime.SpatialPaintHitWorkload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEDisplayListDebugOverlayTest,
	"WebToUE.Runtime.DisplayListDebugOverlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEDisplayListDebugImageTest,
	"WebToUE.Runtime.DisplayListDebugImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUESlateBatchCompatibilityTest,
	"WebToUE.Runtime.SlateBatchCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUESlateBatchCompatibilityTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='a' class='batch'></div><div id='b' class='batch'></div>"
			"<div id='clip'><div id='c' class='batch'></div></div></body>"),
		TEXT("body { width: 320px; height: 180px; gap: 4px; } "
			".batch { width: 80px; height: 30px; border-width: 1px; "
			"border-radius: 4px; border-color: #90a0b0; } "
			"#a { background-color: #203040; } "
			"#b { background-color: #604020; } "
			"#clip { width: 100px; height: 30px; overflow: hidden; } "
			"#c { background-color: #204060; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	View->LayoutForTesting(FVector2f(320.0f, 180.0f));

	FHittestGrid HittestGrid;
	FSlateWindowElementList DrawElements(nullptr);
	const FGeometry Geometry = FGeometry::MakeRoot(
		FVector2D(320.0, 180.0), FSlateLayoutTransform());
	const FPaintArgs PaintArgs(&View.Get(), HittestGrid,
		FVector2D::ZeroVector, 0.0, 0.0f);
	FWebToUEPerformanceSnapshot Snapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->OnPaint(PaintArgs, Geometry,
			FSlateRect(0.0f, 0.0f, 320.0f, 180.0f), DrawElements,
			0, FWidgetStyle(), true);
		Snapshot = Capture.GetSnapshot();
	}

	const auto& RoundedBoxes = DrawElements.GetUncachedDrawElements()
		.Get<(uint8)EElementType::ET_RoundedBox>();
	TestEqual(TEXT("The corpus produces three real Slate rounded-box elements"),
		RoundedBoxes.Num(), 3);
	if (RoundedBoxes.Num() != 3) return false;
	TestEqual(TEXT("Different vertex colors retain the same compatible Slate layer"),
		RoundedBoxes[0].GetLayer(), RoundedBoxes[1].GetLayer());
	TestTrue(TEXT("Equal un-clipped boxes retain the same clip state"),
		RoundedBoxes[0].GetClippingHandle() == RoundedBoxes[1].GetClippingHandle());
	TestTrue(TEXT("A different hierarchical clip receives a different Slate layer"),
		RoundedBoxes[2].GetLayer() > RoundedBoxes[1].GetLayer());
	TestFalse(TEXT("A different hierarchical clip receives a different clip state"),
		RoundedBoxes[1].GetClippingHandle() == RoundedBoxes[2].GetClippingHandle());
	TestEqual(TEXT("The two compatible commands form one source batch run"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::PaintCommandsLayerMerged),
		uint64(1));
	TestEqual(TEXT("The clip boundary splits the source commands into two runs"),
		Snapshot.GetCounter(EWebToUEPerformanceCounter::PaintBatchRuns), uint64(2));
	return true;
}

bool FWebToUEDisplayListOwnershipTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='clip'><button id='label'>Label</button>"
			"<button id='patch'></button></div></body>"),
		TEXT("body { width: 320px; height: 180px; } "
			"#clip { width: 100px; height: 80px; overflow: hidden; gap: 10px; } "
			"button { width: 80px; height: 30px; background-color: #203040; } "
			"#patch:hover { background-color: #406080; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);

	FWebToUENode* Body = nullptr;
	FWebToUENode* Clip = nullptr;
	FWebToUENode* Label = nullptr;
	FWebToUENode* LabelText = nullptr;
	FWebToUENode* Patch = nullptr;
	Document->ForEachNode([&](FWebToUENode& Node)
	{
		const FString Id = Node.GetAttribute(TEXT("id"));
		if (Node.Tag == TEXT("body")) Body = &Node;
		if (Id == TEXT("clip")) Clip = &Node;
		if (Id == TEXT("label")) Label = &Node;
		if (Id == TEXT("patch")) Patch = &Node;
		if (Node.Type == EWebToUENodeType::Text && Node.Parent &&
			Node.Parent->GetAttribute(TEXT("id")) == TEXT("label"))
		{
			LabelText = &Node;
		}
	});
	TestNotNull(TEXT("The body exists"), Body);
	TestNotNull(TEXT("The clip owner exists"), Clip);
	TestNotNull(TEXT("The label owner exists"), Label);
	TestNotNull(TEXT("The label text run exists"), LabelText);
	TestNotNull(TEXT("The paint-only patch target exists"), Patch);
	if (!Body || !Clip || !Label || !LabelText || !Patch) return false;

	FWebToUEPerformanceSnapshot BuildSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->LayoutForTesting(FVector2f(320.0f, 180.0f));
		BuildSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("One display list is built for the hydrated view"),
		BuildSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayListBuilds), uint64(1));
	TestEqual(TEXT("Every runtime node owns one stable display command slot"),
		BuildSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayCommandsBuilt), uint64(5));
	TestEqual(TEXT("The command build creates one spatial index"),
		BuildSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplaySpatialIndexBuilds), uint64(1));
	TestEqual(TEXT("The display list contains one command per runtime node"),
		View->GetDisplayCommandCountForTesting(), 5);
	TestTrue(TEXT("Visible paint or input commands populate spatial cells"),
		View->GetDisplaySpatialCellCountForTesting() > 0);

	const FWebToUEDisplayCommandRange* BodyRange =
		View->GetDisplayCommandRangeForTesting(*Body);
	const FWebToUEDisplayCommandRange* ClipRange =
		View->GetDisplayCommandRangeForTesting(*Clip);
	const FWebToUEDisplayCommandRange* PatchRange =
		View->GetDisplayCommandRangeForTesting(*Patch);
	TestNotNull(TEXT("The body owns a command range"), BodyRange);
	TestNotNull(TEXT("The clipped subtree owns a command range"), ClipRange);
	TestNotNull(TEXT("The leaf owns a command range"), PatchRange);
	if (!BodyRange || !ClipRange || !PatchRange) return false;
	TestEqual(TEXT("The root range covers the complete display list"), BodyRange->Num, 5);
	TestEqual(TEXT("The clipped range covers itself and both button subtrees"), ClipRange->Num, 4);
	TestEqual(TEXT("The empty patch button owns exactly one command"), PatchRange->Num, 1);

	const FWebToUEPaintCommand* LabelCommand =
		View->GetDisplayCommandForTesting(*Label);
	const FWebToUEPaintCommand* TextCommand =
		View->GetDisplayCommandForTesting(*LabelText);
	const FWebToUEPaintCommand* PatchCommand =
		View->GetDisplayCommandForTesting(*Patch);
	const FWebToUEPaintCommand* ClipCommand =
		View->GetDisplayCommandForTesting(*Clip);
	TestNotNull(TEXT("The element command resolves by Instance Handle"), LabelCommand);
	TestNotNull(TEXT("The text command resolves by Instance Handle"), TextCommand);
	TestNotNull(TEXT("The patch command resolves by Instance Handle"), PatchCommand);
	TestNotNull(TEXT("The clip command resolves by Instance Handle"), ClipCommand);
	if (!LabelCommand || !TextCommand || !PatchCommand || !ClipCommand) return false;
	TestEqual(TEXT("The element command retains its Instance Handle owner"),
		View->ResolveInstanceHandleForTesting(LabelCommand->Owner), Label);
	TestEqual(TEXT("The text run is owned by its text node handle"),
		View->ResolveInstanceHandleForTesting(TextCommand->Owner), LabelText);
	TestEqual(TEXT("Text and box commands keep distinct batch kinds"),
		TextCommand->BatchKey.Type, EWebToUEPaintCommandType::Text);
	TestEqual(TEXT("The box command keeps its batch kind"),
		LabelCommand->BatchKey.Type, EWebToUEPaintCommandType::Box);
	TestTrue(TEXT("A descendant command carries the ancestor clip"), LabelCommand->bHasClip);
	TestTrue(TEXT("The clipped subtree retains hierarchical visible bounds"),
		FSlateRect::DoRectanglesIntersect(ClipCommand->SubtreeBounds,
			LabelCommand->VisibleBounds));
	TestEqual(TEXT("The descendant clip uses the clip owner's left bound"),
		LabelCommand->ClipBounds.Left, ClipCommand->Bounds.Left);
	TestEqual(TEXT("The descendant clip uses the clip owner's bottom bound"),
		LabelCommand->ClipBounds.Bottom, ClipCommand->Bounds.Bottom);

	const FWebToUEPaintBatchKey InitialPatchKey = PatchCommand->BatchKey;
	FWebToUEPerformanceSnapshot PatchSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetHoveredNodeForTesting(Patch);
		PatchSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("A paint-only update does not rebuild the display list"),
		PatchSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayListBuilds), uint64(0));
	TestEqual(TEXT("A leaf paint-only update patches exactly one command"),
		PatchSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayCommandsPatched), uint64(1));
	TestEqual(TEXT("The remaining non-intersecting commands are reused"),
		PatchSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayCommandsReused), uint64(4));
	TestEqual(TEXT("The leaf patch updates one spatial entry in place"),
		PatchSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplaySpatialIndexPatches), uint64(1));
	TestEqual(TEXT("The paint-only leaf contributes one dirty command"),
		View->GetDirtyCommandCountForTesting(), 1);
	TestEqual(TEXT("The paint-only leaf contributes one dirty rect"),
		View->GetDirtyRectCountForTesting(), 1);
	PatchCommand = View->GetDisplayCommandForTesting(*Patch);
	TestNotNull(TEXT("The patched command remains in its stable slot"), PatchCommand);
	if (PatchCommand)
	{
		TestTrue(TEXT("A vertex-color-only paint change preserves the compatible batch key"),
			PatchCommand->BatchKey == InitialPatchKey);
		const FSlateRect* DirtyRect = View->GetDirtyRectForTesting(0);
		TestNotNull(TEXT("The local patch exposes a dirty rectangle"), DirtyRect);
		if (DirtyRect)
		{
			TestTrue(TEXT("The dirty rectangle covers the patched command"),
				FSlateRect::DoRectanglesIntersect(*DirtyRect, PatchCommand->VisibleBounds));
			TestFalse(TEXT("The dirty rectangle excludes the disjoint label command"),
				FSlateRect::DoRectanglesIntersect(*DirtyRect, LabelCommand->VisibleBounds));
		}
	}

	FHittestGrid HittestGrid;
	FSlateWindowElementList DrawElements(nullptr);
	const FGeometry Geometry = FGeometry::MakeRoot(
		FVector2D(320.0, 180.0), FSlateLayoutTransform());
	const FPaintArgs PaintArgs(&View.Get(), HittestGrid,
		FVector2D::ZeroVector, 0.0, 0.0f);
	FWebToUEPerformanceSnapshot PaintSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->OnPaint(PaintArgs, Geometry,
			FSlateRect(0.0f, 0.0f, 320.0f, 180.0f), DrawElements,
			0, FWidgetStyle(), true);
		PaintSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("Paint replays the owned command list without rebuilding it"),
		PaintSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayListBuilds), uint64(0));
	TestEqual(TEXT("Paint visits only the three drawable visible commands"),
		PaintSnapshot.GetCounter(EWebToUEPerformanceCounter::PaintCommandsVisited), uint64(3));
	TestEqual(TEXT("Paint culls two non-drawable commands before replay"),
		PaintSnapshot.GetCounter(EWebToUEPerformanceCounter::PaintCommandsCulled), uint64(2));
	TestTrue(TEXT("Display command replay produces real Slate draw elements"),
		PaintSnapshot.GetCounter(EWebToUEPerformanceCounter::PaintDrawElements) > 0);
	TestEqual(TEXT("Paint consumes the pending dirty rectangles"),
		View->GetDirtyRectCountForTesting(), 0);
	return true;
}

bool FWebToUESpatialPaintHitWorkloadTest::RunTest(const FString& Parameters)
{
	constexpr int32 ItemCount = 512;
	FString Html(TEXT("<body>"));
	Html.Reserve(ItemCount * 32);
	for (int32 Index = 0; Index < ItemCount; ++Index)
	{
		Html += FString::Printf(TEXT("<button id='item%d'></button>"), Index);
	}
	Html += TEXT("</body>");
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(Html,
		TEXT("body { width: 100px; height: 10240px; } "
			"button { width: 100px; height: 20px; flex-shrink: 0; "
			"background-color: #203040; } "
			"button:hover { background-color: #406080; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	View->LayoutForTesting(FVector2f(100.0f, 100.0f));
	FWebToUENode* First = nullptr;
	FWebToUENode* Far = nullptr;
	Document->ForEachNode([&](FWebToUENode& Node)
	{
		const FString Id = Node.GetAttribute(TEXT("id"));
		if (Id == TEXT("item0")) First = &Node;
		if (Id == TEXT("item400")) Far = &Node;
	});
	TestNotNull(TEXT("The first spatial workload item exists"), First);
	TestNotNull(TEXT("The distant spatial workload item exists"), Far);
	if (!First || !Far) return false;
	TestEqual(TEXT("The workload owns one command per body and item"),
		View->GetDisplayCommandCountForTesting(), ItemCount + 1);
	TestTrue(TEXT("The tall workload spans multiple spatial cells"),
		View->GetDisplaySpatialCellCountForTesting() > 32);

	FWebToUEPerformanceSnapshot HitSnapshot;
	FWebToUENode* Hit = nullptr;
	{
		FWebToUEPerformanceCapture Capture;
		Hit = View->HitTestForTesting(FVector2f(50.0f, 10.0f));
		HitSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("The spatial hit query preserves the topmost interactive result"), Hit, First);
	TestTrue(TEXT("K=1 hit candidates stay independent of the 512-node corpus"),
		HitSnapshot.GetCounter(EWebToUEPerformanceCounter::HitTestCandidates) <= 2);
	TestTrue(TEXT("K=1 hit visits stay independent of the 512-node corpus"),
		HitSnapshot.GetCounter(EWebToUEPerformanceCounter::HitTestCommandsVisited) <= 2);

	FWebToUEPerformanceSnapshot PatchSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetHoveredNodeForTesting(Far);
		PatchSnapshot = Capture.GetSnapshot();
	}
	TestEqual(TEXT("A distant paint-only change patches one command"),
		PatchSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayCommandsPatched), uint64(1));
	TestEqual(TEXT("A distant paint-only change reuses all disjoint command regions"),
		PatchSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayCommandsReused),
		uint64(ItemCount));
	TestEqual(TEXT("The distant change has one explicit dirty rectangle"),
		View->GetDirtyRectCountForTesting(), 1);
	const FSlateRect* FarDirtyRect = View->GetDirtyRectForTesting(0);
	TestNotNull(TEXT("The distant dirty rectangle is inspectable"), FarDirtyRect);
	if (FarDirtyRect)
	{
		TestTrue(TEXT("The distant dirty rectangle lies outside the visible viewport"),
			FarDirtyRect->Top >= 8000.0f);
	}

	FHittestGrid HittestGrid;
	FSlateWindowElementList DrawElements(nullptr);
	const FGeometry Geometry = FGeometry::MakeRoot(
		FVector2D(100.0, 100.0), FSlateLayoutTransform());
	const FPaintArgs PaintArgs(&View.Get(), HittestGrid,
		FVector2D::ZeroVector, 0.0, 0.0f);
	FWebToUEPerformanceSnapshot PaintSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->OnPaint(PaintArgs, Geometry,
			FSlateRect(0.0f, 0.0f, 100.0f, 100.0f), DrawElements,
			0, FWidgetStyle(), true);
		PaintSnapshot = Capture.GetSnapshot();
	}
	TestTrue(TEXT("Visible paint work is bounded by the six viewport items"),
		PaintSnapshot.GetCounter(EWebToUEPerformanceCounter::PaintCommandsVisited) <= 6);
	TestTrue(TEXT("The spatial query culls at least 500 offscreen commands"),
		PaintSnapshot.GetCounter(EWebToUEPerformanceCounter::PaintCommandsCulled) >= 500);
	TestTrue(TEXT("The retained visible region still emits Slate draw elements"),
		PaintSnapshot.GetCounter(EWebToUEPerformanceCounter::PaintDrawElements) > 0);
	return true;
}

bool FWebToUEDisplayListDebugOverlayTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><button id='target'></button><button id='other'></button></body>"),
		TEXT("body { width: 200px; height: 100px; } "
			"button { width: 80px; height: 30px; background-color: #203040; } "
			"#target:hover { background-color: #406080; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	View->LayoutForTesting(FVector2f(200.0f, 100.0f));
	FWebToUENode* Target = nullptr;
	Document->ForEachNode([&Target](FWebToUENode& Node)
	{
		if (Node.GetAttribute(TEXT("id")) == TEXT("target")) Target = &Node;
	});
	TestNotNull(TEXT("The debug-overlay target exists"), Target);
	if (!Target) return false;
	View->SetHoveredNodeForTesting(Target);
	TestEqual(TEXT("The debug-overlay input owns one dirty rect"),
		View->GetDirtyRectCountForTesting(), 1);

	IConsoleVariable* DebugVariable =
		IConsoleManager::Get().FindConsoleVariable(TEXT("WebToUE.Debug.DisplayList"));
	TestNotNull(TEXT("The Display List debug cvar is registered"), DebugVariable);
	if (!DebugVariable) return false;
	const int32 PreviousDebugMode = DebugVariable->GetInt();
	ON_SCOPE_EXIT
	{
		DebugVariable->Set(PreviousDebugMode, ECVF_SetByCode);
	};
	DebugVariable->Set(3, ECVF_SetByCode);

	FHittestGrid HittestGrid;
	FSlateWindowElementList DrawElements(nullptr);
	const FGeometry Geometry = FGeometry::MakeRoot(
		FVector2D(200.0, 100.0), FSlateLayoutTransform());
	const FPaintArgs PaintArgs(&View.Get(), HittestGrid,
		FVector2D::ZeroVector, 0.0, 0.0f);
	View->OnPaint(PaintArgs, Geometry,
		FSlateRect(0.0f, 0.0f, 200.0f, 100.0f), DrawElements,
		0, FWidgetStyle(), true);
	const auto& DebugBoxes = DrawElements.GetUncachedDrawElements().Get<
		static_cast<uint8>(EElementType::ET_Box)>();
	TestTrue(TEXT("Command and dirty overlays emit inspectable Slate box elements"),
		DebugBoxes.Num() >= 8);
	TestEqual(TEXT("The visualized dirty state is consumed after paint"),
		View->GetDirtyRectCountForTesting(), 0);
	return true;
}

bool FWebToUEDisplayListDebugImageTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><section id='panel'><h1>Display List</h1>"
			"<button id='target'>Dirty Command</button>"
			"<button id='stable'>Reused Region</button></section></body>"),
		TEXT("body { width: 320px; height: 180px; padding: 20px; "
			"background-color: #101820; } "
			"#panel { width: 280px; height: 140px; padding: 12px; gap: 10px; "
			"background-color: #1d2a38; overflow: hidden; } "
			"h1 { width: 240px; height: 28px; color: #102030; font-size: 20px; } "
			"button { width: 220px; height: 34px; flex-shrink: 0; color: #102030; "
			"background-color: #24536b; } "
			"#target:hover { background-color: #d97824; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	View->LayoutForTesting(FVector2f(320.0f, 180.0f));
	FWebToUENode* Target = nullptr;
	Document->ForEachNode([&Target](FWebToUENode& Node)
	{
		if (Node.GetAttribute(TEXT("id")) == TEXT("target")) Target = &Node;
	});
	TestNotNull(TEXT("The debug-image target exists"), Target);
	if (!Target) return false;
	View->SetHoveredNodeForTesting(Target);

	IConsoleVariable* DebugVariable =
		IConsoleManager::Get().FindConsoleVariable(TEXT("WebToUE.Debug.DisplayList"));
	TestNotNull(TEXT("The Display List debug cvar is registered for image evidence"),
		DebugVariable);
	if (!DebugVariable) return false;
	const int32 PreviousDebugMode = DebugVariable->GetInt();
	ON_SCOPE_EXIT
	{
		DebugVariable->Set(PreviousDebugMode, ECVF_SetByCode);
	};
	DebugVariable->Set(3, ECVF_SetByCode);

	FWidgetRenderer Renderer(false, true);
	UTextureRenderTarget2D* RenderTarget =
		Renderer.DrawWidget(View, FVector2D(320.0, 180.0));
	TestNotNull(TEXT("The debug widget renders to a texture"), RenderTarget);
	if (!RenderTarget) return false;
	FlushRenderingCommands();
	FBufferArchive PngBytes;
	TestTrue(TEXT("The debug render target exports as PNG"),
		FImageUtils::ExportRenderTarget2DAsPNG(RenderTarget, PngBytes));
	const FString OutputDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("WebToUEAutomation"));
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString OutputPath = FPaths::Combine(
		OutputDirectory, TEXT("DisplayListDebugOverlay.png"));
	TestTrue(TEXT("The debug PNG is written for visual inspection"),
		FFileHelper::SaveArrayToFile(PngBytes, *OutputPath));
	TestTrue(TEXT("The debug PNG has non-empty encoded content"),
		IFileManager::Get().FileSize(*OutputPath) > 1024);
	AddInfo(FString::Printf(TEXT("DISPLAY_LIST_DEBUG_IMAGE=%s"), *OutputPath));
	return true;
}

#endif
