#pragma once

#include "CoreMinimal.h"
#include "Combat/IFMeleeCombatComponent.h"
#include "IFPlayerCombatComponent.generated.h"

UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFPlayerCombatComponent : public UIFMeleeCombatComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "IronField|Combat")
	void RequestAttack();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat")
	void StartBlock();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat")
	void StopBlock();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat")
	bool RequestSpinAttack();

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	float GetSpinAttackCooldownRemaining() const;

	virtual bool TryBlockIncomingDamage(float DamageAmount, AActor* DamageCauser, AController* EventInstigator) override;
	virtual void PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator) override;

	void OpenComboWindow();
	void CloseComboWindow();

	virtual void HandleOwnerDeath() override;
	virtual void HandleOwnerRevived() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual const FIFMeleeAttackStep* GetCurrentStep() const override;
	virtual void HandleStepStarted() override;
	virtual bool TryBufferedAdvance() override;
	virtual void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted) override;
	virtual void OnHitReactFinished() override;

private:
	void TriggerGuardBreak();

	UFUNCTION()
	void HandleGuardBreakEnded();

	void HandleFallbackComboWindow();
	void ClearComboWindowState();
	void TryResumeBlock();

	bool bBlockInputHeld = false;
	bool bAttackBuffered = false;
	bool bComboWindowOpen = false;
	bool bComboWindowNotifySeen = false;
	bool bIsSpinAttacking = false;

	float NextSpinAttackAllowedTime = 0.f;

	UPROPERTY(Transient)
	FIFMeleeAttackStep LeapSlamAttackStep;

	FTimerHandle FallbackComboWindowHandle;
	FTimerHandle GuardBreakTimerHandle;
};
