#include "WebToUESemanticIdentity.h"

namespace WebToUE::SemanticIdentity::Private
{
	static void AddDiagnostic(
		FWebToUEReimportStatePlan& Plan,
		EWebToUESemanticIdentityDiagnosticSeverity Severity,
		const TCHAR* Code,
		FString Detail,
		const FWebToUESourceProvenance& Provenance = {})
	{
		FWebToUESemanticIdentityDiagnostic& Diagnostic =
			Plan.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Severity = Severity;
		Diagnostic.Code = Code;
		Diagnostic.Detail = MoveTemp(Detail);
		Diagnostic.Provenance = Provenance;
	}

	static FString DescribeAddress(const FWebToUESemanticAddress& Address)
	{
		FString ComponentPath = Address.Component.RouteId;
		for (const FString& Segment : Address.Component.KeyPath)
		{
			ComponentPath += TEXT("/") + Segment;
		}
		return FString::Printf(
			TEXT("%s::%s"), *ComponentPath, *Address.Key.ToString());
	}

	static bool HasOnlyKnownStateFlags(EWebToUECrossReloadState State)
	{
		return (static_cast<uint8>(State) &
			~static_cast<uint8>(EWebToUECrossReloadState::All)) == 0;
	}

	static bool ValidateNode(
		const FWebToUEReimportIdentityContext& Context,
		const FWebToUESemanticNodeDescriptor& Node,
		uint32 ExpectedGeneration,
		FWebToUEReimportStatePlan& Plan,
		const TCHAR* RevisionName)
	{
		if (!Node.Handle.IsValid() || Node.Handle.GetOwnerId() != Context.OwnerId ||
			Node.Handle.GetGeneration() != ExpectedGeneration || !Node.Component.IsValid() ||
			!Node.Provenance.IsValid() || Node.Kind == EWebToUESemanticNodeKind::Invalid ||
			Node.StateContractVersion == 0 || !HasOnlyKnownStateFlags(Node.RetainableState))
		{
			AddDiagnostic(Plan, EWebToUESemanticIdentityDiagnosticSeverity::Error,
				TEXT("WTUE-ID-001"),
				FString::Printf(TEXT("%s node descriptor is outside the declared identity domain"),
					RevisionName), Node.Provenance);
			return false;
		}
		return true;
	}

	static void SortDiagnostics(FWebToUEReimportStatePlan& Plan)
	{
		Plan.Diagnostics.Sort([](
			const FWebToUESemanticIdentityDiagnostic& A,
			const FWebToUESemanticIdentityDiagnostic& B)
		{
			if (A.Code != B.Code)
			{
				return A.Code < B.Code;
			}
			if (A.Provenance.SourceUnit != B.Provenance.SourceUnit)
			{
				return A.Provenance.SourceUnit < B.Provenance.SourceUnit;
			}
			if (A.Provenance.Line != B.Provenance.Line)
			{
				return A.Provenance.Line < B.Provenance.Line;
			}
			if (A.Provenance.Column != B.Provenance.Column)
			{
				return A.Provenance.Column < B.Provenance.Column;
			}
			return A.Detail < B.Detail;
		});
	}

	static void SortActions(FWebToUEReimportStatePlan& Plan)
	{
		Plan.Actions.Sort([](
			const FWebToUEReimportStateAction& A,
			const FWebToUEReimportStateAction& B)
		{
			const bool bAHasCurrent = A.CurrentHandle.IsValid();
			const bool bBHasCurrent = B.CurrentHandle.IsValid();
			if (bAHasCurrent != bBHasCurrent)
			{
				return bAHasCurrent;
			}
			const int32 ASlot = bAHasCurrent
				? A.CurrentHandle.GetSlot() : A.PreviousHandle.GetSlot();
			const int32 BSlot = bBHasCurrent
				? B.CurrentHandle.GetSlot() : B.PreviousHandle.GetSlot();
			return ASlot < BSlot;
		});
	}
}

bool FWebToUESemanticIdentityPolicy::BuildReimportPlan(
	const FWebToUEReimportIdentityContext& Context,
	TConstArrayView<FWebToUESemanticNodeDescriptor> PreviousNodes,
	TConstArrayView<FWebToUESemanticNodeDescriptor> CurrentNodes,
	FWebToUEReimportStatePlan& OutPlan)
{
	using namespace WebToUE::SemanticIdentity::Private;
	OutPlan.Reset();
	if (!Context.IsValid())
	{
		AddDiagnostic(OutPlan, EWebToUESemanticIdentityDiagnosticSeverity::Error,
			TEXT("WTUE-ID-001"), TEXT("reimport identity context must use one owner and two distinct generations"));
		return false;
	}

	TMap<FWebToUESemanticAddress, int32> PreviousByAddress;
	TMap<FWebToUESemanticAddress, int32> CurrentByAddress;
	bool bValid = true;
	const auto IndexNodes = [&](
		TConstArrayView<FWebToUESemanticNodeDescriptor> Nodes,
		uint32 Generation,
		const TCHAR* RevisionName,
		TMap<FWebToUESemanticAddress, int32>& OutIndex)
	{
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FWebToUESemanticNodeDescriptor& Node = Nodes[Index];
			bValid &= ValidateNode(Context, Node, Generation, OutPlan, RevisionName);
			if (!Node.StableKey.IsValid())
			{
				continue;
			}
			const FWebToUESemanticAddress Address = Node.GetAddress();
			if (const int32* ExistingIndex = OutIndex.Find(Address))
			{
				AddDiagnostic(OutPlan, EWebToUESemanticIdentityDiagnosticSeverity::Error,
					TEXT("WTUE-ID-002"),
					FString::Printf(TEXT("duplicate Stable Semantic Key '%s' in %s revision; first declared at %s:%d:%d"),
						*DescribeAddress(Address), RevisionName,
						*Nodes[*ExistingIndex].Provenance.SourceUnit,
						Nodes[*ExistingIndex].Provenance.Line,
						Nodes[*ExistingIndex].Provenance.Column), Node.Provenance);
				bValid = false;
				continue;
			}
			OutIndex.Add(Address, Index);
		}
	};

	IndexNodes(PreviousNodes, Context.PreviousGeneration, TEXT("previous"), PreviousByAddress);
	IndexNodes(CurrentNodes, Context.CurrentGeneration, TEXT("current"), CurrentByAddress);
	if (!bValid)
	{
		OutPlan.Actions.Reset();
		SortDiagnostics(OutPlan);
		return false;
	}

	TSet<int32> MatchedPrevious;
	for (const FWebToUESemanticNodeDescriptor& Current : CurrentNodes)
	{
		FWebToUEReimportStateAction Action;
		Action.Address = Current.GetAddress();
		Action.CurrentHandle = Current.Handle;
		Action.CurrentProvenance = Current.Provenance;
		if (!Current.StableKey.IsValid())
		{
			Action.Disposition = EWebToUEReimportStateDisposition::ResetUnkeyed;
			if (Current.RetainableState != EWebToUECrossReloadState::None)
			{
				AddDiagnostic(OutPlan, EWebToUESemanticIdentityDiagnosticSeverity::Warning,
					TEXT("WTUE-ID-004"),
					TEXT("unkeyed nodes reset even when a state class requests retention"),
					Current.Provenance);
			}
			OutPlan.Actions.Add(MoveTemp(Action));
			continue;
		}

		const int32* PreviousIndex = PreviousByAddress.Find(Action.Address);
		if (!PreviousIndex)
		{
			Action.Disposition = EWebToUEReimportStateDisposition::ResetAdded;
			OutPlan.Actions.Add(MoveTemp(Action));
			continue;
		}

		const FWebToUESemanticNodeDescriptor& Previous = PreviousNodes[*PreviousIndex];
		MatchedPrevious.Add(*PreviousIndex);
		Action.PreviousHandle = Previous.Handle;
		Action.PreviousProvenance = Previous.Provenance;
		if (Previous.Kind != Current.Kind ||
			Previous.StateContractVersion != Current.StateContractVersion)
		{
			Action.Disposition = EWebToUEReimportStateDisposition::ResetIncompatible;
			AddDiagnostic(OutPlan, EWebToUESemanticIdentityDiagnosticSeverity::Warning,
				TEXT("WTUE-ID-003"),
				FString::Printf(TEXT("Stable Semantic Key '%s' matched an incompatible node kind or state contract and was reset"),
					*DescribeAddress(Action.Address)), Current.Provenance);
		}
		else
		{
			Action.Disposition = EWebToUEReimportStateDisposition::Matched;
			Action.PreservedState = static_cast<EWebToUECrossReloadState>(
				static_cast<uint8>(Previous.RetainableState) &
				static_cast<uint8>(Current.RetainableState));
		}
		OutPlan.Actions.Add(MoveTemp(Action));
	}

	for (int32 PreviousIndex = 0; PreviousIndex < PreviousNodes.Num(); ++PreviousIndex)
	{
		if (MatchedPrevious.Contains(PreviousIndex))
		{
			continue;
		}
		const FWebToUESemanticNodeDescriptor& Previous = PreviousNodes[PreviousIndex];
		FWebToUEReimportStateAction& Removed = OutPlan.Actions.AddDefaulted_GetRef();
		Removed.Disposition = EWebToUEReimportStateDisposition::Removed;
		Removed.Address = Previous.GetAddress();
		Removed.PreviousHandle = Previous.Handle;
		Removed.PreviousProvenance = Previous.Provenance;
	}

	SortActions(OutPlan);
	SortDiagnostics(OutPlan);
	return true;
}
