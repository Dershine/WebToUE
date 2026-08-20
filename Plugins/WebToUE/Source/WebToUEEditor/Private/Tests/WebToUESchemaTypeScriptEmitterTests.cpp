#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WebToUEInteropSchema.h"
#include "WebToUESchemaTypeScriptEmitter.h"

#include "Algo/Reverse.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUESchemaTypeScriptEmitterTest,
	"WebToUE.Editor.InteropSchemaTypeScript",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::SchemaTypeScript::Tests
{
	static FWebToUEInteropSchemaDescriptor MakeSchema(bool bReverse)
	{
		FWebToUEInteropSchemaDescriptor Schema;
		Schema.SchemaId = TEXT("PersonalGame.UI");
		Schema.Version = { 3, 2 };

		FWebToUEInteropEnumDefinition Mode;
		Mode.Name = TEXT("MenuMode");
		Mode.Members = { { TEXT("Pause"), 2 }, { TEXT("Main"), 1 } };
		Schema.Enums.Add(MoveTemp(Mode));

		FWebToUEInteropRecordDefinition Result;
		Result.Name = TEXT("RefreshResult");
		Result.Fields = {
			{ TEXT("count"), FWebToUEInteropTypeRef::Primitive(EWebToUEInteropTypeKind::Int32), false },
		};
		FWebToUEInteropRecordDefinition Request;
		Request.Name = TEXT("RefreshRequest");
		Request.Fields = {
			{ TEXT("force"), FWebToUEInteropTypeRef::Primitive(EWebToUEInteropTypeKind::Boolean), false },
		};
		if (bReverse)
		{
			Schema.Records = { MoveTemp(Result), MoveTemp(Request) };
		}
		else
		{
			Schema.Records = { MoveTemp(Request), MoveTemp(Result) };
		}

		Schema.Data = {
			{ TEXT("title"), FWebToUEInteropTypeRef::Primitive(EWebToUEInteropTypeKind::Text), true },
			{ TEXT("mode"), FWebToUEInteropTypeRef::Named(TEXT("MenuMode")), true },
		};
		if (bReverse)
		{
			Algo::Reverse(Schema.Data);
		}

		FWebToUEInteropCommandDefinition Refresh;
		Refresh.Name = TEXT("refresh");
		Refresh.Request = FWebToUEInteropTypeRef::Named(TEXT("RefreshRequest"));
		Refresh.Response = EWebToUEInteropCommandResponse::Async;
		Refresh.Result = FWebToUEInteropTypeRef::Named(TEXT("RefreshResult"));
		Refresh.bCancellable = true;
		Schema.Commands.Add(MoveTemp(Refresh));
		return Schema;
	}
}

bool FWebToUESchemaTypeScriptEmitterTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::SchemaTypeScript::Tests;
	FWebToUEInteropSchemaSnapshot First;
	FWebToUEInteropSchemaSnapshot Second;
	TArray<FWebToUEInteropSchemaDiagnostic> Diagnostics;
	TestTrue(TEXT("First declaration fixture builds"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(MakeSchema(false), First, Diagnostics));
	TestTrue(TEXT("Reordered declaration fixture builds"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(MakeSchema(true), Second, Diagnostics));
	const FString FirstDeclaration = FWebToUESchemaTypeScriptEmitter::Emit(First);
	const FString SecondDeclaration = FWebToUESchemaTypeScriptEmitter::Emit(Second);
	TestEqual(TEXT("Canonical snapshots generate byte-identical TypeScript declarations"),
		FirstDeclaration, SecondDeclaration);
	TestTrue(TEXT("Generated declaration identifies its C++ source and schema version"),
		FirstDeclaration.Contains(TEXT("Generated from the authoritative C++ WebToUE schema")) &&
		FirstDeclaration.Contains(TEXT("PersonalGame.UI@3.2")));
	TestTrue(TEXT("Data and enum types remain typed and readonly"),
		FirstDeclaration.Contains(TEXT("export type MenuMode = \"Main\" | \"Pause\";")) &&
		FirstDeclaration.Contains(TEXT("readonly mode: MenuMode;")) &&
		FirstDeclaration.Contains(TEXT("readonly title: string;")));
	TestTrue(TEXT("Commands are metadata contracts rather than arbitrary UObject calls"),
		FirstDeclaration.Contains(TEXT("readonly refresh: {")) &&
		FirstDeclaration.Contains(TEXT("readonly response: \"async\";")) &&
		FirstDeclaration.Contains(TEXT("readonly cancellable: true;")));
	TestFalse(TEXT("The derived declaration never becomes UHT or UBT source"),
		FirstDeclaration.Contains(TEXT("UFUNCTION")) ||
		FirstDeclaration.Contains(TEXT(".generated.h")) ||
		FirstDeclaration.Contains(TEXT("#include")));
	return true;
}

#endif
