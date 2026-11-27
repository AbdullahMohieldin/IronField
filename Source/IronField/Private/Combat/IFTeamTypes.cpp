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
		const AActor* CurrentActor = Actor;
		// Walk up the instigator chain (Projectile -> Weapon/Tower -> Pawn -> Controller) with a cycle bound.
		for (int32 Hop = 0; Hop < 4 && IsValid(CurrentActor); ++Hop)
		{
			if (const IGenericTeamAgentInterface* const TeamAgent = Cast<const IGenericTeamAgentInterface>(CurrentActor))
			{
				return TeamAgent->GetGenericTeamId();
			}

			if (const APawn* const Pawn = Cast<const APawn>(CurrentActor))
			{
				if (const IGenericTeamAgentInterface* const ControllerAgent = Cast<const IGenericTeamAgentInterface>(Pawn->GetController()))
				{
					return ControllerAgent->GetGenericTeamId();
				}
			}

			if (const AController* const Controller = Cast<const AController>(CurrentActor))
			{
				if (const IGenericTeamAgentInterface* const PawnAgent = Cast<const IGenericTeamAgentInterface>(Controller->GetPawn()))
				{
					return PawnAgent->GetGenericTeamId();
				}
			}

			const APawn* const NextInstigator = CurrentActor->GetInstigator();
			if (!IsValid(NextInstigator) || NextInstigator == CurrentActor)
			{
				break;
			}
			CurrentActor = NextInstigator;
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