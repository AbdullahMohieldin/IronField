#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"

class AActor;

namespace IFTeamIds
{
	constexpr uint8 Player = 0;
	constexpr uint8 Enemy = 1;
	constexpr uint8 Neutral = 255;
}

namespace IFTeamUtils
{
	IRONFIELD_API ETeamAttitude::Type SolveAttitude(FGenericTeamId TeamA, FGenericTeamId TeamB);
	IRONFIELD_API FGenericTeamId GetActorTeamId(const AActor* Actor);
	IRONFIELD_API ETeamAttitude::Type GetAttitudeTowards(const AActor* SourceActor, const AActor* TargetActor);
	IRONFIELD_API bool AreHostile(const AActor* SourceActor, const AActor* TargetActor);
	IRONFIELD_API bool AreFriendly(const AActor* SourceActor, const AActor* TargetActor);
}