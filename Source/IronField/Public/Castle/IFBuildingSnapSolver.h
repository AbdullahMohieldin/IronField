#pragma once

#include "CoreMinimal.h"
#include "Castle/IFBuildingTypes.h"
#include "Castle/IFCastleTypes.h"
#include "Engine/OverlapResult.h"
#include "GenericTeamAgentInterface.h"

class AActor;
class AIFPlacementGhost;
class AIFStructure;
class AIFTerritory;
class UIFStructureDataAsset;
class UWorld;

struct IRONFIELD_API FIFSnapSolverScratch
{
	TArray<const AIFStructure*> AllAliveStructures;
	TArray<const AIFStructure*> FriendlyStructures;
	TArray<FIFWorldCollisionBox> CandidateBoxes;
	TArray<FIFWorldCollisionBox> ExistingBoxes;
	TArray<FIFWorldCollisionBox> OtherBoxes;
	TArray<FIFModularConnector> TargetConnectors;
	TArray<FOverlapResult> OverlapResults;
};

struct IRONFIELD_API FIFSnapQuery
{
	const UWorld* World = nullptr;
	const UIFStructureDataAsset* Data = nullptr;
	FVector CursorGroundLocation = FVector::ZeroVector;
	int32 FreeYawIndex = 0;
	int32 ConnectorFlipIndex = 0;
	int32 StairMountMode = 0;
	float SnapRadiusCm = 200.f;
	float MountSnapRadiusCm = 320.f;
	const AIFStructure* PreviousSnapHost = nullptr;
	int8 PreviousSideSign = 0;
};

struct IRONFIELD_API FIFPlacementValidationContext
{
	const UWorld* World = nullptr;
	const UIFStructureDataAsset* Data = nullptr;
	const FIFSnapResult* Candidate = nullptr;
	const AActor* PlayerPawn = nullptr;
	const AActor* GroundActor = nullptr;
	FVector GroundNormal = FVector::UpVector;
	float MaxPlacementSlopeDegrees = 25.f;
	FGenericTeamId BuilderTeamId = FGenericTeamId::NoTeam;
	bool bRequireHeroInTerritory = false;
	const AIFStructure* RelocatingStructure = nullptr;
	const AIFPlacementGhost* PlacementGhost = nullptr;
};

/**
 * Stateless placement, snapping, and 3D OBB validation solver for IronField castle structures.
 */
class IRONFIELD_API FIFBuildingSnapSolver
{
public:
	static void GatherAliveStructures(
		const UWorld* World,
		FGenericTeamId BuilderTeamId,
		const AIFStructure* IgnoredStructure,
		TArray<const AIFStructure*>& OutAllAlive,
		TArray<const AIFStructure*>& OutFriendly);

	static FIFSnapResult SolvePlacementTransform(
		const FIFSnapQuery& Query,
		TArrayView<const AIFStructure* const> FriendlyStructures,
		FIFSnapSolverScratch& Scratch);

	static bool ValidatePlacement(
		const FIFPlacementValidationContext& Context,
		TArrayView<const AIFStructure* const> AllAliveStructures,
		FIFSnapSolverScratch& Scratch,
		AIFTerritory*& OutTerritory,
		EIFPlacementInvalidReason& OutReason);
};
