#pragma once

#include "CoreMinimal.h"
#include "Tactical/IFTacticalTypes.h"

class UWorld;

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

	float LateralSpacing = 105.f;
	float RankSpacing = 115.f;
	float SquareSpacingScale = 0.95f;
	float LineAspectRatio = 2.5f;
	int32 MinLineColumns = 3;
	int32 MaxLineColumns = 10;
	float MinShapeRadius = 95.f;
	float MaxShapeRadius = 1400.f;

	/** Tight horizontal projection extent rejects slots inside walls/towers while Z follows ramps and stairs. */
	FVector NavQueryExtent = FVector(34.f, 34.f, 220.f);

	/** Disable during live RMB drag previews so 120-unit drags near walls skip synchronous FindPathSync A* fallback. */
	bool bAllowPathfindingFallback = true;
};

/**
 * Stateless Action-RTS formation lattice generator and globally optimal Kuhn-Munkres slot solver.
 */
struct IRONFIELD_API FIFFormationSolver
{
	/**
	 * Generates an ordered candidate lattice and filters slots against the NavMesh using tight
	 * projection plus NavigationRaycast (with a bounded path-length fallback around small obstacles
	 * such as flag poles) so formations wrap cleanly without bleeding through castle walls.
	 */
	static bool SolveFormation(
		UWorld* World,
		const FIFFormationSolveParams& Params,
		TArray<FIFFormationSlot>& OutSlots,
		FVector& OutFormationCenter);

	/**
	 * Maps each soldier index [0..N-1] to a unique slot index [0..N-1] via the exact O(N^3)
	 * Kuhn-Munkres (Hungarian) algorithm minimizing strictly convex squared 2D travel distance.
	 */
	static void AssignSoldiersToSlots(
		const TArray<FVector>& SoldierLocations,
		const TArray<FIFFormationSlot>& Slots,
		TArray<int32>& OutSoldierToSlotIndices);
};
