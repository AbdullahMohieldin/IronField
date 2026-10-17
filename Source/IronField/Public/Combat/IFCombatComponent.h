#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatTypes.h"
#include "Components/ActorComponent.h"
#include "IFCombatComponent.generated.h"

class UAnimInstance;
class UAnimMontage;
class UDamageType;
class UIFStaminaComponent;
class USkeletalMeshComponent;
class USoundBase;
class UNiagaraSystem;

/**
 * Base combat component managing core combat state, hit reactions, and damage scaling.
 */
UCLASS()
class IRONFIELD_API UIFCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFCombatComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Combat|Events")
	FOnCombatStateChanged OnCombatStateChanged;

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	virtual void StartAttack() {}

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	virtual void StartBlock() {}

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	virtual void StopBlock() {}

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	virtual void ResetCombatState();

	/** Stops a playing attack montage and returns to idle. Safe to call when no attack is active. */
	virtual void CancelAttack();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	void HandleOwnerDeath();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	void HandleOwnerRevived();

	/** Virtual attack collision hooks overridden by melee/combo components. */
	virtual void BeginAttackCollision() {}
	virtual void EndAttackCollision() {}

	/** Shared hit entry for melee weapon boxes and ranged projectiles. */
	virtual void ReceiveAttack(AActor* Instigator, float Damage, TSubclassOf<UDamageType> DamageTypeClass);

	void SetAttackTarget(AActor* InTarget) { CachedAttackTarget = InTarget; }
	AActor* GetAttackTarget() const { return CachedAttackTarget.Get(); }

	virtual void LaunchProjectileAttack() {}

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Damage")
	void SetDamageScale(float NewScale) { DamageScale = FMath::Max(0.f, NewScale); }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat|Damage")
	float GetDamageScale() const { return DamageScale; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat|State")
	ECombatState GetCombatState() const { return CombatState; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat|State")
	bool IsIdle() const { return CombatState == ECombatState::Idle; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat|State")
	bool IsAttacking() const { return CombatState == ECombatState::Attacking; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat|State")
	bool IsBlocking() const { return CombatState == ECombatState::Blocking; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat|State")
	bool IsDead() const { return CombatState == ECombatState::Dead; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UIFStaminaComponent> StaminaComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMesh;

	TWeakObjectPtr<AActor> CachedAttackTarget;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Damage", meta = (ClampMin = "0.0"))
	float DamageScale = 1.0f;

	void SetCombatState(ECombatState NewState);
	UAnimInstance* GetAnimInstance() const;
	bool HasUsableStamina(float Amount) const;
	void RestoreIdleStateUnlessDead();
	virtual void ClearReactionMontageDelegates();

	virtual void PlayHitReactionMontage();

	UFUNCTION()
	virtual void HandleHitReactionMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> HitReactionMontage;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Feedback", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Feedback", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNiagaraSystem> HitVFX;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Combat|State", meta = (AllowPrivateAccess = "true"))
	ECombatState CombatState = ECombatState::Idle;
};
