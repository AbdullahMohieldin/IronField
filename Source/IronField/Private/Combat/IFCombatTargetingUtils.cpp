#include "Combat/IFCombatTargetingUtils.h"

#include "AIController.h"
#include "Building/IFStronghold.h"
#include "Combat/IFCombatComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Pawn.h"
#include "Stats/IFHealthComponent.h"

namespace IFCombatTargetingUtils
{
	namespace
	{
		bool IsAIControlled(const APawn* Pawn)
		{
			return Pawn && Cast<AAIController>(Pawn->GetController()) != nullptr;
		}
	}

	UIFHealthComponent* GetValidAttackTargetHealth(const AActor* InstigatorActor, AActor* TargetActor)
	{
		if (!TargetActor || TargetActor == InstigatorActor)
		{
			return nullptr;
		}

		const APawn* const OwnerPawn = Cast<APawn>(InstigatorActor);
		const APawn* const TargetPawn = Cast<APawn>(TargetActor);
		if (IsAIControlled(OwnerPawn) && IsAIControlled(TargetPawn))
		{
			return nullptr;
		}

		// The stronghold is the shared objective: only AI enemies may damage it.
		// This keeps wild player swings/spin next to it from chipping the lose condition.
		if (Cast<AIFStronghold>(TargetActor) && !IsAIControlled(OwnerPawn))
		{
			return nullptr;
		}

		// Any actor with health is damageable once team hostility and stronghold rules pass.
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
