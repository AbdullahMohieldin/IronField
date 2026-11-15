#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatTypes.h"
#include "Components/ActorComponent.h"
#include "IFCombatComponent.generated.h"

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

	virtual void HandleOwnerDeath();
	virtual void HandleOwnerRevived();

protected:
	void SetCombatState(EIFCombatState NewState);

private:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Combat", meta = (AllowPrivateAccess = "true"))
	EIFCombatState CombatState = EIFCombatState::Idle;
};