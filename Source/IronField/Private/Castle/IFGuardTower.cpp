#include "Castle/IFGuardTower.h"

#include "Castle/IFTowerProjectile.h"
#include "Character/IFBaseCharacter.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"

AIFGuardTower::AIFGuardTower()
{
	ProjectileClass = AIFTowerProjectile::StaticClass();
}

void AIFGuardTower::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopFireTimer();
	Super::EndPlay(EndPlayReason);
}

void AIFGuardTower::OnStructureActivated()
{
	Super::OnStructureActivated();
	StartFireTimer();
}

void AIFGuardTower::OnStructureDeactivated()
{
	StopFireTimer();
	Super::OnStructureDeactivated();
}

void AIFGuardTower::OnTeamChanged(FGenericTeamId, FGenericTeamId)
{
	if (IsStructureActive())
	{
		StartFireTimer();
	}
}

void AIFGuardTower::StartFireTimer()
{
	UWorld* const World = GetWorld();
	if (!IsValid(World) || !IsStructureActive())
	{
		return;
	}

	if (!World->GetTimerManager().IsTimerActive(FireTimerHandle))
	{
		World->GetTimerManager().SetTimer(
			FireTimerHandle,
			this,
			&AIFGuardTower::TryFireAtNearestHostile,
			FMath::Max(0.1f, FireIntervalSeconds),
			true);
	}
}

void AIFGuardTower::StopFireTimer()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimerHandle);
	}
}

void AIFGuardTower::TryFireAtNearestHostile()
{
	UWorld* const World = GetWorld();
	if (!IsValid(World) || !IsStructureActive() || GetGenericTeamId() == FGenericTeamId::NoTeam)
	{
		return;
	}

	const FVector Origin = GetActorLocation();
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFGuardTowerScan), false);
	QueryParams.AddIgnoredActor(this);

	if (!World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeSphere(AttackRange),
		QueryParams))
	{
		return;
	}

	AIFBaseCharacter* BestTarget = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AIFBaseCharacter* const Candidate = Cast<AIFBaseCharacter>(Overlap.GetActor());
		if (!IsValid(Candidate) || Candidate->IsDead() || !IFTeamUtils::AreHostile(this, Candidate))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared2D(Origin, Candidate->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestTarget = Candidate;
		}
	}

	if (!IsValid(BestTarget))
	{
		return;
	}

	UClass* SpawnClass = ProjectileClass.Get();
	if (!IsValid(SpawnClass))
	{
		SpawnClass = AIFTowerProjectile::StaticClass();
	}

	const AIFTowerProjectile* const DefaultProj = Cast<AIFTowerProjectile>(SpawnClass->GetDefaultObject());
	const float BoltSpeed = IsValid(DefaultProj) ? FMath::Max(DefaultProj->GetProjectileSpeed(), 400.f) : 2200.f;

	const FVector MuzzleLocation = GetActorTransform().TransformPosition(MuzzleOffset);
	const FVector TargetChest = BestTarget->GetActorLocation() + FVector(0.f, 0.f, 55.f);

	// Linear target-velocity lead prediction (clamped safely) so moving targets are hit reliably.
	const float DistToTarget = FVector::Dist(MuzzleLocation, TargetChest);
	const float LeadTime = FMath::Clamp(DistToTarget / BoltSpeed, 0.f, 0.75f);
	FVector TargetVelocity = BestTarget->GetVelocity();
	TargetVelocity.Z = FMath::Clamp(TargetVelocity.Z, -150.f, 150.f);
	const FVector AimPoint = TargetChest + TargetVelocity * LeadTime;

	const FVector LaunchDir = (AimPoint - MuzzleLocation).GetSafeNormal();
	const FTransform SpawnTransform(LaunchDir.Rotation(), MuzzleLocation);

	if (AIFTowerProjectile* const Projectile = World->SpawnActorDeferred<AIFTowerProjectile>(
		SpawnClass, SpawnTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Projectile->InitializeProjectile(this, GetGenericTeamId(), TowerDamage, LaunchDir);
		Projectile->FinishSpawning(SpawnTransform);
	}
}
