#include "AI/IFBTTask_SoldierMove.h"

#include "AI/IFSoldierAIController.h"
#include "AITypes.h"
#include "Castle/IFStructure.h"
#include "Character/IFSoldierCharacter.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
	constexpr float SurroundPointRecheckIntervalSeconds = 0.25f;
	constexpr float StagingSurroundRecheckIntervalSeconds = 0.40f;
	constexpr float SurroundPointMoveThresholdCm = 65.f;
	constexpr float StagingArrivalThresholdCm = 30.f;
	constexpr float StagingFaceInterpSpeed = 8.f;

	float ComputeAcceptanceRadius(const AIFSoldierAIController& Controller, const AActor* GoalActor)
	{
		const AIFSoldierCharacter* const Soldier = Controller.GetSoldier();
		if (IsValid(Soldier) && IsValid(GoalActor) && GoalActor == Controller.GetCurrentTarget())
		{
			return Soldier->GetEffectiveAttackRange(GoalActor);
		}
		return Controller.GetMoveGoalAcceptanceRadius();
	}

	void FaceGoalActorSmooth(AIFSoldierCharacter& Soldier, const AActor& GoalActor, float DeltaSeconds)
	{
		FVector ToGoal = GoalActor.GetActorLocation() - Soldier.GetActorLocation();
		ToGoal.Z = 0.f;
		if (ToGoal.IsNearlyZero())
		{
			return;
		}

		const FRotator CurrentRot = Soldier.GetActorRotation();
		const FRotator DesiredRot(CurrentRot.Pitch, ToGoal.Rotation().Yaw, CurrentRot.Roll);
		Soldier.SetActorRotation(FMath::RInterpTo(CurrentRot, DesiredRot, DeltaSeconds, StagingFaceInterpSpeed));
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

EBTNodeResult::Type UIFBTTask_SoldierMove::IssueMove(AIFSoldierAIController& Controller, FBTTask_SoldierMoveMemory& Memory, const FVector* PrecomputedApproachPoint)
{
	AActor* const GoalActor = Controller.GetMoveGoalActor();
	const FVector GoalLocation = Controller.GetMoveGoalLocation();
	float RequestAcceptanceRadius = ComputeAcceptanceRadius(Controller, GoalActor);

	Memory.TrackedGoalActor = GoalActor;
	Memory.bTrackedActorGoal = IsValid(GoalActor);
	Memory.TrackedGoalLocation = GoalLocation;
	Memory.LastProgressLocation = IsValid(Controller.GetPawn()) ? Controller.GetPawn()->GetActorLocation() : FVector::ZeroVector;
	Memory.StallElapsedSeconds = 0.f;
	Memory.SurroundCheckElapsedSeconds = 0.f;
	Memory.bUsingPointApproachForActor = false;
	Memory.bHoldingAtStagingPoint = false;

	FAIMoveRequest MoveReq;
	if (IsValid(GoalActor))
	{
		const AIFSoldierCharacter* const Soldier = Controller.GetSoldier();
		if (const AIFStructure* const GoalStructure = Cast<AIFStructure>(GoalActor))
		{
			const FVector ApproachPoint = PrecomputedApproachPoint
				? *PrecomputedApproachPoint
				: (IsValid(Soldier) ? Soldier->GetCombatApproachPoint(GoalStructure) : GoalStructure->GetActorLocation());
			MoveReq.SetGoalLocation(ApproachPoint);
			MoveReq.SetProjectGoalLocation(true);
			Memory.TrackedGoalLocation = ApproachPoint;
			Memory.bUsingPointApproachForActor = true;
			RequestAcceptanceRadius = 25.f;
		}
		else if (IsValid(Soldier) && GoalActor == Controller.GetCurrentTarget())
		{
			const FVector SurroundPoint = PrecomputedApproachPoint
				? *PrecomputedApproachPoint
				: Soldier->GetCombatApproachPoint(GoalActor);
			if (FVector::DistSquared2D(SurroundPoint, GoalActor->GetActorLocation()) > FMath::Square(20.f))
			{
				MoveReq.SetGoalLocation(SurroundPoint);
				MoveReq.SetProjectGoalLocation(true);
				Memory.TrackedGoalLocation = SurroundPoint;
				Memory.bUsingPointApproachForActor = true;
				RequestAcceptanceRadius = 25.f;
			}
			else
			{
				MoveReq.SetGoalActor(GoalActor);
			}
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

	MoveReq.SetAcceptanceRadius(RequestAcceptanceRadius);
	MoveReq.SetUsePathfinding(true);
	MoveReq.SetAllowPartialPath(true);
	MoveReq.SetCanStrafe(false);
	// GetEffectiveAttackRange already adds SelfRadius + TargetRadius, so disable UE radius expansion to prevent double-counting.
	MoveReq.SetReachTestIncludesAgentRadius(false);
	MoveReq.SetReachTestIncludesGoalRadius(false);

	const FPathFollowingRequestResult MoveResult = Controller.MoveTo(MoveReq);

	switch (MoveResult.Code)
	{
	case EPathFollowingRequestResult::AlreadyAtGoal:
	{
		const AIFSoldierCharacter* const Soldier = Controller.GetSoldier();
		if (Memory.bUsingPointApproachForActor && IsValid(Soldier) && IsValid(GoalActor)
			&& GoalActor == Controller.GetCurrentTarget()
			&& !Soldier->IsTargetInMeleeReach(GoalActor, false))
		{
			// Outer-ring staging point reached while still out of melee reach: hold in place instead of failing into MeleeAttack.
			Memory.bHoldingAtStagingPoint = true;
			return EBTNodeResult::InProgress;
		}
		return EBTNodeResult::Succeeded;
	}
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
	Memory->SurroundCheckElapsedSeconds = 0.f;
	Memory->bTrackedActorGoal = false;
	Memory->bUsingPointApproachForActor = false;
	Memory->bHoldingAtStagingPoint = false;

	AIFSoldierAIController* const Controller = Cast<AIFSoldierAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(Controller))
	{
		return EBTNodeResult::Failed;
	}

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

	Memory->RepathCooldownSeconds = FMath::Max(0.f, Memory->RepathCooldownSeconds - DeltaSeconds);
	if (!Memory->bHoldingAtStagingPoint)
	{
		Memory->ElapsedSeconds += DeltaSeconds;
		if (Memory->ElapsedSeconds > MaxMoveSeconds)
		{
			Controller->StopMovement();
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}
	}

	AIFSoldierCharacter* const Soldier = Controller->GetSoldier();
	const AActor* const CurrentGoalActor = Controller->GetMoveGoalActor();
	const FVector CurrentGoalLocation = Controller->GetMoveGoalLocation();
	const bool bGoalActorChanged = (CurrentGoalActor != Memory->TrackedGoalActor.Get())
		|| (IsValid(CurrentGoalActor) != Memory->bTrackedActorGoal);
	const bool bStaticPointMoved = !IsValid(CurrentGoalActor)
		&& FVector::DistSquared2D(CurrentGoalLocation, Memory->TrackedGoalLocation) > FMath::Square(15.f);

	bool bSurroundPointMoved = false;
	FVector LiveApproachPoint = FVector::ZeroVector;
	if (IsValid(CurrentGoalActor) && Memory->bUsingPointApproachForActor && IsValid(Soldier))
	{
		const float RecheckInterval = Memory->bHoldingAtStagingPoint
			? StagingSurroundRecheckIntervalSeconds
			: SurroundPointRecheckIntervalSeconds;
		Memory->SurroundCheckElapsedSeconds += DeltaSeconds;
		if (Memory->SurroundCheckElapsedSeconds >= RecheckInterval)
		{
			Memory->SurroundCheckElapsedSeconds = 0.f;
			LiveApproachPoint = Soldier->GetCombatApproachPoint(CurrentGoalActor);
			bSurroundPointMoved = FVector::DistSquared2D(LiveApproachPoint, Memory->TrackedGoalLocation)
				> FMath::Square(SurroundPointMoveThresholdCm);
		}
	}

	const bool bActivelyTrackingActor = (Controller->GetMoveStatus() == EPathFollowingStatus::Moving)
		&& IsValid(CurrentGoalActor)
		&& !Memory->bUsingPointApproachForActor
		&& !bGoalActorChanged;

	if (!bActivelyTrackingActor && (bGoalActorChanged || bStaticPointMoved || bSurroundPointMoved))
	{
		if (bGoalActorChanged)
		{
			Memory->ElapsedSeconds = 0.f;
		}
		const FVector* const PrecomputedPoint = (!bGoalActorChanged && bSurroundPointMoved) ? &LiveApproachPoint : nullptr;
		const EBTNodeResult::Type IssueResult = IssueMove(*Controller, *Memory, PrecomputedPoint);

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

	// Outer-ring surround staging: stay poised at the staging point facing the target until an inner slot opens.
	if (IsValid(CurrentGoalActor) && IsValid(Soldier) && Memory->bUsingPointApproachForActor
		&& CurrentGoalActor == Controller->GetCurrentTarget())
	{
		const bool bAtStagingPoint = FVector::DistSquared2D(PawnLocation, Memory->TrackedGoalLocation)
			<= FMath::Square(StagingArrivalThresholdCm);
		if (Memory->bHoldingAtStagingPoint || (bAtStagingPoint && Controller->GetMoveStatus() == EPathFollowingStatus::Idle))
		{
			if (!Memory->bHoldingAtStagingPoint)
			{
				Memory->bHoldingAtStagingPoint = true;
				Controller->StopMovement();
			}
			Memory->StallElapsedSeconds = 0.f;
			FaceGoalActorSmooth(*Soldier, *CurrentGoalActor, DeltaSeconds);
			return;
		}
	}

	if (IsValid(CurrentGoalActor))
	{
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
				FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
				return;
			}
		}
	}

	// Dynamic Recast tile rebuilds or partial paths drop PathFollowingComponent to Idle early.
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
				if (RetryResult == EBTNodeResult::InProgress || Memory->RepathRetryCount < MaxDynamicRepathRetries)
				{
					return;
				}
			}
			else
			{
				return;
			}
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
		Controller->StopMovement();

		if (TaskResult == EBTNodeResult::Succeeded)
		{
			Controller->RefreshDecision();
		}
	}
}
