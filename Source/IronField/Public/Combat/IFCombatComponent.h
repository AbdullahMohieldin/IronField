#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatDataAsset.h"
#include "Combat/IFCombatTypes.h"
#include "Components/ActorComponent.h"
#include "IFCombatComponent.generated.h"

class AIFBaseCharacter;
class UAnimInstance;

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
	virtual void PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator);

	virtual void HandleOwnerDeath();
	virtual void HandleOwnerRevived();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetCombatState(EIFCombatState NewState);
	virtual void OnHitReactFinished();

	UPROPERTY(Transient)
	TObjectPtr<UAnimInstance> CachedAnimInstance;

	TWeakObjectPtr<AIFBaseCharacter> OwnerCharacter;

	FTimerHandle HitReactTimerHandle;

private:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Combat", meta = (AllowPrivateAccess = "true"))
	EIFCombatState CombatState = EIFCombatState::Idle;
};