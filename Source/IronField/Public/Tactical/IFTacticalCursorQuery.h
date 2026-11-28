#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class AActor;
class AIFStructure;
class APawn;
class UWorld;
struct FHitResult;

/**
 * Stateless cursor raycasting utilities for Tactical Command Mode.
 * Handles pawn selection, hostile/friendly structure picking, and ground plane traces.
 */
struct IRONFIELD_API FIFTacticalCursorQuery
{
	static AActor* FindBestPawnUnderCursor(
		const UWorld* World,
		const FVector& RayOrigin,
		const FVector& RayDir,
		float MaxDistance,
		float ToleranceRadius,
		const AActor* IgnoredActor,
		TFunctionRef<bool(const AActor*)> IsEligible);

	static AIFStructure* FindBestStructureUnderCursor(
		const UWorld* World,
		const FVector& RayOrigin,
		const FVector& RayDir,
		float MaxDistance,
		const APawn* PlayerPawn,
		const AActor* IgnoredActor,
		bool bHostileOnly);

	static bool TraceGroundUnderCursor(
		const UWorld* World,
		const FVector& RayOrigin,
		const FVector& RayDir,
		float MaxDistance,
		const AActor* IgnoredActor,
		FHitResult& OutHit,
		TArray<FHitResult>* ScratchHits = nullptr);
};
