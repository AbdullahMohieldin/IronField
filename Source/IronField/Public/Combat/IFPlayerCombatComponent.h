#pragma once

#include "CoreMinimal.h"
#include "Combat/IFComboCombatComponent.h"
#include "IFPlayerCombatComponent.generated.h"

class AActor;
class UDamageType;

UCLASS()
class IRONFIELD_API UIFPlayerCombatComponent : public UIFComboCombatComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	void StartSpinAttack();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat|Actions")
	void StopSpinAttack();

	virtual void StartAttack() override;
	virtual void StartBlock() override;
	virtual void StopBlock() override;
	virtual void ResetCombatState() override;
	virtual void ReceiveAttack(AActor* Instigator, float Damage, TSubclassOf<UDamageType> DamageTypeClass) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void ClearReactionMontageDelegates() override;
	virtual void HandleHitReactionMontageEnded(UAnimMontage* Montage, bool bInterrupted) override;
	virtual bool CanQueueComboAttack() const override;
	virtual float GetCurrentAttackDamage() const override;
	virtual TSubclassOf<UDamageType> GetCurrentDamageTypeClass() const override;
	virtual bool RequiresWeaponCollisionBox() const override { return true; }

private:
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Blocking", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> BlockMontage;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Blocking", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> BlockReactionMontage;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Blocking", meta = (AllowPrivateAccess = "true", ClampMin = "-1.0", ClampMax = "1.0"))
	float BlockFacingDotThreshold = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Stamina", meta = (AllowPrivateAccess = "true"))
	float BlockStaminaDrainRate = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Stamina", meta = (AllowPrivateAccess = "true"))
	float MinimumStaminaToStartBlock = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Blocking", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float BlockBlendOutTime = 0.15f;

	/** Fraction of incoming damage that chips through a successful block (0.1 = 10%). */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Blocking", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	float BlockChipFraction = 0.1f;

	/** Stamina charged per blocked hit so swarms break guard. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Blocking", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float BlockStaminaCostPerHit = 6.f;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveBlockMontage;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Animation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> SpinAttackMontage;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Animation", meta = (AllowPrivateAccess = "true"))
	FName SpinIntroSectionName = TEXT("Intro");

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Animation", meta = (AllowPrivateAccess = "true"))
	FName SpinLoopSectionName = TEXT("Loop");

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Animation", meta = (AllowPrivateAccess = "true"))
	FName SpinEndSectionName = TEXT("End");

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Stamina", meta = (AllowPrivateAccess = "true"))
	float SpinStaminaDrainRate = 18.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Stamina", meta = (AllowPrivateAccess = "true"))
	float MinimumStaminaToStartSpin = 5.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Animation", meta = (AllowPrivateAccess = "true"))
	float SpinBlendOutTime = 0.1f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Damage", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float SpinDamage = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Damage", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UDamageType> SpinDamageTypeClass;

	/** Minimum seconds between spin hits on the same target (single long window still re-hits). */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Damage", meta = (AllowPrivateAccess = "true", ClampMin = "0.05"))
	float SpinRehitInterval = 0.5f;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Combat|State", meta = (AllowPrivateAccess = "true"))
	bool bIsSpinning = false;

	UFUNCTION()
	void HandleSpinMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void HandleBlockReactionMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UFUNCTION()
	void HandleStaminaDepleted();

	bool TryPlayBlockMontage();
	void PlayBlockReactionMontage();
	bool IsOwnerFacingTarget(AActor* TargetActor) const;
	void ClearSpinState();
	void StopSpinGracefully();
	void StopSpinImmediately();

protected:
	virtual bool TryRegisterAttackHit(AActor* TargetActor) override;

private:
	/** Last spin-hit time per target so one long collision window re-hits on an interval. */
	TMap<TWeakObjectPtr<AActor>, float> SpinLastHitTimes;

	friend class FIFBlockStaminaDepletionTest;
};
