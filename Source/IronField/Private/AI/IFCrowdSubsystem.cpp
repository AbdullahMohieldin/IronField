#include "AI/IFCrowdSubsystem.h"

#include "AI/IFSoldierAIController.h"
#include "AI/IFSoldierAITypes.h"
#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFPlayerCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"

namespace
{
	constexpr float GridCellSizeCm = 160.f;
	constexpr float CombatSeparationDistCm = 82.f;
	constexpr float MaxVerticalSeparationDistCm = 110.f;
	constexpr float MaxCombatSeparationSpeedCmPerSec = 150.f;
	constexpr float MaxHeroPartingSpeedCmPerSec = 260.f;

	bool IsCombatOrEscortAction(EIFSoldierAction Action)
	{
		return Action == EIFSoldierAction::AttackTarget
			|| Action == EIFSoldierAction::Defend
			|| Action == EIFSoldierAction::FollowActor
			|| Action == EIFSoldierAction::Advance;
	}

	FIntPoint ToCellCoord(const FVector& Location)
	{
		return FIntPoint(
			FMath::FloorToInt(Location.X / GridCellSizeCm),
			FMath::FloorToInt(Location.Y / GridCellSizeCm));
	}

	float WrapAnglePositive(float AngleRad)
	{
		constexpr float TwoPi = 2.f * PI;
		float Wrapped = FMath::Fmod(AngleRad, TwoPi);
		if (Wrapped < 0.f)
		{
			Wrapped += TwoPi;
		}
		return Wrapped;
	}
}

bool UIFCrowdSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	const UWorld* const World = Cast<UWorld>(Outer);
	return IsValid(World) && World->IsGameWorld();
}

bool UIFCrowdSubsystem::IsTickable() const
{
	if (IsTemplate() || RegisteredCharacters.Num() < 2)
	{
		return false;
	}

	const UWorld* const World = GetWorld();
	return IsValid(World) && World->IsGameWorld();
}

TStatId UIFCrowdSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UIFCrowdSubsystem, STATGROUP_Tickables);
}

void UIFCrowdSubsystem::RegisterCharacter(AIFBaseCharacter* Character)
{
	if (!IsValid(Character))
	{
		return;
	}

	const bool bExists = RegisteredCharacters.ContainsByPredicate(
		[Character](const TWeakObjectPtr<AIFBaseCharacter>& Ptr) { return Ptr.Get() == Character; });
	if (!bExists)
	{
		RegisteredCharacters.Add(Character);
		CachedAttackerGroupFrame = MAX_uint64;
	}
}

void UIFCrowdSubsystem::UnregisterCharacter(AIFBaseCharacter* Character)
{
	const int32 Removed = RegisteredCharacters.RemoveAll(
		[Character](const TWeakObjectPtr<AIFBaseCharacter>& Ptr)
		{
			return !Ptr.IsValid() || Ptr.Get() == Character;
		});
	if (Removed > 0)
	{
		CachedAttackerGroupFrame = MAX_uint64;
	}
}

void UIFCrowdSubsystem::EnsureAttackerGroupsBuilt() const
{
	if (CachedAttackerGroupFrame == GFrameCounter)
	{
		return;
	}

	CachedAttackerGroupFrame = GFrameCounter;
	CachedAttackerSoldiers.Reset();
	TargetAttackerHead.Reset();
	NextAttackerInGroup.Reset();

	for (const TWeakObjectPtr<AIFBaseCharacter>& Ptr : RegisteredCharacters)
	{
		const AIFSoldierCharacter* const Soldier = Cast<AIFSoldierCharacter>(Ptr.Get());
		if (!IsValid(Soldier) || Soldier->IsDead())
		{
			continue;
		}

		const AIFSoldierAIController* const AI = Soldier->GetSoldierController();
		const AActor* const Target = IsValid(AI) ? AI->GetCurrentTarget() : nullptr;
		if (!IsValid(Target))
		{
			continue;
		}

		const int32 EntryIndex = CachedAttackerSoldiers.Add(Soldier);
		const FAttackerGroupKey Key{ TObjectKey<const AActor>(Target), Soldier->GetGenericTeamId().GetId() };
		int32& Head = TargetAttackerHead.FindOrAdd(Key, INDEX_NONE);
		NextAttackerInGroup.Add(Head);
		Head = EntryIndex;
	}
}

void UIFCrowdSubsystem::Tick(float DeltaTime)
{
	const float Dt = FMath::Min(DeltaTime, 0.05f);
	if (Dt <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	CachedUnits.Reset();
	CachedUnits.Reserve(RegisteredCharacters.Num());

	bool bAnyCombatPush = false;
	bool bHeroMoving = false;
	bool bAnyIdleUnit = false;

	for (int32 i = RegisteredCharacters.Num() - 1; i >= 0; --i)
	{
		AIFBaseCharacter* const Char = RegisteredCharacters[i].Get();
		if (!IsValid(Char))
		{
			RegisteredCharacters.RemoveAtSwap(i, EAllowShrinking::No);
			continue;
		}
		if (Char->IsDead())
		{
			continue;
		}

		FUnitSnapshot Snap;
		Snap.Character = Char;
		Snap.Location = Char->GetActorLocation();
		Snap.TeamId = Char->GetGenericTeamId().GetId();
		Snap.bIsHero = Char->IsA<AIFPlayerCharacter>();

		if (Snap.bIsHero)
		{
			if (Char->GetVelocity().SizeSquared2D() > 4.f
				|| Char->GetLastMovementInputVector().SizeSquared2D() > 0.01f)
			{
				bHeroMoving = true;
			}
		}
		else if (const AIFSoldierCharacter* const Soldier = Cast<AIFSoldierCharacter>(Char))
		{
			const UIFCombatComponent* const Combat = Soldier->GetCombatComponent();
			const bool bMidSwing = IsValid(Combat) && Combat->IsAttacking();
			if (!bMidSwing)
			{
				if (const AIFSoldierAIController* const AI = Soldier->GetSoldierController())
				{
					const EIFSoldierAction Action = AI->GetCurrentAction();
					Snap.bReceivesCombatPush = IsCombatOrEscortAction(Action);
					Snap.bReceivesHeroParting = (Action == EIFSoldierAction::Idle);
					bAnyCombatPush |= Snap.bReceivesCombatPush;
					bAnyIdleUnit |= Snap.bReceivesHeroParting;
				}
			}
		}

		CachedUnits.Add(Snap);
	}

	if (CachedUnits.Num() < 2 || (!bAnyCombatPush && !(bHeroMoving && bAnyIdleUnit)))
	{
		return;
	}

	// Build flat intrusive spatial hash with zero per-frame heap allocations.
	SpatialCellHead.Reset();
	NextUnitInCell.SetNumUninitialized(CachedUnits.Num(), EAllowShrinking::No);
	for (int32 Idx = 0; Idx < CachedUnits.Num(); ++Idx)
	{
		int32& Head = SpatialCellHead.FindOrAdd(ToCellCoord(CachedUnits[Idx].Location), INDEX_NONE);
		NextUnitInCell[Idx] = Head;
		Head = Idx;
	}

	UWorld* const World = GetWorld();
	UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	for (int32 SelfIdx = 0; SelfIdx < CachedUnits.Num(); ++SelfIdx)
	{
		const FUnitSnapshot& Self = CachedUnits[SelfIdx];
		if (!Self.bReceivesCombatPush && !(Self.bReceivesHeroParting && bHeroMoving))
		{
			continue;
		}

		FVector PushAccum = FVector::ZeroVector;
		float MaxAllowedStep = 0.f;
		const FIntPoint BaseCell = ToCellCoord(Self.Location);

		for (int32 CellDX = -1; CellDX <= 1; ++CellDX)
		{
			for (int32 CellDY = -1; CellDY <= 1; ++CellDY)
			{
				const int32* const HeadPtr = SpatialCellHead.Find(FIntPoint(BaseCell.X + CellDX, BaseCell.Y + CellDY));
				if (!HeadPtr)
				{
					continue;
				}

				for (int32 OtherIdx = *HeadPtr; OtherIdx != INDEX_NONE; OtherIdx = NextUnitInCell[OtherIdx])
				{
					if (OtherIdx == SelfIdx)
					{
						continue;
					}

					const FUnitSnapshot& Other = CachedUnits[OtherIdx];
					if (Other.TeamId != Self.TeamId)
					{
						continue;
					}
					if (FMath::Abs(Self.Location.Z - Other.Location.Z) > MaxVerticalSeparationDistCm)
					{
						continue;
					}

					const bool bHeroPartingPair = Self.bReceivesHeroParting && Other.bIsHero;
					if (!Self.bReceivesCombatPush && !bHeroPartingPair)
					{
						continue;
					}

					const float DesiredDist = bHeroPartingPair ? HeroPartingDistCm : CombatSeparationDistCm;
					const FVector Delta2D(Self.Location.X - Other.Location.X, Self.Location.Y - Other.Location.Y, 0.f);
					const float DistSq = Delta2D.SizeSquared2D();
					if (DistSq >= FMath::Square(DesiredDist))
					{
						continue;
					}

					const float Dist = FMath::Sqrt(DistSq);
					FVector Dir2D;
					if (Dist > 1.f)
					{
						Dir2D = Delta2D / Dist;
					}
					else
					{
						const float Angle = static_cast<float>((SelfIdx * 7 + OtherIdx * 13) % 16) * (2.f * PI / 16.f);
						Dir2D = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
					}

					const float OverlapDeficit = DesiredDist - Dist;
					const float Weight = OverlapDeficit / DesiredDist;
					PushAccum += Dir2D * Weight;
					MaxAllowedStep = FMath::Max(MaxAllowedStep, OverlapDeficit * 0.45f);
				}
			}
		}

		if (PushAccum.IsNearlyZero() || MaxAllowedStep <= 0.25f)
		{
			continue;
		}

		UCharacterMovementComponent* const MoveComp = Self.Character->GetCharacterMovement();
		if (!IsValid(MoveComp) || MoveComp->IsFalling() || MoveComp->MovementMode == MOVE_None)
		{
			continue;
		}

		const float MaxSpeed = Self.bReceivesCombatPush ? MaxCombatSeparationSpeedCmPerSec : MaxHeroPartingSpeedCmPerSec;
		const float StepLen = FMath::Min(PushAccum.Size2D() * MaxSpeed * Dt, MaxAllowedStep);
		FVector MoveDelta = PushAccum.GetSafeNormal2D() * StepLen;
		MoveDelta.Z = 0.f;

		if (MoveDelta.IsNearlyZero())
		{
			continue;
		}

		// Project onto walkable floor slope so separation climbs/descends stairs and ramps without snagging.
		if (MoveComp->CurrentFloor.IsWalkableFloor())
		{
			const FVector& FloorNormal = MoveComp->CurrentFloor.HitResult.ImpactNormal;
			if (FloorNormal.Z > 0.3f && FloorNormal.Z < (1.f - KINDA_SMALL_NUMBER))
			{
				MoveDelta.Z = -(MoveDelta.X * FloorNormal.X + MoveDelta.Y * FloorNormal.Y) / FloorNormal.Z;
			}
		}

		// Validate destination on NavMesh so separation never shoves units off elevated walkways or ledges.
		if (IsValid(NavSys))
		{
			FNavLocation NavDest;
			if (!NavSys->ProjectPointToNavigation(Self.Location + MoveDelta, NavDest, FVector(28.f, 28.f, 120.f)))
			{
				continue;
			}
		}

		// Sweep=true respects solid WorldStatic, Damageable castle walls/gates, and hostile capsules.
		FHitResult MoveHit;
		MoveComp->SafeMoveUpdatedComponent(MoveDelta, Self.Character->GetActorRotation(), true, MoveHit);
		if (MoveHit.IsValidBlockingHit())
		{
			static_cast<UMovementComponent*>(MoveComp)->SlideAlongSurface(MoveDelta, 1.f - MoveHit.Time, MoveHit.Normal, MoveHit, false);
		}
	}
}

FVector UIFCrowdSubsystem::ComputeCombatApproachPoint(const AIFSoldierCharacter& Soldier, const AActor* Target) const
{
	if (!IsValid(Target))
	{
		return Soldier.GetActorLocation();
	}

	EnsureAttackerGroupsBuilt();

	const uint8 TeamId = Soldier.GetGenericTeamId().GetId();
	TArray<const AIFSoldierCharacter*, TInlineAllocator<16>> Attackers;
	const FAttackerGroupKey GroupKey{ TObjectKey<const AActor>(Target), TeamId };
	if (const int32* const HeadPtr = TargetAttackerHead.Find(GroupKey))
	{
		for (int32 EntryIdx = *HeadPtr; EntryIdx != INDEX_NONE; EntryIdx = NextAttackerInGroup[EntryIdx])
		{
			if (const AIFSoldierCharacter* const OtherSoldier = CachedAttackerSoldiers[EntryIdx])
			{
				Attackers.Add(OtherSoldier);
			}
		}
	}

	if (!Attackers.Contains(&Soldier))
	{
		Attackers.Add(&Soldier);
	}

	const FVector SoldierLoc = Soldier.GetActorLocation();
	const float CapsuleRadius = IsValid(Soldier.GetCapsuleComponent())
		? Soldier.GetCapsuleComponent()->GetScaledCapsuleRadius()
		: 42.f;

	if (const AIFStructure* const Structure = Cast<AIFStructure>(Target))
	{
		if (Attackers.Num() <= 1)
		{
			return Structure->GetSpreadCombatApproachPoint(SoldierLoc, CapsuleRadius, 0.f);
		}

		const FVector StructLoc = Structure->GetActorLocation();
		FVector GroupCentroid = FVector::ZeroVector;
		for (const AIFSoldierCharacter* const Attacker : Attackers)
		{
			GroupCentroid += Attacker->GetActorLocation();
		}
		GroupCentroid /= static_cast<float>(Attackers.Num());

		FVector ToCentroid = (GroupCentroid - StructLoc).GetSafeNormal2D();
		if (ToCentroid.IsNearlyZero())
		{
			ToCentroid = (SoldierLoc - StructLoc).GetSafeNormal2D();
		}
		const FVector Tangent(-ToCentroid.Y, ToCentroid.X, 0.f);

		Attackers.Sort([&StructLoc, &Tangent](const AIFSoldierCharacter& A, const AIFSoldierCharacter& B)
		{
			const float ProjA = FVector::DotProduct(A.GetActorLocation() - StructLoc, Tangent);
			const float ProjB = FVector::DotProduct(B.GetActorLocation() - StructLoc, Tangent);
			return !FMath::IsNearlyEqual(ProjA, ProjB, 1.f) ? (ProjA < ProjB) : (A.GetUniqueID() < B.GetUniqueID());
		});

		const int32 RankIndex = Attackers.IndexOfByKey(&Soldier);
		const FVector Extent = Structure->GetFootprintExtent();
		const float ApproxFaceSpan = FMath::Max(Extent.X, Extent.Y) * 2.f;
		const int32 SlotsPerFace = FMath::Max(1, FMath::FloorToInt(ApproxFaceSpan / 92.f));
		const int32 ColIdx = RankIndex % SlotsPerFace;
		const int32 ColsInThisRank = FMath::Min(SlotsPerFace, Attackers.Num() - (RankIndex - ColIdx));
		const float LateralOffset = (static_cast<float>(ColIdx) - 0.5f * static_cast<float>(ColsInThisRank - 1)) * 92.f;

		return Structure->GetSpreadCombatApproachPoint(SoldierLoc, CapsuleRadius, LateralOffset);
	}

	const FVector TargetLoc = Target->GetActorLocation();
	if (Attackers.Num() <= 1)
	{
		return TargetLoc;
	}

	// Anchor surround ring to the normalized sum of attacker-to-target directions so no unit crossing -X flips the ring.
	FVector SumFromTarget = FVector::ZeroVector;
	for (const AIFSoldierCharacter* const Attacker : Attackers)
	{
		SumFromTarget += (Attacker->GetActorLocation() - TargetLoc).GetSafeNormal2D();
	}
	const float BaseAngle = !SumFromTarget.IsNearlyZero()
		? FMath::Atan2(SumFromTarget.Y, SumFromTarget.X)
		: 0.f;

	Attackers.Sort([&TargetLoc, BaseAngle](const AIFSoldierCharacter& A, const AIFSoldierCharacter& B)
	{
		const FVector DA = A.GetActorLocation() - TargetLoc;
		const FVector DB = B.GetActorLocation() - TargetLoc;
		const float RelAngleA = WrapAnglePositive(FMath::Atan2(DA.Y, DA.X) - BaseAngle);
		const float RelAngleB = WrapAnglePositive(FMath::Atan2(DB.Y, DB.X) - BaseAngle);
		return !FMath::IsNearlyEqual(RelAngleA, RelAngleB, 0.01f) ? (RelAngleA < RelAngleB) : (A.GetUniqueID() < B.GetUniqueID());
	});

	const int32 SlotIndex = Attackers.IndexOfByKey(&Soldier);
	constexpr int32 MaxUnitsPerRing = 8;
	const int32 RingIndex = SlotIndex / MaxUnitsPerRing;
	const int32 IndexInRing = SlotIndex % MaxUnitsPerRing;
	const int32 UnitsInRing = FMath::Min(MaxUnitsPerRing, Attackers.Num() - RingIndex * MaxUnitsPerRing);

	const float Angle = BaseAngle + (2.f * PI * static_cast<float>(IndexInRing)) / static_cast<float>(UnitsInRing);
	const float BaseApproachRadius = Soldier.GetEffectiveAttackRange(Target) * 0.88f;
	const float RingRadius = BaseApproachRadius + static_cast<float>(RingIndex) * 86.f;
	const FVector CandidatePoint = TargetLoc + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * RingRadius;

	UWorld* const World = GetWorld();
	UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (IsValid(World) && IsValid(NavSys))
	{
		FNavLocation ProjectedTarget;
		FNavLocation ProjectedSlot;
		if (NavSys->ProjectPointToNavigation(TargetLoc, ProjectedTarget, FVector(120.f, 120.f, 200.f))
			&& NavSys->ProjectPointToNavigation(CandidatePoint, ProjectedSlot, FVector(55.f, 55.f, 180.f)))
		{
			FVector HitLoc = FVector::ZeroVector;
			if (!UNavigationSystemV1::NavigationRaycast(World, ProjectedTarget.Location, ProjectedSlot.Location, HitLoc))
			{
				return ProjectedSlot.Location;
			}
		}
	}

	return TargetLoc;
}
