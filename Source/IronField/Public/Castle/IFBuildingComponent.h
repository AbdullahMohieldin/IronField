#pragma once

#include "CoreMinimal.h"
#include "Castle/IFBuildingSnapSolver.h"
#include "Castle/IFBuildingTypes.h"
#include "Castle/IFCastleTypes.h"
#include "Components/ActorComponent.h"
#include "Engine/HitResult.h"
#include "GenericTeamAgentInterface.h"
#include "Tactical/IFTacticalTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "IFBuildingComponent.generated.h"

class AActor;
class AIFPlacementGhost;
class AIFPlayerController;
class AIFStructure;
class AIFTerritory;
class UIFStructureDataAsset;
class UMaterialInterface;
class UStaticMesh;
struct FStreamableHandle;

/**
 * Controller-owned castle building, placement, snapping, rotation, and structure management component.
 * Zero idle tick: ghost actor ticks only while placement mode is active.
 */
UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFBuildingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFBuildingComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Build|Events")
	FOnBuildPlacementModeChanged OnBuildPlacementModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Build|Events")
	FOnBuildPlacementValidityChanged OnBuildPlacementValidityChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Build|Events")
	FOnBuildPlacementRejected OnBuildPlacementRejected;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Build|Events")
	FOnSelectedStructureChanged OnSelectedStructureChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Build|Events")
	FOnTacticalOrderIssued OnStructureActionCompleted;

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	bool BeginBuildPlacement(UIFStructureDataAsset* StructureData);

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	bool BeginBuildPlacementByType(EIFStructureType StructureType);

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	void CancelBuildPlacement();

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	void RotateBuildPreview(bool bReverse = false);

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	bool TryConfirmPlacement();

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	bool IsInBuildPlacementMode() const { return bIsInBuildPlacementMode && ActiveBuildData.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	bool IsRelocatingStructure() const { return bIsInBuildPlacementMode && bIsRelocatingMode; }

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	bool IsCurrentPlacementValid() const { return bIsCurrentPlacementValid; }

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	FText GetPlacementStatusText() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	UIFStructureDataAsset* GetActiveBuildData() const { return ActiveBuildData.Get(); }

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	UIFStructureDataAsset* GetBuildableStructureByType(EIFStructureType StructureType) const;

	const TArray<TObjectPtr<UIFStructureDataAsset>>& GetLoadedBuildableStructures() const { return LoadedBuildableStructures; }
	void EnsureBuildableStructuresLoaded();

	UFUNCTION(BlueprintPure, Category = "IronField|Build|Selection")
	AIFStructure* GetSelectedStructure() const { return SelectedStructure.Get(); }

	UFUNCTION(BlueprintCallable, Category = "IronField|Build|Selection")
	void SelectFriendlyStructure(AIFStructure* Structure);

	UFUNCTION(BlueprintCallable, Category = "IronField|Build|Selection")
	void ClearSelectedStructure();

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	bool BeginRelocateSelectedStructure();

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	bool DemolishSelectedStructure();

	// Invoked by AIFPlacementGhost::Tick ONLY while placement mode is active.
	void UpdatePlacementPreview(bool bForceFullRefresh = false);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build")
	TArray<TSoftObjectPtr<UIFStructureDataAsset>> BuildableStructures;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build")
	TSoftObjectPtr<UStaticMesh> FallbackGhostCubeMesh;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build")
	TSoftObjectPtr<UMaterialInterface> PlacementGhostValidMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build")
	TSoftObjectPtr<UMaterialInterface> PlacementGhostInvalidMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build", meta = (ClampMin = "50.0"))
	float SnapRadiusCm = 200.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build", meta = (ClampMin = "50.0"))
	float MountSnapRadiusCm = 320.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build", meta = (ClampMin = "1000.0"))
	float GroundTraceDistance = 15000.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxPlacementSlopeDegrees = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build")
	bool bRequireHeroInTerritory = false;

private:
	UFUNCTION()
	void HandleSelectedStructureDestroyed(AIFStructure* DestroyedStructure, AActor* KillerActor);

	UFUNCTION()
	void HandleRelocatingStructureDestroyed(AIFStructure* DestroyedStructure, AActor* KillerActor);

	void RequestBuildAssets();
	void HandleBuildAssetsLoaded();
	void ResolveGhostMaterials();
	void UnbindRelocatingStructureDelegate();
	bool HasLivingMountedDependents(const AIFStructure* HostStructure) const;
	bool TraceFriendlyStructureUnderCursor(
		const FVector& RayOrigin,
		const FVector& RayDir,
		FGenericTeamId BuilderTeamId,
		const AActor* CameraRig,
		float MaxDistance,
		const AIFStructure*& OutStructure,
		FVector& OutImpactPoint,
		FVector& OutImpactNormal);
	bool BeginBuildPlacementInternal(UIFStructureDataAsset* StructureData, AIFStructure* InRelocatingStructure);
	AIFPlacementGhost* EnsurePlacementGhostSpawned();
	FGenericTeamId ResolveBuilderTeamId() const;
	AIFPlayerController* GetOwningPlayerController() const;

	UPROPERTY(Transient)
	TObjectPtr<AIFPlacementGhost> PlacementGhost;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UIFStructureDataAsset>> LoadedBuildableStructures;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CachedValidGhostMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CachedInvalidGhostMaterial;

	TSharedPtr<FStreamableHandle> BuildAssetsLoadHandle;
	TSharedPtr<FStreamableHandle> BuildClassLoadHandle;

	TWeakObjectPtr<UIFStructureDataAsset> ActiveBuildData;
	TWeakObjectPtr<AIFStructure> SelectedStructure;
	TWeakObjectPtr<AIFStructure> RelocatingStructure;
	TWeakObjectPtr<AIFStructure> LastSnapHostStructure;
	TWeakObjectPtr<AIFTerritory> LastValidBuildTerritory;
	TWeakObjectPtr<AActor> LastGroundActor;

	TArray<FHitResult> GroundTraceScratchHits;
	TArray<FHitResult> StructureTraceScratchHits;
	FIFSnapSolverScratch SolverScratch;

	FIFSnapResult CachedSnapResult;
	FVector LastGroundNormal = FVector::UpVector;
	FVector CurrentPreviewLocation = FVector::ZeroVector;
	float CurrentPreviewYaw = 0.f;

	FVector2D LastPreviewMousePos = FVector2D::ZeroVector;
	FVector LastPreviewRayOrigin = FVector::ZeroVector;
	FVector LastPreviewRayDir = FVector::ForwardVector;
	int32 LastPreviewFreeYawIndex = INDEX_NONE;
	int32 LastPreviewConnectorFlipIndex = INDEX_NONE;
	int32 LastPreviewStairMountMode = INDEX_NONE;
	int32 LastKnownStructureCount = INDEX_NONE;
	double LastValidationRealTime = 0.0;
	TWeakObjectPtr<UIFStructureDataAsset> LastPreviewBuildData;
	TWeakObjectPtr<AIFStructure> LastPreviewRelocatingStructure;

	int32 FreeYawIndex = 0;
	int32 ConnectorFlipIndex = 0;
	int32 StairMountMode = 0;
	int8 LastSnapSideSign = 0;

	bool bBuildableStructuresInitialized = false;
	bool bIsInBuildPlacementMode = false;
	bool bIsRelocatingMode = false;
	bool bIsCurrentlySnapped = false;
	bool bIsCurrentPlacementValid = false;
	bool bHasBroadcastValidity = false;
	bool bHasPreviewCache = false;
	bool bLastCursorTraceHit = false;
	EIFPlacementInvalidReason LastInvalidReason = EIFPlacementInvalidReason::None;
	EIFPlacementSnapState CurrentSnapState = EIFPlacementSnapState::Free;
};
