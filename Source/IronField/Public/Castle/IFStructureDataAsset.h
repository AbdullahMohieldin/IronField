#pragma once

#include "CoreMinimal.h"
#include "Castle/IFBuildingTypes.h"
#include "Castle/IFCastleTypes.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "IFStructureDataAsset.generated.h"

class AIFStructure;
class UGeometryCollection;
class UParticleSystem;
class UPhysicalMaterial;
class UStaticMesh;

/**
 * Data-driven definition for a buildable or level-placed castle structure.
 */
UCLASS(BlueprintType)
class IRONFIELD_API UIFStructureDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure")
	EIFStructureType StructureType = EIFStructureType::Farm;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|UI")
	FText RoleTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure")
	TSoftClassPtr<AIFStructure> StructureClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Stats", meta = (ClampMin = "1.0"))
	float MaxHealth = 400.f;

	// Fallback half-extents (cm) for structures without explicit CollisionBoxes and selection ring sizing.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Footprint")
	FVector FootprintExtent = FVector(150.f, 150.f, 150.f);

	// World-space Z height of the overhead vital bar above the actor origin.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|UI", meta = (ClampMin = "50.0"))
	float OverheadBarZOffset = 340.f;

	// --- Placement & Modular Snapping ---------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	bool bContinuousPlacement = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	EIFPlacementRule PlacementRule = EIFPlacementRule::FreeOrEndSnap;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	bool bSupportsGateDoor = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement", meta = (ClampMin = "1.0"))
	float AttachmentDepthHalfExtent = 62.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement", meta = (ClampMin = "10.0"))
	float StairHalfLength = 172.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement", meta = (ClampMin = "0.0"))
	float StairParallelLandingShift = 52.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Footprint")
	TArray<FIFCollisionBox> CollisionBoxes;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	TArray<FIFModularConnector> Connectors;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	TArray<FIFLateralMountSurface> MountSurfaces;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	TSoftObjectPtr<UStaticMesh> PreviewMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	FTransform PreviewMeshRelativeTransform = FTransform::Identity;

	// --- Destruction & VFX --------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction", meta = (ClampMin = "0.0"))
	float DebrisLifeSpan = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction")
	TSoftObjectPtr<UGeometryCollection> DestructionGeometryCollection;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction")
	TSoftObjectPtr<UPhysicalMaterial> DestructionPhysicalMaterial = TSoftObjectPtr<UPhysicalMaterial>(FSoftObjectPath(TEXT("/Game/IronField/Structures/Destruction/PM_IF_StoneRubble.PM_IF_StoneRubble")));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction")
	FTransform DestructionGCRelativeTransform = FTransform::Identity;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction", meta = (ClampMin = "0.0"))
	float ChaosBreakStrain = 500000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction", meta = (ClampMin = "0.0"))
	float ChaosRadialImpulse = 60.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction", meta = (ClampMin = "0.0"))
	float ChaosRadialFalloffRadius = 350.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction")
	TSoftObjectPtr<UParticleSystem> DestructionCascadeVFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction")
	TSoftObjectPtr<UParticleSystem> BuildCompleteCascadeVFX;

	// --- Economy & Civilians Hooks ------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Economy", meta = (ClampMin = "0"))
	int32 FoodCost = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Economy", meta = (ClampMin = "0"))
	int32 GoldCost = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Economy", meta = (ClampMin = "0.0"))
	float BuildTimeSeconds = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Economy")
	bool bInstantBuild = true;
};
