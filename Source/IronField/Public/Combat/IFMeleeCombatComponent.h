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

UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFMeleeCombatComponent : public UIFCombatComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "IronField|Combat")
	bool TryStartAttack();

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	bool IsCombatReady() const { return bCombatReady; }

	int32 GetCurrentMontageInstanceID() const { return CurrentMontageInstanceID; }

	void OpenHitWindow();
	void UpdateHitWindow(float DeltaTime);
	void CloseHitWindow();

	virtual void PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator) override;

	virtual void HandleOwnerDeath() override;
	virtual void HandleOwnerRevived() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat")
	TSoftObjectPtr<UIFCombatDataAsset> CombatData;

	UPROPERTY(EditAnywhere, Category = "IronField|Combat")
	bool bDrawDebugHitWindow = false;

	bool PlayAttackMontage(UAnimMontage* Montage);
	void InvalidateMontageExecution();
	bool PlayStep(int32 StepIndex);
	bool PlayGuardAttack();
	int32 GetNextStepIndex() const;
	virtual const FIFMeleeAttackStep* GetCurrentStep() const;
	UAnimMontage* GetCurrentMontage() const { return CurrentMontage; }

	// Player subclass snaps aim and arms the combo window here; enemies need neither.
	virtual void HandleStepStarted() {}

	// Last-chance buffer consumption at natural montage end; base has no buffer.
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

	TSharedPtr<FStreamableHandle> DataLoadHandle;

	// Owns residency for soft-referenced combat assets until EndPlay.
	TSharedPtr<FStreamableHandle> StepAssetsLoadHandle;

	bool bCombatReady = false;
	bool bHitWindowOpen = false;

	TSet<TWeakObjectPtr<AActor>> HitActorsThisWindow;
	TArray<FVector> PreviousSamplePoints;

	FTimerHandle ComboResetTimerHandle;
};