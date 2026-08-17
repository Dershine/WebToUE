#pragma once

#include "CoreMinimal.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "WebToUEIdentity.h"
#include "WebToUESemantics.h"

class SWidget;
class UClass;
class UObject;
class UScriptStruct;

/** Explicit opt-in capabilities for a registered UE-native component type. */
enum class EWebToUENativeComponentCapability : uint8
{
	None = 0,
	Measure = 1 << 0,
	PointerInput = 1 << 1,
	KeyInput = 1 << 2,
	Focus = 1 << 3,
	Semantics = 1 << 4,
	Resources = 1 << 5
};
ENUM_CLASS_FLAGS(EWebToUENativeComponentCapability);

/** Host-owned lifecycle transitions. Attach and Detach occur exactly once per instance. */
enum class EWebToUENativeComponentLifecyclePhase : uint8
{
	Attach,
	Suspend,
	Resume,
	Detach
};

struct WEBTOUERUNTIME_API FWebToUENativeComponentMeasureRequest
{
	FVector2f AvailableSize = FVector2f::ZeroVector;
	bool bWidthConstrained = false;
	bool bHeightConstrained = false;
};

/** A typed resource input declared by the component registration. */
struct WEBTOUERUNTIME_API FWebToUENativeComponentResourceSlot
{
	FName Name;
	const UClass* ExpectedClass = nullptr;
	bool bRequired = false;
};

DECLARE_DELEGATE_ThreeParams(FWebToUENativeComponentEventSink,
	FName /* EventName */, const UScriptStruct* /* PayloadType */, const void* /* Payload */);

/** Immutable creation data owned by the WTUE host for one component instance generation. */
struct WEBTOUERUNTIME_API FWebToUENativeComponentCreateContext
{
	FWebToUEInstanceHandle Owner;
	FWebToUENativeComponentEventSink EventSink;
};

/**
 * One explicitly registered native component instance.
 *
 * Calls are Game Thread-only. Props and event payload memory are borrowed for the duration of the
 * call. Resources remain host-owned. The host owns lifecycle ordering and rejects stale Owner
 * handles before dispatch. Ordinary WTUE nodes never instantiate this interface.
 */
class WEBTOUERUNTIME_API IWebToUENativeComponentInstance
{
public:
	virtual ~IWebToUENativeComponentInstance() = default;

	virtual TSharedRef<SWidget> GetWidget() = 0;
	virtual bool ApplyProps(const UScriptStruct* PropsType, const void* Props, FString& OutError) = 0;
	virtual FVector2f Measure(const FWebToUENativeComponentMeasureRequest& Request) const = 0;
	virtual FReply HandlePointerEvent(const FGeometry& Geometry, const FPointerEvent& Event) = 0;
	virtual FReply HandleKeyEvent(const FGeometry& Geometry, const FKeyEvent& Event) = 0;
	virtual bool SupportsKeyboardFocus() const = 0;
	virtual bool RequestFocus(EFocusCause Cause) = 0;
	virtual void AppendSemanticNodes(TArray<FWebToUESemanticNode>& OutNodes) const = 0;
	virtual bool BindResource(FName SlotName, UObject* Resource, FString& OutError) = 0;
	virtual void OnLifecycle(EWebToUENativeComponentLifecyclePhase Phase) = 0;
};

class WEBTOUERUNTIME_API IWebToUENativeComponentFactory
{
public:
	virtual ~IWebToUENativeComponentFactory() = default;
	virtual TSharedRef<IWebToUENativeComponentInstance> Create(
		const FWebToUENativeComponentCreateContext& Context) const = 0;
};

/** Versioned type/schema contract registered before a component can be instantiated. */
struct WEBTOUERUNTIME_API FWebToUENativeComponentDescriptor
{
	FName TypeName;
	uint32 ContractVersion = 1;
	EWebToUENativeComponentCapability Capabilities =
		EWebToUENativeComponentCapability::None;
	const UScriptStruct* PropsType = nullptr;
	TMap<FName, const UScriptStruct*> EventPayloadTypes;
	TArray<FWebToUENativeComponentResourceSlot> ResourceSlots;

	const FWebToUENativeComponentResourceSlot* FindResourceSlot(FName SlotName) const;
	bool Validate(FString& OutError) const;
};

class FWebToUENativeComponentRegistry;

/** Move-only token proving ownership of one registry entry. Destruction unregisters it. */
class WEBTOUERUNTIME_API FWebToUENativeComponentRegistration
{
public:
	FWebToUENativeComponentRegistration() = default;
	~FWebToUENativeComponentRegistration();

	FWebToUENativeComponentRegistration(FWebToUENativeComponentRegistration&& Other) noexcept;
	FWebToUENativeComponentRegistration& operator=(
		FWebToUENativeComponentRegistration&& Other) noexcept;

	FWebToUENativeComponentRegistration(const FWebToUENativeComponentRegistration&) = delete;
	FWebToUENativeComponentRegistration& operator=(
		const FWebToUENativeComponentRegistration&) = delete;

	bool IsValid() const { return !TypeName.IsNone() && Token != 0; }
	void Reset();

private:
	friend class FWebToUENativeComponentRegistry;
	FWebToUENativeComponentRegistration(FName InTypeName, uint64 InToken)
		: TypeName(InTypeName), Token(InToken)
	{
	}

	FName TypeName;
	uint64 Token = 0;
};

/** Game Thread-only registry for explicit native component extension types. */
class WEBTOUERUNTIME_API FWebToUENativeComponentRegistry
{
public:
	static FWebToUENativeComponentRegistry& Get();

	TUniquePtr<FWebToUENativeComponentRegistration> Register(
		const FWebToUENativeComponentDescriptor& Descriptor,
		TSharedRef<IWebToUENativeComponentFactory> Factory,
		FString& OutError);
	bool FindDescriptor(FName TypeName, FWebToUENativeComponentDescriptor& OutDescriptor) const;
	TSharedPtr<IWebToUENativeComponentFactory> FindFactory(FName TypeName) const;
	int32 Num() const;

private:
	friend class FWebToUENativeComponentRegistration;

	struct FEntry
	{
		FWebToUENativeComponentDescriptor Descriptor;
		TSharedRef<IWebToUENativeComponentFactory> Factory;
		uint64 Token = 0;

		FEntry(const FWebToUENativeComponentDescriptor& InDescriptor,
			TSharedRef<IWebToUENativeComponentFactory> InFactory, uint64 InToken)
			: Descriptor(InDescriptor), Factory(MoveTemp(InFactory)), Token(InToken)
		{
		}
	};

	void Unregister(FName TypeName, uint64 Token);

	TMap<FName, FEntry> Entries;
	uint64 NextToken = 1;
};
