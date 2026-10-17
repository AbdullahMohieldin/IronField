#include "Combat/IFCombatTargetingUtils.h"

#include "Combat/IFCombatComponent.h"
#include "Combat/IFTeamTypes.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Pawn.h"
#include "Stats/IFHealthComponent.h"

namespace IFCombatTargetingUtils
{
	UIFHealthComponent* GetValidAttackTargetHealth(const AActor* InstigatorActor, AActor* TargetActor)
	{
		if (!TargetActor || TargetActor == InstigatorActor)
		{
			return nullptr;
		}

		if (!IFTeamUtils::AreHostile(InstigatorActor, TargetActor))
		{
			return nullptr;
		}

		// Any actor with health is damageable once team hostility rules pass.
		UIFHealthComponent* const TargetHealth = TargetActor->FindComponentByClass<UIFHealthComponent>();
		return (TargetHealth && !TargetHealth->IsDead()) ? TargetHealth : nullptr;
	}

	void ApplyDamageTo(AActor* TargetActor, AActor* Instigator, float Damage, TSubclassOf<UDamageType> DamageTypeClass)
	{
		if (!TargetActor || !Instigator || FMath::IsNaN(Damage) || Damage <= 0.0f)
		{
			return;
		}

		const APawn* const InstigatorPawn = Cast<APawn>(Instigator);
		AController* const InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;

		FDamageEvent DamageEvent(DamageTypeClass);
		TargetActor->TakeDamage(Damage, DamageEvent, InstigatorController, Instigator);
	}

	void DeliverDamage(AActor* TargetActor, AActor* Instigator, float Damage, TSubclassOf<UDamageType> DamageTypeClass)
	{
		if (FMath::IsNaN(Damage) || Damage <= 0.0f)
		{
			return;
		}

		if (UIFCombatComponent* const TargetCombat = FindCombatComponent(TargetActor))
		{
			TargetCombat->ReceiveAttack(Instigator, Damage, DamageTypeClass);
			return;
		}

		ApplyDamageTo(TargetActor, Instigator, Damage, DamageTypeClass);
	}

	UIFCombatComponent* FindCombatComponent(const AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UIFCombatComponent>() : nullptr;
	}
}
