#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"
#include "WebToUEPerformance.h"
#include "WebToUERuntimePresentation.h"

#include "Input/HittestGrid.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEDisplayListOwnershipTest,
	"WebToUE.Runtime.DisplayListOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWebToUEDisplayListOwnershipTest::RunTest(const FString& Parameters)
{
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='clip'><button id='label'>Label</button>"
			"<button id='patch'></button></div></body>"),
		TEXT("body { width: 320px; height: 180px; } "
			"#clip { width: 100px; height: 80px; overflow: hidden; } "
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
	TestEqual(TEXT("The display list contains one command per runtime node"),
		View->GetDisplayCommandCountForTesting(), 5);

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
	PatchCommand = View->GetDisplayCommandForTesting(*Patch);
	TestNotNull(TEXT("The patched command remains in its stable slot"), PatchCommand);
	if (PatchCommand)
	{
		TestFalse(TEXT("The changed paint command receives a new batch key"),
			PatchCommand->BatchKey == InitialPatchKey);
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
	TestEqual(TEXT("Paint visits each stable command in the current checkpoint"),
		PaintSnapshot.GetCounter(EWebToUEPerformanceCounter::PaintCommandsVisited), uint64(5));
	TestTrue(TEXT("Display command replay produces real Slate draw elements"),
		PaintSnapshot.GetCounter(EWebToUEPerformanceCounter::PaintDrawElements) > 0);
	return true;
}

#endif
