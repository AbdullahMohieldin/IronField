#pragma once

#include "CoreMinimal.h"
#include "Castle/IFBuildingTypes.h"
#include "Castle/IFCastleTypes.h"
#include "Components/ActorComponent.h"
#include "Tactical/IFTacticalTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "IFBuildingComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBuildPlacementRejected, EIFPlacementInvalidReason, InvalidReason);

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
	FOnTacticalStructureSelectionChanged OnSelectedStructureChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Build|Events")
	FOnTacticalOrderIssued OnStructureActionCompleted;

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	bool BeginBuildPlacement(UIFStructureDataAsset* StructureData);

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	bool BeginBuildPlacementByType(EIFStructureType StructureType);

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	bool BeginBuildPlacementByIndex(int32 BuildIndex);

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	void CancelBuildPlacement();

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	void RotateBuildPreview(bool bReverse = false);

	UFUNCTION(BlueprintCallable, Category = "IronField|Build")
	bool TryConfirmPlacement();

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	bool IsInBuildPlacementMode() const { return bIsInBuildPlacementMode && ActiveBuildData.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	bool IsRelocatingStructure() const { return bIsInBuildPlacementMode && RelocatingStructure.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	bool IsCurrentPlacementValid() const { return bIsCurrentPlacementValid; }

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	EIFPlacementInvalidReason GetLastInvalidReason() const { return LastInvalidReason; }

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	EIFPlacementSnapState GetCurrentSnapState() const { return CurrentSnapState; }

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
	void UpdatePlacementPreview();

	void RequestBuildAssets();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build")
	TArray<TSoftObjectPtr<UIFStructureDataAsset>> BuildableStructures;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build")
	TSoftObjectPtr<UStaticMesh> PlacementGhostMesh;

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

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Build")
	bool bRequireHeroInTerritory = false;

private:
	UFUNCTION()
	void HandleSelectedStructureDestroyed(AIFStructure* DestroyedStructure, AActor* KillerActor);

	void HandleBuildAssetsLoaded();
	bool BeginBuildPlacementInternal(UIFStructureDataAsset* StructureData, AIFStructure* InRelocatingStructure);
	AIFPlacementGhost* EnsurePlacementGhostSpawned();
	bool TraceGroundUnderCursor(const FVector& RayOrigin, const FVector& RayDir, FHitResult& OutHit) const;
	AIFPlayerController* GetOwningPlayerController() const;

	UPROPERTY(Transient)
	TObjectPtr<AIFPlacementGhost> PlacementGhost;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UIFStructureDataAsset>> LoadedBuildableStructures;

	TSharedPtr<FStreamableHandle> BuildAssetsLoadHandle;
	TSharedPtr<FStreamableHandle> BuildClassLoadHandle;

	TWeakObjectPtr<UIFStructureDataAsset> ActiveBuildData;
	TWeakObjectPtr<AIFStructure> SelectedStructure;
	TWeakObjectPtr<AIFStructure> RelocatingStructure;
	TWeakObjectPtr<AIFStructure> LastSnapHostStructure;
	TWeakObjectPtr<AIFTerritory> LastValidBuildTerritory;

	FVector CurrentPreviewLocation = FVector::ZeroVector;
	float CurrentPreviewYaw = 0.f;

	int32 FreeYawIndex = 0;
	int32 ConnectorFlipIndex = 0;
	int32 StairMountMode = 0;

	bool bIsInBuildPlacementMode = false;
	bool bIsCurrentlySnapped = false;
	bool bIsCurrentPlacementValid = false;
	bool bHasBroadcastValidity = false;
	EIFPlacementInvalidReason LastInvalidReason = EIFPlacementInvalidReason::None;
	EIFPlacementSnapState CurrentSnapState = EIFPlacementSnapState::Free;
};
