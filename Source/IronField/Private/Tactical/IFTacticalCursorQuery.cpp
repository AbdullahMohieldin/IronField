#include "Tactical/IFTacticalCursorQuery.h"

#include "Castle/IFGateDoor.h"
#include "Castle/IFStructure.h"
#include "Combat/IFTeamTypes.h"
#include "Core/IFGameTypes.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

AActor* FIFTacticalCursorQuery::FindBestPawnUnderCursor(
	const UWorld* World,
	const FVector& RayOrigin,
	const FVector& RayDir,
	float MaxDistance,
	float ToleranceRadius,
	const AActor* IgnoredActor,
	TFunctionRef<bool(const AActor*)> IsEligible)
{
	if (!IsValid(World))
	{
		return nullptr;
	}

	FCollisionObjectQueryParams EnvObjectParams;
	EnvObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams EnvQueryParams(SCENE_QUERY_STAT(IFTacticalEnvTrace), false);
	if (IsValid(IgnoredActor))
	{
		EnvQueryParams.AddIgnoredActor(IgnoredActor);
	}

	FHitResult EnvHit;
	float EnvDistance = MaxDistance;
	if (World->LineTraceSingleByObjectType(EnvHit, RayOrigin, RayOrigin + RayDir * MaxDistance, EnvObjectParams, EnvQueryParams))
	{
		EnvDistance = EnvHit.Distance;
	}

	const float SweepDistance = FMath::Min(MaxDistance, EnvDistance + ToleranceRadius);
	const FVector SweepEnd = RayOrigin + RayDir * SweepDistance;

	const FCollisionObjectQueryParams PawnObjectParams = IFCollision::MakePawnObjectQueryParams();
	FCollisionQueryParams PawnQueryParams(SCENE_QUERY_STAT(IFTacticalPawnTrace), false);

	// Direct line trace gives priority to exact cursor hits even when another unit stands nearby.
	TArray<FHitResult> DirectHits;
	if (World->LineTraceMultiByObjectType(DirectHits, RayOrigin, SweepEnd, PawnObjectParams, PawnQueryParams))
	{
		for (const FHitResult& Hit : DirectHits)
		{
			AActor* const Candidate = Hit.GetActor();
			if (Hit.Distance <= EnvDistance + 25.f && IsEligible(Candidate))
			{
				return Candidate;
			}
		}
	}

	// Forgiving sphere sweep catches moving targets or units clicked near capsule edges from overhead.
	TArray<FHitResult> SweepHits;
	if (!World->SweepMultiByObjectType(
		SweepHits,
		RayOrigin,
		SweepEnd,
		FQuat::Identity,
		PawnObjectParams,
		FCollisionShape::MakeSphere(ToleranceRadius),
		PawnQueryParams))
	{
		return nullptr;
	}

	AActor* BestCandidate = nullptr;
	float BestPerpDistSq = TNumericLimits<float>::Max();

	for (const FHitResult& Hit : SweepHits)
	{
		AActor* const Candidate = Hit.GetActor();
		if (!IsEligible(Candidate))
		{
			continue;
		}

		const float PerpDistSq = FMath::PointDistToSegmentSquared(Candidate->GetActorLocation(), RayOrigin, SweepEnd);
		if (PerpDistSq < BestPerpDistSq)
		{
			BestPerpDistSq = PerpDistSq;
			BestCandidate = Candidate;
		}
	}

	return BestCandidate;
}

AIFStructure* FIFTacticalCursorQuery::FindBestStructureUnderCursor(
	const UWorld* World,
	const FVector& RayOrigin,
	const FVector& RayDir,
	float MaxDistance,
	const APawn* PlayerPawn,
	const AActor* IgnoredActor,
	bool bHostileOnly)
{
	if (!IsValid(World) || !IsValid(PlayerPawn))
	{
		return nullptr;
	}

	FCollisionObjectQueryParams StructObjectParams;
	StructObjectParams.AddObjectTypesToQuery(IFCollision::Damageable);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFTacticalStructureTrace), false);
	if (IsValid(IgnoredActor))
	{
		QueryParams.AddIgnoredActor(IgnoredActor);
	}

	auto PickBestMatch = [PlayerPawn, bHostileOnly](const TArray<FHitResult>& Hits) -> AIFStructure*
	{
		AIFStructure* FirstMatch = nullptr;
		for (const FHitResult& Hit : Hits)
		{
			AIFStructure* const Candidate = Cast<AIFStructure>(Hit.GetActor());
			if (!IsValid(Candidate) || !Candidate->IsStructureAlive())
			{
				continue;
			}

			const bool bMatchesAttitude = bHostileOnly
				? IFTeamUtils::AreHostile(PlayerPawn, Candidate)
				: IFTeamUtils::AreFriendly(PlayerPawn, Candidate);
			if (bMatchesAttitude)
			{
				if (Cast<AIFGateDoor>(Candidate))
				{
					return Candidate;
				}
				if (!IsValid(FirstMatch))
				{
					FirstMatch = Candidate;
				}
			}
		}
		return FirstMatch;
	};

	const FVector TraceEnd = RayOrigin + RayDir * MaxDistance;
	TArray<FHitResult> DirectHits;
	if (World->LineTraceMultiByObjectType(DirectHits, RayOrigin, TraceEnd, StructObjectParams, QueryParams))
	{
		if (AIFStructure* const DirectMatch = PickBestMatch(DirectHits))
		{
			return DirectMatch;
		}
	}

	// Forgiving 35 cm sphere sweep catches narrow towers viewed obliquely from overhead RTS angles.
	TArray<FHitResult> SweepHits;
	if (World->SweepMultiByObjectType(
		SweepHits,
		RayOrigin,
		TraceEnd,
		FQuat::Identity,
		StructObjectParams,
		FCollisionShape::MakeSphere(35.f),
		QueryParams))
	{
		return PickBestMatch(SweepHits);
	}

	return nullptr;
}

bool FIFTacticalCursorQuery::TraceGroundUnderCursor(
	const UWorld* World,
	const FVector& RayOrigin,
	const FVector& RayDir,
	float MaxDistance,
	const AActor* IgnoredActor,
	FHitResult& OutHit,
	TArray<FHitResult>* ScratchHits)
{
	if (!IsValid(World))
	{
		return false;
	}

	FCollisionObjectQueryParams GroundObjectParams;
	GroundObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFTacticalGroundTrace), false);
	if (IsValid(IgnoredActor))
	{
		QueryParams.AddIgnoredActor(IgnoredActor);
	}

	TArray<FHitResult> LocalHits;
	TArray<FHitResult>& Hits = ScratchHits ? *ScratchHits : LocalHits;
	Hits.Reset();

	if (!World->LineTraceMultiByObjectType(Hits, RayOrigin, RayOrigin + RayDir * MaxDistance, GroundObjectParams, QueryParams))
	{
		return false;
	}

	for (const FHitResult& Hit : Hits)
	{
		if (Hit.bBlockingHit && !Cast<APawn>(Hit.GetActor()))
		{
			OutHit = Hit;
			return true;
		}
	}

	return false;
}
