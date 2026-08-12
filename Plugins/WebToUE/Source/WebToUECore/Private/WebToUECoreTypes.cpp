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

bool FWebToUEDocument::HasErrors() const
{
	return Diagnostics.ContainsByPredicate([](const FWebToUEDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EWebToUEDiagnosticSeverity::Error;
	});
}

void FWebToUEDocument::InitializeRuntimeData()
{
	RuntimeNodeStates.Reset();
	RuntimeRenderData.Reset();
	int32 NodeCount = 0;
	ForEachNode([&NodeCount](FWebToUENode&) { ++NodeCount; });
	if (NodeCount > 0)
	{
		RuntimeNodeStates.Reserve(NodeCount);
		RuntimeRenderData.Reserve(NodeCount);
		FWebToUEPerformanceCapture::RecordAllocationPayload(
			static_cast<uint64>(NodeCount) * sizeof(FWebToUERuntimeNodeState));
		FWebToUEPerformanceCapture::RecordAllocationPayload(
			static_cast<uint64>(NodeCount) * sizeof(FWebToUERuntimeRenderData));
	}
	ForEachNode([this](FWebToUENode& Node)
	{
		AddRuntimeNodeData(Node);
	});
}

void FWebToUEDocument::AddRuntimeNodeData(FWebToUENode& Node)
{
	Node.RuntimeDataIndex = RuntimeNodeStates.AddDefaulted();
	const int32 RenderDataIndex = RuntimeRenderData.AddDefaulted();
	check(Node.RuntimeDataIndex == RenderDataIndex);
}

bool FWebToUEDocument::IsDisplayed(const FWebToUENode& Node) const
{
	const FWebToUEComputedStyle& Style = GetComputedStyle(Node);
	return Style.Display != EWebToUEDisplay::None && Style.bVisible && GetRuntimeNodeState(Node).bRuntimeVisible;
}

bool FWebToUEDocument::ClipsOverflow(const FWebToUENode& Node) const
{
	return GetComputedStyle(Node).Overflow != EWebToUEOverflow::Visible;
}

bool FWebToUEDocument::IsScrollable(const FWebToUENode& Node) const
{
	const EWebToUEOverflow Overflow = GetComputedStyle(Node).Overflow;
	return Overflow == EWebToUEOverflow::Auto || Overflow == EWebToUEOverflow::Scroll;
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
