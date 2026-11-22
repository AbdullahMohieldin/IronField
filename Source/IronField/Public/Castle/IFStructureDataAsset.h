#pragma once

#include "CoreMinimal.h"
#include "Castle/IFCastleTypes.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "IFStructureDataAsset.generated.h"

class AIFStructure;
class UGeometryCollection;
class UParticleSystem;
class UStaticMesh;

/**
 * Data-driven definition for a buildable or level-placed castle structure.
 * Economy/worker fields are included as zero-overhead hooks for Part 10+.
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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure")
	TSoftClassPtr<AIFStructure> StructureClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Stats", meta = (ClampMin = "1.0"))
	float MaxHealth = 400.f;

	// Half-extents (cm) of the structure's collision, NavMesh carving, placement validation, and melee reach box.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Footprint")
	FVector FootprintExtent = FVector(150.f, 150.f, 150.f);

	// World-space Z height of the overhead vital bar above the actor origin.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|UI", meta = (ClampMin = "50.0"))
	float OverheadBarZOffset = 340.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	bool bChainPlacementByDefault = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	bool bAllowWallSnapping = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction", meta = (ClampMin = "0.0"))
	float DebrisLifeSpan = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction")
	TSoftObjectPtr<UGeometryCollection> DestructionGeometryCollection;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction", meta = (ClampMin = "0.0"))
	float ChaosBreakStrain = 500000.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction", meta = (ClampMin = "0.0"))
	float ChaosRadialImpulse = 450.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction")
	TSoftObjectPtr<UParticleSystem> DestructionCascadeVFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Destruction")
	TSoftObjectPtr<UParticleSystem> BuildCompleteCascadeVFX;

	// --- Future-proof hooks for Part 10 Economy & Civilians -----------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Economy", meta = (ClampMin = "0"))
	int32 FoodCost = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Economy", meta = (ClampMin = "0"))
	int32 GoldCost = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Economy", meta = (ClampMin = "0.0"))
	float BuildTimeSeconds = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Economy")
	bool bInstantBuild = true;
};
