#include "WebToUECoreTypes.h"
#include "WebToUEPerformance.h"

FString FWebToUENode::GetAttribute(const FString& Name) const
{
	if (const FString* Value = Attributes.Find(Name.ToLower()))
	{
		return *Value;
	}
	return FString();
}

bool FWebToUENode::HasClass(const FString& ClassName) const
{
	TArray<FString> Classes;
	GetAttribute(TEXT("class")).ParseIntoArrayWS(Classes);
	return Classes.ContainsByPredicate([&ClassName](const FString& Candidate)
	{
		return Candidate.Equals(ClassName, ESearchCase::IgnoreCase);
	});
}

bool FWebToUENode::IsInteractive() const
{
	return Tag == TEXT("button") || !GetAttribute(TEXT("data-ue-on-click")).IsEmpty();
}

bool FWebToUENode::IsDisplayed(const FWebToUERuntimeNodeState& State) const
{
	return Style.Display != EWebToUEDisplay::None && Style.bVisible && State.bRuntimeVisible;
}

bool FWebToUEDocument::HasErrors() const
{
	return Diagnostics.ContainsByPredicate([](const FWebToUEDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EWebToUEDiagnosticSeverity::Error;
	});
}

void FWebToUEDocument::InitializeRuntimeNodeStates()
{
	RuntimeNodeStates.Reset();
	int32 NodeCount = 0;
	ForEachNode([&NodeCount](FWebToUENode&) { ++NodeCount; });
	if (NodeCount > 0)
	{
		RuntimeNodeStates.Reserve(NodeCount);
		FWebToUEPerformanceCapture::RecordAllocationPayload(
			static_cast<uint64>(NodeCount) * sizeof(FWebToUERuntimeNodeState));
	}
	ForEachNode([this](FWebToUENode& Node)
	{
		AddRuntimeNodeState(Node);
	});
}

FWebToUERuntimeNodeState& FWebToUEDocument::AddRuntimeNodeState(FWebToUENode& Node)
{
	Node.RuntimeStateIndex = RuntimeNodeStates.AddDefaulted();
	return RuntimeNodeStates[Node.RuntimeStateIndex];
}

void FWebToUEDocument::ForEachNode(TFunctionRef<void(FWebToUENode&)> Visitor) const
{
	TFunction<void(const TSharedPtr<FWebToUENode>&)> Walk = [&](const TSharedPtr<FWebToUENode>& Node)
	{
		if (!Node)
		{
			return;
		}
		Visitor(*Node);
		for (const TSharedPtr<FWebToUENode>& Child : Node->Children)
		{
			Walk(Child);
		}
	};
	Walk(Root);
}
