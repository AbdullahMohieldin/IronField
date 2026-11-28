#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatComponent.h"
#include "IFPlayerCombatComponent.generated.h"

UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFPlayerCombatComponent : public UIFCombatComponent
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
	bool RequestLeapSlam();

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat")
	void CancelPendingInputsForTacticalMode();

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	float GetLeapSlamCooldownRemaining() const;

	virtual bool TryBlockIncomingDamage(float DamageAmount, AActor* DamageCauser, AController* EventInstigator) override;
	virtual void PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator, bool bIsHeavyStagger = false) override;

	void OpenComboWindow();
	void CloseComboWindow();

	virtual void HandleOwnerDeath() override;
	virtual void HandleOwnerRevived() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GatherRequiredAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const override;
	virtual void HandleCombatStateTransition(EIFCombatState PreviousState, EIFCombatState NewState) override;
	virtual const FIFMeleeAttackStep* GetCurrentStep() const override;
	virtual void HandleStepStarted() override;
	virtual bool TryBufferedAdvance() override;
	virtual void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted) override;
	virtual void OnHitReactFinished() override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Stamina", meta = (ClampMin = "0.0"))
	float BlockingStaminaRegenMultiplier = 0.2f;

private:
	bool PlayGuardAttack();
	void TriggerGuardBreak();
	void HandleGuardBreakEnded();
	void HandleFallbackComboWindow();
	void ClearComboWindowState();
	void TryResumeBlock();
	void SnapOwnerYawToController();

	bool bBlockInputHeld = false;
	bool bAttackBuffered = false;
	bool bComboWindowOpen = false;
	bool bComboWindowNotifySeen = false;
	bool bIsLeapSlamming = false;
	bool bIsExecutingGuardAttack = false;

	float NextLeapSlamAllowedTime = 0.f;

	FTimerHandle FallbackComboWindowHandle;
	FTimerHandle GuardBreakTimerHandle;
};
