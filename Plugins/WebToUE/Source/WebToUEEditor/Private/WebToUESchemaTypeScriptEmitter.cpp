#include "WebToUESchemaTypeScriptEmitter.h"

#include "WebToUEInteropSchema.h"

namespace WebToUE::SchemaTypeScript::Private
{
	static FString BaseType(const FWebToUEInteropTypeRef& Type)
	{
		switch (Type.Kind)
		{
		case EWebToUEInteropTypeKind::Void: return TEXT("undefined");
		case EWebToUEInteropTypeKind::Boolean: return TEXT("boolean");
		case EWebToUEInteropTypeKind::Int32:
		case EWebToUEInteropTypeKind::Float32:
		case EWebToUEInteropTypeKind::Float64: return TEXT("number");
		case EWebToUEInteropTypeKind::String:
		case EWebToUEInteropTypeKind::Name:
		case EWebToUEInteropTypeKind::Text: return TEXT("string");
		case EWebToUEInteropTypeKind::Named: return Type.NamedType;
		default: return TEXT("never");
		}
	}

	static FString TypeScriptType(const FWebToUEInteropTypeRef& Type)
	{
		FString Result = BaseType(Type);
		if (Type.Container == EWebToUEInteropContainer::Array)
		{
			Result = FString::Printf(TEXT("ReadonlyArray<%s>"), *Result);
		}
		if (Type.bOptional)
		{
			Result += TEXT(" | undefined");
		}
		return Result;
	}

	static const TCHAR* ResponseName(EWebToUEInteropCommandResponse Response)
	{
		switch (Response)
		{
		case EWebToUEInteropCommandResponse::None: return TEXT("none");
		case EWebToUEInteropCommandResponse::Immediate: return TEXT("immediate");
		case EWebToUEInteropCommandResponse::Async: return TEXT("async");
		default: return TEXT("invalid");
		}
	}
}

FString FWebToUESchemaTypeScriptEmitter::Emit(
	const FWebToUEInteropSchemaSnapshot& Snapshot)
{
	using namespace WebToUE::SchemaTypeScript::Private;
	FString Output;
	Output += TEXT("/**\n");
	Output += FString::Printf(
		TEXT(" * Generated from the authoritative C++ WebToUE schema %s@%s.\n"),
		*Snapshot.SchemaId, *Snapshot.Version.ToString());
	Output += TEXT(" * Derived authoring output only; never an input to UHT or UBT.\n");
	Output += TEXT(" * Do not edit. Regenerate after the C++ descriptor changes.\n");
	Output += TEXT(" */\n");
	Output += TEXT("export namespace WTUE {\n");
	Output += FString::Printf(TEXT("\texport const schemaId: \"%s\";\n"), *Snapshot.SchemaId);
	Output += FString::Printf(TEXT("\texport const schemaVersion: \"%s\";\n\n"),
		*Snapshot.Version.ToString());

	for (const FWebToUEInteropEnumDefinition& Enum : Snapshot.Enums)
	{
		Output += FString::Printf(TEXT("\texport type %s = "), *Enum.Name);
		for (int32 Index = 0; Index < Enum.Members.Num(); ++Index)
		{
			if (Index > 0)
			{
				Output += TEXT(" | ");
			}
			Output += FString::Printf(TEXT("\"%s\""), *Enum.Members[Index].Name);
		}
		Output += TEXT(";\n");
	}
	if (!Snapshot.Enums.IsEmpty())
	{
		Output += TEXT("\n");
	}

	for (const FWebToUEInteropRecordDefinition& Record : Snapshot.Records)
	{
		Output += FString::Printf(TEXT("\texport interface %s {\n"), *Record.Name);
		for (const FWebToUEInteropFieldDefinition& Field : Record.Fields)
		{
			Output += FString::Printf(TEXT("\t\treadonly %s: %s;\n"),
				*Field.Name, *TypeScriptType(Field.Type));
		}
		Output += TEXT("\t}\n\n");
	}

	Output += TEXT("\texport interface Data {\n");
	for (const FWebToUEInteropFieldDefinition& Field : Snapshot.Data)
	{
		Output += FString::Printf(TEXT("\t\treadonly %s: %s;\n"),
			*Field.Name, *TypeScriptType(Field.Type));
	}
	Output += TEXT("\t}\n\n");

	Output += TEXT("\texport interface CommandMap {\n");
	for (const FWebToUEInteropCommandDefinition& Command : Snapshot.Commands)
	{
		Output += FString::Printf(TEXT("\t\treadonly %s: {\n"), *Command.Name);
		Output += FString::Printf(TEXT("\t\t\treadonly request: %s;\n"),
			*TypeScriptType(Command.Request));
		Output += FString::Printf(TEXT("\t\t\treadonly response: \"%s\";\n"),
			ResponseName(Command.Response));
		Output += FString::Printf(TEXT("\t\t\treadonly result: %s;\n"),
			*TypeScriptType(Command.Result));
		Output += FString::Printf(TEXT("\t\t\treadonly cancellable: %s;\n"),
			Command.bCancellable ? TEXT("true") : TEXT("false"));
		Output += TEXT("\t\t};\n");
	}
	Output += TEXT("\t}\n");
	Output += TEXT("}\n");
	return Output;
}
