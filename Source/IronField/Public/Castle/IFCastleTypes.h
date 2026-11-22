#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "IFCastleTypes.generated.h"

class AActor;
class AIFStructure;
class AIFTerritory;

UENUM(BlueprintType)
enum class EIFStructureType : uint8
{
	Keep,
	Barracks,
	Farm,
	GoldMine,
	GuardTower,
	WallSegment
};

UENUM(BlueprintType)
enum class EIFStructureState : uint8
{
	UnderConstruction,
	Active,
	Destroyed
};

UENUM(BlueprintType)
enum class EIFTerritoryCaptureState : uint8
{
	Idle,
	Capturing,
	Contested,
	Decaying
};

UENUM(BlueprintType)
enum class EIFPlacementInvalidReason : uint8
{
	None,
	OutsideOwnedTerritory,
	HeroNotInTerritory,
	KeepAlreadyExists,
	TooCloseToFlag,
	BlockedByObstacle,
	InvalidLocation
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnStructureStateChanged,
	AIFStructure*,
	Structure,
	EIFStructureState,
	PreviousState,
	EIFStructureState,
	NewState);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnStructureDestroyed,
	AIFStructure*,
	Structure,
	AActor*,
	KillerActor);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnTerritoryOwnershipChanged,
	AIFTerritory*,
	Territory,
	uint8,
	PreviousTeamId,
	uint8,
	NewTeamId);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnTerritoryCaptureProgressChanged,
	AIFTerritory*,
	Territory,
	EIFTerritoryCaptureState,
	CaptureState,
	uint8,
	CapturingTeamId,
	float,
	Progress);
