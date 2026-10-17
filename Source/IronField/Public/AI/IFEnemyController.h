#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/IFBTUtils.h"
#include "Combat/IFCombatTypes.h"
#include "IFEnemyController.generated.h"

class UBehaviorTree;
class UIFCombatComponent;
class UIFEnemyAIData;
class UIFHealthComponent;

UCLASS()
class IRONFIELD_API AIFEnemyController : public AAIController
{
	GENERATED_BODY()

public:
	bool IsReadyForNewAttack() const;

	UIFCombatComponent* GetControlledCombatComponent() const { return CachedCombatComponent; }

	/** Shared AI tunables. Never null at runtime — falls back to CDO defaults if unset. */
	const UIFEnemyAIData* GetAIData() const;

	FName GetTargetActorKeyName() const { return TargetActorKeyName; }

	void UpdateFocusOnTarget(AActor* NewTarget);

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Behavior")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Data")
	TObjectPtr<UIFEnemyAIData> AIData;

	// Must match the TargetActor key on BB_Enemy and every BT node selector (see IFAI::TargetActorKey).
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Behavior")
	FName TargetActorKeyName = IFAI::TargetActorKey;

private:
	// Negative sentinel = "no attack has ended yet", so IsReadyForNewAttack() returns true.
	static constexpr float NeverAttackEndedTime = -1.f;

	float LastAttackEndedTime = NeverAttackEndedTime;
	float CurrentReattackCooldownSeconds = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UIFCombatComponent> CachedCombatComponent;

	// Cached so unbind survives pawn detach (GetPawn() is null in OnUnPossess after death).
	UPROPERTY(Transient)
	TObjectPtr<UIFHealthComponent> CachedHealthComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CachedPlayer;

	UPROPERTY(Transient)
	TWeakObjectPtr<UIFHealthComponent> CachedPlayerHealth;

	UFUNCTION()
	void HandleOwnCombatStateChanged(ECombatState PreviousState, ECombatState NewState);

	UFUNCTION()
	void HandleOwnHealthDepleted();

	UFUNCTION()
	void HandlePlayerDowned();

	UFUNCTION()
	void HandleActorRegistered(FName ActorRole, AActor* Actor);

	void InitializeControlledPawn();
	void RollPawnAggression();
	void BindOwnDelegates();
	void UnbindOwnDelegates();
	void BindPlayer(AActor* InPlayer);
	void UnbindPlayer();
	void ApplyMovementSpeedForState(ECombatState State);
};
