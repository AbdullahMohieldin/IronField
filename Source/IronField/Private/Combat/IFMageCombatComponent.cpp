#include "Combat/IFMageCombatComponent.h"

#include "AIController.h"
#include "AI/IFBTUtils.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Combat/IFCombatTargetingUtils.h"
#include "Combat/IFProjectile.h"
#include "Components/CapsuleComponent.h"
#include "Core/IFAnimMontageUtils.h"
#include "GameFramework/Pawn.h"

namespace
{
	constexpr float SpawnClearancePadding = 20.f;
	constexpr float CastBlendOutTime = 0.2f;
}

void UIFMageCombatComponent::StartAttack()
{
	if (IsDead() || IsAttacking() || !CastMontage)
	{
		return;
	}

	UAnimInstance* const AnimInstance = GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	ClearCastMontageDelegate();

	const float PlayLength = AnimInstance->Montage_Play(CastMontage);
	if (PlayLength <= 0.f)
	{
		return;
	}

	SetCombatState(ECombatState::Attacking);

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UIFMageCombatComponent::HandleCastMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, CastMontage);
}

void UIFMageCombatComponent::CancelAttack()
{
	ClearCastMontageDelegate();

	if (UAnimInstance* const AnimInstance = GetAnimInstance())
	{
		if (CastMontage && AnimInstance->Montage_IsPlaying(CastMontage))
		{
			AnimInstance->Montage_Stop(CastBlendOutTime, CastMontage);
		}
	}

	RestoreIdleStateUnlessDead();
}

void UIFMageCombatComponent::ClearCastMontageDelegate()
{
	if (!CastMontage)
	{
		return;
	}

	if (UAnimInstance* const AnimInstance = GetAnimInstance())
	{
		IFAnimMontageUtils::ClearMontageEndDelegate(AnimInstance, CastMontage);
	}
}

void UIFMageCombatComponent::HandleCastMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != CastMontage)
	{
		return;
	}

	ClearCastMontageDelegate();
	RestoreIdleStateUnlessDead();
}

void UIFMageCombatComponent::LaunchProjectileAttack()
{
	if (!ProjectileClass)
	{
		return;
	}

	AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	AActor* const Target = ResolveLiveTarget();
	if (!Target)
	{
		return;
	}

	// One aim vector drives facing, spawn offset, rotation, and velocity.
	FVector AimDir = Target->GetActorLocation() - Owner->GetActorLocation();
	if (AimDir.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		AimDir = Owner->GetActorForwardVector().GetSafeNormal2D();
	}
	else
	{
		AimDir.Normalize();
	}
	if (AimDir.IsNearlyZero())
	{
		return;
	}

	Owner->SetActorRotation(FRotator(0.f, AimDir.Rotation().Yaw, 0.f));

	// Authored offset is the floor; body radius plus shell radius wins.
	float ClearanceRadius = ProjectileSpawnForwardOffset;
	if (const UCapsuleComponent* const OwnerCapsule = Owner->FindComponentByClass<UCapsuleComponent>())
	{
		const AIFProjectile* const ProjectileCDO = ProjectileClass->GetDefaultObject<AIFProjectile>();
		const float ProjectileRadius = ProjectileCDO ? ProjectileCDO->GetCollisionSphereRadius() : 0.f;
		ClearanceRadius = FMath::Max(ClearanceRadius, OwnerCapsule->GetScaledCapsuleRadius() + ProjectileRadius + SpawnClearancePadding);
	}

	FIFProjectileSpawnArgs Args;
	Args.SpawnLocation = Owner->GetActorLocation() + AimDir * ClearanceRadius;
	Args.LaunchDirection = AimDir;
	Args.Instigator = Owner;
	Args.Damage = AttackDamage * GetDamageScale();
	Args.DamageTypeClass = DamageTypeClass;

	// Deferred so InitializeProjectile runs before overlap and BeginPlay.
	const FTransform SpawnTransform(Args.LaunchDirection.Rotation(), Args.SpawnLocation);
	AIFProjectile* const Projectile = World->SpawnActorDeferred<AIFProjectile>(ProjectileClass, SpawnTransform, Owner, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Projectile)
	{
		return;
	}

	Projectile->InitializeProjectile(Args);
	Projectile->FinishSpawning(SpawnTransform);
}

AActor* UIFMageCombatComponent::ResolveLiveTarget() const
{
	AActor* const Owner = GetOwner();
	const APawn* const OwnerPawn = Cast<APawn>(Owner);
	const AAIController* const AIController = OwnerPawn ? Cast<AAIController>(OwnerPawn->GetController()) : nullptr;
	const UBlackboardComponent* const Blackboard = AIController ? AIController->GetBlackboardComponent() : nullptr;

	AActor* const LiveTarget = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(IFAI::TargetActorKey)) : nullptr;
	if (IFCombatTargetingUtils::GetValidAttackTargetHealth(Owner, LiveTarget))
	{
		return LiveTarget;
	}

	AActor* const CachedTarget = GetAttackTarget();
	if (IFCombatTargetingUtils::GetValidAttackTargetHealth(Owner, CachedTarget))
	{
		return CachedTarget;
	}

	return nullptr;
}
