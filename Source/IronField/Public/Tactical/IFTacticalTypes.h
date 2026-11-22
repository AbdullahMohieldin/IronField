#pragma once

#include "CoreMinimal.h"
#include "Castle/IFCastleTypes.h"
#include "IFTacticalTypes.generated.h"

class AIFStructure;
class UIFStructureDataAsset;

UENUM(BlueprintType)
enum class EIFTacticalOrderType : uint8
{
	None,
	MoveTo,
	AttackTarget,
	FollowCommander,
	BuildStructure,
	RelocateStructure,
	DemolishStructure
};

UENUM(BlueprintType)
enum class EIFTacticalFormationType : uint8
{
	Line,
	Square,
	Circle
};

UENUM(BlueprintType)
enum class EIFTacticalRingMode : uint8
{
	None,
	SelectedAlly,
	HostileTarget
};

USTRUCT(BlueprintType)
struct IRONFIELD_API FIFFormationSlot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IronField|Tactical|Formation")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "IronField|Tactical|Formation")
	float FacingYaw = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTacticalModeChanged, bool, bIsTacticalModeActive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTacticalSelectionChanged, int32, SelectedUnitCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTacticalOrderIssued, EIFTacticalOrderType, OrderType, FVector, TargetLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTacticalFormationChanged, EIFTacticalFormationType, NewFormationType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBuildPlacementModeChanged, bool, bIsPlacementActive, UIFStructureDataAsset*, ActiveStructureData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBuildPlacementValidityChanged, bool, bIsValid, EIFPlacementInvalidReason, InvalidReason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTacticalStructureSelectionChanged, AIFStructure*, SelectedStructure);
