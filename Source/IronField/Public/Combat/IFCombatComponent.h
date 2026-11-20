#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatDataAsset.h"
#include "Combat/IFCombatTypes.h"
#include "Components/ActorComponent.h"
#include "IFCombatComponent.generated.h"

class AIFBaseCharacter;
class UAnimInstance;
class UAnimMontage;

UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFCombatComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Combat|Events")
	FOnCombatStateChanged OnCombatStateChanged;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat")
	FIFDirectionalHitReactConfig DirectionalHitReacts;

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

	virtual bool TryBlockIncomingDamage(float DamageAmount, AActor* DamageCauser, AController* EventInstigator);
	virtual void PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator, bool bIsHeavyStagger = false);

	virtual void HandleOwnerDeath();
	virtual void HandleOwnerRevived();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetCombatState(EIFCombatState NewState);
	virtual void HandleCombatStateTransition(EIFCombatState PreviousState, EIFCombatState NewState) {}
	virtual void OnHitReactFinished();

	UAnimMontage* ResolveDirectionalHitReactMontage(AActor* DamageCauser, AController* EventInstigator);
	bool PlayAdditiveFlinchMontage(UAnimMontage* MontageToPlay);

	UPROPERTY(Transient)
	TObjectPtr<UAnimInstance> CachedAnimInstance;

	TWeakObjectPtr<AIFBaseCharacter> OwnerCharacter;
	TWeakObjectPtr<UAnimMontage> ActiveFlinchMontage;

	FTimerHandle HitReactTimerHandle;

private:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Combat", meta = (AllowPrivateAccess = "true"))
	EIFCombatState CombatState = EIFCombatState::Idle;
};