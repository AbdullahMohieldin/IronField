#include "AI/IFBTTask_MeleeAttack.h"

#include "AI/IFSoldierAIController.h"
#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFCombatComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	FVector GetTargetFacingDirection2D(const AIFSoldierCharacter& Soldier, const AActor& Target)
	{
		if (const AIFStructure* const TargetStructure = Cast<AIFStructure>(&Target))
		{
			FVector ToTarget = TargetStructure->GetClosestSurfacePoint2D(Soldier.GetActorLocation()) - Soldier.GetActorLocation();
			ToTarget.Z = 0.f;
			if (ToTarget.SizeSquared2D() < KINDA_SMALL_NUMBER)
			{
				ToTarget = TargetStructure->GetActorLocation() - Soldier.GetActorLocation();
				ToTarget.Z = 0.f;
			}
			return ToTarget;
		}

		FVector ToTarget = Soldier.GetCombatApproachPoint(&Target) - Soldier.GetActorLocation();
		ToTarget.Z = 0.f;
		return ToTarget;
	}

	float GetYawErrorDegrees(const AIFSoldierCharacter& Soldier, const AActor& Target)
	{
		const FVector ToTarget = GetTargetFacingDirection2D(Soldier, Target);
		if (ToTarget.IsNearlyZero())
		{
			return 0.f;
		}

		const float DesiredYaw = ToTarget.Rotation().Yaw;
		return FMath::Abs(FRotator::NormalizeAxis(DesiredYaw - Soldier.GetActorRotation().Yaw));
	}

	float FaceTargetSmooth(AIFSoldierCharacter& Soldier, const AActor& Target, float DeltaSeconds, float InterpSpeed)
	{
		const FVector ToTarget = GetTargetFacingDirection2D(Soldier, Target);
		if (ToTarget.IsNearlyZero())
		{
			return 0.f;
		}

		const FRotator CurrentRot = Soldier.GetActorRotation();
		const FRotator DesiredRot(CurrentRot.Pitch, ToTarget.Rotation().Yaw, CurrentRot.Roll);
		const FRotator NewRot = FMath::RInterpTo(CurrentRot, DesiredRot, DeltaSeconds, InterpSpeed);
		Soldier.SetActorRotation(NewRot);

		return FMath::Abs(FRotator::NormalizeAxis(DesiredRot.Yaw - NewRot.Yaw));
	}

	bool IsTargetValidAndInRange(const AIFSoldierCharacter& Soldier, const AActor* Target)
	{
		return AIFSoldierAIController::IsValidCombatTarget(&Soldier, Target)
			&& Soldier.IsTargetInMeleeReach(Target, true);
	}
}

UIFBTTask_MeleeAttack::UIFBTTask_MeleeAttack()
{
	NodeName = TEXT("IF Melee Attack");
	bNotifyTick = true;
	bNotifyTaskFinished = true;
}

uint16 UIFBTTask_MeleeAttack::GetInstanceMemorySize() const
{
	return static_cast<uint16>(sizeof(FBTTask_MeleeAttackMemory));
}

EBTNodeResult::Type UIFBTTask_MeleeAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTTask_MeleeAttackMemory* const Memory = CastInstanceNodeMemory<FBTTask_MeleeAttackMemory>(NodeMemory);
	Memory->Controller.Reset();
	Memory->Soldier.Reset();
	Memory->Phase = EIFMeleeAttackPhase::WaitingForStamina;
	Memory->ElapsedSeconds = 0.f;
	Memory->StaminaWaitElapsed = 0.f;
	Memory->TurnAlignElapsed = 0.f;
	Memory->BreatherRemaining = 0.f;

	AIFSoldierAIController* const Controller = Cast<AIFSoldierAIController>(OwnerComp.GetAIOwner());
	AIFSoldierCharacter* const Soldier = IsValid(Controller) ? Controller->GetSoldier() : nullptr;
	if (!IsValid(Soldier) || Soldier->IsDead())
	{
		return EBTNodeResult::Failed;
	}

	UIFCombatComponent* const Combat = Soldier->GetCombatComponent();
	if (!IsValid(Combat) || !Combat->IsCombatReady())
	{
		return EBTNodeResult::Failed;
	}

	AActor* const Target = Controller->GetCurrentTarget();
	if (!IsTargetValidAndInRange(*Soldier, Target))
	{
		return EBTNodeResult::Failed;
	}

	Combat->EnsureAIComboPlan();

	Memory->Controller = Controller;
	Memory->Soldier = Soldier;
	Memory->Phase = EIFMeleeAttackPhase::WaitingForStamina;

	if (Combat->HasEnoughStaminaForPlannedCombo()
		&& GetYawErrorDegrees(*Soldier, *Target) <= OpeningMaxYawErrorDegrees
		&& Combat->TryExecuteNextPlannedAIStep())
	{
		Memory->Phase = EIFMeleeAttackPhase::ExecutingCombo;
	}

	return EBTNodeResult::InProgress;
}

void UIFBTTask_MeleeAttack::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTTask_MeleeAttackMemory* const Memory = CastInstanceNodeMemory<FBTTask_MeleeAttackMemory>(NodeMemory);

	Memory->ElapsedSeconds += DeltaSeconds;
	if (Memory->ElapsedSeconds > MaxAttackSeconds)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AIFSoldierCharacter* const Soldier = Memory->Soldier.Get();
	AIFSoldierAIController* const Controller = Memory->Controller.Get();
	if (!IsValid(Soldier) || Soldier->IsDead() || !IsValid(Controller))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* const Target = Controller->GetCurrentTarget();
	UIFCombatComponent* const Combat = Soldier->GetCombatComponent();
	if (!IsValid(Combat))
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	switch (Memory->Phase)
	{
	case EIFMeleeAttackPhase::WaitingForStamina:
	{
		if (!IsTargetValidAndInRange(*Soldier, Target))
		{
			// Preserve the combo plan so the soldier keeps saving stamina while chasing.
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		const float YawError = FaceTargetSmooth(*Soldier, *Target, DeltaSeconds, TurnInterpSpeed);

		if (!Combat->HasEnoughStaminaForPlannedCombo())
		{
			Memory->StaminaWaitElapsed += DeltaSeconds;
		}

		// Timed-out stamina wait falls through so a low-stamina unit attempts an opener or resets its combo plan instead of deadlocking.
		const float MaxStaminaWait = Combat->GetAIMaxStaminaWaitSeconds();
		const bool bStaminaTimedOut = Memory->StaminaWaitElapsed >= MaxStaminaWait;
		if (!Combat->HasEnoughStaminaForPlannedCombo() && !bStaminaTimedOut)
		{
			return;
		}

		if (YawError > OpeningMaxYawErrorDegrees && Memory->TurnAlignElapsed < MaxTurnAlignSeconds)
		{
			Memory->TurnAlignElapsed += DeltaSeconds;
			return;
		}

		if (!Combat->IsIdle())
		{
			return;
		}

		Memory->TurnAlignElapsed = 0.f;
		if (Combat->TryExecuteNextPlannedAIStep())
		{
			Memory->Phase = EIFMeleeAttackPhase::ExecutingCombo;
			return;
		}

		if (bStaminaTimedOut)
		{
			Combat->ClearAIComboPlan();
		}
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	case EIFMeleeAttackPhase::ExecutingCombo:
	{
		if (Combat->IsAttacking())
		{
			return;
		}

		if (Combat->HasRemainingPlannedAISteps() && IsTargetValidAndInRange(*Soldier, Target))
		{
			const float YawError = FaceTargetSmooth(*Soldier, *Target, DeltaSeconds, TurnInterpSpeed);
			if (YawError > OpeningMaxYawErrorDegrees && Memory->TurnAlignElapsed < MaxTurnAlignSeconds)
			{
				Memory->TurnAlignElapsed += DeltaSeconds;
				return;
			}

			Memory->TurnAlignElapsed = 0.f;
			if (Combat->TryExecuteNextPlannedAIStep())
			{
				return;
			}
		}

		Combat->ClearAIComboPlan();
		Memory->Phase = EIFMeleeAttackPhase::Breather;
		Memory->BreatherRemaining = Combat->GetAIComboBreatherSeconds();
		if (Memory->BreatherRemaining <= 0.f)
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		return;
	}

	case EIFMeleeAttackPhase::Breather:
	{
		if (IsValid(Target))
		{
			const AIFBaseCharacter* const TargetCharacter = Cast<AIFBaseCharacter>(Target);
			if (!IsValid(TargetCharacter) || !TargetCharacter->IsDead())
			{
				FaceTargetSmooth(*Soldier, *Target, DeltaSeconds, TurnInterpSpeed);
			}
		}

		Memory->BreatherRemaining -= DeltaSeconds;
		if (Memory->BreatherRemaining <= 0.f)
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		return;
	}

	default:
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}
}

void UIFBTTask_MeleeAttack::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	if (TaskResult != EBTNodeResult::Succeeded)
	{
		const FBTTask_MeleeAttackMemory* const Memory = CastInstanceNodeMemory<FBTTask_MeleeAttackMemory>(NodeMemory);
		if (AIFSoldierCharacter* const Soldier = Memory->Soldier.Get())
		{
			if (UIFCombatComponent* const Combat = Soldier->GetCombatComponent();
				IsValid(Combat) && Combat->GetPlannedAIExecutedHits() > 0)
			{
				Combat->ClearAIComboPlan();
			}
		}
	}

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}
