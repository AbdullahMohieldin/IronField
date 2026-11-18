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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameResultChanged, EIFGameResult, NewResult);