#pragma once

#include "CoreMinimal.h"
#include "WebToUEDocument.h"
#include "WebToUEMaterialParameters.h"

class FWebToUELayoutEngine;

struct FWebToUERuntimeBindingOp
{
	EWebToUEBindingKind Kind = EWebToUEBindingKind::Text;
	FWebToUEInstanceHandle Target;
	bool bRichText = false;
};

struct FWebToUEMaterialParameterRuntimeState
{
	FWebToUEMaterialParameterValue Value;
	EWebToUEPropertyWriter DurableOwner = EWebToUEPropertyWriter::Binding;
};

class FWebToUERuntimeInstance
{
public:
	FWebToUERuntimeInstance();
	~FWebToUERuntimeInstance();
	void Reset();
	bool Hydrate(const UWebToUEDocument& CompiledDocument);
	void AdoptDocumentForTesting(TSharedRef<FWebToUEDocument> InDocument);

	FWebToUEDocument* GetDocument() { return RuntimeDocument.Get(); }
	const FWebToUEDocument* GetDocument() const { return RuntimeDocument.Get(); }
	FWebToUERuntimeNodeState& GetState(FWebToUENode& Node) { return RuntimeDocument->GetRuntimeNodeState(Node); }
	const FWebToUERuntimeNodeState& GetState(const FWebToUENode& Node) const { return RuntimeDocument->GetRuntimeNodeState(Node); }
	FWebToUEComputedStyle& GetStyle(FWebToUENode& Node) { return RuntimeDocument->GetComputedStyle(Node); }
	const FWebToUEComputedStyle& GetStyle(const FWebToUENode& Node) const { return RuntimeDocument->GetComputedStyle(Node); }
	FWebToUERuntimeLayoutResult& GetLayout(FWebToUENode& Node) { return RuntimeDocument->GetLayoutResult(Node); }
	const FWebToUERuntimeLayoutResult& GetLayout(const FWebToUENode& Node) const { return RuntimeDocument->GetLayoutResult(Node); }
	FWebToUEInstanceHandle GetHandle(const FWebToUENode* Node) const;
	FWebToUENode* ResolveNode(FWebToUEInstanceHandle Handle);
	const FWebToUENode* ResolveNode(FWebToUEInstanceHandle Handle) const;
	const TMap<FName, TArray<FWebToUERuntimeBindingOp>>& GetBindingIndex() const
	{
		return BindingOpsByField;
	}
	TConstArrayView<FWebToUERuntimeBindingOp> GetBindingOps(FName RootField) const;
	TConstArrayView<FWebToUECompiledResource> GetResourceManifest() const
	{
		return ResourceManifest;
	}
	const FWebToUEMaterialParameterRuntimeState* FindMaterialParameterState(
		FWebToUEInstanceHandle Target,
		const FWebToUEPropertyAddress& Address) const;
	const TMap<FWebToUEPropertyAddress, FWebToUEMaterialParameterRuntimeState>*
		FindMaterialParameterStates(FWebToUEInstanceHandle Target) const;
	bool CommitMaterialParameterState(
		FWebToUEInstanceHandle Target,
		const FWebToUEPropertyAddress& Address,
		const FWebToUEMaterialParameterValue& Value,
		EWebToUEPropertyWriter DurableOwner);
	FWebToUELayoutEngine& GetLayoutEngine() { return *LayoutEngine; }

#if WITH_DEV_AUTOMATION_TESTS
	uint64 GetKnownOwnedBytesForTesting() const;
	uint64 GetSharedStyleTemplateKnownOwnedBytesForTesting() const;
	const void* GetSharedStyleTemplateIdentityForTesting() const;
	int32 GetRuntimeNodeCountForTesting() const;
	int32 GetRuntimeRuleCountForTesting() const;
#endif

private:
	TSharedPtr<FWebToUEDocument> RuntimeDocument;
	uint64 OwnerId = 0;
	uint32 Generation = 0;
	TMap<FName, TArray<FWebToUERuntimeBindingOp>> BindingOpsByField;
	TArray<FWebToUECompiledResource> ResourceManifest;
	TMap<FWebToUEInstanceHandle,
		TMap<FWebToUEPropertyAddress, FWebToUEMaterialParameterRuntimeState>>
		MaterialParameterStates;
	TUniquePtr<FWebToUELayoutEngine> LayoutEngine;
};
