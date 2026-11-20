#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DamageType.h"
#include "IFCombatTypes.generated.h"

UENUM(BlueprintType)
enum class EIFCombatState : uint8
{
	Idle,
	Attacking,
	Blocking,
	HitReact,
	Stunned,
	Dead
};

/**
 * Damage type tagging attacks heavy enough to interrupt active swings and trigger full-body hit reacts.
 */
UCLASS()
class IRONFIELD_API UIFHeavyMeleeDamageType : public UDamageType
{
	GENERATED_BODY()
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCombatStateChanged, EIFCombatState, PreviousState, EIFCombatState, NewState);