#pragma once

#include "CoreMinimal.h"

/** Version of one project-owned Data/Command contract. Major changes may break consumers. */
struct WEBTOUECORE_API FWebToUEInteropSchemaVersion
{
	uint16 Major = 0;
	uint16 Minor = 0;

	bool IsValid() const { return Major != 0; }
	FString ToString() const { return FString::Printf(TEXT("%u.%u"), Major, Minor); }

	friend bool operator==(
		const FWebToUEInteropSchemaVersion& A,
		const FWebToUEInteropSchemaVersion& B) = default;
};

/** Closed P0.5 value algebra. UObject and arbitrary reflected function types are absent. */
enum class EWebToUEInteropTypeKind : uint8
{
	Invalid,
	Void,
	Boolean,
	Int32,
	Float32,
	Float64,
	String,
	Name,
	Text,
	Named
};

enum class EWebToUEInteropContainer : uint8
{
	Scalar,
	Array
};

struct WEBTOUECORE_API FWebToUEInteropTypeRef
{
	EWebToUEInteropTypeKind Kind = EWebToUEInteropTypeKind::Invalid;
	EWebToUEInteropContainer Container = EWebToUEInteropContainer::Scalar;
	FString NamedType;
	bool bOptional = false;

	static FWebToUEInteropTypeRef Primitive(
		EWebToUEInteropTypeKind InKind,
		EWebToUEInteropContainer InContainer = EWebToUEInteropContainer::Scalar,
		bool bInOptional = false);
	static FWebToUEInteropTypeRef Named(
		FString InName,
		EWebToUEInteropContainer InContainer = EWebToUEInteropContainer::Scalar,
		bool bInOptional = false);
	static FWebToUEInteropTypeRef Void();

	bool IsVoid() const { return Kind == EWebToUEInteropTypeKind::Void; }

	friend bool operator==(
		const FWebToUEInteropTypeRef& A,
		const FWebToUEInteropTypeRef& B) = default;
};

struct WEBTOUECORE_API FWebToUEInteropEnumMember
{
	FString Name;
	int64 Value = 0;

	friend bool operator==(
		const FWebToUEInteropEnumMember& A,
		const FWebToUEInteropEnumMember& B) = default;
};

struct WEBTOUECORE_API FWebToUEInteropEnumDefinition
{
	FString Name;
	TArray<FWebToUEInteropEnumMember> Members;

	friend bool operator==(
		const FWebToUEInteropEnumDefinition& A,
		const FWebToUEInteropEnumDefinition& B) = default;
};

/** bObservable declares a Data/FieldNotify capability; record members normally leave it false. */
struct WEBTOUECORE_API FWebToUEInteropFieldDefinition
{
	FString Name;
	FWebToUEInteropTypeRef Type;
	bool bObservable = false;

	friend bool operator==(
		const FWebToUEInteropFieldDefinition& A,
		const FWebToUEInteropFieldDefinition& B) = default;
};

struct WEBTOUECORE_API FWebToUEInteropRecordDefinition
{
	FString Name;
	TArray<FWebToUEInteropFieldDefinition> Fields;

	friend bool operator==(
		const FWebToUEInteropRecordDefinition& A,
		const FWebToUEInteropRecordDefinition& B) = default;
};

enum class EWebToUEInteropCommandResponse : uint8
{
	None,
	Immediate,
	Async
};

struct WEBTOUECORE_API FWebToUEInteropCommandDefinition
{
	FString Name;
	FWebToUEInteropTypeRef Request = FWebToUEInteropTypeRef::Void();
	EWebToUEInteropCommandResponse Response = EWebToUEInteropCommandResponse::None;
	FWebToUEInteropTypeRef Result = FWebToUEInteropTypeRef::Void();
	bool bCancellable = false;

	friend bool operator==(
		const FWebToUEInteropCommandDefinition& A,
		const FWebToUEInteropCommandDefinition& B) = default;
};

/**
 * Authoritative descriptor authored in project C++.
 *
 * UHT types, MVVM adapters, generated .d.ts files and UI Source are consumers; none are allowed
 * to generate or mutate this descriptor as part of UBT.
 */
struct WEBTOUECORE_API FWebToUEInteropSchemaDescriptor
{
	FString SchemaId;
	FWebToUEInteropSchemaVersion Version;
	TArray<FWebToUEInteropEnumDefinition> Enums;
	TArray<FWebToUEInteropRecordDefinition> Records;
	TArray<FWebToUEInteropFieldDefinition> Data;
	TArray<FWebToUEInteropCommandDefinition> Commands;
};

/** Immutable-by-convention, validated and deterministically sorted compiler/runtime input. */
struct WEBTOUECORE_API FWebToUEInteropSchemaSnapshot
{
	FString SchemaId;
	FWebToUEInteropSchemaVersion Version;
	TArray<FWebToUEInteropEnumDefinition> Enums;
	TArray<FWebToUEInteropRecordDefinition> Records;
	TArray<FWebToUEInteropFieldDefinition> Data;
	TArray<FWebToUEInteropCommandDefinition> Commands;

	void Reset();

	friend bool operator==(
		const FWebToUEInteropSchemaSnapshot& A,
		const FWebToUEInteropSchemaSnapshot& B) = default;
};

enum class EWebToUEInteropSchemaDiagnosticSeverity : uint8
{
	Warning,
	Error
};

struct WEBTOUECORE_API FWebToUEInteropSchemaDiagnostic
{
	EWebToUEInteropSchemaDiagnosticSeverity Severity =
		EWebToUEInteropSchemaDiagnosticSeverity::Error;
	FString Code;
	FString Path;
	FString Detail;
};

/** Builds canonical snapshots and enforces explicit major/minor schema evolution. */
class WEBTOUECORE_API FWebToUEInteropSchemaPolicy final
{
public:
	static bool BuildSnapshot(
		const FWebToUEInteropSchemaDescriptor& Descriptor,
		FWebToUEInteropSchemaSnapshot& OutSnapshot,
		TArray<FWebToUEInteropSchemaDiagnostic>& OutDiagnostics);

	static bool ValidateEvolution(
		const FWebToUEInteropSchemaSnapshot& Previous,
		const FWebToUEInteropSchemaSnapshot& Current,
		TArray<FWebToUEInteropSchemaDiagnostic>& OutDiagnostics);
};
