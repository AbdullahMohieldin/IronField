#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFCombatTypes.h"
#include "IFComboCombatComponent.generated.h"

class UAnimMontage;
class UBoxComponent;
class UIFHealthComponent;
class UPrimitiveComponent;

/**
 * Combat component with melee combo steps, weapon collision box resolution, and attack hit registration.
 */
UCLASS()
class IRONFIELD_API UIFComboCombatComponent : public UIFCombatComponent
{
	GENERATED_BODY()

public:
	UIFComboCombatComponent();

	virtual void StartAttack() override;
	virtual void ResetCombatState() override;
	virtual void CancelAttack() override;

	virtual void BeginAttackCollision() override;
	virtual void EndAttackCollision() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** AI melee only; returns auto-continue chance for step. Player returns 0. */
	virtual float GetComboContinueChance(int32 ComboIndex) const { return 0.f; }

protected:
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveAttackMontage;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Combo")
	TArray<FIFComboStep> ComboSteps;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Combat|State")
	bool bComboQueued = false;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Combat|State")
	int32 CurrentComboIndex = 0;

	virtual float GetCurrentAttackDamage() const;
	virtual TSubclassOf<UDamageType> GetCurrentDamageTypeClass() const;
	virtual bool CanQueueComboAttack() const;

	virtual bool RequiresWeaponCollisionBox() const { return false; }
	virtual bool TryRegisterAttackHit(AActor* TargetActor);

	bool CanPlayAttackMontage(int32 ComboIndex) const;
	bool TryPlayAttackMontage(int32 ComboIndex);
	float GetComboStaminaCost(int32 ComboIndex) const;
	bool HasNextComboStep() const { return ComboSteps.IsValidIndex(CurrentComboIndex + 1); }

	void ResetRegisteredAttackHits();
	void ClearAttackMontageDelegate();

	UFUNCTION()
	void HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// Runtime cache for the fighter's single weapon box, resolved in BeginPlay.
	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> WeaponCollisionBox;

	void ResolveWeaponCollisionBox();
	void SetWeaponCollisionEnabled(bool bEnabled) const;
	void PlayHitFeedback(const AActor* TargetActor) const;

private:
	bool bAttackCollisionActive = false;
	float ActiveAttackDamage = 0.f;
	TSubclassOf<UDamageType> ActiveDamageTypeClass;
	TSet<TWeakObjectPtr<AActor>> RegisteredAttackHits;

	UFUNCTION()
	void HandleWeaponBoxBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void ResolveAttackHit(AActor* TargetActor);
	UIFHealthComponent* GetValidActiveAttackTargetHealth(AActor* TargetActor) const;
};
