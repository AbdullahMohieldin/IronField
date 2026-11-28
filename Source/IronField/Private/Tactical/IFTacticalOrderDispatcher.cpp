#include "Tactical/IFTacticalOrderDispatcher.h"

#include "AI/IFSoldierAIController.h"
#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Core/IFGameTypes.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Tactical/IFFormationSolver.h"

void FIFTacticalOrderDispatcher::DispatchFollowOrder(
	const TArray<AIFSoldierCharacter*>& Squad,
	AActor* TargetActor,
	float FollowRadius)
{
	if (!IsValid(TargetActor))
	{
		return;
	}

	for (AIFSoldierCharacter* const Soldier : Squad)
	{
		if (AIFSoldierAIController* const AI = IsValid(Soldier) ? Soldier->GetSoldierController() : nullptr)
		{
			AI->IssueFollowActor(TargetActor, FollowRadius);
		}
	}
}

void FIFTacticalOrderDispatcher::DispatchClusterAttackOrder(
	UWorld* World,
	const APawn* CommanderPawn,
	const TArray<AIFSoldierCharacter*>& Squad,
	const TArray<FVector>& SquadLocations,
	AActor* PrimaryTarget,
	float ClusterRadius,
	float TargetSpreadPenalty)
{
	if (!IsValid(PrimaryTarget) || Squad.IsEmpty())
	{
		return;
	}

	// Distribute the squad across nearby hostiles around the clicked target so soldiers do not dogpile a single actor.
	TArray<AIFBaseCharacter*> ClusterTargets;
	if (AIFBaseCharacter* const PrimaryBase = Cast<AIFBaseCharacter>(PrimaryTarget))
	{
		ClusterTargets.Add(PrimaryBase);
	}

	const FVector PrimaryLoc = PrimaryTarget->GetActorLocation();
	const float EffectiveClusterRadius = FMath::Max(ClusterRadius, 100.f);
	if (IsValid(World) && IsValid(CommanderPawn))
	{
		TArray<FOverlapResult> Overlaps;
		const FCollisionObjectQueryParams PawnParams = IFCollision::MakePawnObjectQueryParams();
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFTacticalClusterAttack), false);
		if (World->OverlapMultiByObjectType(
			Overlaps,
			PrimaryLoc,
			FQuat::Identity,
			PawnParams,
			FCollisionShape::MakeSphere(EffectiveClusterRadius),
			QueryParams))
		{
			for (const FOverlapResult& Overlap : Overlaps)
			{
				AIFBaseCharacter* const Candidate = Cast<AIFBaseCharacter>(Overlap.GetActor());
				if (IsValid(Candidate) && Candidate != PrimaryTarget && Candidate->IsAlive()
					&& IFTeamUtils::AreHostile(CommanderPawn, Candidate))
				{
					ClusterTargets.AddUnique(Candidate);
				}
			}
		}
	}

	if (ClusterTargets.Num() <= 1 || Squad.Num() <= 1)
	{
		for (AIFSoldierCharacter* const Soldier : Squad)
		{
			if (AIFSoldierAIController* const AI = IsValid(Soldier) ? Soldier->GetSoldierController() : nullptr)
			{
				AI->IssueAttackTarget(PrimaryTarget);
			}
		}
		return;
	}

	TArray<int32> AssignedCounts;
	AssignedCounts.Init(0, ClusterTargets.Num());

	// Guarantee the closest soldier immediately commits to the clicked primary target.
	int32 PrimarySoldierIdx = 0;
	float BestPrimaryDistSq = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Squad.Num(); ++i)
	{
		const float DistSq = FVector::DistSquared2D(SquadLocations[i], PrimaryLoc);
		if (DistSq < BestPrimaryDistSq)
		{
			BestPrimaryDistSq = DistSq;
			PrimarySoldierIdx = i;
		}
	}

	if (AIFSoldierAIController* const PrimaryAI = Squad[PrimarySoldierIdx]->GetSoldierController())
	{
		PrimaryAI->IssueAttackTarget(ClusterTargets[0]);
		AssignedCounts[0] = 1;
	}

	for (int32 i = 0; i < Squad.Num(); ++i)
	{
		if (i == PrimarySoldierIdx)
		{
			continue;
		}

		AIFSoldierAIController* const AI = Squad[i]->GetSoldierController();
		if (!IsValid(AI))
		{
			continue;
		}

		int32 BestTargetIdx = 0;
		float BestScore = TNumericLimits<float>::Max();
		for (int32 TargetIdx = 0; TargetIdx < ClusterTargets.Num(); ++TargetIdx)
		{
			const float Score =
				FVector::Dist2D(SquadLocations[i], ClusterTargets[TargetIdx]->GetActorLocation())
				+ (static_cast<float>(AssignedCounts[TargetIdx]) * TargetSpreadPenalty);
			if (Score < BestScore)
			{
				BestScore = Score;
				BestTargetIdx = TargetIdx;
			}
		}

		AI->IssueAttackTarget(ClusterTargets[BestTargetIdx]);
		++AssignedCounts[BestTargetIdx];
	}
}

void FIFTacticalOrderDispatcher::DispatchStructureAttackOrder(
	const TArray<AIFSoldierCharacter*>& Squad,
	AIFStructure* HostileStructure)
{
	if (!IsValid(HostileStructure))
	{
		return;
	}

	for (AIFSoldierCharacter* const Soldier : Squad)
	{
		if (AIFSoldierAIController* const AI = IsValid(Soldier) ? Soldier->GetSoldierController() : nullptr)
		{
			AI->IssueAttackTarget(HostileStructure);
		}
	}
}

bool FIFTacticalOrderDispatcher::DispatchFormationMoveOrder(
	UWorld* World,
	const TArray<AIFSoldierCharacter*>& Squad,
	const TArray<FVector>& SquadLocations,
	const FVector& SquadCentroid,
	const FIFTacticalFormationOrderConfig& Config,
	FVector& OutFormationCenter)
{
	if (!IsValid(World) || Squad.IsEmpty())
	{
		return false;
	}

	FIFFormationSolveParams Params;
	Params.FormationType = Config.FormationType;
	Params.UnitCount = Squad.Num();
	Params.AnchorPoint = Config.AnchorPoint;
	Params.DragEndPoint = Config.DragEndPoint;
	Params.bIsDrag = Config.bIsDrag;
	Params.SquadCentroid = SquadCentroid;
	Params.CameraForward = Config.CameraForward;
	Params.LateralSpacing = Config.LateralSpacing;
	Params.RankSpacing = Config.RankSpacing;
	Params.LineAspectRatio = Config.LineAspectRatio;
	Params.MinLineColumns = Config.MinLineColumns;
	Params.MaxLineColumns = Config.MaxLineColumns;
	Params.MinShapeRadius = Config.MinShapeRadius;
	Params.MaxShapeRadius = Config.MaxShapeRadius;

	TArray<FIFFormationSlot> SolvedSlots;
	OutFormationCenter = Config.AnchorPoint;
	if (!FIFFormationSolver::SolveFormation(World, Params, SolvedSlots, OutFormationCenter))
	{
		return false;
	}

	TArray<int32> SoldierToSlotIndices;
	FIFFormationSolver::AssignSoldiersToSlots(SquadLocations, SolvedSlots, SoldierToSlotIndices);

	const int32 AssignCount = FMath::Min(Squad.Num(), SoldierToSlotIndices.Num());
	for (int32 i = 0; i < AssignCount; ++i)
	{
		const int32 SlotIndex = SoldierToSlotIndices[i];
		if (!SolvedSlots.IsValidIndex(SlotIndex))
		{
			continue;
		}

		AIFSoldierCharacter* const Soldier = Squad[i];
		if (AIFSoldierAIController* const AI = IsValid(Soldier) ? Soldier->GetSoldierController() : nullptr)
		{
			const FIFFormationSlot& AssignedSlot = SolvedSlots[SlotIndex];
			AI->IssueMoveToLocation(AssignedSlot.Location, Config.MoveOrderAcceptanceRadius, AssignedSlot.FacingYaw, true);
		}
	}

	return true;
}
