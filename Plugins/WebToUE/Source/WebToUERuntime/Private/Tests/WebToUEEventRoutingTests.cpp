#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "SWebToUEView.h"
#include "WebToUECompiler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEEventRoutingTest,
	"WebToUE.Runtime.EventRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEEventPathSafetyTest,
	"WebToUE.Runtime.EventPathSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::EventRouting::Tests
{
	struct FFixture
	{
		TSharedRef<FWebToUEDocument> Document = FWebToUECompiler::Compile(
			TEXT("<body id='root'><div id='panel'><button id='target' data-ue-on-click='Open'>Open</button></div></body>"),
			TEXT("body { width: 320px; height: 180px; } #panel, #target { width: 160px; height: 60px; }"));
		TSharedRef<SWebToUEView> View = SNew(SWebToUEView);
		FWebToUENode* Root = nullptr;
		FWebToUENode* Panel = nullptr;
		FWebToUENode* Target = nullptr;

		FFixture()
		{
			View->SetRuntimeDocumentForTesting(Document);
			View->LayoutForTesting(FVector2f(320.0f, 180.0f));
			Root = View->FindRuntimeNodeByIdForTesting(TEXT("root"));
			Panel = View->FindRuntimeNodeByIdForTesting(TEXT("panel"));
			Target = View->FindRuntimeNodeByIdForTesting(TEXT("target"));
		}

		FWebToUEInstanceHandle Handle(const FWebToUENode* Node) const
		{
			return Node ? View->GetInstanceHandleForTesting(*Node) : FWebToUEInstanceHandle();
		}
	};
}

bool FWebToUEEventRoutingTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::EventRouting::Tests;
	FFixture Fixture;
	TestNotNull(TEXT("The event corpus contains a root"), Fixture.Root);
	TestNotNull(TEXT("The event corpus contains an ancestor"), Fixture.Panel);
	TestNotNull(TEXT("The event corpus contains a target"), Fixture.Target);
	if (!Fixture.Root || !Fixture.Panel || !Fixture.Target)
	{
		return false;
	}

	TArray<FString> Order;
	bool bStateCommitted = false;
	Fixture.View->AddEventListener(Fixture.Handle(Fixture.Root),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Capture,
		[&Order](FWebToUERuntimeEvent&, FWebToUEUpdateTransaction&)
		{
			Order.Add(TEXT("root-capture"));
		});
	Fixture.View->AddEventListener(Fixture.Handle(Fixture.Panel),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Capture,
		[&Order](FWebToUERuntimeEvent&, FWebToUEUpdateTransaction&)
		{
			Order.Add(TEXT("panel-capture"));
		});
	Fixture.View->AddEventListener(Fixture.Handle(Fixture.Target),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Target,
		[&Order, &bStateCommitted](
			FWebToUERuntimeEvent&, FWebToUEUpdateTransaction& Transaction)
		{
			Order.Add(TEXT("target"));
			Transaction.AddStateMutation([&Order, &bStateCommitted]()
			{
				bStateCommitted = true;
				Order.Add(TEXT("state"));
			});
		});
	Fixture.View->AddEventListener(Fixture.Handle(Fixture.Panel),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Bubble,
		[&Order](FWebToUERuntimeEvent&, FWebToUEUpdateTransaction&)
		{
			Order.Add(TEXT("panel-bubble"));
		});
	Fixture.View->AddEventListener(Fixture.Handle(Fixture.Root),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Bubble,
		[&Order](FWebToUERuntimeEvent&, FWebToUEUpdateTransaction&)
		{
			Order.Add(TEXT("root-bubble"));
		});
	Fixture.View->SetDefaultEventObserverForTesting(
		[&Order, &bStateCommitted](const FWebToUEEventPathSnapshot& Snapshot)
		{
			if (bStateCommitted && Snapshot.CorrelationId != 0)
			{
				Order.Add(TEXT("default"));
			}
		});
	Fixture.View->DispatchClickForTesting(*Fixture.Target);
	TestEqual(TEXT("Event evaluation follows capture, target and bubble before commit/default"),
		FString::Join(Order, TEXT(",")),
		FString(TEXT("root-capture,panel-capture,target,panel-bubble,root-bubble,state,default")));
	TestTrue(TEXT("The declared click default action observes committed state"),
		bStateCommitted && Fixture.View->GetLastEventDispatchResultForTesting() ==
			EWebToUEEventDispatchResult::Dispatched);

	FFixture Stopped;
	TArray<FString> StoppedOrder;
	int32 DefaultCount = 0;
	Stopped.View->AddEventListener(Stopped.Handle(Stopped.Root),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Capture,
		[&StoppedOrder](FWebToUERuntimeEvent& Event, FWebToUEUpdateTransaction&)
		{
			StoppedOrder.Add(TEXT("root"));
			Event.StopPropagation();
		});
	Stopped.View->AddEventListener(Stopped.Handle(Stopped.Target),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Target,
		[&StoppedOrder](FWebToUERuntimeEvent&, FWebToUEUpdateTransaction&)
		{
			StoppedOrder.Add(TEXT("must-not-run"));
		});
	Stopped.View->SetDefaultEventObserverForTesting(
		[&DefaultCount](const FWebToUEEventPathSnapshot&) { ++DefaultCount; });
	Stopped.View->DispatchClickForTesting(*Stopped.Target);
	TestEqual(TEXT("StopPropagation ends the route after the current node"),
		FString::Join(StoppedOrder, TEXT(",")), FString(TEXT("root")));
	TestEqual(TEXT("StopPropagation does not implicitly cancel the default action"),
		DefaultCount, 1);

	FFixture Prevented;
	TArray<FString> PreventedOrder;
	int32 PreventedDefaultCount = 0;
	Prevented.View->AddEventListener(Prevented.Handle(Prevented.Target),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Target,
		[&PreventedOrder](FWebToUERuntimeEvent& Event, FWebToUEUpdateTransaction&)
		{
			PreventedOrder.Add(TEXT("first"));
			Event.PreventDefault();
			Event.StopImmediatePropagation();
		});
	Prevented.View->AddEventListener(Prevented.Handle(Prevented.Target),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Target,
		[&PreventedOrder](FWebToUERuntimeEvent&, FWebToUEUpdateTransaction&)
		{
			PreventedOrder.Add(TEXT("must-not-run"));
		});
	Prevented.View->SetDefaultEventObserverForTesting(
		[&PreventedDefaultCount](const FWebToUEEventPathSnapshot&)
		{
			++PreventedDefaultCount;
		});
	Prevented.View->DispatchClickForTesting(*Prevented.Target);
	TestEqual(TEXT("StopImmediatePropagation skips later listeners on the same target"),
		FString::Join(PreventedOrder, TEXT(",")), FString(TEXT("first")));
	TestEqual(TEXT("PreventDefault suppresses the declared click default action"),
		PreventedDefaultCount, 0);
	TestTrue(TEXT("Default cancellation is explicit in the dispatch result"),
		Prevented.View->GetLastEventDispatchResultForTesting() ==
			EWebToUEEventDispatchResult::DefaultPrevented);
	return true;
}

bool FWebToUEEventPathSafetyTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::EventRouting::Tests;
	FFixture Fixture;
	if (!Fixture.Root || !Fixture.Target)
	{
		AddError(TEXT("The stale-path corpus failed to compile."));
		return false;
	}
	const TSharedRef<FWebToUEDocument> Replacement = FWebToUECompiler::Compile(
		TEXT("<body id='replacement'><button id='new-target'>New</button></body>"),
		TEXT("body, button { width: 160px; height: 60px; }"));
	int32 TargetCount = 0;
	int32 DefaultCount = 0;
	Fixture.View->AddEventListener(Fixture.Handle(Fixture.Root),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Capture,
		[View = Fixture.View, Replacement](
			FWebToUERuntimeEvent&, FWebToUEUpdateTransaction&)
		{
			View->SetRuntimeDocumentForTesting(Replacement);
		});
	Fixture.View->AddEventListener(Fixture.Handle(Fixture.Target),
		EWebToUERuntimeEventType::Click, EWebToUERuntimeEventPhase::Target,
		[&TargetCount](FWebToUERuntimeEvent&, FWebToUEUpdateTransaction& Transaction)
		{
			++TargetCount;
			Transaction.AddStateMutation([]() {});
		});
	Fixture.View->SetDefaultEventObserverForTesting(
		[&DefaultCount](const FWebToUEEventPathSnapshot&) { ++DefaultCount; });
	Fixture.View->DispatchClickForTesting(*Fixture.Target);
	TestEqual(TEXT("A rehydrated path never reaches the stale target listener"), TargetCount, 0);
	TestEqual(TEXT("A stale event path never runs its default action"), DefaultCount, 0);
	TestTrue(TEXT("Generation invalidation produces a deterministic stale-path result"),
		Fixture.View->GetLastEventDispatchResultForTesting() ==
			EWebToUEEventDispatchResult::DroppedStalePath);
	return true;
}

#endif
