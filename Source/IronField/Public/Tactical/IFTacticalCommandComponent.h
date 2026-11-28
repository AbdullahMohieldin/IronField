#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputActionValue.h"
#include "Tactical/IFTacticalSelectionState.h"
#include "Tactical/IFTacticalTypes.h"
#include "UObject/ObjectKey.h"
#include "UObject/SoftObjectPtr.h"
#include "IFTacticalCommandComponent.generated.h"

class AActor;
class AIFBaseCharacter;
class AIFPlayerController;
class AIFSoldierCharacter;
class AIFStructure;
class AIFTacticalCameraRig;
class AIFTacticalHUD;
class UIFBuildingComponent;
class UIFStructureDataAsset;
class UIFTacticalHUDWidget;
class UInputAction;
class UInputComponent;
class UInputMappingContext;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UParticleSystem;
class UParticleSystemComponent;
class UStaticMesh;
struct FHitResult;
struct FStreamableHandle;

/**
 * Controller-owned Tactical Command coordinator (zero tick).
 * Orchestrates Tab mode swapping, IMC lifecycle, RTS camera rig, formation preview ISM,
 * order confirmation VFX, and delegates cursor queries, selection state, and order dispatch
 * to focused C++ helper modules.
 */
UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFTacticalCommandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFTacticalCommandComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Tactical|Events")
	FOnTacticalModeChanged OnTacticalModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Tactical|Events")
	FOnTacticalSelectionChanged OnSelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Tactical|Events")
	FOnTacticalOrderIssued OnOrderIssued;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Tactical|Events")
	FOnTacticalFormationChanged OnFormationTypeChanged;

	void SetupControllerInput(UInputComponent* PlayerInputComponent);

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|Flow")
	void ToggleTacticalMode();

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|Flow")
	void EnterTacticalMode();

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|Flow")
	void ExitTacticalMode(bool bImmediateSnap = false);

	void HandleMatchPausedChanged(bool bIsPaused);
	void CancelMarqueeSelection();
	void CancelFormationDragPreview();

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|Selection")
	void ClearSelection();

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|Formation")
	void SetActiveFormationType(EIFTacticalFormationType NewFormationType);

	UFUNCTION(BlueprintPure, Category = "IronField|Tactical|Formation")
	EIFTacticalFormationType GetActiveFormationType() const { return ActiveFormationType; }

	UFUNCTION(BlueprintPure, Category = "IronField|Tactical|State")
	bool IsTacticalModeActive() const { return bIsTacticalModeActive; }

	UFUNCTION(BlueprintPure, Category = "IronField|Tactical|Selection")
	int32 GetSelectedSoldierCount() const;

	AActor* GetTacticalCameraRigActor() const;
	UIFTacticalHUDWidget* GetTacticalHUDWidget() const { return TacticalHUDWidget; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Time", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TacticalTimeDilation = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Time", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TimeDilationRampSeconds = 0.20f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera", meta = (ClampMin = "0.0"))
	float CameraBlendInSeconds = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera", meta = (ClampMin = "0.0"))
	float CameraBlendOutSeconds = 0.30f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera")
	TSubclassOf<AIFTacticalCameraRig> TacticalCameraRigClass;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputMappingContext> TacticalMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input", meta = (ClampMin = "0"))
	int32 TacticalMappingPriority = 1;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> ToggleTacticalModeAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> PanAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> ZoomAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> RotateAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> SelectAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> SelectModifierAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> CommandAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> FormationLineAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> FormationSquareAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Input")
	TSoftObjectPtr<UInputAction> FormationCircleAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Selection", meta = (ClampMin = "1.0"))
	float MarqueeDragThresholdPixels = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Selection", meta = (ClampMin = "10.0"))
	float PawnClickToleranceRadius = 65.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Orders", meta = (ClampMin = "1000.0"))
	float CommandTraceDistance = 15000.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Orders", meta = (ClampMin = "10.0"))
	float MoveOrderAcceptanceRadius = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Orders", meta = (ClampMin = "50.0"))
	float FollowOrderRadius = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Orders", meta = (ClampMin = "100.0"))
	float AttackOrderClusterRadius = 450.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Orders", meta = (ClampMin = "50.0"))
	float AttackOrderTargetSpreadPenalty = 180.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation", meta = (ClampMin = "80.0"))
	float FormationLateralSpacing = 105.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation", meta = (ClampMin = "80.0"))
	float FormationRankSpacing = 115.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation", meta = (ClampMin = "1.0"))
	float FormationLineAspectRatio = 2.5f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation", meta = (ClampMin = "1"))
	int32 FormationMinLineColumns = 3;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation", meta = (ClampMin = "1"))
	int32 FormationMaxLineColumns = 10;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation", meta = (ClampMin = "50.0"))
	float FormationMinShapeRadius = 110.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation", meta = (ClampMin = "150.0"))
	float FormationMaxShapeRadius = 1400.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation", meta = (ClampMin = "20.0"))
	float FormationDragThresholdCm = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation")
	TSoftObjectPtr<UStaticMesh> FormationPreviewMesh;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation")
	TSoftObjectPtr<UMaterialInterface> FormationPreviewMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation")
	FVector FormationPreviewScale = FVector(0.58f, 0.58f, 0.04f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Formation")
	float FormationPreviewGroundOffset = 3.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|VFX")
	TSoftObjectPtr<UParticleSystem> MoveOrderCascadeVFX;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|VFX")
	TSoftObjectPtr<UParticleSystem> AttackOrderCascadeVFX;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|VFX")
	TSoftObjectPtr<UParticleSystem> FollowOrderCascadeVFX;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|VFX", meta = (ClampMin = "0.1"))
	float OrderMarkerDurationSeconds = 1.25f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|VFX", meta = (ClampMin = "0.1"))
	float OrderMarkerScale = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|VFX", meta = (ClampMin = "0.0"))
	float OrderMarkerOverheadOffsetCm = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|UI")
	TSoftClassPtr<UIFTacticalHUDWidget> TacticalHUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Build")
	TSoftObjectPtr<UInputAction> RotateBuildAction;

private:
	void RequestTacticalAssets();
	void HandleTacticalAssetsLoaded();
	void BindTacticalInputActions();
	void EnsureTacticalHUDWidgetCreated();
	void ShowTacticalHUDWidget();
	void HideTacticalHUDWidget();
	AIFTacticalCameraRig* EnsureCameraRigSpawned();
	UInstancedStaticMeshComponent* EnsureFormationPreviewComponent();
	void ApplyFormationPreviewAssets();

	void AddTacticalMappingContext();
	void RemoveTacticalMappingContext();

	void HandlePanInput(const FInputActionValue& Value);
	void HandlePanStopped();
	void HandleZoomInput(const FInputActionValue& Value);
	void HandleRotateInput(const FInputActionValue& Value);
	void HandleRotateStopped();
	void HandleSelectStarted();
	void HandleSelectTriggered();
	void HandleSelectCompleted();
	void HandleSelectModifierStarted();
	void HandleSelectModifierCompleted();
	void HandleCommandStarted();
	void HandleCommandTriggered();
	void HandleCommandCompleted();
	void HandleFormationLineInput();
	void HandleFormationSquareInput();
	void HandleFormationCircleInput();
	void HandleRotateBuildInput();
	void HandleEscapeCancelInput();

	void HandleApplicationActivationChanged(bool bIsAppActive);

	UFUNCTION()
	void HandleSelectedSoldierDied(AIFBaseCharacter* DeadCharacter);

	UFUNCTION()
	void HandleHighlightedHostileDied(AIFBaseCharacter* DeadCharacter);

	UFUNCTION()
	void HandleHighlightedStructureDestroyed(AIFStructure* DestroyedStructure, AActor* KillerActor);

	UFUNCTION()
	void HandleBuildPlacementModeChanged(bool bIsPlacementActive, UIFStructureDataAsset* ActiveStructureData);

	void PerformSingleClickSelection();
	void PerformMarqueeSelection(const FVector2D& StartPos, const FVector2D& EndPos);
	void SetHighlightedHostileTarget(AIFSoldierCharacter* HostileSoldier);
	void ClearHighlightedHostileTarget();
	void SetHighlightedHostileStructure(AIFStructure* HostileStructure);
	void ClearHighlightedHostileStructure();
	void BindSoldierDeath(AIFSoldierCharacter* Soldier);
	void UnbindSoldierDeath(AIFSoldierCharacter* Soldier);
	void BindHostileDeath(AIFSoldierCharacter* Hostile);
	void UnbindHostileDeath(AIFSoldierCharacter* Hostile);
	void BindStructureDestroyed(AIFStructure* Structure);
	void UnbindStructureDestroyed(AIFStructure* Structure);
	void PruneInvalidSoldiers();
	void UpdateHUDMarqueeRect(bool bActive);
	void UpdateHUDFormationPreview(bool bActive);
	void BroadcastSelectionCount();

	bool IsSelectModifierDown() const;
	FVector ResolvePreviewCameraForward() const;
	void UpdateFormationDragPreview(const FVector& CurrentGroundPoint);
	void IssueFormationMoveOrder(const FVector& AnchorPoint, const FVector& DragEndPoint, bool bIsDrag);
	void SpawnOrderMarkerVFX(EIFTacticalOrderType OrderType, const FVector& Location, AActor* AttachTarget = nullptr);
	void SyncOrderMarkerTimeDilation(float EffectiveWorldDilation);
	void CleanupOrderMarkerVFX();

	float ComputeDilatedBlendTime(float DesiredRealSeconds, float StartDilation, float EndDilation) const;
	AIFPlayerController* GetOwningPlayerController() const;
	AIFTacticalHUD* GetTacticalHUD() const;
	UIFBuildingComponent* GetBuildingComponent() const;

	UPROPERTY(Transient)
	TObjectPtr<AIFTacticalCameraRig> TacticalCameraRig;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> FormationPreviewISM;

	UPROPERTY(Transient)
	TObjectPtr<UIFTacticalHUDWidget> TacticalHUDWidget;

	TWeakObjectPtr<UParticleSystemComponent> ActiveOrderMarkerPSC;
	FTimerHandle OrderMarkerTimerHandle;
	double OrderMarkerExpireRealTime = 0.0;

	TSharedPtr<FStreamableHandle> TacticalAssetsLoadHandle;
	FDelegateHandle AppActivationHandle;

	TSet<TObjectKey<UInputAction>> BoundInputActions;
	FIFTacticalSelectionState SelectionState;
	mutable TArray<FHitResult> GroundTraceScratchHits;

	EIFTacticalFormationType ActiveFormationType = EIFTacticalFormationType::Line;
	FVector FormationDragStartPoint = FVector::ZeroVector;
	FVector FormationDragCurrentPoint = FVector::ZeroVector;
	FVector LastPreviewAnchorPoint = FVector::ZeroVector;
	FVector LastPreviewGroundPoint = FVector::ZeroVector;
	float LastPreviewFacingYaw = 0.f;
	EIFTacticalFormationType LastPreviewFormationType = EIFTacticalFormationType::Line;
	int32 LastPreviewUnitCount = 0;
	bool bPreviewDirty = true;
	TArray<FIFFormationSlot> PreviewFormationSlots;
	TArray<FTransform> PreviewInstanceTransforms;

	bool bIsTacticalModeActive = false;
	bool bIsSelectModifierHeld = false;
	bool bIsCommandPressed = false;
	bool bCommandHandledOnPress = false;
	bool bIsFormationDragging = false;
};
