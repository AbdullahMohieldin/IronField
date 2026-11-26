#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "IFGameTypes.generated.h"

namespace IFCollision
{
	constexpr ECollisionChannel Damageable = ECC_GameTraceChannel1;
}

UENUM(BlueprintType)
enum class EIFGameResult : uint8
{
	None,
	Victory,
	Defeat
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameResultChanged, EIFGameResult, NewResult);