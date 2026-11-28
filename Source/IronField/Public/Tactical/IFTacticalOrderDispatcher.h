#pragma once

#include "CoreMinimal.h"
#include "Tactical/IFTacticalTypes.h"

class AActor;
class AIFSoldierCharacter;
class AIFStructure;
class APawn;
class UWorld;

struct FIFTacticalFormationOrderConfig
{
	EIFTacticalFormationType FormationType = EIFTacticalFormationType::Line;
	FVector AnchorPoint = FVector::ZeroVector;
	FVector DragEndPoint = FVector::ZeroVector;
	bool bIsDrag = false;
	FVector CameraForward = FVector::ForwardVector;
	float LateralSpacing = 105.f;
	float RankSpacing = 115.f;
	float LineAspectRatio = 2.5f;
	int32 MinLineColumns = 3;
	int32 MaxLineColumns = 10;
	float MinShapeRadius = 110.f;
	float MaxShapeRadius = 1400.f;
	float MoveOrderAcceptanceRadius = 20.f;
};

/**
 * Stateless order execution utilities for Tactical Command Mode.
 * Handles Follow, Cluster Attack distribution, Structure Attack, and Formation Move commits.
 */
struct IRONFIELD_API FIFTacticalOrderDispatcher
{
	static void DispatchFollowOrder(
		const TArray<AIFSoldierCharacter*>& Squad,
		AActor* TargetActor,
		float FollowRadius);

	static void DispatchClusterAttackOrder(
		UWorld* World,
		const APawn* CommanderPawn,
		const TArray<AIFSoldierCharacter*>& Squad,
		const TArray<FVector>& SquadLocations,
		AActor* PrimaryTarget,
		float ClusterRadius,
		float TargetSpreadPenalty);

	static void DispatchStructureAttackOrder(
		const TArray<AIFSoldierCharacter*>& Squad,
		AIFStructure* HostileStructure);

	static bool DispatchFormationMoveOrder(
		UWorld* World,
		const TArray<AIFSoldierCharacter*>& Squad,
		const TArray<FVector>& SquadLocations,
		const FVector& SquadCentroid,
		const FIFTacticalFormationOrderConfig& Config,
		FVector& OutFormationCenter);
};
