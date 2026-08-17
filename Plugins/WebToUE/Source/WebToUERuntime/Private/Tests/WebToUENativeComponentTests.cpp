#if WITH_DEV_AUTOMATION_TESTS

#include "WebToUENativeComponent.h"

#include "Misc/AutomationTest.h"
#include "Widgets/SNullWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUENativeComponentRegistryTest,
	"WebToUE.Runtime.NativeComponentRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::NativeComponent::Tests
{
	class FTestInstance final : public IWebToUENativeComponentInstance
	{
	public:
		virtual TSharedRef<SWidget> GetWidget() override
		{
			return SNullWidget::NullWidget;
		}

		virtual bool ApplyProps(
			const UScriptStruct* PropsType, const void* Props, FString& OutError) override
		{
			OutError.Reset();
			return PropsType != nullptr && Props != nullptr;
		}

		virtual FVector2f Measure(
			const FWebToUENativeComponentMeasureRequest& Request) const override
		{
			return Request.AvailableSize.ComponentMin(FVector2f(320.0f, 180.0f));
		}

		virtual FReply HandlePointerEvent(
			const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			return FReply::Unhandled();
		}

		virtual FReply HandleKeyEvent(
			const FGeometry& Geometry, const FKeyEvent& Event) override
		{
			return FReply::Unhandled();
		}

		virtual bool SupportsKeyboardFocus() const override { return true; }
		virtual bool RequestFocus(EFocusCause Cause) override { return true; }
		virtual void AppendSemanticNodes(TArray<FWebToUESemanticNode>& OutNodes) const override {}

		virtual bool BindResource(FName SlotName, UObject* Resource, FString& OutError) override
		{
			OutError.Reset();
			return SlotName == TEXT("texture") && Resource != nullptr;
		}

		virtual void OnLifecycle(EWebToUENativeComponentLifecyclePhase Phase) override
		{
			LastPhase = Phase;
		}

		EWebToUENativeComponentLifecyclePhase LastPhase =
			EWebToUENativeComponentLifecyclePhase::Attach;
	};

	class FTestFactory final : public IWebToUENativeComponentFactory
	{
	public:
		virtual TSharedRef<IWebToUENativeComponentInstance> Create(
			const FWebToUENativeComponentCreateContext& Context) const override
		{
			return MakeShared<FTestInstance>();
		}
	};
}

bool FWebToUENativeComponentRegistryTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::NativeComponent::Tests;
	FWebToUENativeComponentRegistry& Registry = FWebToUENativeComponentRegistry::Get();
	const int32 InitialCount = Registry.Num();

	FWebToUENativeComponentDescriptor InvalidDescriptor;
	InvalidDescriptor.TypeName = TEXT("Unnamespaced");
	FString Error;
	TUniquePtr<FWebToUENativeComponentRegistration> InvalidRegistration = Registry.Register(
		InvalidDescriptor, MakeShared<FTestFactory>(), Error);
	TestFalse(TEXT("An unnamespaced component type is rejected"), InvalidRegistration.IsValid());
	TestTrue(TEXT("Invalid registration returns an actionable diagnostic"), !Error.IsEmpty());

	InvalidDescriptor.TypeName = TEXT("webtoue.tests.zero-version");
	InvalidDescriptor.ContractVersion = 0;
	InvalidRegistration = Registry.Register(
		InvalidDescriptor, MakeShared<FTestFactory>(), Error);
	TestFalse(TEXT("A zero contract version is rejected"), InvalidRegistration.IsValid());

	InvalidDescriptor.ContractVersion = 1;
	InvalidDescriptor.EventPayloadTypes.Add(TEXT("invalid-event"), nullptr);
	InvalidRegistration = Registry.Register(
		InvalidDescriptor, MakeShared<FTestFactory>(), Error);
	TestFalse(TEXT("An event without a payload type is rejected"), InvalidRegistration.IsValid());

	InvalidDescriptor.EventPayloadTypes.Reset();
	InvalidDescriptor.ResourceSlots.Add({ TEXT("texture"), UObject::StaticClass(), true });
	InvalidRegistration = Registry.Register(
		InvalidDescriptor, MakeShared<FTestFactory>(), Error);
	TestFalse(TEXT("Resource slots require the explicit Resources capability"),
		InvalidRegistration.IsValid());

	InvalidDescriptor.Capabilities = EWebToUENativeComponentCapability::Resources;
	InvalidDescriptor.ResourceSlots.Add({ TEXT("texture"), UObject::StaticClass(), false });
	InvalidRegistration = Registry.Register(
		InvalidDescriptor, MakeShared<FTestFactory>(), Error);
	TestFalse(TEXT("Duplicate resource slot names are rejected"), InvalidRegistration.IsValid());

	FWebToUENativeComponentDescriptor Descriptor;
	Descriptor.TypeName = TEXT("webtoue.tests.native-component");
	Descriptor.ContractVersion = 1;
	Descriptor.Capabilities = EWebToUENativeComponentCapability::Measure |
		EWebToUENativeComponentCapability::PointerInput |
		EWebToUENativeComponentCapability::KeyInput |
		EWebToUENativeComponentCapability::Focus |
		EWebToUENativeComponentCapability::Semantics |
		EWebToUENativeComponentCapability::Resources;
	Descriptor.PropsType = TBaseStructure<FVector2D>::Get();
	Descriptor.EventPayloadTypes.Add(TEXT("activated"), TBaseStructure<FVector>::Get());
	Descriptor.ResourceSlots.Add(
		{ TEXT("texture"), UObject::StaticClass(), true });

	TSharedRef<FTestFactory> Factory = MakeShared<FTestFactory>();
	TUniquePtr<FWebToUENativeComponentRegistration> Registration =
		Registry.Register(Descriptor, Factory, Error);
	TestTrue(TEXT("A valid namespaced component contract registers"), Registration.IsValid());
	TestEqual(TEXT("Registration adds exactly one contract"), Registry.Num(), InitialCount + 1);

	FWebToUENativeComponentDescriptor FoundDescriptor;
	TestTrue(TEXT("The registered descriptor can be queried"),
		Registry.FindDescriptor(Descriptor.TypeName, FoundDescriptor));
	TestEqual(TEXT("The contract version is preserved"), FoundDescriptor.ContractVersion, uint32(1));
	TestTrue(TEXT("Typed props metadata is preserved"), FoundDescriptor.PropsType ==
		TBaseStructure<FVector2D>::Get());
	TestNotNull(TEXT("The named resource slot remains queryable"),
		FoundDescriptor.FindResourceSlot(TEXT("texture")));

	TUniquePtr<FWebToUENativeComponentRegistration> DuplicateRegistration = Registry.Register(
		Descriptor, MakeShared<FTestFactory>(), Error);
	TestFalse(TEXT("A duplicate component type is rejected"), DuplicateRegistration.IsValid());
	TestTrue(TEXT("Duplicate registration identifies the collision"),
		Error.Contains(TEXT("already registered")));

	TSharedPtr<IWebToUENativeComponentFactory> FoundFactory =
		Registry.FindFactory(Descriptor.TypeName);
	TestTrue(TEXT("The registered factory can be resolved"), FoundFactory.IsValid());
	if (FoundFactory)
	{
		const TSharedRef<IWebToUENativeComponentInstance> Instance =
			FoundFactory->Create(FWebToUENativeComponentCreateContext());
		FWebToUENativeComponentMeasureRequest Request;
		Request.AvailableSize = FVector2f(640.0f, 100.0f);
		TestEqual(TEXT("The instance participates in the explicit measure contract"),
			Instance->Measure(Request), FVector2f(320.0f, 100.0f));
		TestTrue(TEXT("The instance exposes explicit focus capability"),
			Instance->SupportsKeyboardFocus() && Instance->RequestFocus(EFocusCause::SetDirectly));
		Instance->OnLifecycle(EWebToUENativeComponentLifecyclePhase::Detach);
	}

	Registration.Reset();
	TestEqual(TEXT("Releasing the owner token unregisters exactly its contract"),
		Registry.Num(), InitialCount);
	TestFalse(TEXT("The released component type can no longer resolve"),
		Registry.FindFactory(Descriptor.TypeName).IsValid());
	Registration = Registry.Register(Descriptor, MakeShared<FTestFactory>(), Error);
	TestTrue(TEXT("A type can register again after its prior owner releases it"),
		Registration.IsValid());
	Registration.Reset();
	return true;
}

#endif
