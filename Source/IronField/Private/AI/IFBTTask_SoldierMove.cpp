#include "AI/IFBTTask_SoldierMove.h"

#include "AI/IFSoldierAIController.h"
#include "AITypes.h"
#include "Castle/IFStructure.h"
#include "Character/IFSoldierCharacter.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	float ComputeAcceptanceRadius(const AIFSoldierAIController& Controller, const AActor* GoalActor)
	{
		const AIFSoldierCharacter* const Soldier = Controller.GetSoldier();
		if (IsValid(Soldier) && IsValid(GoalActor) && GoalActor == Controller.GetCurrentTarget())
		{
			return Soldier->GetEffectiveAttackRange(GoalActor);
		}
		return Controller.GetMoveGoalAcceptanceRadius();
	}
}

UIFBTTask_SoldierMove::UIFBTTask_SoldierMove()
{
	NodeName = TEXT("IF Soldier Move");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
}

uint16 UIFBTTask_SoldierMove::GetInstanceMemorySize() const
{
	return static_cast<uint16>(sizeof(FBTTask_SoldierMoveMemory));
}

EBTNodeResult::Type UIFBTTask_SoldierMove::IssueMove(AIFSoldierAIController& Controller, FBTTask_SoldierMoveMemory& Memory)
{
	AActor* const GoalActor = Controller.GetMoveGoalActor();
	const FVector GoalLocation = Controller.GetMoveGoalLocation();
	const float AcceptanceRadius = ComputeAcceptanceRadius(Controller, GoalActor);

	Memory.TrackedGoalActor = GoalActor;
	Memory.bTrackedActorGoal = IsValid(GoalActor);
	Memory.TrackedGoalLocation = GoalLocation;
	Memory.LastProgressLocation = IsValid(Controller.GetPawn()) ? Controller.GetPawn()->GetActorLocation() : FVector::ZeroVector;
	Memory.StallElapsedSeconds = 0.f;

	FAIMoveRequest MoveReq;
	if (IsValid(GoalActor))
	{
		if (const AIFStructure* const GoalStructure = Cast<AIFStructure>(GoalActor))
		{
			const AIFSoldierCharacter* const Soldier = Controller.GetSoldier();
			const FVector ApproachPoint = IsValid(Soldier)
				? Soldier->GetCombatApproachPoint(GoalStructure)
				: GoalStructure->GetActorLocation();
			MoveReq.SetGoalLocation(ApproachPoint);
			MoveReq.SetProjectGoalLocation(true);
		}
		else
		{
			MoveReq.SetGoalActor(GoalActor);
		}
	}
	else
	{
		MoveReq.SetGoalLocation(GoalLocation);
		MoveReq.SetProjectGoalLocation(true);
	}

	MoveReq.SetAcceptanceRadius(AcceptanceRadius);
	MoveReq.SetUsePathfinding(true);
	MoveReq.SetAllowPartialPath(true);
	MoveReq.SetCanStrafe(false);
	// Disable Unreal's implicit radius padding because GetEffectiveAttackRange already accounts for both capsule radii.
	MoveReq.SetReachTestIncludesAgentRadius(false);
	MoveReq.SetReachTestIncludesGoalRadius(false);

	const FPathFollowingRequestResult MoveResult = Controller.MoveTo(MoveReq);

	switch (MoveResult.Code)
	{
	case EPathFollowingRequestResult::AlreadyAtGoal:
		return EBTNodeResult::Succeeded;
	case EPathFollowingRequestResult::RequestSuccessful:
		return EBTNodeResult::InProgress;
	default:
		return EBTNodeResult::Failed;
	}
}

EBTNodeResult::Type UIFBTTask_SoldierMove::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTTask_SoldierMoveMemory* const Memory = CastInstanceNodeMemory<FBTTask_SoldierMoveMemory>(NodeMemory);
	Memory->Controller.Reset();
	Memory->TrackedGoalActor.Reset();
	Memory->TrackedGoalLocation = FVector::ZeroVector;
	Memory->LastProgressLocation = FVector::ZeroVector;
	Memory->ElapsedSeconds = 0.f;
	Memory->StallElapsedSeconds = 0.f;
	Memory->RepathRetryCount = 0;
	Memory->RepathCooldownSeconds = 0.f;
	Memory->bTrackedActorGoal = false;

	AIFSoldierAIController* const Controller = Cast<AIFSoldierAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(Controller))
	{
		return EBTNodeResult::Failed;
	}

	// No goal means "already in position" (in-range attack, hold at the post).
	// Succeeding here lets the combat branch continue straight into the attack task.
	if (!Controller->HasMoveGoal())
	{
		return EBTNodeResult::Succeeded;
	}

	Memory->Controller = Controller;
	return IssueMove(*Controller, *Memory);
}

void UIFBTTask_SoldierMove::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTTask_SoldierMoveMemory* const Memory = CastInstanceNodeMemory<FBTTask_SoldierMoveMemory>(NodeMemory);

	AIFSoldierAIController* const Controller = Memory->Controller.Get();
	if (!IsValid(Controller) || !IsValid(Controller->GetPawn()))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	if (!Controller->HasMoveGoal())
	{
		Controller->StopMovement();
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	Memory->ElapsedSeconds += DeltaSeconds;
	Memory->RepathCooldownSeconds = FMath::Max(0.f, Memory->RepathCooldownSeconds - DeltaSeconds);
	if (Memory->ElapsedSeconds > MaxMoveSeconds)
	{
		Controller->StopMovement();
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// Retarget when the decision moved to a different actor or to a significantly
	// different static point (follow target, advance goal, new order).
	const AActor* const CurrentGoalActor = Controller->GetMoveGoalActor();
	const FVector CurrentGoalLocation = Controller->GetMoveGoalLocation();
	const bool bGoalActorChanged = (CurrentGoalActor != Memory->TrackedGoalActor.Get())
		|| (IsValid(CurrentGoalActor) != Memory->bTrackedActorGoal);
	const bool bGoalPointMoved = !IsValid(CurrentGoalActor)
		&& FVector::DistSquared(CurrentGoalLocation, Memory->TrackedGoalLocation) > FMath::Square(10.f);

	const bool bMovingWithSameActor = (Controller->GetMoveStatus() == EPathFollowingStatus::Moving)
		&& IsValid(CurrentGoalActor)
		&& !bGoalActorChanged;

	if (!bMovingWithSameActor && (bGoalActorChanged || bGoalPointMoved))
	{
		Memory->ElapsedSeconds = 0.f;
		const EBTNodeResult::Type IssueResult = IssueMove(*Controller, *Memory);

		if (IssueResult == EBTNodeResult::Succeeded)
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		else if (IssueResult == EBTNodeResult::Failed)
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		}
		return;
	}

	const FVector PawnLocation = Controller->GetPawn()->GetActorLocation();
	const AIFSoldierCharacter* const Soldier = Controller->GetSoldier();
	const float Radius = ComputeAcceptanceRadius(*Controller, CurrentGoalActor);
	const float Dist2D = (IsValid(CurrentGoalActor) && IsValid(Soldier))
		? Soldier->GetHorizontalDistanceToTarget(CurrentGoalActor)
		: FVector::Dist2D(PawnLocation, CurrentGoalLocation);
	const bool bArrived = (IsValid(CurrentGoalActor) && IsValid(Soldier) && CurrentGoalActor == Controller->GetCurrentTarget())
		? Soldier->IsTargetInMeleeReach(CurrentGoalActor, false)
		: (Dist2D <= Radius);
	if (bArrived)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	if (FVector::DistSquared2D(PawnLocation, Memory->LastProgressLocation) >= FMath::Square(MinStallProgressDistance))
	{
		Memory->LastProgressLocation = PawnLocation;
		Memory->StallElapsedSeconds = 0.f;
		Memory->RepathRetryCount = 0;
	}
	else
	{
		Memory->StallElapsedSeconds += DeltaSeconds;
		if (Memory->StallElapsedSeconds >= StallTimeoutSeconds)
		{
			Controller->StopMovement();
			Controller->NotifyMoveOrderBlockedOrStalled();
			FinishLatentTask(OwnerComp, IsValid(CurrentGoalActor) ? EBTNodeResult::Failed : EBTNodeResult::Succeeded);
			return;
		}
	}

	// A request that has already gone Idle without reaching the goal ended early
	// (e.g., Dynamic Recast aborted during wall placement or goal was unreachable).
	if (Controller->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		if (Dist2D > (Radius + 5.f) && Memory->RepathRetryCount < MaxDynamicRepathRetries)
		{
			if (Memory->RepathCooldownSeconds <= 0.f)
			{
				++Memory->RepathRetryCount;
				Memory->RepathCooldownSeconds = 0.25f;
				const EBTNodeResult::Type RetryResult = IssueMove(*Controller, *Memory);
				if (RetryResult == EBTNodeResult::Succeeded)
				{
					FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
					return;
				}
				if (RetryResult == EBTNodeResult::InProgress)
				{
					return;
				}
				if (Memory->RepathRetryCount < MaxDynamicRepathRetries)
				{
					return;
				}
			}
			else
			{
				return;
			}
		}

		if (!IsValid(CurrentGoalActor))
		{
			Controller->NotifyMoveOrderBlockedOrStalled();
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			return;
		}

		const EBTNodeResult::Type Result = (Dist2D <= (Radius + 5.f))
			? EBTNodeResult::Succeeded
			: EBTNodeResult::Failed;
		FinishLatentTask(OwnerComp, Result);
	}
}

void UIFBTTask_SoldierMove::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);

	const FBTTask_SoldierMoveMemory* const Memory = CastInstanceNodeMemory<FBTTask_SoldierMoveMemory>(NodeMemory);
	if (AIFSoldierAIController* const Controller = Memory->Controller.Get())
	{
		// Always stop, including on success: the attack task starts immediately after
		// and residual velocity would make the soldier slide through its wind-up.
		Controller->StopMovement();

		// Immediately evaluate arrival so formation orders transition to Anchor Idle & facing on this exact frame.
		if (TaskResult == EBTNodeResult::Succeeded)
		{
			Controller->RefreshDecision();
		}
	}
}
