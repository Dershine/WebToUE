#include "WebToUEPropertyOwnership.h"

#include "WebToUEStyleProperties.h"

namespace
{
	constexpr const TCHAR* InvalidTargetCode = TEXT("WTUE-OWN-001");
	constexpr const TCHAR* WriterNotAllowedCode = TEXT("WTUE-OWN-002");
	constexpr const TCHAR* DurableConflictCode = TEXT("WTUE-OWN-003");

	void AddError(FWebToUEPropertyOwnershipDecision& Decision,
		const TCHAR* Code, FString Message)
	{
		FWebToUEPropertyOwnershipDiagnostic& Diagnostic =
			Decision.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Code = FName(Code);
		Diagnostic.Severity = EWebToUEPropertyOwnershipDiagnosticSeverity::Error;
		Diagnostic.Message = MoveTemp(Message);
	}

	bool IsWriterAllowed(const FWebToUEPropertyAddress& Address,
		EWebToUEPropertyWriter Writer)
	{
		switch (Address.Kind)
		{
		case EWebToUEPropertyTargetKind::NodeText:
			return Writer == EWebToUEPropertyWriter::Source ||
				Writer == EWebToUEPropertyWriter::Binding ||
				Writer == EWebToUEPropertyWriter::Behavior;
		case EWebToUEPropertyTargetKind::Visibility:
			return Writer == EWebToUEPropertyWriter::Source ||
				Writer == EWebToUEPropertyWriter::Css ||
				Writer == EWebToUEPropertyWriter::CssPseudo ||
				Writer == EWebToUEPropertyWriter::Binding ||
				Writer == EWebToUEPropertyWriter::Behavior;
		case EWebToUEPropertyTargetKind::Enabled:
			return Writer == EWebToUEPropertyWriter::Source ||
				Writer == EWebToUEPropertyWriter::Binding ||
				Writer == EWebToUEPropertyWriter::Behavior;
		case EWebToUEPropertyTargetKind::CssProperty:
			return Writer == EWebToUEPropertyWriter::Css ||
				Writer == EWebToUEPropertyWriter::CssPseudo ||
				Writer == EWebToUEPropertyWriter::Binding ||
				Writer == EWebToUEPropertyWriter::Behavior ||
				(Writer == EWebToUEPropertyWriter::Animation &&
					FWebToUEPropertyOwnershipPolicy::IsAnimationTarget(Address));
		case EWebToUEPropertyTargetKind::VisualTransform:
			return Writer == EWebToUEPropertyWriter::Css ||
				Writer == EWebToUEPropertyWriter::CssPseudo ||
				Writer == EWebToUEPropertyWriter::Binding ||
				Writer == EWebToUEPropertyWriter::Behavior ||
				Writer == EWebToUEPropertyWriter::Animation;
		case EWebToUEPropertyTargetKind::MaterialParameter:
			return Writer == EWebToUEPropertyWriter::Source ||
				Writer == EWebToUEPropertyWriter::Binding ||
				Writer == EWebToUEPropertyWriter::Behavior ||
				(Writer == EWebToUEPropertyWriter::Animation &&
					FWebToUEPropertyOwnershipPolicy::IsAnimationTarget(Address));
		default:
			return false;
		}
	}
}

FWebToUEPropertyAddress FWebToUEPropertyAddress::NodeText()
{
	FWebToUEPropertyAddress Result;
	Result.Kind = EWebToUEPropertyTargetKind::NodeText;
	return Result;
}

FWebToUEPropertyAddress FWebToUEPropertyAddress::Visibility()
{
	FWebToUEPropertyAddress Result;
	Result.Kind = EWebToUEPropertyTargetKind::Visibility;
	return Result;
}

FWebToUEPropertyAddress FWebToUEPropertyAddress::Enabled()
{
	FWebToUEPropertyAddress Result;
	Result.Kind = EWebToUEPropertyTargetKind::Enabled;
	return Result;
}

FWebToUEPropertyAddress FWebToUEPropertyAddress::Css(EWebToUECssProperty Property)
{
	if (Property == EWebToUECssProperty::Visibility)
	{
		return Visibility();
	}
	FWebToUEPropertyAddress Result;
	Result.Kind = EWebToUEPropertyTargetKind::CssProperty;
	Result.CssProperty = Property;
	return Result;
}

FWebToUEPropertyAddress FWebToUEPropertyAddress::VisualTransform()
{
	FWebToUEPropertyAddress Result;
	Result.Kind = EWebToUEPropertyTargetKind::VisualTransform;
	return Result;
}

FWebToUEPropertyAddress FWebToUEPropertyAddress::Material(
	FName Parameter, EWebToUEMaterialParameterType Type)
{
	FWebToUEPropertyAddress Result;
	Result.Kind = EWebToUEPropertyTargetKind::MaterialParameter;
	Result.MaterialParameter = Parameter;
	Result.MaterialParameterType = Type;
	return Result;
}

bool FWebToUEPropertyAddress::IsValid() const
{
	switch (Kind)
	{
	case EWebToUEPropertyTargetKind::NodeText:
	case EWebToUEPropertyTargetKind::Visibility:
	case EWebToUEPropertyTargetKind::Enabled:
	case EWebToUEPropertyTargetKind::VisualTransform:
		return CssProperty == EWebToUECssProperty::Invalid &&
			MaterialParameter.IsNone() &&
			MaterialParameterType == EWebToUEMaterialParameterType::None;
	case EWebToUEPropertyTargetKind::CssProperty:
		return static_cast<uint8>(CssProperty) <=
				static_cast<uint8>(EWebToUECssProperty::ZIndex) &&
			WebToUE::Private::IsCanonicalComputedStyleProperty(CssProperty) &&
			MaterialParameter.IsNone() &&
			MaterialParameterType == EWebToUEMaterialParameterType::None;
	case EWebToUEPropertyTargetKind::MaterialParameter:
		return CssProperty == EWebToUECssProperty::Invalid &&
			!MaterialParameter.IsNone() &&
			MaterialParameterType != EWebToUEMaterialParameterType::None;
	default:
		return false;
	}
}

FString FWebToUEPropertyAddress::ToString() const
{
	switch (Kind)
	{
	case EWebToUEPropertyTargetKind::NodeText: return TEXT("node.text");
	case EWebToUEPropertyTargetKind::Visibility: return TEXT("node.visibility");
	case EWebToUEPropertyTargetKind::Enabled: return TEXT("node.enabled");
	case EWebToUEPropertyTargetKind::CssProperty:
		return FString::Printf(TEXT("style.%s"),
			WebToUE::Private::LexToString(CssProperty));
	case EWebToUEPropertyTargetKind::VisualTransform: return TEXT("style.transform");
	case EWebToUEPropertyTargetKind::MaterialParameter:
	{
		const TCHAR* TypeName = MaterialParameterType ==
			EWebToUEMaterialParameterType::Scalar ? TEXT("scalar") :
			MaterialParameterType == EWebToUEMaterialParameterType::Vector ?
				TEXT("vector") :
			MaterialParameterType == EWebToUEMaterialParameterType::Texture ?
				TEXT("texture") : TEXT("invalid");
		return FString::Printf(TEXT("material.%s.%s"), TypeName,
			*MaterialParameter.ToString());
	}
	default: return TEXT("invalid");
	}
}

uint32 GetTypeHash(const FWebToUEPropertyAddress& Address)
{
	return HashCombineFast(GetTypeHash(static_cast<uint8>(Address.Kind)),
		HashCombineFast(GetTypeHash(static_cast<uint8>(Address.CssProperty)),
			HashCombineFast(GetTypeHash(Address.MaterialParameter),
				GetTypeHash(static_cast<uint8>(Address.MaterialParameterType)))));
}

FString FWebToUEPropertyOwnershipDecision::DescribePrecedence() const
{
	TArray<FString> Layers;
	if (bHasAnimationOverlay)
	{
		Layers.Add(TEXT("Animation(active)"));
	}
	if (bHasDurableOwner)
	{
		Layers.Add(FWebToUEPropertyOwnershipPolicy::LexToString(DurableOwner));
	}
	if (bHasCssCascadeBaseline)
	{
		Layers.Add(TEXT("CSS cascade(CSS+Pseudo)"));
	}
	if (bHasSourceBaseline)
	{
		Layers.Add(TEXT("Source"));
	}
	const TCHAR* Separator = Composition == EWebToUEPropertyComposition::RestrictiveGate
		? TEXT(" & ") : TEXT(" > ");
	FString Result = FString::Join(Layers, Separator);
	if (Composition == EWebToUEPropertyComposition::RestrictiveGate && !Result.IsEmpty())
	{
		Result += TEXT(" (restrictive)");
	}
	return Result;
}

FWebToUEPropertyOwnershipDecision FWebToUEPropertyOwnershipPolicy::Resolve(
	const FWebToUEPropertyAddress& Address,
	TConstArrayView<FWebToUEPropertyOwnershipClaim> Claims)
{
	FWebToUEPropertyOwnershipDecision Decision;
	Decision.Composition =
		Address.Kind == EWebToUEPropertyTargetKind::Visibility ||
		Address.Kind == EWebToUEPropertyTargetKind::Enabled
			? EWebToUEPropertyComposition::RestrictiveGate
			: EWebToUEPropertyComposition::LayeredOverride;
	if (!Address.IsValid())
	{
		AddError(Decision, InvalidTargetCode,
			TEXT("WTUE property ownership target is invalid or incompletely typed."));
		return Decision;
	}

	bool SeenWriters[static_cast<uint8>(EWebToUEPropertyWriter::Count)] = {};
	TArray<FString> WriterSources[static_cast<uint8>(EWebToUEPropertyWriter::Count)];
	for (const FWebToUEPropertyOwnershipClaim& Claim : Claims)
	{
		const uint8 WriterIndex = static_cast<uint8>(Claim.Writer);
		if (WriterIndex >= static_cast<uint8>(EWebToUEPropertyWriter::Count))
		{
			AddError(Decision, WriterNotAllowedCode, FString::Printf(
				TEXT("Unknown writer cannot own '%s'."), *Address.ToString()));
			continue;
		}
		if (!Claim.Source.IsEmpty())
		{
			WriterSources[WriterIndex].AddUnique(Claim.Source);
		}
		if (SeenWriters[WriterIndex]) continue;
		SeenWriters[WriterIndex] = true;
		if (!IsWriterAllowed(Address, Claim.Writer))
		{
			AddError(Decision, WriterNotAllowedCode, FString::Printf(
				TEXT("%s cannot own '%s'."), LexToString(Claim.Writer),
				*Address.ToString()));
		}
	}

	const bool bBinding = SeenWriters[static_cast<uint8>(EWebToUEPropertyWriter::Binding)];
	const bool bBehavior = SeenWriters[static_cast<uint8>(EWebToUEPropertyWriter::Behavior)];
	if (bBinding && bBehavior)
	{
		const auto DescribeClaim = [&WriterSources](EWebToUEPropertyWriter Writer)
		{
			TArray<FString>& Sources = WriterSources[static_cast<uint8>(Writer)];
			Sources.Sort();
			return Sources.IsEmpty()
				? FString(LexToString(Writer))
				: FString::Printf(TEXT("%s[%s]"), LexToString(Writer),
					*FString::Join(Sources, TEXT(", ")));
		};
		AddError(Decision, DurableConflictCode, FString::Printf(
			TEXT("%s and %s both claim durable ownership of '%s'; "
				"declare exactly one durable owner instead of relying on write order."),
			*DescribeClaim(EWebToUEPropertyWriter::Binding),
			*DescribeClaim(EWebToUEPropertyWriter::Behavior),
			*Address.ToString()));
	}

	Decision.bHasSourceBaseline =
		SeenWriters[static_cast<uint8>(EWebToUEPropertyWriter::Source)];
	Decision.bHasCssCascadeBaseline =
		SeenWriters[static_cast<uint8>(EWebToUEPropertyWriter::Css)] ||
		SeenWriters[static_cast<uint8>(EWebToUEPropertyWriter::CssPseudo)];
	Decision.bHasDurableOwner = bBinding != bBehavior;
	if (Decision.bHasDurableOwner)
	{
		Decision.DurableOwner = bBinding
			? EWebToUEPropertyWriter::Binding : EWebToUEPropertyWriter::Behavior;
	}
	Decision.bHasAnimationOverlay =
		SeenWriters[static_cast<uint8>(EWebToUEPropertyWriter::Animation)] &&
		IsAnimationTarget(Address);
	Decision.bAccepted = !Decision.Diagnostics.ContainsByPredicate([](
		const FWebToUEPropertyOwnershipDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity ==
			EWebToUEPropertyOwnershipDiagnosticSeverity::Error;
	});
	return Decision;
}

bool FWebToUEPropertyOwnershipPolicy::IsAnimationTarget(
	const FWebToUEPropertyAddress& Address)
{
	if (!Address.IsValid())
	{
		return false;
	}
	if (Address.Kind == EWebToUEPropertyTargetKind::VisualTransform)
	{
		return true;
	}
	if (Address.Kind == EWebToUEPropertyTargetKind::MaterialParameter)
	{
		return Address.MaterialParameterType == EWebToUEMaterialParameterType::Scalar ||
			Address.MaterialParameterType == EWebToUEMaterialParameterType::Vector;
	}
	if (Address.Kind != EWebToUEPropertyTargetKind::CssProperty)
	{
		return false;
	}
	switch (Address.CssProperty)
	{
	case EWebToUECssProperty::Color:
	case EWebToUECssProperty::BackgroundColor:
	case EWebToUECssProperty::BorderColor:
	case EWebToUECssProperty::Opacity:
		return true;
	default:
		return false;
	}
}

const TCHAR* FWebToUEPropertyOwnershipPolicy::LexToString(
	EWebToUEPropertyWriter Writer)
{
	switch (Writer)
	{
	case EWebToUEPropertyWriter::Source: return TEXT("Source");
	case EWebToUEPropertyWriter::Css: return TEXT("CSS");
	case EWebToUEPropertyWriter::CssPseudo: return TEXT("Pseudo");
	case EWebToUEPropertyWriter::Binding: return TEXT("Binding");
	case EWebToUEPropertyWriter::Behavior: return TEXT("Behavior");
	case EWebToUEPropertyWriter::Animation: return TEXT("Animation");
	default: return TEXT("Unknown");
	}
}
