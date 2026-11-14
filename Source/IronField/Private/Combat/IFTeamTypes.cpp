#include "Combat/IFTeamTypes.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace IFTeamUtils
{
	ETeamAttitude::Type SolveAttitude(FGenericTeamId TeamA, FGenericTeamId TeamB)
	{
		if (TeamA == FGenericTeamId::NoTeam || TeamB == FGenericTeamId::NoTeam)
		{
			return ETeamAttitude::Neutral;
		}

		return (TeamA == TeamB) ? ETeamAttitude::Friendly : ETeamAttitude::Hostile;
	}

	FGenericTeamId GetActorTeamId(const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return FGenericTeamId::NoTeam;
		}

		if (const IGenericTeamAgentInterface* const TeamAgent = Cast<const IGenericTeamAgentInterface>(Actor))
		{
			return TeamAgent->GetGenericTeamId();
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

		if (const APawn* const InstigatorPawn = Actor->GetInstigator())
		{
			if (InstigatorPawn != Actor)
			{
				return GetActorTeamId(InstigatorPawn);
			}
		}

		return FGenericTeamId::NoTeam;
	}

	ETeamAttitude::Type GetAttitudeTowards(const AActor* SourceActor, const AActor* TargetActor)
	{
		if (!IsValid(SourceActor) || !IsValid(TargetActor) || SourceActor == TargetActor)
		{
			return ETeamAttitude::Neutral;
		}

		return SolveAttitude(GetActorTeamId(SourceActor), GetActorTeamId(TargetActor));
	}

	bool AreHostile(const AActor* SourceActor, const AActor* TargetActor)
	{
		return GetAttitudeTowards(SourceActor, TargetActor) == ETeamAttitude::Hostile;
	}

	bool AreFriendly(const AActor* SourceActor, const AActor* TargetActor)
	{
		return GetAttitudeTowards(SourceActor, TargetActor) == ETeamAttitude::Friendly;
	}
}