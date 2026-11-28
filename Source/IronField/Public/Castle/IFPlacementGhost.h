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
		UStaticMesh* InFallbackCubeMesh,
		UMaterialInterface* InValidMaterial,
		UMaterialInterface* InInvalidMaterial);

	void EndPreview();

	void SetPreviewMeshVisible(bool bVisible);

	void UpdatePreviewTransformAndValidity(const FVector& WorldLocation, float YawDegrees, bool bIsValidPlacement);

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Build|Ghost")
	TObjectPtr<USceneComponent> GhostRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Build|Ghost")
	TObjectPtr<UStaticMeshComponent> PrimaryPreviewMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Build|Ghost")
	TObjectPtr<UStaticMeshComponent> MirroredPreviewMesh;

private:
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DefaultCubeMesh;

	TWeakObjectPtr<UIFBuildingComponent> OwningBuildingComponent;
	TWeakObjectPtr<UMaterialInterface> ValidMaterial;
	TWeakObjectPtr<UMaterialInterface> InvalidMaterial;

	FVector CurrentFootprintExtent = FVector(150.f, 150.f, 150.f);
	FVector LastAppliedLocation = FVector::ZeroVector;
	float LastAppliedYaw = 0.f;
	bool bPreviewActive = false;
	bool bUseMirroredMesh = false;
	bool bHasAppliedTransform = false;
	bool bLastValidityState = false;
	bool bHasAppliedMaterial = false;
};
