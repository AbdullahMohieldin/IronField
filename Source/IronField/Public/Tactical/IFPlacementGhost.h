#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IFPlacementGhost.generated.h"

class UIFBuildingComponent;
class UIFStructureDataAsset;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Translucent placement preview actor for Tactical Build Mode.
 * Zero idle tick: actor tick is enabled ONLY while placement mode is active.
 */
UCLASS()
class IRONFIELD_API AIFPlacementGhost : public AActor
{
	GENERATED_BODY()

public:
	AIFPlacementGhost();

	void SetOwningBuildingComponent(UIFBuildingComponent* InOwnerComponent);

	void BeginPreview(
		const UIFStructureDataAsset* InStructureData,
		UStaticMesh* InPreviewMesh,
		UMaterialInterface* InValidMaterial,
		UMaterialInterface* InInvalidMaterial);

	void EndPreview();

	void UpdatePreviewTransformAndValidity(const FVector& WorldLocation, float YawDegrees, bool bIsValidPlacement);

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Tactical|Placement")
	TObjectPtr<USceneComponent> GhostRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Tactical|Placement")
	TObjectPtr<UStaticMeshComponent> FootprintPreviewMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Tactical|Placement")
	TObjectPtr<UStaticMeshComponent> SecondaryPreviewMesh;

private:
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DefaultCubeMesh;

	TWeakObjectPtr<UIFBuildingComponent> OwningBuildingComponent;
	TWeakObjectPtr<UMaterialInterface> ValidMaterial;
	TWeakObjectPtr<UMaterialInterface> InvalidMaterial;

	FVector CurrentFootprintExtent = FVector(150.f, 150.f, 150.f);
	bool bPreviewActive = false;
	bool bLastValidityState = false;
	bool bHasAppliedMaterial = false;
};
