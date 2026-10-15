#include "Character/IFMageEnemyCharacter.h"

#include "AI/IFEnemyController.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
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
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

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

void AIFMageEnemyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateFocusOnTarget();
}

void AIFMageEnemyCharacter::OnDeathStarted()
{
	SetActorTickEnabled(false);
}

void AIFMageEnemyCharacter::UpdateFocusOnTarget()
{
	if (IsDead())
	{
		return;
	}

	if (!CachedEnemyController.IsValid())
	{
		CachedEnemyController = Cast<AIFEnemyController>(GetController());
	}

	AIFEnemyController* const EnemyController = CachedEnemyController.Get();
	if (!EnemyController)
	{
		return;
	}

	// The blackboard appears once the behavior tree starts, so keep retrying until it resolves.
	if (!CachedBlackboard.IsValid())
	{
		CachedBlackboard = EnemyController->GetBlackboardComponent();
		if (CachedBlackboard.IsValid())
		{
			CachedTargetKey = EnemyController->GetTargetActorKeyName();
		}
	}

	const UBlackboardComponent* const Blackboard = CachedBlackboard.Get();
	if (!Blackboard)
	{
		return;
	}

	AActor* const DesiredFocus = Cast<AActor>(Blackboard->GetValueAsObject(CachedTargetKey));

	if (DesiredFocus == CurrentFocusTarget.Get())
	{
		return;
	}

	CurrentFocusTarget = DesiredFocus;
	if (DesiredFocus)
	{
		EnemyController->SetFocus(DesiredFocus);
	}
	else
	{
		EnemyController->ClearFocus(EAIFocusPriority::Gameplay);
	}
}
