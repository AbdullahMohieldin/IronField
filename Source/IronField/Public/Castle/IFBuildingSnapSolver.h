#pragma once

#include "CoreMinimal.h"
#include "Castle/IFBuildingTypes.h"
#include "Castle/IFCastleTypes.h"

class AIFPlacementGhost;
class AIFStructure;
class AIFTerritory;
class UIFStructureDataAsset;
class UWorld;

/**
 * Stateless placement, snapping, and 3D OBB validation solver for IronField castle structures.
 */
class IRONFIELD_API FIFBuildingSnapSolver
{
public:
	static bool DoesWorldBoxOverlap(
		const FIFWorldCollisionBox& A,
		const FIFWorldCollisionBox& B,
		float XYInsetCm = 3.0f,
		float ZToleranceCm = 2.0f);

	static FIFSnapResult SolvePlacementTransform(
		const UWorld* World,
		const UIFStructureDataAsset* Data,
		const FVector& CursorGroundLocation,
		int32 FreeYawIndex,
		int32 ConnectorFlipIndex,
		int32 StairMountMode,
		float SnapRadiusCm,
		float MountSnapRadiusCm,
		const AIFStructure* IgnoredStructure);

	static bool ValidatePlacement(
		const UWorld* World,
		const UIFStructureDataAsset* Data,
		const FIFSnapResult& Candidate,
		const AActor* PlayerPawn,
		bool bRequireHeroInTerritory,
		const AIFStructure* RelocatingStructure,
		const AIFPlacementGhost* PlacementGhost,
		AIFTerritory*& OutTerritory,
		EIFPlacementInvalidReason& OutReason);

	static bool HasStoneWallBacking(
		const UWorld* World,
		const FVector& WalkwayLocation,
		float WalkwayYaw,
		const AIFStructure* IgnoredStructure,
		bool& OutIsCornerInnerWalkway,
		const AIFStructure** OutBackingHost = nullptr);
};
