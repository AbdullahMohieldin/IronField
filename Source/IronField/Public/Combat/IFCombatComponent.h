#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatTypes.h"
#include "Components/ActorComponent.h"
#include "IFCombatComponent.generated.h"

class UAnimInstance;
class UBoxComponent;
class UDamageType;
class UIFHealthComponent;
class UIFStaminaComponent;
class USkeletalMeshComponent;
class UPrimitiveComponent;
class USoundBase;
class UNiagaraSystem;

UCLASS()
class IRONFIELD_API UIFCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFCombatComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Combat|Events")
	FOnCombatStateChanged OnCombatStateChanged;

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	virtual void StartAttack();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	virtual void StartBlock() {}

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	virtual void StopBlock() {}

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	virtual void ResetCombatState();

	/** Stops a playing attack montage and returns to idle. Safe to call when no attack is active. */
	void CancelAttack();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	void HandleOwnerDeath();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	void HandleOwnerRevived();

	virtual void BeginAttackCollision();
	virtual void EndAttackCollision();

	/** Shared hit entry for melee weapon boxes and ranged projectiles. */
	virtual void ReceiveAttack(AActor* Instigator, float Damage, TSubclassOf<UDamageType> DamageTypeClass);

	void SetAttackTarget(AActor* InTarget) { CachedAttackTarget = InTarget; }
	AActor* GetAttackTarget() const { return CachedAttackTarget.Get(); }

	virtual void LaunchProjectileAttack() {}

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

	/** AI melee only; the base returns 0 so the player combo never auto-continues. */
	virtual float GetComboContinueChance(int32 ComboIndex) const { return 0.f; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UIFStaminaComponent> StaminaComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> CachedMesh;

	TWeakObjectPtr<AActor> CachedAttackTarget;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveAttackMontage;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Combo")
	TArray<FIFComboStep> ComboSteps;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Combat|State")
	bool bComboQueued = false;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Combat|State")
	int32 CurrentComboIndex = 0;

	void SetCombatState(ECombatState NewState);
	UAnimInstance* GetAnimInstance() const;
	bool HasUsableStamina(float Amount) const;
	void ResetRegisteredAttackHits();
	void RestoreIdleStateUnlessDead();
	void ClearAttackMontageDelegate();
	virtual void ClearReactionMontageDelegates();

	virtual float GetCurrentAttackDamage() const;
	virtual TSubclassOf<UDamageType> GetCurrentDamageTypeClass() const;
	virtual bool CanQueueComboAttack() const;

	/** Melee-style components deal damage through the weapon box; true makes a missing assignment a logged warning. */
	virtual bool RequiresWeaponCollisionBox() const { return false; }

	virtual void PlayHitReactionMontage();

	bool CanPlayAttackMontage(int32 ComboIndex) const;
	bool TryPlayAttackMontage(int32 ComboIndex);
	float GetComboStaminaCost(int32 ComboIndex) const;
	bool HasNextComboStep() const { return ComboSteps.IsValidIndex(CurrentComboIndex + 1); }

	UFUNCTION()
	void HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	virtual void HandleHitReactionMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> HitReactionMontage;

	// Empty until assigned in Blueprint; guarded at play time.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Feedback", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Feedback", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNiagaraSystem> HitVFX;

private:
	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Combat|State", meta = (AllowPrivateAccess = "true"))
	ECombatState CombatState = ECombatState::Idle;

	bool bAttackCollisionActive = false;
	float ActiveAttackDamage = 0.f;
	TSubclassOf<UDamageType> ActiveDamageTypeClass;
	TSet<TWeakObjectPtr<AActor>> RegisteredAttackHits;

	// Runtime cache for the fighter's single weapon box, resolved in BeginPlay.
	// Not editor-visible: one box per fighter by convention, so no picker.
	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> WeaponCollisionBox;

	UFUNCTION()
	void HandleWeaponBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void ResolveAttackHit(AActor* TargetActor);
	/** Health component only when attack collision is active and the target is legally hittable. */
	UIFHealthComponent* GetValidActiveAttackTargetHealth(AActor* TargetActor) const;
	void ResolveWeaponCollisionBox();
	void SetWeaponCollisionEnabled(bool bEnabled) const;
	void PlayHitFeedback(const AActor* TargetActor) const;

protected:
	/** Single-hit gate per attack window. Virtual so spin can allow timed re-hits. */
	virtual bool TryRegisterAttackHit(AActor* TargetActor);
};
