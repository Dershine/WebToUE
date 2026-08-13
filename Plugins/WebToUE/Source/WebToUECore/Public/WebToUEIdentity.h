#pragma once

#include "CoreMinimal.h"

struct WEBTOUECORE_API FWebToUETemplateNodeId
{
	static FWebToUETemplateNodeId FromIndex(int32 InIndex)
	{
		FWebToUETemplateNodeId Result;
		Result.Index = InIndex;
		return Result;
	}

	bool IsValid() const { return Index != INDEX_NONE; }
	int32 GetIndex() const { return Index; }

	friend bool operator==(const FWebToUETemplateNodeId& A, const FWebToUETemplateNodeId& B)
	{
		return A.Index == B.Index;
	}

	friend bool operator!=(const FWebToUETemplateNodeId& A, const FWebToUETemplateNodeId& B)
	{
		return !(A == B);
	}

private:
	int32 Index = INDEX_NONE;
};

FORCEINLINE uint32 GetTypeHash(const FWebToUETemplateNodeId& Id)
{
	return GetTypeHash(Id.GetIndex());
}

struct WEBTOUECORE_API FWebToUEInstanceHandle
{
	static FWebToUEInstanceHandle Create(uint64 InOwnerId, uint32 InGeneration, int32 InSlot)
	{
		FWebToUEInstanceHandle Result;
		Result.OwnerId = InOwnerId;
		Result.Generation = InGeneration;
		Result.Slot = InSlot;
		return Result;
	}

	bool IsValid() const
	{
		return OwnerId != 0 && Generation != 0 && Slot != INDEX_NONE;
	}

	uint64 GetOwnerId() const { return OwnerId; }
	uint32 GetGeneration() const { return Generation; }
	int32 GetSlot() const { return Slot; }

	friend bool operator==(const FWebToUEInstanceHandle& A, const FWebToUEInstanceHandle& B)
	{
		return A.OwnerId == B.OwnerId && A.Generation == B.Generation && A.Slot == B.Slot;
	}

	friend bool operator!=(const FWebToUEInstanceHandle& A, const FWebToUEInstanceHandle& B)
	{
		return !(A == B);
	}

private:
	uint64 OwnerId = 0;
	uint32 Generation = 0;
	int32 Slot = INDEX_NONE;
};

FORCEINLINE uint32 GetTypeHash(const FWebToUEInstanceHandle& Handle)
{
	return HashCombineFast(GetTypeHash(Handle.GetOwnerId()),
		HashCombineFast(GetTypeHash(Handle.GetGeneration()), GetTypeHash(Handle.GetSlot())));
}

WEBTOUECORE_API uint64 AllocateWebToUEInstanceOwnerId();
