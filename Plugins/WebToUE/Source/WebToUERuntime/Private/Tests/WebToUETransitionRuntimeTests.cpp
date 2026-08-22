#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUEDocument.h"
#include "WebToUEPerformance.h"
#include "WebToUEResourceContractTestUtils.h"
#include "WebToUERuntimePresentation.h"
#include "WebToUESession.h"
#include "WebToUEStyleProperties.h"
#include "WebToUEView.h"

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Input/HittestGrid.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETransitionOpacityAdapterTest,
	"WebToUE.Runtime.TransitionOpacityAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUETransitionPaintTransformAdapterTest,
	"WebToUE.Runtime.TransitionPaintTransformAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::Transition::Tests
{
	static void AddAttribute(FWebToUECompiledNode& Node,
		const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUECompiledAttribute& Attribute = Node.Attributes.AddDefaulted_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
	}

	static void AddDeclaration(FAutomationTestBase& Test,
		FWebToUECompiledRule& Rule,
		const TCHAR* Name, const TCHAR* Value)
	{
		FWebToUEStyleDeclaration Parsed;
		if (!WebToUE::Private::TryParseCssDeclaration(Name, Value, Parsed))
		{
			Test.AddError(FString::Printf(
				TEXT("Test fixture declaration failed to parse: %s: %s"),
				Name, Value));
			return;
		}
		FWebToUECompiledDeclaration& Declaration =
			Rule.Declarations.AddDefaulted_GetRef();
		Declaration.Property = Parsed.Property;
		Declaration.TypedValue = Parsed.TypedValue;
	}

	static void PaintView(const TSharedRef<SWebToUEView>& View)
	{
		FHittestGrid HittestGrid;
		FSlateWindowElementList DrawElements(nullptr);
		const FGeometry Geometry = FGeometry::MakeRoot(
			FVector2D(320.0, 180.0), FSlateLayoutTransform());
		const FPaintArgs PaintArgs(
			&View.Get(), HittestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
		View->OnPaint(PaintArgs, Geometry,
			FSlateRect(0.0f, 0.0f, 320.0f, 180.0f), DrawElements,
			0, FWidgetStyle(), true);
	}
}

bool FWebToUETransitionOpacityAdapterTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::Transition::Tests;
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;
	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	FWebToUECompiledNode& Button = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Button.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Button.Tag = TEXT("button");
	Button.ParentIndex = 0;
	AddAttribute(Button, TEXT("id"), TEXT("target"));
	FWebToUECompiledNode& Text = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Text.Type = static_cast<uint8>(EWebToUENodeType::Text);
	Text.Tag = TEXT("#text");
	Text.Text = TEXT("Opacity transition");
	Text.LocalizedText = FText::FromString(Text.Text);
	Text.ParentIndex = 1;

	FWebToUECompiledRule& BaseRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	BaseRule.Specificity = 100;
	BaseRule.Selector.AddDefaulted_GetRef().Id = TEXT("target");
	AddDeclaration(*this, BaseRule, TEXT("width"), TEXT("160px"));
	AddDeclaration(*this, BaseRule, TEXT("height"), TEXT("48px"));
	AddDeclaration(*this, BaseRule, TEXT("background-color"), TEXT("white"));
	AddDeclaration(*this, BaseRule, TEXT("opacity"), TEXT("1"));
	AddDeclaration(*this, BaseRule, TEXT("transition"), TEXT("opacity 1s linear"));
	FWebToUECompiledRule& HoverRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	HoverRule.Specificity = 110;
	FWebToUECompiledSelectorSegment& HoverSelector =
		HoverRule.Selector.AddDefaulted_GetRef();
	HoverSelector.Id = TEXT("target");
	HoverSelector.RequiredState = static_cast<uint8>(EWebToUEPseudoState::Hover);
	AddDeclaration(*this, HoverRule, TEXT("opacity"), TEXT("0.2"));

	FWebToUECompiledTransition& Transition =
		CompiledDocument.AnimationIR.Transitions.AddDefaulted_GetRef();
	Transition.TransitionId = TEXT("transition.00000001.opacity");
	Transition.Target.TargetNodeIndex = 1;
	Transition.Target.Kind = EWebToUECompiledAnimationTargetKind::Opacity;
	Transition.DurationSeconds = 1.0;
	Transition.Easing = EWebToUETransitionEasing::Linear;
	Transition.ClockDomain = EWebToUEClockDomain::Game;
	WebToUE::Tests::SealResourceContractForTesting(CompiledDocument);
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));

	UWorld* World = NewObject<UWorld>(GetTransientPackage());
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	UWebToUEView* View = NewObject<UWebToUEView>(GetTransientPackage());
	World->AddToRoot();
	Player->AddToRoot();
	View->AddToRoot();
	const TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
	FString Error;
	Clock->SetTimeSeconds(EWebToUEClockDomain::Game, 10.0, Error);
	FWebToUESessionCreateParams SessionParams;
	SessionParams.World = World;
	SessionParams.LocalPlayer = Player;
	SessionParams.Surface.SurfaceId = TEXT("webtoue.transition.opacity-test");
	SessionParams.Clock = Clock;
	const TSharedPtr<FWebToUESession> Session =
		FWebToUESession::Create(SessionParams, Error);
	TestNotNull(TEXT("Opacity adapter test creates a controlled UI Session"),
		Session.Get());
	if (!Session)
	{
		View->RemoveFromRoot();
		Player->RemoveFromRoot();
		World->RemoveFromRoot();
		return false;
	}
	View->SetDocument(Document);
	View->SetSession(Session);
	View->TakeWidget();
	const TSharedPtr<SWebToUEView> SlateView = View->GetSlateViewForTesting();
	TestNotNull(TEXT("The production UWidget owns its Slate transition adapter"),
		SlateView.Get());
	if (!SlateView)
	{
		Session->Invalidate();
		View->RemoveFromRoot();
		Player->RemoveFromRoot();
		World->RemoveFromRoot();
		return false;
	}
	PaintView(SlateView.ToSharedRef());
	FWebToUENode* RuntimeButton =
		SlateView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	TestNotNull(TEXT("The compiled Transition target hydrates"), RuntimeButton);
	if (!RuntimeButton)
	{
		Session->Invalidate();
		View->ReleaseSlateResources(true);
		View->RemoveFromRoot();
		Player->RemoveFromRoot();
		World->RemoveFromRoot();
		return false;
	}
	const FWebToUENode* RuntimeText = RuntimeButton->Children.IsEmpty()
		? nullptr : RuntimeButton->Children[0].Get();
	TestFalse(TEXT("Idle View owns no animation ticker"),
		Session->GetAnimationCoordinator()->IsTickerRegistered());
	TestEqual(TEXT("Idle View owns zero active Tracks"),
		Session->GetAnimationCoordinator()->GetActiveTrackCount(), 0);

	SlateView->SetHoveredNodeForTesting(RuntimeButton);
	const FWebToUEPaintCommand* StartedCommand =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	const FWebToUEPaintCommand* StartedTextCommand = RuntimeText
		? SlateView->GetDisplayCommandForTesting(*RuntimeText) : nullptr;
	TestTrue(TEXT("Opacity underlying style commits transactionally before sampling"),
		FMath::IsNearlyEqual(
			SlateView->GetComputedStyleForTesting(*RuntimeButton).Opacity, 0.2f));
	TestTrue(TEXT("Opacity Track starts from the previous visible value"),
		StartedCommand && FMath::IsNearlyEqual(StartedCommand->Opacity, 1.0f));
	TestTrue(TEXT("Parent opacity overlay propagates to descendant paint commands"),
		StartedTextCommand && FMath::IsNearlyEqual(StartedTextCommand->Opacity, 1.0f));
	TestTrue(TEXT("Active Opacity owns one Track and one active-only ticker"),
		Session->GetAnimationCoordinator()->GetActiveTrackCount() == 1 &&
		Session->GetAnimationCoordinator()->IsTickerRegistered());
	TestFalse(TEXT("Opacity sampling never dirties Yoga layout"),
		SlateView->IsPresentationLayoutDirtyForTesting());

	Clock->Advance(EWebToUEClockDomain::Game, 0.5, Error);
	TestEqual(TEXT("K=1 Pump evaluates exactly one Opacity Track"),
		Session->GetAnimationCoordinator()->Pump(), 1);
	const FWebToUEPaintCommand* MidpointCommand =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	TestTrue(TEXT("Virtual Clock produces the exact linear Opacity midpoint"),
		MidpointCommand && FMath::IsNearlyEqual(MidpointCommand->Opacity, 0.6f));
	TestFalse(TEXT("Midpoint sampling remains layout-clean"),
		SlateView->IsPresentationLayoutDirtyForTesting());

	Clock->Advance(EWebToUEClockDomain::Game, 0.5, Error);
	Session->GetAnimationCoordinator()->Pump();
	const FWebToUEPaintCommand* CompletedCommand =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	TestTrue(TEXT("Completion releases overlay to the latest underlying Opacity"),
		CompletedCommand && FMath::IsNearlyEqual(CompletedCommand->Opacity, 0.2f));
	TestTrue(TEXT("Completion releases Track and ticker"),
		Session->GetAnimationCoordinator()->GetActiveTrackCount() == 0 &&
		!Session->GetAnimationCoordinator()->IsTickerRegistered());

	SlateView->SetHoveredNodeForTesting(nullptr);
	TestEqual(TEXT("Reverse state change reacquires one controlled Track"),
		Session->GetAnimationCoordinator()->GetActiveTrackCount(), 1);
	View->SetDocument(nullptr);
	TestTrue(TEXT("Document generation advance synchronously releases View overlays"),
		Session->GetAnimationCoordinator()->GetActiveTrackCount() == 0 &&
		!Session->GetAnimationCoordinator()->IsTickerRegistered());

	View->ReleaseSlateResources(true);
	View->ClearSession();
	Session->Invalidate();
	View->RemoveFromRoot();
	Player->RemoveFromRoot();
	World->RemoveFromRoot();
	return true;
}

bool FWebToUETransitionPaintTransformAdapterTest::RunTest(
	const FString& Parameters)
{
	using namespace WebToUE::Transition::Tests;
	UWebToUEDocument* Document = NewObject<UWebToUEDocument>(GetTransientPackage());
	FWebToUECompiledDocumentData CompiledDocument;
	CompiledDocument.RootNodeIndex = 0;
	FWebToUECompiledNode& Body = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Body.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Body.Tag = TEXT("body");
	FWebToUECompiledNode& Button = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Button.Type = static_cast<uint8>(EWebToUENodeType::Element);
	Button.Tag = TEXT("button");
	Button.ParentIndex = 0;
	AddAttribute(Button, TEXT("id"), TEXT("target"));
	FWebToUECompiledNode& Text = CompiledDocument.Nodes.AddDefaulted_GetRef();
	Text.Type = static_cast<uint8>(EWebToUENodeType::Text);
	Text.Tag = TEXT("#text");
	Text.Text = TEXT("Paint and transform transition");
	Text.LocalizedText = FText::FromString(Text.Text);
	Text.ParentIndex = 1;

	FWebToUECompiledRule& BaseRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	BaseRule.Specificity = 100;
	BaseRule.Selector.AddDefaulted_GetRef().Id = TEXT("target");
	AddDeclaration(*this, BaseRule, TEXT("width"), TEXT("160px"));
	AddDeclaration(*this, BaseRule, TEXT("height"), TEXT("48px"));
	AddDeclaration(*this, BaseRule, TEXT("background-color"), TEXT("blue"));
	AddDeclaration(*this, BaseRule, TEXT("border-color"), TEXT("white"));
	AddDeclaration(*this, BaseRule, TEXT("color"), TEXT("white"));
	AddDeclaration(*this, BaseRule, TEXT("transform-origin"), TEXT("0px 0px"));
	AddDeclaration(*this, BaseRule, TEXT("transform"), TEXT("translate(0px, 0px)"));
	AddDeclaration(*this, BaseRule, TEXT("transition"),
		TEXT("color 1s ease-in-out, background-color 1s ease-in-out, "
			"border-color 1s ease-in-out, transform 1s ease-in-out 500ms"));
	FWebToUECompiledRule& HoverRule = CompiledDocument.Rules.AddDefaulted_GetRef();
	HoverRule.Specificity = 110;
	FWebToUECompiledSelectorSegment& HoverSelector =
		HoverRule.Selector.AddDefaulted_GetRef();
	HoverSelector.Id = TEXT("target");
	HoverSelector.RequiredState = static_cast<uint8>(EWebToUEPseudoState::Hover);
	AddDeclaration(*this, HoverRule, TEXT("color"), TEXT("red"));
	AddDeclaration(*this, HoverRule, TEXT("background-color"), TEXT("transparent"));
	AddDeclaration(*this, HoverRule, TEXT("border-color"), TEXT("#ffff00"));
	AddDeclaration(*this, HoverRule, TEXT("transform"), TEXT("translate(40px, 0px)"));

	const auto AddTransition = [&CompiledDocument](
		EWebToUECompiledAnimationTargetKind Kind, const TCHAR* Name,
		double DelaySeconds)
	{
		FWebToUECompiledTransition& Transition =
			CompiledDocument.AnimationIR.Transitions.AddDefaulted_GetRef();
		Transition.TransitionId = FName(Name);
		Transition.Target.TargetNodeIndex = 1;
		Transition.Target.Kind = Kind;
		Transition.DurationSeconds = 1.0;
		Transition.DelaySeconds = DelaySeconds;
		Transition.Easing = EWebToUETransitionEasing::EaseInOut;
		Transition.ClockDomain = EWebToUEClockDomain::Game;
	};
	AddTransition(EWebToUECompiledAnimationTargetKind::Color,
		TEXT("transition.00000001.color"), 0.0);
	AddTransition(EWebToUECompiledAnimationTargetKind::BackgroundColor,
		TEXT("transition.00000001.background-color"), 0.0);
	AddTransition(EWebToUECompiledAnimationTargetKind::BorderColor,
		TEXT("transition.00000001.border-color"), 0.0);
	AddTransition(EWebToUECompiledAnimationTargetKind::VisualTransform,
		TEXT("transition.00000001.transform"), 0.5);
	WebToUE::Tests::SealResourceContractForTesting(CompiledDocument);
	Document->CommitCompiledDocument(MoveTemp(CompiledDocument));

	UWorld* World = NewObject<UWorld>(GetTransientPackage());
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	UWebToUEView* View = NewObject<UWebToUEView>(GetTransientPackage());
	World->AddToRoot();
	Player->AddToRoot();
	View->AddToRoot();
	const TSharedRef<FWebToUEVirtualClock> Clock = MakeShared<FWebToUEVirtualClock>();
	FString Error;
	Clock->SetTimeSeconds(EWebToUEClockDomain::Game, 20.0, Error);
	FWebToUESessionCreateParams SessionParams;
	SessionParams.World = World;
	SessionParams.LocalPlayer = Player;
	SessionParams.Surface.SurfaceId = TEXT("webtoue.transition.paint-transform-test");
	SessionParams.Clock = Clock;
	const TSharedPtr<FWebToUESession> Session =
		FWebToUESession::Create(SessionParams, Error);
	TestNotNull(TEXT("Paint/Transform adapter creates a controlled UI Session"),
		Session.Get());
	if (!Session)
	{
		View->RemoveFromRoot();
		Player->RemoveFromRoot();
		World->RemoveFromRoot();
		return false;
	}
	View->SetDocument(Document);
	View->SetSession(Session);
	View->TakeWidget();
	const TSharedPtr<SWebToUEView> SlateView = View->GetSlateViewForTesting();
	if (!TestNotNull(TEXT("Paint/Transform Slate adapter exists"), SlateView.Get()))
	{
		Session->Invalidate();
		View->RemoveFromRoot();
		Player->RemoveFromRoot();
		World->RemoveFromRoot();
		return false;
	}
	PaintView(SlateView.ToSharedRef());
	FWebToUENode* RuntimeButton =
		SlateView->FindRuntimeNodeByIdForTesting(TEXT("target"));
	if (!TestNotNull(TEXT("Paint/Transform target hydrates"), RuntimeButton) ||
		RuntimeButton->Children.IsEmpty())
	{
		Session->Invalidate();
		View->ReleaseSlateResources(true);
		View->RemoveFromRoot();
		Player->RemoveFromRoot();
		World->RemoveFromRoot();
		return false;
	}
	const FWebToUENode& RuntimeText = *RuntimeButton->Children[0];
	const void* BrushIdentity =
		SlateView->GetPresentationBrushIdentityForTesting(*RuntimeButton);
	const FWebToUEPaintCommand* Baseline =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	const FVector2f BaselinePosition = Baseline
		? Baseline->Bounds.GetTopLeft2f() : FVector2f::ZeroVector;

	SlateView->SetHoveredNodeForTesting(RuntimeButton);
	TestEqual(TEXT("Four typed Paint/Transform addresses acquire four Tracks"),
		Session->GetAnimationCoordinator()->GetActiveTrackCount(), 4);
	const FWebToUEPaintCommand* Started =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	const FWebToUEPaintCommand* StartedText =
		SlateView->GetDisplayCommandForTesting(RuntimeText);
	TestTrue(TEXT("Color overlays start from old fill/border/text values"),
		Started && StartedText &&
		Started->BackgroundColor.Equals(FLinearColor::Blue) &&
		Started->BorderColor.Equals(FLinearColor::White) &&
		StartedText->Color.Equals(FLinearColor::White));
	TestTrue(TEXT("Delayed transform holds the old spatial position"),
		Started && Started->Bounds.GetTopLeft2f().Equals(BaselinePosition, 0.01f));

	Clock->Advance(EWebToUEClockDomain::Game, 0.5, Error);
	FWebToUEPerformanceSnapshot MidpointSnapshot;
	{
		FWebToUEPerformanceCapture Capture;
		TestEqual(TEXT("One Pump services the complete legal four-Track set"),
			Session->GetAnimationCoordinator()->Pump(), 4);
		MidpointSnapshot = Capture.GetSnapshot();
	}
	const FWebToUEPaintCommand* ColorMidpoint =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	const FWebToUEPaintCommand* TextMidpoint =
		SlateView->GetDisplayCommandForTesting(RuntimeText);
	TestTrue(TEXT("Ease-in-out colors reach the exact symmetric midpoint"),
		ColorMidpoint && TextMidpoint &&
		ColorMidpoint->BackgroundColor.Equals(
			FLinearColor(0.0f, 0.0f, 0.5f, 0.5f), 0.001f) &&
		ColorMidpoint->BorderColor.Equals(
			FLinearColor(1.0f, 1.0f, 0.5f, 1.0f), 0.001f) &&
		TextMidpoint->Color.Equals(
			FLinearColor(1.0f, 0.5f, 0.5f, 1.0f), 0.001f));
	TestTrue(TEXT("A fading Background remains drawable until sampled alpha reaches zero"),
		ColorMidpoint && ColorMidpoint->bDrawable);
	TestTrue(TEXT("Transform delay is still at its exact start boundary"),
		ColorMidpoint &&
		ColorMidpoint->Bounds.GetTopLeft2f().Equals(BaselinePosition, 0.01f));
	TestEqual(TEXT("Color/Transform sampling performs zero Yoga style writes"),
		MidpointSnapshot.GetCounter(EWebToUEPerformanceCounter::YogaStyleWrites),
		uint64(0));
	TestEqual(TEXT("Color/Transform sampling performs zero Yoga result changes"),
		MidpointSnapshot.GetCounter(
			EWebToUEPerformanceCounter::YogaLayoutResultsChanged), uint64(0));
	TestEqual(TEXT("Color/Transform sampling performs zero resource loads"),
		MidpointSnapshot.GetCounter(
			EWebToUEPerformanceCounter::ResourceLoadAttempts), uint64(0));
	TestEqual(TEXT("Active color samples preserve the existing Brush allocation"),
		SlateView->GetPresentationBrushIdentityForTesting(*RuntimeButton),
		BrushIdentity);
	TestFalse(TEXT("Paint/Transform sampling remains Yoga layout-clean"),
		SlateView->IsPresentationLayoutDirtyForTesting());

	Clock->Advance(EWebToUEClockDomain::Game, 0.5, Error);
	Session->GetAnimationCoordinator()->Pump();
	const FWebToUEPaintCommand* TransformMidpoint =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	TestTrue(TEXT("Delayed Transform reaches its exact symmetric midpoint"),
		TransformMidpoint && FMath::IsNearlyEqual(
			TransformMidpoint->Bounds.Left, BaselinePosition.X + 20.0f, 0.01f));
	TestEqual(TEXT("Completed colors release while delayed Transform stays active"),
		Session->GetAnimationCoordinator()->GetActiveTrackCount(), 1);

	Clock->Advance(EWebToUEClockDomain::Game, 0.5, Error);
	Session->GetAnimationCoordinator()->Pump();
	const FWebToUEPaintCommand* Completed =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	TestTrue(TEXT("Fill releases every overlay to latest underlying values"),
		Completed &&
		Completed->BackgroundColor.Equals(FLinearColor::Transparent) &&
		Completed->BorderColor.Equals(FLinearColor::Yellow) &&
		FMath::IsNearlyEqual(
			Completed->Bounds.Left, BaselinePosition.X + 40.0f, 0.01f));
	TestTrue(TEXT("Transparent fill with no border width becomes non-drawable only at completion"),
		Completed && !Completed->bDrawable);
	TestTrue(TEXT("All Paint/Transform Tracks release the active-only ticker"),
		Session->GetAnimationCoordinator()->GetActiveTrackCount() == 0 &&
		!Session->GetAnimationCoordinator()->IsTickerRegistered());

	SlateView->SetHoveredNodeForTesting(nullptr);
	Clock->Advance(EWebToUEClockDomain::Game, 0.25, Error);
	Session->GetAnimationCoordinator()->Pump();
	const FWebToUEPaintCommand* ReverseSample =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	const FLinearColor ReverseBackground = ReverseSample
		? ReverseSample->BackgroundColor : FLinearColor::Transparent;
	SlateView->SetHoveredNodeForTesting(RuntimeButton);
	const FWebToUEPaintCommand* Retargeted =
		SlateView->GetDisplayCommandForTesting(*RuntimeButton);
	TestTrue(TEXT("Reverse interruption retargets from the visible Color sample"),
		ReverseSample && Retargeted &&
		Retargeted->BackgroundColor.Equals(
			ReverseBackground, 0.001f));
	View->SetDocument(nullptr);
	TestTrue(TEXT("Retargeted multi-property generation releases every lease/ticker"),
		Session->GetAnimationCoordinator()->GetActiveTrackCount() == 0 &&
		!Session->GetAnimationCoordinator()->IsTickerRegistered());

	View->ReleaseSlateResources(true);
	View->ClearSession();
	Session->Invalidate();
	View->RemoveFromRoot();
	Player->RemoveFromRoot();
	World->RemoveFromRoot();
	return true;
}

#endif
