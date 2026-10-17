#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace IFTeamIds
{
	constexpr uint8 Player = 0;
	constexpr uint8 Enemy = 1;
	constexpr uint8 Neutral = 255;
}

namespace IFTeamUtils
{
	inline ETeamAttitude::Type SolveAttitude(FGenericTeamId TeamA, FGenericTeamId TeamB)
	{
		if (TeamA == FGenericTeamId::NoTeam || TeamB == FGenericTeamId::NoTeam)
		{
			return ETeamAttitude::Neutral;
		}
		return (TeamA == TeamB) ? ETeamAttitude::Friendly : ETeamAttitude::Hostile;
	}

	inline FGenericTeamId GetActorTeamId(const AActor* Actor)
	{
		if (!Actor)
		{
			return FGenericTeamId::NoTeam;
		}

		if (const IGenericTeamAgentInterface* const Agent = Cast<const IGenericTeamAgentInterface>(Actor))
		{
			return Agent->GetGenericTeamId();
		}

		if (const APawn* const Pawn = Cast<const APawn>(Actor))
		{
			if (const IGenericTeamAgentInterface* const ControllerAgent = Cast<const IGenericTeamAgentInterface>(Pawn->GetController()))
			{
				return ControllerAgent->GetGenericTeamId();
			}
		}

		if (const AController* const Controller = Cast<const AController>(Actor))
		{
			if (const IGenericTeamAgentInterface* const PawnAgent = Cast<const IGenericTeamAgentInterface>(Controller->GetPawn()))
			{
				return PawnAgent->GetGenericTeamId();
			}
		}

		return FGenericTeamId::NoTeam;
	}

	inline ETeamAttitude::Type GetAttitudeTowards(const AActor* SourceActor, const AActor* TargetActor)
	{
		if (!SourceActor || !TargetActor || SourceActor == TargetActor)
		{
			return ETeamAttitude::Neutral;
		}

		return SolveAttitude(GetActorTeamId(SourceActor), GetActorTeamId(TargetActor));
	}

	inline bool AreHostile(const AActor* SourceActor, const AActor* TargetActor)
	{
		return GetAttitudeTowards(SourceActor, TargetActor) == ETeamAttitude::Hostile;
	}

	inline bool AreFriendly(const AActor* SourceActor, const AActor* TargetActor)
	{
		return GetAttitudeTowards(SourceActor, TargetActor) == ETeamAttitude::Friendly;
	}
}
