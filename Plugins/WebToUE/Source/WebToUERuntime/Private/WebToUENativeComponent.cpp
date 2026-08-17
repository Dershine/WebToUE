#include "WebToUENativeComponent.h"

#include "UObject/Class.h"

const FWebToUENativeComponentResourceSlot*
FWebToUENativeComponentDescriptor::FindResourceSlot(FName SlotName) const
{
	return ResourceSlots.FindByPredicate([SlotName](const FWebToUENativeComponentResourceSlot& Slot)
	{
		return Slot.Name == SlotName;
	});
}

bool FWebToUENativeComponentDescriptor::Validate(FString& OutError) const
{
	OutError.Reset();
	if (TypeName.IsNone())
	{
		OutError = TEXT("Native component TypeName must not be None.");
		return false;
	}

	const FString TypeString = TypeName.ToString();
	bool bHasWhitespace = false;
	for (TCHAR Character : TypeString)
	{
		bHasWhitespace |= FChar::IsWhitespace(Character);
	}
	if (!TypeString.Contains(TEXT(".")) || bHasWhitespace)
	{
		OutError = FString::Printf(
			TEXT("Native component TypeName '%s' must be namespaced and contain no whitespace."),
			*TypeString);
		return false;
	}

	if (ContractVersion == 0)
	{
		OutError = FString::Printf(
			TEXT("Native component '%s' must use a non-zero ContractVersion."), *TypeString);
		return false;
	}

	for (const TPair<FName, const UScriptStruct*>& Event : EventPayloadTypes)
	{
		if (Event.Key.IsNone() || Event.Value == nullptr)
		{
			OutError = FString::Printf(
				TEXT("Native component '%s' has an invalid typed event declaration."),
				*TypeString);
			return false;
		}
	}

	TSet<FName> ResourceNames;
	for (const FWebToUENativeComponentResourceSlot& Slot : ResourceSlots)
	{
		if (Slot.Name.IsNone() || Slot.ExpectedClass == nullptr || ResourceNames.Contains(Slot.Name))
		{
			OutError = FString::Printf(
				TEXT("Native component '%s' has an invalid or duplicate resource slot."),
				*TypeString);
			return false;
		}
		ResourceNames.Add(Slot.Name);
	}

	if (!ResourceSlots.IsEmpty() &&
		!EnumHasAnyFlags(Capabilities, EWebToUENativeComponentCapability::Resources))
	{
		OutError = FString::Printf(
			TEXT("Native component '%s' declares resources without the Resources capability."),
			*TypeString);
		return false;
	}
	return true;
}

FWebToUENativeComponentRegistration::~FWebToUENativeComponentRegistration()
{
	Reset();
}

FWebToUENativeComponentRegistration::FWebToUENativeComponentRegistration(
	FWebToUENativeComponentRegistration&& Other) noexcept
	: TypeName(Other.TypeName), Token(Other.Token)
{
	Other.TypeName = NAME_None;
	Other.Token = 0;
}

FWebToUENativeComponentRegistration& FWebToUENativeComponentRegistration::operator=(
	FWebToUENativeComponentRegistration&& Other) noexcept
{
	if (this != &Other)
	{
		Reset();
		TypeName = Other.TypeName;
		Token = Other.Token;
		Other.TypeName = NAME_None;
		Other.Token = 0;
	}
	return *this;
}

void FWebToUENativeComponentRegistration::Reset()
{
	if (!IsValid())
	{
		return;
	}
	if (!ensureMsgf(IsInGameThread(),
		TEXT("Native component registrations must be released on the Game Thread.")))
	{
		return;
	}
	FWebToUENativeComponentRegistry::Get().Unregister(TypeName, Token);
	TypeName = NAME_None;
	Token = 0;
}

FWebToUENativeComponentRegistry& FWebToUENativeComponentRegistry::Get()
{
	static FWebToUENativeComponentRegistry Registry;
	return Registry;
}

TUniquePtr<FWebToUENativeComponentRegistration> FWebToUENativeComponentRegistry::Register(
	const FWebToUENativeComponentDescriptor& Descriptor,
	TSharedRef<IWebToUENativeComponentFactory> Factory,
	FString& OutError)
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("Native component registration is Game Thread-only.");
		return nullptr;
	}
	if (!Descriptor.Validate(OutError))
	{
		return nullptr;
	}
	if (Entries.Contains(Descriptor.TypeName))
	{
		OutError = FString::Printf(TEXT("Native component '%s' is already registered."),
			*Descriptor.TypeName.ToString());
		return nullptr;
	}

	uint64 Token = NextToken++;
	if (Token == 0)
	{
		Token = NextToken++;
	}
	Entries.Add(Descriptor.TypeName, FEntry(Descriptor, MoveTemp(Factory), Token));
	return TUniquePtr<FWebToUENativeComponentRegistration>(
		new FWebToUENativeComponentRegistration(Descriptor.TypeName, Token));
}

bool FWebToUENativeComponentRegistry::FindDescriptor(
	FName TypeName, FWebToUENativeComponentDescriptor& OutDescriptor) const
{
	if (!IsInGameThread())
	{
		return false;
	}
	if (const FEntry* Entry = Entries.Find(TypeName))
	{
		OutDescriptor = Entry->Descriptor;
		return true;
	}
	return false;
}

TSharedPtr<IWebToUENativeComponentFactory> FWebToUENativeComponentRegistry::FindFactory(
	FName TypeName) const
{
	if (!IsInGameThread())
	{
		return nullptr;
	}
	if (const FEntry* Entry = Entries.Find(TypeName))
	{
		return Entry->Factory;
	}
	return nullptr;
}

int32 FWebToUENativeComponentRegistry::Num() const
{
	return IsInGameThread() ? Entries.Num() : 0;
}

void FWebToUENativeComponentRegistry::Unregister(FName TypeName, uint64 Token)
{
	check(IsInGameThread());
	const FEntry* Entry = Entries.Find(TypeName);
	if (Entry && Entry->Token == Token)
	{
		Entries.Remove(TypeName);
	}
}
