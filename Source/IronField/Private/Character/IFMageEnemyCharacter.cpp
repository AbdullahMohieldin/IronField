#include "Character/IFMageEnemyCharacter.h"

#include "Combat/IFMageCombatComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Stats/IFHealthComponent.h"

namespace
{
	constexpr float MageDefaultCombatRange = 700.f;
	constexpr float MageDefaultMaxHealth = 50.f;
}

AIFMageEnemyCharacter::AIFMageEnemyCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer
		.SetDefaultSubobjectClass<UIFMageCombatComponent>(TEXT("Combat")))
{
	PrimaryActorTick.bCanEverTick = false;

	CombatRange = MageDefaultCombatRange;

	// 2 player combo hits to kill. BP can still override.
	if (UIFHealthComponent* const Health = GetHealthComponent())
	{
		Health->SetMaxHealth(MageDefaultMaxHealth);
	}

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = true;
	}
}
