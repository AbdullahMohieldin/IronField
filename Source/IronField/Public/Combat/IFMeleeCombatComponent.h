#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFCombatDataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "IFMeleeCombatComponent.generated.h"

class AIFBaseCharacter;
class UAnimInstance;
class UAnimMontage;
class UIFStaminaComponent;
struct FStreamableHandle;

struct FIFPlannedAICombo
{
	bool bHasPlan = false;
	int32 OpenerStepIndex = 0;
	int32 PlannedHitCount = 1;
	int32 ExecutedHits = 0;
	float RequiredStamina = 0.f;

	void Reset()
	{
		bHasPlan = false;
		OpenerStepIndex = 0;
		PlannedHitCount = 1;
		ExecutedHits = 0;
		RequiredStamina = 0.f;
	}
};

UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFMeleeCombatComponent : public UIFCombatComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "IronField|Combat")
	bool TryStartAttack();

	float GetAIComboBreatherSeconds() const;

	void EnsureAIComboPlan();
	bool HasEnoughStaminaForPlannedCombo() const;
	bool TryExecuteNextPlannedAIStep();
	bool HasRemainingPlannedAISteps() const;
	void ClearAIComboPlan();
	float GetAIMaxStaminaWaitSeconds() const;
	int32 GetPlannedAIExecutedHits() const { return PlannedAICombo.ExecutedHits; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	bool IsCombatReady() const { return bCombatReady; }

	int32 GetCurrentMontageInstanceID() const { return CurrentMontageInstanceID; }

	void OpenHitWindow();
	void UpdateHitWindow(float DeltaTime);
	void CloseHitWindow();

	virtual void PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator, bool bIsHeavyStagger = false) override;

	virtual void HandleOwnerDeath() override;
	virtual void HandleOwnerRevived() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void GatherRequiredAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const;
	virtual void HandleCombatStateTransition(EIFCombatState PreviousState, EIFCombatState NewState) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat")
	TSoftObjectPtr<UIFCombatDataAsset> CombatData;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|Stamina", meta = (ClampMin = "0.0"))
	float BlockingStaminaRegenMultiplier = 0.2f;

	UPROPERTY(EditAnywhere, Category = "IronField|Combat")
	bool bDrawDebugHitWindow = false;

	bool PlayAttackMontage(UAnimMontage* Montage);
	void InvalidateMontageExecution();
	bool PlayStep(int32 StepIndex);
	bool PlayGuardAttack();
	int32 GetNextStepIndex() const;
	virtual const FIFMeleeAttackStep* GetCurrentStep() const;
	UAnimMontage* GetCurrentMontage() const { return CurrentMontage; }

	virtual void HandleStepStarted() {}
	virtual bool TryBufferedAdvance() { return false; }

	virtual void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	void FinishCombo();

	UPROPERTY(Transient)
	TObjectPtr<UIFCombatDataAsset> LoadedCombatData;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CurrentMontage;

	UPROPERTY(Transient)
	TObjectPtr<UIFStaminaComponent> Stamina;

	int32 CurrentMontageInstanceID = INDEX_NONE;
	int32 CurrentStepIndex = INDEX_NONE;
	int32 LastFinishedStepIndex = INDEX_NONE;
	bool bIsExecutingGuardAttack = false;

private:
	void HandleOwnedMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted, int32 InstanceID);
	void HandleCombatDataLoaded();
	void HandleStepAssetsLoaded();
	bool AreRequiredAssetsLoaded() const;
	void StartComboResetTimer();
	void HandleComboReset();
	void BuildSamplePoints(TArray<FVector>& OutPoints) const;
	void ProcessHit(AActor* Target, const FVector& HitLocation);
	void TriggerAdditiveFlinch(AActor* DamageCauser, AController* EventInstigator);

	TSharedPtr<FStreamableHandle> DataLoadHandle;
	TSharedPtr<FStreamableHandle> StepAssetsLoadHandle;

	bool bCombatReady = false;
	bool bHitWindowOpen = false;

	float NextHitReactAllowedTime = 0.f;
	float NextAdditiveFlinchAllowedTime = 0.f;

	TSet<TWeakObjectPtr<AActor>> HitActorsThisWindow;
	TArray<FVector> PreviousSamplePoints;

	FIFPlannedAICombo PlannedAICombo;

	FTimerHandle ComboResetTimerHandle;
};