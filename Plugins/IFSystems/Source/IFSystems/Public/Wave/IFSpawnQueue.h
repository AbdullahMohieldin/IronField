#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "IFSpawnQueue.generated.h"

/**
 * Pure spawn queue logic with concurrency cap support and cursor-based O(1) popping.
 */
USTRUCT(BlueprintType)
struct IFSYSTEMS_API FIFSpawnQueue
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<TSubclassOf<APawn>> PendingClasses;

	UPROPERTY(Transient)
	int32 HeadCursor = 0;

	void Reset()
	{
		PendingClasses.Reset();
		HeadCursor = 0;
	}

	void Enqueue(TSubclassOf<APawn> Class)
	{
		if (Class)
		{
			PendingClasses.Add(Class);
		}
	}

	bool HasPending() const
	{
		return HeadCursor < PendingClasses.Num();
	}

	int32 NumPending() const
	{
		return FMath::Max(0, PendingClasses.Num() - HeadCursor);
	}

	int32 TotalQueued() const
	{
		return PendingClasses.Num();
	}

	TSubclassOf<APawn> Dequeue()
	{
		if (HasPending())
		{
			return PendingClasses[HeadCursor++];
		}
		return nullptr;
	}

	TSubclassOf<APawn> Peek() const
	{
		if (HasPending())
		{
			return PendingClasses[HeadCursor];
		}
		return nullptr;
	}
};
