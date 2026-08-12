#pragma once

#include "CoreMinimal.h"
#include "WebToUECoreTypes.h"

class UWebToUEDocument;

class FWebToUERuntimeInstance
{
public:
	void Reset();
	bool Hydrate(const UWebToUEDocument& CompiledDocument);
	void AdoptDocumentForTesting(TSharedRef<FWebToUEDocument> InDocument)
	{
		Reset();
		RuntimeDocument = MoveTemp(InDocument);
	}

	FWebToUEDocument* GetDocument() { return RuntimeDocument.Get(); }
	const FWebToUEDocument* GetDocument() const { return RuntimeDocument.Get(); }
	FWebToUERuntimeNodeState& GetState(FWebToUENode& Node) { return RuntimeDocument->GetRuntimeNodeState(Node); }
	const FWebToUERuntimeNodeState& GetState(const FWebToUENode& Node) const { return RuntimeDocument->GetRuntimeNodeState(Node); }

	FWebToUENode* GetHoveredNode() const { return HoveredNode; }
	FWebToUENode* GetPressedNode() const { return PressedNode; }
	FWebToUENode* GetFocusedNode() const { return FocusedNode; }
	void SetHoveredNode(FWebToUENode* Node) { HoveredNode = Node; }
	void SetPressedNode(FWebToUENode* Node) { PressedNode = Node; }
	void SetFocusedNode(FWebToUENode* Node) { FocusedNode = Node; }

private:
	TSharedPtr<FWebToUEDocument> RuntimeDocument;
	FWebToUENode* HoveredNode = nullptr;
	FWebToUENode* PressedNode = nullptr;
	FWebToUENode* FocusedNode = nullptr;
};
