#pragma once

#include "CoreMinimal.h"
#include "IFCombatTypes.generated.h"

class UAnimMontage;
class UDamageType;

UENUM(BlueprintType)
enum class ECombatState : uint8
{
	Idle,
	Attacking,
	Blocking,
	Dead
};

/** One step in a melee combo. Shared by the player and melee enemies. */
USTRUCT(BlueprintType)
struct FIFComboStep
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat|ComboStep")
	TObjectPtr<UAnimMontage> AttackMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat|ComboStep", meta = (ClampMin = "0.0"))
	float StaminaCost = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat|ComboStep", meta = (ClampMin = "0.0"))
	float Damage = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat|ComboStep")
	TSubclassOf<UDamageType> DamageTypeClass;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCombatStateChanged, ECombatState, PreviousState, ECombatState, NewState);
