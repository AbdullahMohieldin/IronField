#pragma once

#include "CoreMinimal.h"
#include "Tactical/IFTacticalTypes.h"

class UNavigationSystemV1;

struct IRONFIELD_API FIFFormationSolveParams
{
	EIFTacticalFormationType FormationType = EIFTacticalFormationType::Line;
	int32 UnitCount = 0;

	/** Click position for instant RMB orders, or drag origin for RMB hold-and-drag orders. */
	FVector AnchorPoint = FVector::ZeroVector;

	/** Drag endpoint when bIsDrag is true (right flank for Line, or perimeter handle for Square/Circle). */
	FVector DragEndPoint = FVector::ZeroVector;
	bool bIsDrag = false;

	/** Squad center and camera forward used for default orientation and single-unit fallbacks. */
	FVector SquadCentroid = FVector::ZeroVector;
	FVector CameraForward = FVector::ForwardVector;

	float LateralSpacing = 140.f;
	float RankSpacing = 150.f;
	float SquareSpacingScale = 1.0f;
	float LineAspectRatio = 2.5f;
	int32 MinLineColumns = 3;
	int32 MaxLineColumns = 10;
	float MinShapeRadius = 110.f;
	float MaxShapeRadius = 1400.f;

	/** Projection query extent for snapping individual slots onto the NavMesh. */
	FVector NavQueryExtent = FVector(180.f, 180.f, 300.f);
};

/**
 * Stateless Action-RTS formation geometry and crossing-free slot assignment solver.
 * Supports instant RMB clicks and RMB hold-and-drag sizing/orientation for Line, Square, and Circle.
 */
struct IRONFIELD_API FIFFormationSolver
{
	/**
	 * Computes NavMesh-projected slots with per-slot arrival facing yaws.
	 * Returns true if at least one slot was produced.
	 */
	static bool SolveFormation(
		const FIFFormationSolveParams& Params,
		const UNavigationSystemV1* NavSys,
		TArray<FIFFormationSlot>& OutSlots,
		FVector& OutFormationCenter);

	/**
	 * Maps each soldier index [0..N-1] to a unique slot index [0..N-1] using greedy nearest seeding
	 * followed by a deterministic 2-opt swap pass minimizing total squared 2D travel distance.
	 */
	static void AssignSoldiersToSlots(
		const TArray<FVector>& SoldierLocations,
		const TArray<FIFFormationSlot>& Slots,
		TArray<int32>& OutSoldierToSlotIndices);
};
