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

	// Snapshot the authored collision state so revive restores it exactly, including
	// "Custom" presets, which have no named profile to reapply.
	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		CapsuleCollisionProfile = Capsule->GetCollisionProfileName();
		CapsuleCollisionEnabled = Capsule->GetCollisionEnabled();
		SavedCapsulePawnResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
		bCapsuleAffectsNavigation = Capsule->CanEverAffectNavigation();
	}
	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		MeshCollisionProfile = MeshComp->GetCollisionProfileName();
		MeshCollisionEnabled = MeshComp->GetCollisionEnabled();
		bMeshAffectsNavigation = MeshComp->CanEverAffectNavigation();
	}

	if (const UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		bSavedOrientRotationToMovement = Movement->bOrientRotationToMovement;
		bSavedUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
	}

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
		HealthComponent->OnDeath.RemoveAll(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AIFBaseCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// Corpses keep falling until they land, then become fully inert.
	if (IsDead())
	{
		if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
		{
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
	if (IsValid(EventInstigator) && IFTeamUtils::AreFriendly(this, EventInstigator->GetPawn()))
	{
		return 0.f;
	}

	if (IsValid(CombatComponent) && CombatComponent->TryBlockIncomingDamage(DamageAmount, DamageCauser, EventInstigator))
	{
		return 0.f;
	}

	if (!IsValid(HealthComponent) || HealthComponent->IsInvincible())
	{
		return 0.f;
	}

	const AActor* Attacker = DamageCauser;
	if (!IsValid(Attacker) && IsValid(EventInstigator))
	{
		Attacker = EventInstigator->GetPawn();
	}

	FVector PushDir = -GetActorForwardVector();
	if (IsValid(Attacker) && Attacker != this)
	{
		FVector Delta = GetActorLocation() - Attacker->GetActorLocation();
		Delta.Z = 0.f;
		if (Delta.Normalize())
		{
			PushDir = Delta;
		}
		else
		{
			PushDir.Z = 0.f;
			if (!PushDir.Normalize())
			{
				PushDir = FVector::ForwardVector;
			}
		}
	}
	else
	{
		PushDir.Z = 0.f;
		if (!PushDir.Normalize())
		{
			PushDir = FVector::ForwardVector;
		}
	}
	LastDamageDirection = PushDir;

	const bool bIsHeavyStagger = DamageEvent.DamageTypeClass != nullptr
		&& DamageEvent.DamageTypeClass->IsChildOf(UIFHeavyMeleeDamageType::StaticClass());

	const float PrevHealth = HealthComponent->GetCurrentHealth();
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	const float ActualDamage = PrevHealth - HealthComponent->GetCurrentHealth();

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

	// After this point death is visual only; the AnimBP owns the pose.
	OnCharacterDied.Broadcast(this);
}

void AIFBaseCharacter::StopMovementForDeath()
{
	UCharacterMovementComponent* const Movement = GetCharacterMovement();
	if (!IsValid(Movement))
	{
		return;
	}

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
		// Keep blocking the environment so the corpse does not fall through the floor,
		// but let other pawns walk through it.
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCanEverAffectNavigation(false);
	}

	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		// Channel responses are left untouched so "Custom" presets survive death/revive;
		// NoCollision already makes them irrelevant while dead.
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCanEverAffectNavigation(false);
	}
}

void AIFBaseCharacter::RestoreAliveState()
{
	bHasDied = false;
	ClearLocalHitstop();
	LastDamageDirection = FVector::ZeroVector;

	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionProfileName(CapsuleCollisionProfile);
		Capsule->SetCollisionEnabled(CapsuleCollisionEnabled);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, SavedCapsulePawnResponse);
		Capsule->SetCanEverAffectNavigation(bCapsuleAffectsNavigation);
	}

	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		MeshComp->SetCollisionProfileName(MeshCollisionProfile);
		// Applying a profile can reset the enabled mode; the snapshot wins so QueryOnly stays QueryOnly.
		MeshComp->SetCollisionEnabled(MeshCollisionEnabled);
		MeshComp->SetCanEverAffectNavigation(bMeshAffectsNavigation);
	}

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
		Movement->FindFloor(GetActorLocation(), Movement->CurrentFloor, false);
		Movement->bOrientRotationToMovement = bSavedOrientRotationToMovement;
		Movement->bUseControllerDesiredRotation = bSavedUseControllerDesiredRotation;
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