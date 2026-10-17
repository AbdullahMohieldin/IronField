#include "Character/IFEnemyCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Stats/IFHealthComponent.h"

namespace
{
	constexpr float DefaultAvoidanceWeight = 0.5f;
}

AIFEnemyCharacter::AIFEnemyCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TeamID = FGenericTeamId(IFTeamIds::Enemy);

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->bUseRVOAvoidance = true;
		Movement->AvoidanceWeight = DefaultAvoidanceWeight;
		Movement->bOrientRotationToMovement = true;
	}
}

void AIFEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (UIFHealthComponent* const Health = GetHealthComponent())
	{
		Health->OnDamaged.AddDynamic(this, &AIFEnemyCharacter::HandleDamaged);
	}
}

void AIFEnemyCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UIFHealthComponent* const Health = GetHealthComponent())
	{
		Health->OnDamaged.RemoveAll(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AIFEnemyCharacter::HandleDamaged(AActor* InstigatorActor, float Damage)
{
	if (InstigatorActor && IFTeamUtils::AreHostile(this, InstigatorActor))
	{
		NotifyHitByPlayer();
	}
}

void AIFEnemyCharacter::RollAggression(float Bias, float Spread)
{
	Aggression = FMath::Clamp(BaseAggression + FMath::FRandRange(-Spread, Spread) - Bias, 0.f, 1.f);
}

void AIFEnemyCharacter::NotifyHitByPlayer()
{
	if (const UWorld* const World = GetWorld())
	{
		LastPlayerHitTime = World->GetTimeSeconds();
	}
}

void AIFEnemyCharacter::NotifyTargetSwitched()
{
	if (const UWorld* const World = GetWorld())
	{
		LastTargetSwitchTime = World->GetTimeSeconds();
	}
}

void AIFEnemyCharacter::ApplyMovementSpeedForState(ECombatState State)
{
	UCharacterMovementComponent* const Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	switch (State)
	{
	case ECombatState::Attacking:
		Movement->MaxWalkSpeed = AttackingSpeed;
		break;
	case ECombatState::Idle:
		Movement->MaxWalkSpeed = ChaseSpeed;
		break;
	default:
		break;
	}
}
