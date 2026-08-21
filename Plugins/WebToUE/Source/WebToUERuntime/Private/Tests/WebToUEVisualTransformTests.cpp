#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"
#include "WebToUEPerformance.h"
#include "WebToUERuntimePresentation.h"

#include "Input/HittestGrid.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEVisualTransformClipTest,
	"WebToUE.Runtime.VisualTransformClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEVisualTransformPatchWorkloadTest,
	"WebToUE.Runtime.VisualTransformPatchWorkload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::VisualTransform::Tests
{
	static bool IsInsideClip(const FWebToUEClipZone& Clip, const FVector2f& Point)
	{
		return FSlateClippingZone(Clip.TopLeft, Clip.TopRight,
			Clip.BottomLeft, Clip.BottomRight).IsPointInside(Point);
	}

	static bool NearlyEqualRect(const FSlateRect& A, const FSlateRect& B)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left, 0.1f) &&
			FMath::IsNearlyEqual(A.Top, B.Top, 0.1f) &&
			FMath::IsNearlyEqual(A.Right, B.Right, 0.1f) &&
			FMath::IsNearlyEqual(A.Bottom, B.Bottom, 0.1f);
	}
}

bool FWebToUEVisualTransformClipTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::VisualTransform::Tests;
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><div id='outer'><div id='inner'><button id='target'></button>"
			"</div></div></body>"),
		TEXT("body { width: 320px; height: 180px; } "
			"#outer { width: 120px; height: 110px; overflow: hidden; "
			"background-color: #203040; transform: translate(90px, 20px) rotate(18deg); "
			"transform-origin: left top; } "
			"#inner { width: 100px; height: 90px; overflow: hidden; "
			"transform: translate(8px, 8px); transform-origin: 0 0; } "
			"#target { width: 150px; height: 44px; background-color: #e06040; "
			"transform: translate(24px, 12px) rotate(28deg) scale(1.15, 0.85); "
			"transform-origin: 0 0; }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	FWebToUEPerformanceSnapshot BuildSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->LayoutForTesting(FVector2f(320.0f, 180.0f));
		BuildSnapshot = Capture.GetSnapshot();
	}

	FWebToUENode* Target = View->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The transformed interactive target hydrates"), Target);
	if (!Target) return false;
	const FWebToUEPaintCommand* Command = View->GetDisplayCommandForTesting(*Target);
	TestNotNull(TEXT("The target owns a transformed display command"), Command);
	if (!Command) return false;
	TestEqual(TEXT("Nested overflow creates a two-zone clip chain"),
		Command->ClipChain.Num(), 2);
	TestTrue(TEXT("The command stores an invertible local-to-view transform"),
		Command->bInvertibleTransform && !Command->LocalToView.IsIdentity());
	TestTrue(TEXT("The transformed AABB differs from the Yoga border-box size"),
		!FMath::IsNearlyEqual(Command->Bounds.GetSize2f().X, Command->LocalSize.X, 0.1f) ||
		!FMath::IsNearlyEqual(Command->Bounds.GetSize2f().Y, Command->LocalSize.Y, 0.1f));
	TestTrue(TEXT("The spatial index consumes transformed visible bounds"),
		Command->bSpatiallyIndexed && View->GetDisplaySpatialCellCountForTesting() > 0);
	TestTrue(TEXT("Build telemetry records transformed commands"),
		BuildSnapshot.GetCounter(
			EWebToUEPerformanceCounter::VisualTransformCommandsResolved) >= 3);
	TestTrue(TEXT("Build telemetry records inherited clip-chain work"),
		BuildSnapshot.GetCounter(
			EWebToUEPerformanceCounter::ClipChainZonesResolved) >= 2);

	FVector2f InsidePoint = FVector2f::ZeroVector;
	bool bFoundInside = false;
	FVector2f ClippedPoint = FVector2f::ZeroVector;
	bool bFoundClipped = false;
	for (int32 Y = 2; Y < FMath::FloorToInt(Command->LocalSize.Y); Y += 4)
	{
		for (int32 X = 2; X < FMath::FloorToInt(Command->LocalSize.X); X += 4)
		{
			const FVector2f Point = Command->LocalToView.TransformPoint(
				FVector2f(static_cast<float>(X), static_cast<float>(Y)));
			bool bInsideAllClips = true;
			for (const FWebToUEClipZone& Clip : Command->ClipChain)
			{
				bInsideAllClips &= IsInsideClip(Clip, Point);
			}
			if (!bFoundInside && bInsideAllClips)
			{
				InsidePoint = Point;
				bFoundInside = true;
			}
			if (!bFoundClipped && !bInsideAllClips)
			{
				ClippedPoint = Point;
				bFoundClipped = true;
			}
		}
	}
	TestTrue(TEXT("The fixture exposes a visible transformed target point"), bFoundInside);
	TestTrue(TEXT("The fixture exposes target geometry outside its clip chain"), bFoundClipped);
	if (bFoundInside)
	{
		FWebToUEPerformanceSnapshot HitSnapshot;
		FWebToUENode* Hit = nullptr;
		{
			FWebToUEPerformanceCapture Capture;
			Hit = View->HitTestForTesting(InsidePoint);
			HitSnapshot = Capture.GetSnapshot();
		}
		TestEqual(TEXT("Inverse-transform hit testing resolves the visual target"), Hit, Target);
		TestTrue(TEXT("Hit telemetry records the exact inverse test"),
			HitSnapshot.GetCounter(EWebToUEPerformanceCounter::InverseHitTests) >= 1);
		TestTrue(TEXT("Hit telemetry records every evaluated clip zone"),
			HitSnapshot.GetCounter(EWebToUEPerformanceCounter::ExactClipTests) >= 2);
	}
	if (bFoundClipped)
	{
		TestNotEqual(TEXT("A transformed point outside either ancestor clip is rejected"),
			View->HitTestForTesting(ClippedPoint), Target);
	}

	bool bRejectedAabbCandidate = false;
	for (float Y = Command->VisibleBounds.Top + 0.5f;
		Y < Command->VisibleBounds.Bottom && !bRejectedAabbCandidate; Y += 1.0f)
	{
		for (float X = Command->VisibleBounds.Left + 0.5f;
			X < Command->VisibleBounds.Right; X += 1.0f)
		{
			const FVector2f Point(X, Y);
			bool bInsideAllClips = true;
			for (const FWebToUEClipZone& Clip : Command->ClipChain)
			{
				bInsideAllClips &= IsInsideClip(Clip, Point);
			}
			const FVector2f LocalPoint = Command->ViewToLocal.TransformPoint(Point);
			const bool bInsideLocalRect = LocalPoint.X >= 0.0f && LocalPoint.Y >= 0.0f &&
				LocalPoint.X <= Command->LocalSize.X && LocalPoint.Y <= Command->LocalSize.Y;
			if (bInsideAllClips && !bInsideLocalRect &&
				View->HitTestForTesting(Point) != Target)
			{
				bRejectedAabbCandidate = true;
				break;
			}
		}
	}
	TestTrue(TEXT("AABB broad phase is followed by exact rotated-quad rejection"),
		bRejectedAabbCandidate);

	TArray<FWebToUESemanticNode> Semantics;
	View->GetSemanticNodes(Semantics);
	const FWebToUESemanticNode* TargetSemantic = Semantics.FindByPredicate(
		[](const FWebToUESemanticNode& Node)
		{
			return Node.ElementId == TEXT("target");
		});
	TestNotNull(TEXT("The transformed button remains in the semantic projection"),
		TargetSemantic);
	if (TargetSemantic)
	{
		TestTrue(TEXT("Semantic bounds expose the transformed border-box bounds"),
			NearlyEqualRect(TargetSemantic->Bounds, Command->Bounds));
	}

	FHittestGrid HittestGrid;
	const TSharedRef<SWindow> PaintWindow = SNew(SWindow)
		.ClientSize(FVector2D(320.0, 180.0));
	FSlateWindowElementList DrawElements(PaintWindow);
	const FGeometry Geometry = FGeometry::MakeRoot(
		FVector2D(320.0, 180.0), FSlateLayoutTransform());
	const FPaintArgs PaintArgs(&View.Get(), HittestGrid,
		FVector2D::ZeroVector, 0.0, 0.0f);
	View->OnPaint(PaintArgs, Geometry,
		FSlateRect(0.0f, 0.0f, 320.0f, 180.0f), DrawElements,
		0, FWidgetStyle(), true);
	const auto& RoundedBoxes = DrawElements.GetUncachedDrawElements()
		.Get<(uint8)EElementType::ET_RoundedBox>();
	TestTrue(TEXT("The visual fixture emits real Slate box elements"),
		!RoundedBoxes.IsEmpty());
	TestTrue(TEXT("Slate receives at least one non-identity render transform"),
		RoundedBoxes.ContainsByPredicate([](const auto& Element)
		{
			return !Element.GetRenderTransform().IsIdentity();
		}));
	return true;
}

bool FWebToUEVisualTransformPatchWorkloadTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::VisualTransform::Tests;
	const TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
		TEXT("<body><button id='target'></button><div id='stable'></div></body>"),
		TEXT("body { width: 320px; height: 180px; gap: 20px; } "
			"#target, #stable { width: 60px; height: 40px; background-color: #406080; "
			"transform-origin: 0 0; } "
			"#target { transform: translate(20px, 10px); } "
			"#target:hover { transform: translate(110px, 30px) rotate(35deg) scale(1.1); }"));
	const TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
	View->SetRuntimeDocumentForTesting(Document);
	View->LayoutForTesting(FVector2f(320.0f, 180.0f));
	FWebToUENode* Target = View->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The patch target hydrates"), Target);
	if (!Target) return false;
	const FWebToUEPaintCommand* Before = View->GetDisplayCommandForTesting(*Target);
	TestNotNull(TEXT("The initial transform command exists"), Before);
	if (!Before) return false;
	const FSlateRect BeforeBounds = Before->VisibleBounds;
	const int32 CommandCount = View->GetDisplayCommandCountForTesting();

	FWebToUEPerformanceSnapshot PatchSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		View->SetHoveredNodeForTesting(Target);
		PatchSnapshot = Capture.GetSnapshot();
	}
	const FWebToUEPaintCommand* After = View->GetDisplayCommandForTesting(*Target);
	TestNotNull(TEXT("The transformed command keeps its stable slot"), After);
	if (!After) return false;
	TestFalse(TEXT("The hover transform changes transformed bounds"),
		NearlyEqualRect(BeforeBounds, After->VisibleBounds));
	TestEqual(TEXT("A leaf transform patches exactly one display command"),
		PatchSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayCommandsPatched),
		uint64(1));
	TestEqual(TEXT("All unrelated display commands remain reused"),
		PatchSnapshot.GetCounter(EWebToUEPerformanceCounter::DisplayCommandsReused),
		static_cast<uint64>(CommandCount - 1));
	TestEqual(TEXT("The transformed spatial entry is patched in place"),
		PatchSnapshot.GetCounter(
			EWebToUEPerformanceCounter::DisplaySpatialIndexPatches), uint64(1));
	TestEqual(TEXT("The transform patch performs no Yoga style write"),
		PatchSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaStyleWrites), uint64(0));
	TestEqual(TEXT("The transform patch performs no Yoga layout result change"),
		PatchSnapshot.GetCounter(
			EWebToUEPerformanceCounter::YogaLayoutResultsChanged), uint64(0));
	TestEqual(TEXT("The leaf patch resolves one visual transform"),
		PatchSnapshot.GetCounter(
			EWebToUEPerformanceCounter::VisualTransformCommandsResolved), uint64(1));
	TestEqual(TEXT("The patch emits one old/new dirty region"),
		View->GetDirtyRectCountForTesting(), 1);
	const FSlateRect* Dirty = View->GetDirtyRectForTesting(0);
	TestNotNull(TEXT("The transformed patch exposes its dirty region"), Dirty);
	if (Dirty)
	{
		TestTrue(TEXT("The dirty region covers the old transformed bounds"),
			FSlateRect::DoRectanglesIntersect(*Dirty, BeforeBounds));
		TestTrue(TEXT("The dirty region covers the new transformed bounds"),
			FSlateRect::DoRectanglesIntersect(*Dirty, After->VisibleBounds));
	}
	return true;
}

#endif
