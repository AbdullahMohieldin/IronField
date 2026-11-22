#pragma once

#include "CoreMinimal.h"
#include "Character/IFBaseCharacter.h"
#include "Tactical/IFTacticalTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "IFSoldierCharacter.generated.h"

class AActor;
class AIFSoldierAIController;
class UIFMeleeCombatComponent;
class UIFStaminaComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UWidgetComponent;
struct FStreamableHandle;

/**
 * Base pawn for Layer 3 individual AI. Adds the melee combat slot, an AI controller,
 * camera-safe collision, RVO and the tuning the controller reads on every decision.
 */
UCLASS()
class IRONFIELD_API AIFSoldierCharacter : public AIFBaseCharacter
{
	GENERATED_BODY()

public:
	AIFSoldierCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Components")
	UIFMeleeCombatComponent* GetMeleeCombatComponent() const { return MeleeCombat; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Components")
	UIFStaminaComponent* GetStaminaComponent() const { return StaminaComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	AIFSoldierAIController* GetSoldierController() const;

	// --- Combat surface consumed by the BT tasks ---------------------------
	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Combat")
	bool TryPerformMeleeAttack();

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Combat")
	bool TryContinueMeleeCombo(int32 CompletedChainIndex);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Combat")
	void EnsureMeleeComboPlan();

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Combat")
	bool HasEnoughStaminaForPlannedCombo() const;

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Combat")
	bool TryExecuteNextPlannedMeleeStep();

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Combat")
	bool HasRemainingPlannedMeleeSteps() const;

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Combat")
	void ClearMeleeComboPlan();

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Combat")
	bool IsAttacking() const;

	// Base standoff distance in centimetres between capsule surfaces where the soldier stops and swings.
	// Prefer GetEffectiveAttackRange / GetMaxMeleeEngagementRange for any center-to-center distance test.
	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Combat")
	float GetAttackRange() const { return AttackRange; }

	// Step-back deadzone (cm) beyond AttackRange before re-chasing or aborting an active melee attack.
	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Combat")
	float GetAttackRangeBuffer() const { return AttackRangeBuffer; }

	// Stop/swing distance (surface-relative for AIFStructure, center-to-center for characters).
	float GetEffectiveAttackRange(const AActor* Target) const;

	// Hold/rechase threshold: GetEffectiveAttackRange(Target) + AttackRangeBuffer.
	float GetMaxMeleeEngagementRange(const AActor* Target) const;

	// Exact box-surface horizontal distance for AIFStructure targets; center-to-center 2D distance for characters.
	float GetHorizontalDistanceToTarget(const AActor* Target) const;

	// Nearest box-surface point for AIFStructure targets; actor location for characters.
	FVector GetCombatApproachPoint(const AActor* Target) const;

	// --- Decision tuning read by AIFSoldierAIController --------------------
	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	FVector GetAnchorLocation() const { return bHasAnchorLocation ? AnchorLocation : GetActorLocation(); }

	// Moves the post. Pass false from inside controller decisions to avoid recursion.
	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|AI")
	void SetAnchorLocation(const FVector& NewAnchorLocation, bool bRefreshControllerDecision = true);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Formation")
	void SetAnchorFacingYaw(float NewFacingYaw);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Formation")
	void ClearAnchorFacing();

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Formation")
	bool HasAnchorFacing() const { return bHasAnchorFacing; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Formation")
	float GetAnchorFacingYaw() const { return AnchorFacingYaw; }

	void BeginAnchorFacingAlignment();
	void CancelAnchorFacingAlignment();

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	bool IsLeashedToAnchor() const { return bLeashToAnchor; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	float GetAutonomousEngageRadius() const { return AutonomousEngageRadius; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	bool ShouldAdvanceWhenIdle() const { return bAdvanceWhenIdle; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	bool ShouldAdvanceTowardsPlayer() const { return bAdvanceTowardsPlayer; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	AActor* GetAdvanceGoalActor() const { return AdvanceGoalActor; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	FVector GetAdvanceGoalLocation() const { return AdvanceGoalLocation; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	float GetAdvanceStopDistance() const { return AdvanceStopDistance; }

	// Keeps perception sight aligned with actual pawn orientation without ticking the AI controller.
	virtual void GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Tactical")
	void SetTacticalSelected(bool bSelected, bool bShowRingNow = true);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Tactical")
	void SetSelectionRingDisplayVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Tactical")
	void SetHostileTargetHighlighted(bool bHighlighted);

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Tactical")
	bool IsTacticalSelected() const { return bIsTacticalSelected; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Tactical")
	EIFTacticalRingMode GetTacticalRingMode() const { return TacticalRingMode; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath(AActor* DeadActor) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Combat", meta = (ClampMin = "0.0"))
	float AttackRange = 60.f;

	// Step-back deadzone (cm) beyond AttackRange before re-chasing or aborting an active melee attack.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Combat", meta = (ClampMin = "0.0"))
	float AttackRangeBuffer = 25.f;

	// Corpses are removed after this long; 0 keeps them for the level lifetime.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Death", meta = (ClampMin = "0.0"))
	float CorpseLifeSpan = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Death")
	bool bEnableRagdollDeath = true;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Death", meta = (ClampMin = "0.0"))
	float RagdollTransitionDelaySeconds = 0.28f;

	// Mass-independent velocity-change impulse (cm/s) applied via bVelChange = true.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Death", meta = (ClampMin = "0.0"))
	float RagdollDeathImpulse = 220.f;

	// Dawn: true (defends a post). Dusk: false (advances on the player).
	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI")
	bool bLeashToAnchor = true;

	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float AutonomousEngageRadius = 400.f;

	// Dusk: true. The controller resolves the goal below and walks to it while idle.
	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI")
	bool bAdvanceWhenIdle = false;

	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI")
	bool bAdvanceTowardsPlayer = true;

	// Optional explicit goal (level actor). Ignored when bAdvanceTowardsPlayer is true.
	UPROPERTY(EditInstanceOnly, Category = "IronField|Soldier|AI")
	TObjectPtr<AActor> AdvanceGoalActor;

	// Fallback static goal when neither the player nor an actor is configured.
	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI")
	FVector AdvanceGoalLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float AdvanceStopDistance = 350.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Soldier|UI")
	FLinearColor AllyHealthBarColor = FLinearColor(0.15f, 0.55f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Soldier|UI")
	FLinearColor EnemyHealthBarColor = FLinearColor(0.90f, 0.18f, 0.18f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Tactical")
	TSoftObjectPtr<UStaticMesh> SelectionRingMesh;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Tactical")
	TSoftObjectPtr<UMaterialInterface> SelectionRingMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Tactical")
	TSoftObjectPtr<UMaterialInterface> HostileTargetRingMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Tactical", meta = (ClampMin = "0.0"))
	float SelectionRingGroundClearance = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Formation", meta = (ClampMin = "1.0"))
	float AnchorFacingTurnSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Formation", meta = (ClampMin = "0.2", ClampMax = "15.0"))
	float AnchorFacingToleranceDegrees = 1.5f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UIFMeleeCombatComponent> MeleeCombat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFStaminaComponent> StaminaComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> OverheadWidgetComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Tactical", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> SelectionRingComponent;

	void RequestSelectionRingAssets();
	void HandleSelectionRingAssetsLoaded();
	void UpdateSelectionRingTransform();
	void RefreshTacticalRingVisual();
	void UpdateOverheadWidgetVisibility();

	UFUNCTION()
	void HandleHealthChanged(float HealthPercent);

	TSharedPtr<FStreamableHandle> SelectionRingLoadHandle;
	EIFTacticalRingMode TacticalRingMode = EIFTacticalRingMode::None;
	bool bIsTacticalSelected = false;
	bool bSelectionRingDisplayEnabled = false;

	FVector AnchorLocation = FVector::ZeroVector;
	float AnchorFacingYaw = 0.f;
	bool bHasAnchorFacing = false;
	bool bIsAligningToAnchorFacing = false;

	// OnPossess (and therefore the first RefreshDecision) can run before BeginPlay,
	// so the anchor is only authoritative once this is true.
	bool bHasAnchorLocation = false;

	void ActivateRagdollDeath();
	FTimerHandle RagdollTransitionTimerHandle;
};
