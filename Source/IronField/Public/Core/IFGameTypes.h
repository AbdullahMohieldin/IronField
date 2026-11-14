#pragma once

#include "CoreMinimal.h"
#include "IFGameTypes.generated.h"

UENUM(BlueprintType)
enum class EIFMatchState : uint8
{
	Preparation,
	WaveActive,
	Intermission,
	Victory,
	Defeat
};

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