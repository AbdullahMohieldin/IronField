#pragma once

#include "CoreMinimal.h"
#include "Castle/IFBuildingTypes.h"
#include "Castle/IFCastleTypes.h"
#include "Combat/IFTeamTypes.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPtr.h"
#include "IFStructure.generated.h"

class AIFTerritory;
class UAIPerceptionStimuliSourceComponent;
class UBoxComponent;
class UGeometryCollection;
class UGeometryCollectionComponent;
class UIFHealthComponent;
class UIFStructureDataAsset;
class UIFVitalBarsWidget;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UParticleSystem;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UWidgetComponent;
struct FStreamableHandle;

/**
 * Base class for all territory structures (Keep, Barracks, Farm, GoldMine, GuardTower, WallSegment).
 * Zero idle tick, event-driven health/destruction, dynamic NavMesh carving, exact box-surface reach,
 * team-colored component-tag visuals, and Civilian construction/repair hooks for Part 10+.
 */
UCLASS()
class IRONFIELD_API AIFStructure : public AActor, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AIFStructure();

	static const FName TeamColoredComponentTag;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Structure|Events")
	FOnStructureStateChanged OnStructureStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Structure|Events")
	FOnStructureDestroyed OnStructureDestroyed;

	// Initializes spawned structure from data, team, and owning territory (safe before BeginPlay).
	void InitializeStructure(
		UIFStructureDataAsset* InStructureData,
		FGenericTeamId InTeamID,
		AIFTerritory* InOwningTerritory,
		bool bStartActive = true);

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	UIFStructureDataAsset* GetStructureData() const { return StructureData; }

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	EIFStructureType GetStructureType() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	EIFStructureState GetStructureState() const { return StructureState; }

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	bool IsStructureAlive() const { return StructureState != EIFStructureState::Destroyed; }

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	bool IsStructureActive() const { return StructureState == EIFStructureState::Active; }

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	bool IsKeep() const { return GetStructureType() == EIFStructureType::Keep; }

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	UIFHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	AIFTerritory* GetOwningTerritory() const { return OwningTerritory.Get(); }

	void SetOwningTerritory(AIFTerritory* InTerritory);

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	FVector GetFootprintExtent() const;

	// Exact horizontal box-surface point closest to FromLocation (for rectangular walls & large keeps).
	UFUNCTION(BlueprintPure, Category = "IronField|Structure|Combat")
	FVector GetClosestSurfacePoint2D(const FVector& FromLocation) const;

	// Exact 3D box-surface point closest to FromLocation, preserving surface Z for vertical melee reach checks.
	UFUNCTION(BlueprintPure, Category = "IronField|Structure|Combat")
	FVector GetClosestSurfacePoint(const FVector& FromLocation) const;

	// Approach point shifted laterally along the nearest box face so multiple attackers spread along walls/gates.
	FVector GetSpreadCombatApproachPoint(const FVector& FromLocation, float CapsuleRadius, float LateralOffset) const;

	// Horizontal distance from FromLocation to the nearest point on this structure's collision footprint.
	UFUNCTION(BlueprintPure, Category = "IronField|Structure|Combat")
	float GetHorizontalSurfaceDistance(const FVector& FromLocation) const;

	void GetWorldCollisionBoxes(TArray<FIFWorldCollisionBox>& OutBoxes) const;
	void GetWorldConnectors(TArray<FIFModularConnector>& OutWorldConnectors) const;

	static void ComputeWorldCollisionBoxes(
		const UIFStructureDataAsset* Data,
		const FVector& Location,
		float YawDeg,
		TArray<FIFWorldCollisionBox>& OutBoxes);

	static void ComputeWorldConnectors(
		const UIFStructureDataAsset* Data,
		const FVector& Location,
		float YawDeg,
		TArray<FIFModularConnector>& OutWorldConnectors);

	// --- Future Civilian / Worker Hooks (Part 10+) --------------------------
	UFUNCTION(BlueprintCallable, Category = "IronField|Structure|Construction")
	void AddConstructionProgress(float DeltaProgress);

	UFUNCTION(BlueprintCallable, Category = "IronField|Structure|Construction")
	void RepairStructure(float HealAmount);

	UFUNCTION(BlueprintPure, Category = "IronField|Structure|Construction")
	float GetConstructionProgress() const { return ConstructionProgress; }

	// --- Scorched-Earth Collapse (Method B when Territory Keep falls) -------
	void CollapseStructure(AActor* KillerActor = nullptr);

	// --- Host Support Tracking (prevents floating walkways/stairs/doors) ----
	void SetHostStructure(AIFStructure* InHost);

	UFUNCTION(BlueprintPure, Category = "IronField|Structure")
	AIFStructure* GetHostStructure() const { return HostStructure.Get(); }

	// --- Tactical Selection, Relocate & Demolish ----------------------------
	UFUNCTION(BlueprintCallable, Category = "IronField|Structure|Tactical")
	void SetHostileTargetHighlighted(bool bHighlighted);

	UFUNCTION(BlueprintCallable, Category = "IronField|Structure|Tactical")
	void SetFriendlySelected(bool bSelected);

	UFUNCTION(BlueprintPure, Category = "IronField|Structure|Tactical")
	bool IsFriendlySelected() const { return bFriendlySelected; }

	UFUNCTION(BlueprintCallable, Category = "IronField|Structure|Tactical")
	void RelocateStructure(const FVector& NewLocation, float NewYaw, AIFTerritory* NewTerritory = nullptr);

	UFUNCTION(BlueprintCallable, Category = "IronField|Structure|Tactical")
	void DemolishStructure();

	// --- Damage & Team Interface --------------------------------------------
	virtual float TakeDamage(
		float DamageAmount,
		struct FDamageEvent const& DamageEvent,
		class AController* EventInstigator,
		AActor* DamageCauser) override;

	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamID; }
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Hook for subclasses (e.g., AIFGuardTower) to react to ownership flips or state changes.
	virtual void OnTeamChanged(FGenericTeamId PreviousTeam, FGenericTeamId NewTeam) {}
	virtual void OnStructureActivated() {}
	virtual void OnStructureDeactivated() {}

	// Clean hook for Chaos GeometryCollection swap without refactoring core destruction flow.
	virtual void TriggerDestructionFeedback();
	virtual bool ShouldStripPrimitiveCollision(const UPrimitiveComponent* Prim) const;
	virtual void GatherStructureAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const;

	UGeometryCollectionComponent* SpawnFracturedGC(
		UGeometryCollection* GCAsset,
		const FTransform& WorldOrRelTransform,
		bool bIsWorldTransform,
		const FVector& ImpulseOrigin,
		float ImpulseRadiusOverride = -1.f);
	void SpawnDestructionVFX() const;

	void SetStructureState(EIFStructureState NewState);
	void SetStructureCollisionBlocking(bool bBlockPawnsAndNav);
	void ApplyFootprintConfiguration();
	void ApplyTeamVisuals();
	void RefreshOverheadWidget();
	void UpdateOverheadWidgetVisibility();
	void RefreshSelectionRingVisual();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Structure|Components")
	TObjectPtr<UBoxComponent> BoundsCollision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Structure|Components")
	TObjectPtr<USceneComponent> VisualRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Structure|Components")
	TObjectPtr<UStaticMeshComponent> RubbleVisualComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Structure|Components")
	TObjectPtr<UIFHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Structure|Components")
	TObjectPtr<UWidgetComponent> OverheadWidgetComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Structure|Components")
	TObjectPtr<UAIPerceptionStimuliSourceComponent> PerceptionStimuliSource;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Structure|Components")
	TObjectPtr<UStaticMeshComponent> SelectionRingComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Structure|Config")
	TObjectPtr<UIFStructureDataAsset> StructureData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Team")
	FGenericTeamId TeamID = FGenericTeamId(IFTeamIds::Dawn);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|Visuals")
	TSoftObjectPtr<UMaterialInterface> DawnTeamMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|Visuals")
	TSoftObjectPtr<UMaterialInterface> DuskTeamMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|Visuals")
	TSoftObjectPtr<UMaterialInterface> NeutralTeamMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|UI")
	FLinearColor AllyHealthBarColor = FLinearColor(0.18f, 0.56f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|UI")
	FLinearColor EnemyHealthBarColor = FLinearColor(0.85f, 0.16f, 0.16f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|UI")
	FLinearColor NeutralHealthBarColor = FLinearColor(0.75f, 0.75f, 0.75f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|Tactical")
	TSoftObjectPtr<UStaticMesh> SelectionRingMesh;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|Tactical")
	TSoftObjectPtr<UMaterialInterface> FriendlyRingMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|Tactical")
	TSoftObjectPtr<UMaterialInterface> HostileRingMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Structure|Tactical", meta = (ClampMin = "0.0"))
	float SelectionOutlineMarginCm = 18.f;

private:
	UFUNCTION()
	void HandleHealthChanged(float HealthPercent);

	UFUNCTION()
	void HandleHealthDepleted(AActor* DeadActor);

	UFUNCTION()
	void HandleHostStructureDestroyed(AIFStructure* DestroyedHost, AActor* KillerActor);

	void HandleStructureDestroyedInternal(AActor* KillerActor);
	void RequestStructureAssets();
	void HandleStructureAssetsLoaded();
	void EnforceCameraIgnoreOnVisuals();
	UStaticMeshComponent* FindPrimaryVisualMeshComponent() const;
	void CleanupActiveDestructionGCs();
	static void RegisterDestroyedStructureAndEnforceCap(AIFStructure* NewlyDestroyed);
	static void UnregisterDestroyedStructure(const AIFStructure* Structure);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UGeometryCollectionComponent>> ActiveDestructionGCs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> ExtraCollisionBoxes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SelectionOutlineMID;

	TWeakObjectPtr<UMaterialInterface> LastSelectionOutlineSourceMat;

	TWeakObjectPtr<AIFTerritory> OwningTerritory;
	TWeakObjectPtr<AIFStructure> HostStructure;
	TWeakObjectPtr<AActor> LastDamageInstigator;
	TSharedPtr<FStreamableHandle> StructureAssetsLoadHandle;
	FTimerHandle HostCollapseTimerHandle;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Structure|State", meta = (AllowPrivateAccess = "true"))
	EIFStructureState StructureState = EIFStructureState::Active;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Structure|Construction", meta = (AllowPrivateAccess = "true"))
	float ConstructionProgress = 1.f;

	bool bInitializedFromSpawn = false;
	bool bFriendlySelected = false;
	bool bHostileRingHighlighted = false;
};
