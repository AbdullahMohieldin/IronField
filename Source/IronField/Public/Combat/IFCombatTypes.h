#pragma once

#include "CoreMinimal.h"
#include "IFCombatTypes.generated.h"

UENUM(BlueprintType)
enum class EIFCombatState : uint8
{
	Idle,
	Attacking,
	Blocking,
	Dead
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCombatStateChanged, EIFCombatState, PreviousState, EIFCombatState, NewState);