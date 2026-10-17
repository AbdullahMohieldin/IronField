#include "Character/IFBaseCharacter.h"

#include "Animation/AnimInstance.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFTeamTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IFFeedbackUtils.h"
#include "Core/IFLog.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Stats/IFHealthComponent.h"

AIFBaseCharacter::AIFBaseCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	HealthComponent = CreateDefaultSubobject<UIFHealthComponent>(TEXT("Health"));
	CombatComponent = CreateDefaultSubobject<UIFCombatComponent>(TEXT("Combat"));
}

bool AIFBaseCharacter::IsDead() const
{
	return HealthComponent && HealthComponent->IsDead();
}


bool AIFBaseCharacter::IsAttacking() const
{
	return CombatComponent && CombatComponent->IsAttacking();
}

void AIFBaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Cache the live profile names instead of hardcoding strings that can silently go stale.
	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		CapsuleCollisionProfile = Capsule->GetCollisionProfileName();
	}
	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		MeshCollisionProfile = MeshComp->GetCollisionProfileName();
	}

	if (const UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		bSavedOrientRotationToMovement = Movement->bOrientRotationToMovement;
		bSavedUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
	}

	BindGameplayDelegates();
}

void AIFBaseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindGameplayDelegates();
	Super::EndPlay(EndPlayReason);
}

void AIFBaseCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// Corpses keep falling until they land, then become fully inert.
	if (HealthComponent && HealthComponent->IsDead())
	{
		if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
		{
			Movement->DisableMovement();
		}
	}
}

void AIFBaseCharacter::BindGameplayDelegates()
{
	if (HealthComponent)
	{
		HealthComponent->OnHealthDepleted.AddDynamic(this, &AIFBaseCharacter::HandleDeath);
	}
}

void AIFBaseCharacter::UnbindGameplayDelegates()
{
	if (HealthComponent)
	{
		HealthComponent->OnHealthDepleted.RemoveAll(this);
	}
}

void AIFBaseCharacter::HandleDeath()
{
	if (bHasDied)
	{
		return;
	}
	bHasDied = true;

	UE_LOG(LogIronField, Log, TEXT("[IF-Death] %s died."), *GetName());

	if (CombatComponent)
	{
		CombatComponent->HandleOwnerDeath();
	}

	if (UAnimInstance* const AnimInstance = GetMeshAnimInstance())
	{
		AnimInstance->Montage_Stop(DeathMontageBlendOutTime);
	}

	StopMovementForDeath();
	DisableCollisionForDeath();
	PlayDeathFeedback();
	OnDeathStarted();

	// After this point death is visual only; the AnimBP owns the pose.
	OnCharacterDied.Broadcast(this);
}

void AIFBaseCharacter::PlayDeathFeedback() const
{
	IFFeedbackUtils::PlayAtLocation(GetWorld(), DeathSound, DeathVFX, GetActorLocation());
}

void AIFBaseCharacter::StopMovementForDeath()
{
	UCharacterMovementComponent* const Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	Movement->StopMovementImmediately();
	Movement->SetAvoidanceEnabled(false);

	if (!Movement->IsFalling())
	{
		Movement->DisableMovement();
	}

	// Stop the controller from continuing to turn the dead pawn.
	Movement->bUseControllerDesiredRotation = false;
	Movement->bOrientRotationToMovement = false;
}

void AIFBaseCharacter::DisableCollisionForDeath()
{
	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		// Keep blocking the environment so the corpse does not fall through the floor,
		// but let other pawns walk through it.
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCanEverAffectNavigation(false);
	}

	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComp->SetCanEverAffectNavigation(false);
	}
}

void AIFBaseCharacter::RestoreAliveState()
{
	bHasDied = false;

	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionProfileName(CapsuleCollisionProfile);
		Capsule->SetCanEverAffectNavigation(true);
	}

	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComp->SetCollisionProfileName(MeshCollisionProfile);
		MeshComp->SetCanEverAffectNavigation(true);
	}

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
		Movement->FindFloor(GetActorLocation(), Movement->CurrentFloor, false);
		Movement->bOrientRotationToMovement = bSavedOrientRotationToMovement;
		Movement->bUseControllerDesiredRotation = bSavedUseControllerDesiredRotation;
	}
}

UAnimInstance* AIFBaseCharacter::GetMeshAnimInstance() const
{
	USkeletalMeshComponent* const MeshComp = GetMesh();
	return MeshComp ? MeshComp->GetAnimInstance() : nullptr;
}

ETeamAttitude::Type AIFBaseCharacter::GetTeamAttitudeTowards(const AActor& Other) const
{
	return IFTeamUtils::GetAttitudeTowards(this, &Other);
}
