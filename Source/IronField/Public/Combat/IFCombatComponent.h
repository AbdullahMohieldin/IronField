#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatDataAsset.h"
#include "Combat/IFCombatTypes.h"
#include "Components/ActorComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "UObject/SoftObjectPtr.h"
#include "IFCombatComponent.generated.h"

class AIFBaseCharacter;
class UAnimInstance;
class UAnimMontage;
class UIFStaminaComponent;
struct FStreamableHandle;

UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFCombatComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Combat|Events")
	FOnCombatStateChanged OnCombatStateChanged;

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	EIFCombatState GetCombatState() const { return CombatState; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	bool IsIdle() const { return CombatState == EIFCombatState::Idle; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	bool IsAttacking() const { return CombatState == EIFCombatState::Attacking; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	bool IsBlocking() const { return CombatState == EIFCombatState::Blocking; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	bool IsInHitReact() const { return CombatState == EIFCombatState::HitReact; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	bool IsStunned() const { return CombatState == EIFCombatState::Stunned; }

	UFUNCTION(BlueprintPure, Category = "IronField|Combat")
	bool IsCombatReady() const { return bCombatReady; }

	UFUNCTION(BlueprintCallable, Category = "IronField|Combat")
	bool TryStartAttack();

	bool TryStartAttackStep(int32 StepIndex);
	const UIFCombatDataAsset* GetLoadedCombatData() const { return LoadedCombatData; }

	int32 GetCurrentMontageInstanceID() const { return CurrentMontageInstanceID; }

	void OpenHitWindow();
	void UpdateHitWindow(float DeltaTime);
	void CloseHitWindow();

	virtual bool TryBlockIncomingDamage(float DamageAmount, AActor* DamageCauser, AController* EventInstigator);
	virtual void PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator, bool bIsHeavyStagger = false);

	virtual void HandleOwnerDeath();
	virtual void HandleOwnerRevived();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetCombatState(EIFCombatState NewState);
	virtual void HandleCombatStateTransition(EIFCombatState PreviousState, EIFCombatState NewState);
	virtual void GatherRequiredAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const;
	virtual void OnHitReactFinished();
	virtual const FIFMeleeAttackStep* GetCurrentStep() const;
	virtual void HandleStepStarted() {}
	virtual bool TryBufferedAdvance() { return false; }
	virtual void HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	bool ExecuteAttackStep(const FIFMeleeAttackStep& Step, int32 InStepIndex);
	bool PlayAttackMontage(UAnimMontage* Montage);
	void InvalidateMontageExecution();
	bool PlayStep(int32 StepIndex);
	int32 GetNextStepIndex() const;
	void FinishCombo();

	UAnimMontage* ResolveDirectionalHitReactMontage(AActor* DamageCauser, AController* EventInstigator);
	bool PlayAdditiveFlinchMontage(UAnimMontage* MontageToPlay);
	void TriggerAdditiveFlinch(AActor* DamageCauser, AController* EventInstigator);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat")
	TSoftObjectPtr<UIFCombatDataAsset> CombatData;

	UPROPERTY(EditAnywhere, Category = "IronField|Combat")
	bool bDrawDebugHitWindow = false;

	UPROPERTY(Transient)
	TObjectPtr<UIFCombatDataAsset> LoadedCombatData;

	UPROPERTY(Transient)
	TObjectPtr<UAnimInstance> CachedAnimInstance;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CurrentMontage;

	UPROPERTY(Transient)
	TObjectPtr<UIFStaminaComponent> Stamina;

	TWeakObjectPtr<AIFBaseCharacter> OwnerCharacter;
	TWeakObjectPtr<UAnimMontage> ActiveFlinchMontage;

	int32 CurrentMontageInstanceID = INDEX_NONE;
	int32 CurrentStepIndex = INDEX_NONE;
	int32 LastFinishedStepIndex = INDEX_NONE;
	bool bCombatReady = false;

	FTimerHandle HitReactTimerHandle;
	FTimerHandle ComboResetTimerHandle;

private:
	void HandleOwnedMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted, int32 InstanceID);
	void HandleCombatDataLoaded();
	void HandleStepAssetsLoaded();
	bool AreRequiredAssetsLoaded() const;
	void StartComboResetTimer();
	void HandleComboReset();
	void BuildSamplePoints(TArray<FVector, TInlineAllocator<16>>& OutPoints) const;
	void ProcessHit(AActor* Target, const FVector& HitLocation);
	void RestoreAnimTickOption();

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Combat", meta = (AllowPrivateAccess = "true"))
	EIFCombatState CombatState = EIFCombatState::Idle;

	TSharedPtr<FStreamableHandle> DataLoadHandle;
	// Held until EndPlay to keep soft-referenced montages and VFX pinned against GC.
	TSharedPtr<FStreamableHandle> StepAssetsLoadHandle;

	bool bHitWindowOpen = false;
	bool bHasSavedAnimTickOption = false;
	EVisibilityBasedAnimTickOption SavedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;

	float NextHitReactAllowedTime = 0.f;
	float NextAdditiveFlinchAllowedTime = 0.f;

	TSet<TWeakObjectPtr<AActor>> HitActorsThisWindow;
	TArray<FVector, TInlineAllocator<16>> PreviousSamplePoints;
	TArray<FVector, TInlineAllocator<16>> CurrentSamplePoints;
	TArray<FOverlapResult> ScratchOverlaps;
	TArray<FHitResult> ScratchHits;
};
