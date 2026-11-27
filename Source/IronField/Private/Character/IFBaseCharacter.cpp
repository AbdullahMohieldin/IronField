#include "Character/IFBaseCharacter.h"

#include "Animation/AnimInstance.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFCombatTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IFLog.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Stats/IFHealthComponent.h"
#include "TimerManager.h"

const FName AIFBaseCharacter::CombatComponentName(TEXT("Combat"));

AIFBaseCharacter::AIFBaseCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	HealthComponent = CreateDefaultSubobject<UIFHealthComponent>(TEXT("Health"));
	CombatComponent = CreateDefaultSubobject<UIFCombatComponent>(CombatComponentName);
}

bool AIFBaseCharacter::IsDead() const
{
	return IsValid(HealthComponent) && HealthComponent->IsDead();
}

ETeamAttitude::Type AIFBaseCharacter::GetTeamAttitudeTowards(const AActor& Other) const
{
	return IFTeamUtils::GetAttitudeTowards(this, &Other);
}

void AIFBaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(HealthComponent))
	{
		HealthComponent->OnDeath.AddUniqueDynamic(this, &AIFBaseCharacter::HandleDeath);
	}
}

void AIFBaseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearLocalHitstop();

	if (IsValid(HealthComponent))
	{
		HealthComponent->OnDeath.RemoveDynamic(this, &AIFBaseCharacter::HandleDeath);
	}

	Super::EndPlay(EndPlayReason);
}

void AIFBaseCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (IsDead())
	{
		if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Capsule->SetCanEverAffectNavigation(false);
		}
		if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
		{
			Movement->SetAvoidanceEnabled(false);
			Movement->DisableMovement();
		}
	}
}

float AIFBaseCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	if (DamageAmount <= 0.f || !IsAlive())
	{
		return 0.f;
	}

	if (IsValid(DamageCauser) && IFTeamUtils::AreFriendly(this, DamageCauser))
	{
		return 0.f;
	}
	if (IsValid(EventInstigator) && IFTeamUtils::AreFriendly(this, EventInstigator))
	{
		return 0.f;
	}

	if (IsValid(CombatComponent) && CombatComponent->TryBlockIncomingDamage(DamageAmount, DamageCauser, EventInstigator))
	{
		return 0.f;
	}

	if (!IsValid(HealthComponent))
	{
		return 0.f;
	}

	AActor* InstigatorActor = IsValid(EventInstigator) ? static_cast<AActor*>(EventInstigator->GetPawn()) : nullptr;
	if (!IsValid(InstigatorActor))
	{
		InstigatorActor = DamageCauser;
	}

	FVector PushDir = (IsValid(InstigatorActor) && InstigatorActor != this)
		? (GetActorLocation() - InstigatorActor->GetActorLocation()).GetSafeNormal2D()
		: FVector::ZeroVector;
	if (PushDir.IsNearlyZero())
	{
		PushDir = (-GetActorForwardVector()).GetSafeNormal2D();
	}
	LastDamageDirection = PushDir;

	const bool bIsHeavyStagger = DamageEvent.DamageTypeClass != nullptr
		&& DamageEvent.DamageTypeClass->IsChildOf(UIFHeavyMeleeDamageType::StaticClass());

	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const float ActualDamage = HealthComponent->ApplyDamage(DamageAmount, InstigatorActor);

	if (ActualDamage > 0.f && IsAlive() && IsValid(CombatComponent))
	{
		CombatComponent->PlayDirectionalHitReact(DamageCauser, EventInstigator, bIsHeavyStagger);
	}

	return ActualDamage;
}

void AIFBaseCharacter::HandleDeath(AActor*)
{
	if (bHasDied)
	{
		return;
	}
	bHasDied = true;

	ClearLocalHitstop();

	UE_LOG(LogIronField, Log, TEXT("[IF-Death] %s died."), *GetName());

	if (IsValid(CombatComponent))
	{
		CombatComponent->HandleOwnerDeath();
	}

	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		if (UAnimInstance* const AnimInstance = MeshComp->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(DeathMontageBlendOutTime);
		}
	}

	StopMovementForDeath();
	DisableCollisionForDeath();
	OnDeathStarted();
	OnCharacterDied.Broadcast(this);
}

void AIFBaseCharacter::StopMovementForDeath()
{
	UCharacterMovementComponent* const Movement = GetCharacterMovement();
	if (!IsValid(Movement))
	{
		return;
	}

	Movement->SetAvoidanceEnabled(false);
	Movement->StopMovementImmediately();

	if (!Movement->IsFalling())
	{
		Movement->DisableMovement();
	}

	Movement->bUseControllerDesiredRotation = false;
	Movement->bOrientRotationToMovement = false;
}

void AIFBaseCharacter::DisableCollisionForDeath()
{
	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		const UCharacterMovementComponent* const Movement = GetCharacterMovement();
		const bool bStillFalling = IsValid(Movement) && Movement->IsFalling();

		// Keep WorldStatic blocking while airborne so Landed() can disable collision on the floor.
		Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
		Capsule->SetCanEverAffectNavigation(false);
		if (bStillFalling)
		{
			Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		}
		else
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCanEverAffectNavigation(false);
	}
}

void AIFBaseCharacter::RestoreAliveState()
{
	bHasDied = false;
	ClearLocalHitstop();
	LastDamageDirection = FVector::ZeroVector;

	const AIFBaseCharacter* const DefaultChar = GetClass()->GetDefaultObject<AIFBaseCharacter>();

	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		if (const UCapsuleComponent* const DefaultCapsule = DefaultChar ? DefaultChar->GetCapsuleComponent() : nullptr)
		{
			Capsule->SetCollisionProfileName(DefaultCapsule->GetCollisionProfileName());
			Capsule->SetCollisionResponseToChannels(DefaultCapsule->GetCollisionResponseToChannels());
			Capsule->SetCollisionEnabled(DefaultCapsule->GetCollisionEnabled());
			Capsule->SetCanEverAffectNavigation(DefaultCapsule->CanEverAffectNavigation());
		}
	}

	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		if (const USkeletalMeshComponent* const DefaultMesh = DefaultChar ? DefaultChar->GetMesh() : nullptr)
		{
			MeshComp->SetCollisionProfileName(DefaultMesh->GetCollisionProfileName());
			// SetCollisionProfileName resets CollisionEnabled, so reapply the CDO mode after it.
			MeshComp->SetCollisionEnabled(DefaultMesh->GetCollisionEnabled());
			MeshComp->SetCanEverAffectNavigation(DefaultMesh->CanEverAffectNavigation());
		}
	}

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
		Movement->FindFloor(GetActorLocation(), Movement->CurrentFloor, false);
		if (const UCharacterMovementComponent* const DefaultMovement = DefaultChar ? DefaultChar->GetCharacterMovement() : nullptr)
		{
			Movement->bOrientRotationToMovement = DefaultMovement->bOrientRotationToMovement;
			Movement->bUseControllerDesiredRotation = DefaultMovement->bUseControllerDesiredRotation;
			Movement->SetAvoidanceEnabled(DefaultMovement->bUseRVOAvoidance);
		}
	}
}

void AIFBaseCharacter::ApplyLocalHitstop(float DurationSeconds, float TimeDilation)
{
	if (DurationSeconds <= 0.f || !IsAlive())
	{
		return;
	}

	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	// Per-actor dilation freezes only the attacker and victim without slowing global world time.
	CustomTimeDilation = FMath::Clamp(TimeDilation, 0.01f, 1.f);
	bInLocalHitstop = true;
	World->GetTimerManager().SetTimer(HitstopTimerHandle, this, &AIFBaseCharacter::ClearLocalHitstop, DurationSeconds, false);
}

void AIFBaseCharacter::ClearLocalHitstop()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitstopTimerHandle);
	}

	if (bInLocalHitstop)
	{
		bInLocalHitstop = false;
		CustomTimeDilation = 1.f;
	}
}