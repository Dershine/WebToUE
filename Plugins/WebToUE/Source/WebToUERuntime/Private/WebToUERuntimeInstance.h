#pragma once

#include "CoreMinimal.h"
#include "WebToUECoreTypes.h"

class UWebToUEDocument;

class FWebToUERuntimeInstance
{
public:
	FWebToUERuntimeInstance();
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

	FWebToUENode* GetHoveredNode() const { return const_cast<FWebToUENode*>(ResolveNode(HoveredNode)); }
	FWebToUENode* GetPressedNode() const { return const_cast<FWebToUENode*>(ResolveNode(PressedNode)); }
	FWebToUENode* GetFocusedNode() const { return const_cast<FWebToUENode*>(ResolveNode(FocusedNode)); }
	void SetHoveredNode(FWebToUENode* Node) { HoveredNode = GetHandle(Node); }
	void SetPressedNode(FWebToUENode* Node) { PressedNode = GetHandle(Node); }
	void SetFocusedNode(FWebToUENode* Node) { FocusedNode = GetHandle(Node); }

#if WITH_DEV_AUTOMATION_TESTS
	uint64 GetKnownOwnedBytesForTesting() const;
	int32 GetRuntimeNodeCountForTesting() const;
	int32 GetRuntimeRuleCountForTesting() const;
#endif

private:
	TSharedPtr<FWebToUEDocument> RuntimeDocument;
	uint64 OwnerId = 0;
	uint32 Generation = 0;
	FWebToUEInstanceHandle HoveredNode;
	FWebToUEInstanceHandle PressedNode;
	FWebToUEInstanceHandle FocusedNode;
};
