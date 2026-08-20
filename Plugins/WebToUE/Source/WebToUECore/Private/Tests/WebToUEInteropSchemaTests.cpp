#if WITH_DEV_AUTOMATION_TESTS

#include "WebToUEInteropSchema.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEInteropSchemaCanonicalizationTest,
	"WebToUE.Core.InteropSchemaCanonicalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWebToUEInteropSchemaEvolutionTest,
	"WebToUE.Core.InteropSchemaEvolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WebToUE::InteropSchema::Tests
{
	static FWebToUEInteropTypeRef Type(EWebToUEInteropTypeKind Kind)
	{
		return FWebToUEInteropTypeRef::Primitive(Kind);
	}

	static FWebToUEInteropTypeRef Named(const TCHAR* Name)
	{
		return FWebToUEInteropTypeRef::Named(Name);
	}

	static FWebToUEInteropSchemaDescriptor MakeSchema()
	{
		FWebToUEInteropSchemaDescriptor Schema;
		Schema.SchemaId = TEXT("PersonalGame.UI");
		Schema.Version = { 1, 0 };

		FWebToUEInteropEnumDefinition Mode;
		Mode.Name = TEXT("MenuMode");
		Mode.Members = {
			{ TEXT("Pause"), 2 },
			{ TEXT("Main"), 1 },
		};
		Schema.Enums.Add(MoveTemp(Mode));

		FWebToUEInteropRecordDefinition Result;
		Result.Name = TEXT("RefreshResult");
		Result.Fields = {
			{ TEXT("count"), Type(EWebToUEInteropTypeKind::Int32), false },
		};
		Schema.Records.Add(MoveTemp(Result));

		FWebToUEInteropRecordDefinition Request;
		Request.Name = TEXT("RefreshRequest");
		Request.Fields = {
			{ TEXT("force"), Type(EWebToUEInteropTypeKind::Boolean), false },
		};
		Schema.Records.Add(MoveTemp(Request));

		Schema.Data = {
			{ TEXT("title"), Type(EWebToUEInteropTypeKind::Text), true },
			{ TEXT("mode"), Named(TEXT("MenuMode")), true },
		};

		FWebToUEInteropCommandDefinition Refresh;
		Refresh.Name = TEXT("refresh");
		Refresh.Request = Named(TEXT("RefreshRequest"));
		Refresh.Response = EWebToUEInteropCommandResponse::Async;
		Refresh.Result = Named(TEXT("RefreshResult"));
		Refresh.bCancellable = true;
		Schema.Commands.Add(MoveTemp(Refresh));
		return Schema;
	}

	static bool HasCode(
		TConstArrayView<FWebToUEInteropSchemaDiagnostic> Diagnostics,
		const TCHAR* Code)
	{
		return Diagnostics.ContainsByPredicate([Code](const FWebToUEInteropSchemaDiagnostic& Item)
		{
			return Item.Code == Code;
		});
	}
}

bool FWebToUEInteropSchemaCanonicalizationTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::InteropSchema::Tests;
	FWebToUEInteropSchemaSnapshot Snapshot;
	TArray<FWebToUEInteropSchemaDiagnostic> Diagnostics;
	TestTrue(TEXT("An explicit C++ Data/Command schema builds a canonical snapshot"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(MakeSchema(), Snapshot, Diagnostics));
	TestTrue(TEXT("A valid schema has no diagnostics"), Diagnostics.IsEmpty());
	TestTrue(TEXT("Canonical data fields are sorted independently of declaration order"),
		Snapshot.Data.Num() == 2 && Snapshot.Data[0].Name == TEXT("mode") &&
		Snapshot.Data[1].Name == TEXT("title"));
	TestTrue(TEXT("Canonical records and their fields have deterministic order"),
		Snapshot.Records.Num() == 2 && Snapshot.Records[0].Name == TEXT("RefreshRequest") &&
		Snapshot.Records[1].Name == TEXT("RefreshResult"));
	TestEqual(TEXT("The nonzero schema version is preserved"),
		Snapshot.Version.ToString(), FString(TEXT("1.0")));

	FWebToUEInteropSchemaDescriptor Duplicate = MakeSchema();
	Duplicate.Data.Add({ TEXT("Title"), Type(EWebToUEInteropTypeKind::String), true });
	TestFalse(TEXT("UE-ambiguous case-only duplicate names fail closed"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(Duplicate, Snapshot, Diagnostics));
	TestTrue(TEXT("Duplicate rejection is actionable"), HasCode(Diagnostics, TEXT("WTUE-SCHEMA-002")));
	TestTrue(TEXT("A failed build exposes no partial canonical snapshot"), Snapshot.Data.IsEmpty());

	FWebToUEInteropSchemaDescriptor Unknown = MakeSchema();
	Unknown.Data[0].Type = Named(TEXT("MissingType"));
	TestFalse(TEXT("Unknown named types cannot reach Runtime or authoring declarations"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(Unknown, Snapshot, Diagnostics));
	TestTrue(TEXT("Unknown type rejection uses the type-graph diagnostic"),
		HasCode(Diagnostics, TEXT("WTUE-SCHEMA-003")));

	FWebToUEInteropSchemaDescriptor InvalidCommand = MakeSchema();
	InvalidCommand.Commands[0].Response = EWebToUEInteropCommandResponse::None;
	TestFalse(TEXT("A fire-and-forget command cannot silently retain a result contract"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(InvalidCommand, Snapshot, Diagnostics));
	TestTrue(TEXT("Invalid command shape uses the schema-shape diagnostic"),
		HasCode(Diagnostics, TEXT("WTUE-SCHEMA-003")));
	return true;
}

bool FWebToUEInteropSchemaEvolutionTest::RunTest(const FString& Parameters)
{
	using namespace WebToUE::InteropSchema::Tests;
	FWebToUEInteropSchemaSnapshot Previous;
	FWebToUEInteropSchemaSnapshot Current;
	TArray<FWebToUEInteropSchemaDiagnostic> Diagnostics;
	FWebToUEInteropSchemaDescriptor PreviousDescriptor = MakeSchema();
	TestTrue(TEXT("Previous schema fixture is valid"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(PreviousDescriptor, Previous, Diagnostics));
	TestTrue(TEXT("An unchanged schema may keep the same version"),
		FWebToUEInteropSchemaPolicy::ValidateEvolution(Previous, Previous, Diagnostics));

	FWebToUEInteropSchemaDescriptor AdditiveDescriptor = MakeSchema();
	AdditiveDescriptor.Version.Minor = 1;
	AdditiveDescriptor.Data.Add({ TEXT("enabled"), Type(EWebToUEInteropTypeKind::Boolean), true });
	TestTrue(TEXT("Additive descriptor fixture is valid"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(AdditiveDescriptor, Current, Diagnostics));
	TestTrue(TEXT("A minor version may add a typed field without changing existing contracts"),
		FWebToUEInteropSchemaPolicy::ValidateEvolution(Previous, Current, Diagnostics));

	FWebToUEInteropSchemaDescriptor BreakingDescriptor = MakeSchema();
	BreakingDescriptor.Version.Minor = 1;
	BreakingDescriptor.Data[0].Type = Type(EWebToUEInteropTypeKind::String);
	TestTrue(TEXT("Breaking descriptor is structurally valid on its own"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(BreakingDescriptor, Current, Diagnostics));
	TestFalse(TEXT("A minor version cannot change an existing field type"),
		FWebToUEInteropSchemaPolicy::ValidateEvolution(Previous, Current, Diagnostics));
	TestTrue(TEXT("Breaking minor evolution requires an explicit major version"),
		HasCode(Diagnostics, TEXT("WTUE-SCHEMA-004")));

	BreakingDescriptor.Version = { 2, 0 };
	TestTrue(TEXT("Major-version descriptor is structurally valid"),
		FWebToUEInteropSchemaPolicy::BuildSnapshot(BreakingDescriptor, Current, Diagnostics));
	TestTrue(TEXT("An explicit major version owns a deliberate breaking contract"),
		FWebToUEInteropSchemaPolicy::ValidateEvolution(Previous, Current, Diagnostics));
	return true;
}

#endif
