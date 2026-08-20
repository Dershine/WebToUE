#include "WebToUEInteropSchema.h"

namespace WebToUE::InteropSchema::Private
{
	static void AddDiagnostic(
		TArray<FWebToUEInteropSchemaDiagnostic>& Diagnostics,
		const TCHAR* Code,
		FString Path,
		FString Detail)
	{
		FWebToUEInteropSchemaDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Code = Code;
		Diagnostic.Path = MoveTemp(Path);
		Diagnostic.Detail = MoveTemp(Detail);
	}

	static void SortDiagnostics(TArray<FWebToUEInteropSchemaDiagnostic>& Diagnostics)
	{
		Diagnostics.Sort([](
			const FWebToUEInteropSchemaDiagnostic& A,
			const FWebToUEInteropSchemaDiagnostic& B)
		{
			if (A.Code != B.Code)
			{
				return A.Code < B.Code;
			}
			if (A.Path != B.Path)
			{
				return A.Path < B.Path;
			}
			return A.Detail < B.Detail;
		});
	}

	static bool IsAsciiLetter(TCHAR Character)
	{
		return (Character >= TEXT('A') && Character <= TEXT('Z')) ||
			(Character >= TEXT('a') && Character <= TEXT('z'));
	}

	static bool IsAsciiDigit(TCHAR Character)
	{
		return Character >= TEXT('0') && Character <= TEXT('9');
	}

	static bool IsIdentifier(const FString& Value)
	{
		if (Value.IsEmpty() || (!IsAsciiLetter(Value[0]) && Value[0] != TEXT('_')))
		{
			return false;
		}
		for (int32 Index = 1; Index < Value.Len(); ++Index)
		{
			if (!IsAsciiLetter(Value[Index]) && !IsAsciiDigit(Value[Index]) &&
				Value[Index] != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	static bool IsSchemaId(const FString& Value)
	{
		TArray<FString> Segments;
		Value.ParseIntoArray(Segments, TEXT("."), false);
		if (Segments.IsEmpty())
		{
			return false;
		}
		int32 ExpectedLength = Segments.Num() - 1;
		for (const FString& Segment : Segments)
		{
			if (!IsIdentifier(Segment))
			{
				return false;
			}
			ExpectedLength += Segment.Len();
		}
		return ExpectedLength == Value.Len();
	}

	template <typename DefinitionType>
	static void SortDefinitions(TArray<DefinitionType>& Definitions)
	{
		Definitions.Sort([](const DefinitionType& A, const DefinitionType& B)
		{
			return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0;
		});
	}

	template <typename DefinitionType>
	static bool ValidateUniqueNames(
		TConstArrayView<DefinitionType> Definitions,
		const FString& PathPrefix,
		TArray<FWebToUEInteropSchemaDiagnostic>& Diagnostics)
	{
		TMap<FString, FString> CanonicalNames;
		bool bValid = true;
		for (const DefinitionType& Definition : Definitions)
		{
			const FString Path = PathPrefix + TEXT(".") + Definition.Name;
			if (!IsIdentifier(Definition.Name))
			{
				AddDiagnostic(Diagnostics, TEXT("WTUE-SCHEMA-001"), Path,
					TEXT("Schema names must be ASCII C++/TypeScript identifiers"));
				bValid = false;
				continue;
			}
			const FString Folded = Definition.Name.ToLower();
			if (const FString* Existing = CanonicalNames.Find(Folded))
			{
				AddDiagnostic(Diagnostics, TEXT("WTUE-SCHEMA-002"), Path,
					FString::Printf(TEXT("Name conflicts with '%s' under UE name semantics"),
						**Existing));
				bValid = false;
			}
			else
			{
				CanonicalNames.Add(Folded, Definition.Name);
			}
		}
		return bValid;
	}

	static bool ValidateTypeRef(
		const FWebToUEInteropTypeRef& Type,
		const FString& Path,
		const TSet<FString>& NamedTypes,
		bool bAllowVoid,
		TArray<FWebToUEInteropSchemaDiagnostic>& Diagnostics)
	{
		bool bValid = true;
		if (Type.Kind == EWebToUEInteropTypeKind::Invalid ||
			(Type.Kind == EWebToUEInteropTypeKind::Named && Type.NamedType.IsEmpty()) ||
			(Type.Kind != EWebToUEInteropTypeKind::Named && !Type.NamedType.IsEmpty()))
		{
			AddDiagnostic(Diagnostics, TEXT("WTUE-SCHEMA-003"), Path,
				TEXT("Type reference is malformed"));
			bValid = false;
		}
		if (Type.Kind == EWebToUEInteropTypeKind::Void &&
			(!bAllowVoid || Type.Container != EWebToUEInteropContainer::Scalar || Type.bOptional))
		{
			AddDiagnostic(Diagnostics, TEXT("WTUE-SCHEMA-003"), Path,
				TEXT("Void is legal only as a scalar command request/result"));
			bValid = false;
		}
		if (Type.Kind == EWebToUEInteropTypeKind::Named &&
			!NamedTypes.Contains(Type.NamedType))
		{
			AddDiagnostic(Diagnostics, TEXT("WTUE-SCHEMA-003"), Path,
				FString::Printf(TEXT("Named type '%s' is not declared by this schema"),
					*Type.NamedType));
			bValid = false;
		}
		return bValid;
	}

	static bool ValidateRecordCycles(
		TConstArrayView<FWebToUEInteropRecordDefinition> Records,
		TArray<FWebToUEInteropSchemaDiagnostic>& Diagnostics)
	{
		TMap<FString, const FWebToUEInteropRecordDefinition*> ByName;
		for (const FWebToUEInteropRecordDefinition& Record : Records)
		{
			ByName.Add(Record.Name, &Record);
		}
		TSet<FString> Visiting;
		TSet<FString> Visited;
		TFunction<bool(const FWebToUEInteropRecordDefinition&)> Visit =
			[&](const FWebToUEInteropRecordDefinition& Record)
		{
			if (Visited.Contains(Record.Name))
			{
				return true;
			}
			if (Visiting.Contains(Record.Name))
			{
				AddDiagnostic(Diagnostics, TEXT("WTUE-SCHEMA-003"),
					TEXT("records.") + Record.Name,
					TEXT("Recursive record graphs are outside the bounded P0.5 value algebra"));
				return false;
			}
			Visiting.Add(Record.Name);
			bool bValid = true;
			for (const FWebToUEInteropFieldDefinition& Field : Record.Fields)
			{
				if (Field.Type.Kind == EWebToUEInteropTypeKind::Named)
				{
					if (const FWebToUEInteropRecordDefinition* const* Child =
						ByName.Find(Field.Type.NamedType))
					{
						bValid &= Visit(**Child);
					}
				}
			}
			Visiting.Remove(Record.Name);
			Visited.Add(Record.Name);
			return bValid;
		};

		bool bValid = true;
		for (const FWebToUEInteropRecordDefinition& Record : Records)
		{
			bValid &= Visit(Record);
		}
		return bValid;
	}

	template <typename DefinitionType>
	static const DefinitionType* FindNamed(
		TConstArrayView<DefinitionType> Definitions,
		const FString& Name)
	{
		return Definitions.FindByPredicate([&Name](const DefinitionType& Definition)
		{
			return Definition.Name == Name;
		});
	}

	static bool IsAdditiveCompatible(
		const FWebToUEInteropSchemaSnapshot& Previous,
		const FWebToUEInteropSchemaSnapshot& Current,
		FString& OutReason)
	{
		for (const FWebToUEInteropEnumDefinition& PreviousEnum : Previous.Enums)
		{
			const FWebToUEInteropEnumDefinition* CurrentEnum =
				FindNamed<FWebToUEInteropEnumDefinition>(Current.Enums, PreviousEnum.Name);
			if (!CurrentEnum)
			{
				OutReason = TEXT("An existing enum was removed");
				return false;
			}
			for (const FWebToUEInteropEnumMember& Member : PreviousEnum.Members)
			{
				const FWebToUEInteropEnumMember* CurrentMember =
					FindNamed<FWebToUEInteropEnumMember>(CurrentEnum->Members, Member.Name);
				if (!CurrentMember || *CurrentMember != Member)
				{
					OutReason = TEXT("An existing enum member was removed or changed");
					return false;
				}
			}
		}
		for (const FWebToUEInteropRecordDefinition& PreviousRecord : Previous.Records)
		{
			const FWebToUEInteropRecordDefinition* CurrentRecord =
				FindNamed<FWebToUEInteropRecordDefinition>(Current.Records, PreviousRecord.Name);
			if (!CurrentRecord)
			{
				OutReason = TEXT("An existing record was removed");
				return false;
			}
			for (const FWebToUEInteropFieldDefinition& Field : PreviousRecord.Fields)
			{
				const FWebToUEInteropFieldDefinition* CurrentField =
					FindNamed<FWebToUEInteropFieldDefinition>(CurrentRecord->Fields, Field.Name);
				if (!CurrentField || *CurrentField != Field)
				{
					OutReason = TEXT("An existing record field was removed or changed");
					return false;
				}
			}
		}
		for (const FWebToUEInteropFieldDefinition& Field : Previous.Data)
		{
			const FWebToUEInteropFieldDefinition* CurrentField =
				FindNamed<FWebToUEInteropFieldDefinition>(Current.Data, Field.Name);
			if (!CurrentField || *CurrentField != Field)
			{
				OutReason = TEXT("An existing Data field was removed or changed");
				return false;
			}
		}
		for (const FWebToUEInteropCommandDefinition& Command : Previous.Commands)
		{
			const FWebToUEInteropCommandDefinition* CurrentCommand =
				FindNamed<FWebToUEInteropCommandDefinition>(Current.Commands, Command.Name);
			if (!CurrentCommand || *CurrentCommand != Command)
			{
				OutReason = TEXT("An existing Command contract was removed or changed");
				return false;
			}
		}
		return true;
	}
}

FWebToUEInteropTypeRef FWebToUEInteropTypeRef::Primitive(
	EWebToUEInteropTypeKind InKind,
	EWebToUEInteropContainer InContainer,
	bool bInOptional)
{
	FWebToUEInteropTypeRef Result;
	Result.Kind = InKind;
	Result.Container = InContainer;
	Result.bOptional = bInOptional;
	return Result;
}

FWebToUEInteropTypeRef FWebToUEInteropTypeRef::Named(
	FString InName,
	EWebToUEInteropContainer InContainer,
	bool bInOptional)
{
	FWebToUEInteropTypeRef Result;
	Result.Kind = EWebToUEInteropTypeKind::Named;
	Result.Container = InContainer;
	Result.NamedType = MoveTemp(InName);
	Result.bOptional = bInOptional;
	return Result;
}

FWebToUEInteropTypeRef FWebToUEInteropTypeRef::Void()
{
	return Primitive(EWebToUEInteropTypeKind::Void);
}

void FWebToUEInteropSchemaSnapshot::Reset()
{
	SchemaId.Reset();
	Version = {};
	Enums.Reset();
	Records.Reset();
	Data.Reset();
	Commands.Reset();
}

bool FWebToUEInteropSchemaPolicy::BuildSnapshot(
	const FWebToUEInteropSchemaDescriptor& Descriptor,
	FWebToUEInteropSchemaSnapshot& OutSnapshot,
	TArray<FWebToUEInteropSchemaDiagnostic>& OutDiagnostics)
{
	using namespace WebToUE::InteropSchema::Private;
	OutSnapshot.Reset();
	OutDiagnostics.Reset();
	bool bValid = true;
	if (!IsSchemaId(Descriptor.SchemaId))
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-001"), TEXT("schema"),
			TEXT("SchemaId must be a dot-separated ASCII identifier"));
		bValid = false;
	}
	if (!Descriptor.Version.IsValid())
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-001"), TEXT("version"),
			TEXT("Schema major version must be nonzero"));
		bValid = false;
	}

	bValid &= ValidateUniqueNames<FWebToUEInteropEnumDefinition>(
		Descriptor.Enums, TEXT("enums"), OutDiagnostics);
	bValid &= ValidateUniqueNames<FWebToUEInteropRecordDefinition>(
		Descriptor.Records, TEXT("records"), OutDiagnostics);
	bValid &= ValidateUniqueNames<FWebToUEInteropFieldDefinition>(
		Descriptor.Data, TEXT("data"), OutDiagnostics);
	bValid &= ValidateUniqueNames<FWebToUEInteropCommandDefinition>(
		Descriptor.Commands, TEXT("commands"), OutDiagnostics);

	TMap<FString, FString> TypeNames;
	for (const FWebToUEInteropEnumDefinition& Enum : Descriptor.Enums)
	{
		const FString Folded = Enum.Name.ToLower();
		if (const FString* Existing = TypeNames.Find(Folded))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-002"),
				TEXT("enums.") + Enum.Name,
				FString::Printf(TEXT("Type name conflicts with '%s'"), **Existing));
			bValid = false;
		}
		else
		{
			TypeNames.Add(Folded, Enum.Name);
		}
	}
	for (const FWebToUEInteropRecordDefinition& Record : Descriptor.Records)
	{
		const FString Folded = Record.Name.ToLower();
		if (const FString* Existing = TypeNames.Find(Folded))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-002"),
				TEXT("records.") + Record.Name,
				FString::Printf(TEXT("Type name conflicts with '%s'"), **Existing));
			bValid = false;
		}
		else
		{
			TypeNames.Add(Folded, Record.Name);
		}
	}
	TSet<FString> NamedTypes;
	for (const TPair<FString, FString>& Pair : TypeNames)
	{
		NamedTypes.Add(Pair.Value);
	}

	for (const FWebToUEInteropEnumDefinition& Enum : Descriptor.Enums)
	{
		bValid &= ValidateUniqueNames<FWebToUEInteropEnumMember>(
			Enum.Members, TEXT("enums.") + Enum.Name, OutDiagnostics);
		if (Enum.Members.IsEmpty())
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-003"),
				TEXT("enums.") + Enum.Name, TEXT("Enums must declare at least one member"));
			bValid = false;
		}
		TSet<int64> Values;
		for (const FWebToUEInteropEnumMember& Member : Enum.Members)
		{
			if (Values.Contains(Member.Value))
			{
				AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-002"),
					TEXT("enums.") + Enum.Name + TEXT(".") + Member.Name,
					TEXT("Enum wire values must be unique"));
				bValid = false;
			}
			Values.Add(Member.Value);
		}
	}
	for (const FWebToUEInteropRecordDefinition& Record : Descriptor.Records)
	{
		bValid &= ValidateUniqueNames<FWebToUEInteropFieldDefinition>(
			Record.Fields, TEXT("records.") + Record.Name, OutDiagnostics);
		for (const FWebToUEInteropFieldDefinition& Field : Record.Fields)
		{
			bValid &= ValidateTypeRef(Field.Type,
				TEXT("records.") + Record.Name + TEXT(".") + Field.Name,
				NamedTypes, false, OutDiagnostics);
			if (Field.bObservable)
			{
				AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-003"),
					TEXT("records.") + Record.Name + TEXT(".") + Field.Name,
					TEXT("Only root Data fields may declare FieldNotify observability"));
				bValid = false;
			}
		}
	}
	for (const FWebToUEInteropFieldDefinition& Field : Descriptor.Data)
	{
		bValid &= ValidateTypeRef(Field.Type, TEXT("data.") + Field.Name,
			NamedTypes, false, OutDiagnostics);
	}
	for (const FWebToUEInteropCommandDefinition& Command : Descriptor.Commands)
	{
		bValid &= ValidateTypeRef(Command.Request, TEXT("commands.") + Command.Name +
			TEXT(".request"), NamedTypes, true, OutDiagnostics);
		bValid &= ValidateTypeRef(Command.Result, TEXT("commands.") + Command.Name +
			TEXT(".result"), NamedTypes, true, OutDiagnostics);
		const bool bInvalidNone = Command.Response == EWebToUEInteropCommandResponse::None &&
			(!Command.Result.IsVoid() || Command.bCancellable);
		const bool bInvalidImmediate =
			Command.Response == EWebToUEInteropCommandResponse::Immediate && Command.bCancellable;
		if (bInvalidNone || bInvalidImmediate)
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-003"),
				TEXT("commands.") + Command.Name,
				TEXT("Command result/cancellation shape does not match its response mode"));
			bValid = false;
		}
	}
	bValid &= ValidateRecordCycles(Descriptor.Records, OutDiagnostics);

	if (!bValid)
	{
		SortDiagnostics(OutDiagnostics);
		return false;
	}

	OutSnapshot.SchemaId = Descriptor.SchemaId;
	OutSnapshot.Version = Descriptor.Version;
	OutSnapshot.Enums = Descriptor.Enums;
	OutSnapshot.Records = Descriptor.Records;
	OutSnapshot.Data = Descriptor.Data;
	OutSnapshot.Commands = Descriptor.Commands;
	SortDefinitions(OutSnapshot.Enums);
	SortDefinitions(OutSnapshot.Records);
	SortDefinitions(OutSnapshot.Data);
	SortDefinitions(OutSnapshot.Commands);
	for (FWebToUEInteropEnumDefinition& Enum : OutSnapshot.Enums)
	{
		SortDefinitions(Enum.Members);
	}
	for (FWebToUEInteropRecordDefinition& Record : OutSnapshot.Records)
	{
		SortDefinitions(Record.Fields);
	}
	return true;
}

bool FWebToUEInteropSchemaPolicy::ValidateEvolution(
	const FWebToUEInteropSchemaSnapshot& Previous,
	const FWebToUEInteropSchemaSnapshot& Current,
	TArray<FWebToUEInteropSchemaDiagnostic>& OutDiagnostics)
{
	using namespace WebToUE::InteropSchema::Private;
	OutDiagnostics.Reset();
	if (Previous.SchemaId != Current.SchemaId)
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-004"), TEXT("schema"),
			TEXT("Schema identity cannot change during version evolution"));
	}
	else if (Current.Version.Major < Previous.Version.Major ||
		(Current.Version.Major == Previous.Version.Major &&
			Current.Version.Minor < Previous.Version.Minor))
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-004"), TEXT("version"),
			TEXT("Schema versions cannot move backwards"));
	}
	else if (Current.Version == Previous.Version && Current != Previous)
	{
		AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-004"), TEXT("version"),
			TEXT("A changed schema cannot reuse the previous version"));
	}
	else if (Current.Version.Major == Previous.Version.Major &&
		Current.Version.Minor > Previous.Version.Minor)
	{
		FString Reason;
		if (!IsAdditiveCompatible(Previous, Current, Reason))
		{
			AddDiagnostic(OutDiagnostics, TEXT("WTUE-SCHEMA-004"), TEXT("version"),
				Reason + TEXT("; increment the major version"));
		}
	}
	SortDiagnostics(OutDiagnostics);
	return OutDiagnostics.IsEmpty();
}
