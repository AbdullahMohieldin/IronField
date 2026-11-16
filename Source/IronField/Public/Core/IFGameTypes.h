#pragma once

#include "CoreMinimal.h"
#include "IFGameTypes.generated.h"

UENUM(BlueprintType)
enum class EIFGameResult : uint8
{
	None,
	Victory,
	Defeat
};

UENUM(BlueprintType)
enum class EIFRunMode : uint8
{
	Normal,
	Unlimited
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameResultChanged, EIFGameResult, NewResult);