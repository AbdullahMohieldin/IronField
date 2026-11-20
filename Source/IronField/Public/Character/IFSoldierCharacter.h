#pragma once

#include "CoreMinimal.h"
#include "Character/IFBaseCharacter.h"
#include "IFSoldierCharacter.generated.h"

class AActor;
class UIFMeleeCombatComponent;
class UIFStaminaComponent;
class AIFSoldierAIController;
class UWidgetComponent;

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

	// Base reach in centimetres between capsule surfaces. Prefer GetEffectiveAttackRange
	// for any distance test; this value alone ignores both actors' collision radii.
	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Combat")
	float GetAttackRange() const { return AttackRange; }

	// Center-to-center reach: AttackRange plus both simple collision radii. Used by the
	// controller's move goal and the melee task so small minions and large buildings
	// share one distance space.
	float GetEffectiveAttackRange(const AActor* Target) const;

	// --- Decision tuning read by AIFSoldierAIController --------------------
	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	FVector GetAnchorLocation() const { return bHasAnchorLocation ? AnchorLocation : GetActorLocation(); }

	// Moves the post. Pass false from inside controller decisions to avoid recursion.
	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|AI")
	void SetAnchorLocation(const FVector& NewAnchorLocation, bool bRefreshControllerDecision = true);

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

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath(AActor* DeadActor) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Combat", meta = (ClampMin = "0.0"))
	float AttackRange = 60.f;

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
	float AutonomousEngageRadius = 900.f;

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

private:
	UPROPERTY(Transient)
	TObjectPtr<UIFMeleeCombatComponent> MeleeCombat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFStaminaComponent> StaminaComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> OverheadWidgetComponent;

	FVector AnchorLocation = FVector::ZeroVector;

	// OnPossess (and therefore the first RefreshDecision) can run before BeginPlay,
	// so the anchor is only authoritative once this is true.
	bool bHasAnchorLocation = false;

	void ActivateRagdollDeath();
	FTimerHandle RagdollTransitionTimerHandle;
};
