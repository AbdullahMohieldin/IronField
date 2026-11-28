#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Combat/IFTeamTypes.h"
#include "Engine/EngineTypes.h"
#include "IFGameTypes.generated.h"

namespace IFCollision
{
	constexpr ECollisionChannel Damageable = ECC_GameTraceChannel1;
	constexpr ECollisionChannel PawnDawn = ECC_GameTraceChannel2;
	constexpr ECollisionChannel PawnDusk = ECC_GameTraceChannel3;

	inline ECollisionChannel GetTeamPawnChannel(FGenericTeamId TeamId)
	{
		const uint8 RawId = TeamId.GetId();
		if (RawId == IFTeamIds::Dawn)
		{
			return PawnDawn;
		}
		if (RawId == IFTeamIds::Dusk)
		{
			return PawnDusk;
		}
		return ECC_Pawn;
	}

	inline FCollisionObjectQueryParams MakePawnObjectQueryParams()
	{
		FCollisionObjectQueryParams Params;
		Params.AddObjectTypesToQuery(ECC_Pawn);
		Params.AddObjectTypesToQuery(PawnDawn);
		Params.AddObjectTypesToQuery(PawnDusk);
		return Params;
	}
}

UENUM(BlueprintType)
enum class EIFGameResult : uint8
{
	None,
	Victory,
	Defeat
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameResultChanged, EIFGameResult, NewResult);