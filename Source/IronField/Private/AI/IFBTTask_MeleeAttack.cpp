#include "AI/IFBTTask_MeleeAttack.h"

#include "AI/IFSoldierAIController.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFMeleeCombatComponent.h"
#include "Combat/IFTeamTypes.h"
#include "GameFramework/Actor.h"

namespace
{
	float GetYawErrorDegrees(const AIFSoldierCharacter& Soldier, const AActor& Target)
	{
		FVector ToTarget = Target.GetActorLocation() - Soldier.GetActorLocation();
		ToTarget.Z = 0.f;
		if (ToTarget.IsNearlyZero())
		{
			return 0.f;
		}

		const float DesiredYaw = ToTarget.Rotation().Yaw;
		return FMath::Abs(FRotator::NormalizeAxis(DesiredYaw - Soldier.GetActorRotation().Yaw));
	}

	float FaceTargetSmooth(AIFSoldierCharacter& Soldier, const AActor& Target, float DeltaSeconds, float InterpSpeed)
	{
		FVector ToTarget = Target.GetActorLocation() - Soldier.GetActorLocation();
		ToTarget.Z = 0.f;
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
		if (!IsValid(Target) || Target->IsActorBeingDestroyed())
		{
			return false;
		}

		if (const AIFBaseCharacter* const TargetCharacter = Cast<AIFBaseCharacter>(Target))
		{
			if (TargetCharacter->IsDead())
			{
				return false;
			}
		}

		if (!IFTeamUtils::AreHostile(&Soldier, Target))
		{
			return false;
		}

		const float MaxEngagementRange = Soldier.GetMaxMeleeEngagementRange(Target);
		return FVector::DistSquared2D(Soldier.GetActorLocation(), Target->GetActorLocation())
			<= FMath::Square(MaxEngagementRange);
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

	const UIFMeleeCombatComponent* const Combat = Soldier->GetMeleeCombatComponent();
	if (!IsValid(Combat) || !Combat->IsCombatReady())
	{
		return EBTNodeResult::Failed;
	}

	AActor* const Target = Controller->GetCurrentTarget();
	if (!IsTargetValidAndInRange(*Soldier, Target))
	{
		return EBTNodeResult::Failed;
	}

	Soldier->EnsureMeleeComboPlan();

	Memory->Controller = Controller;
	Memory->Soldier = Soldier;
	Memory->Phase = EIFMeleeAttackPhase::WaitingForStamina;

	if (Soldier->HasEnoughStaminaForPlannedCombo()
		&& GetYawErrorDegrees(*Soldier, *Target) <= OpeningMaxYawErrorDegrees
		&& Soldier->TryExecuteNextPlannedMeleeStep())
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
	UIFMeleeCombatComponent* const Combat = Soldier->GetMeleeCombatComponent();

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

		if (!Soldier->HasEnoughStaminaForPlannedCombo())
		{
			Memory->StaminaWaitElapsed += DeltaSeconds;
		}

		const float MaxStaminaWait = IsValid(Combat) ? Combat->GetAIMaxStaminaWaitSeconds() : 3.5f;
		const bool bStaminaTimedOut = Memory->StaminaWaitElapsed >= MaxStaminaWait;
		if (!Soldier->HasEnoughStaminaForPlannedCombo() && !bStaminaTimedOut)
		{
			return;
		}

		if (YawError > OpeningMaxYawErrorDegrees && Memory->TurnAlignElapsed < MaxTurnAlignSeconds)
		{
			Memory->TurnAlignElapsed += DeltaSeconds;
			return;
		}

		if (!IsValid(Combat) || !Combat->IsIdle())
		{
			return;
		}

		Memory->TurnAlignElapsed = 0.f;
		if (Soldier->TryExecuteNextPlannedMeleeStep())
		{
			Memory->Phase = EIFMeleeAttackPhase::ExecutingCombo;
			return;
		}

		if (bStaminaTimedOut)
		{
			Soldier->ClearMeleeComboPlan();
		}
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	case EIFMeleeAttackPhase::ExecutingCombo:
	{
		if (Soldier->IsAttacking())
		{
			return;
		}

		if (Soldier->HasRemainingPlannedMeleeSteps() && IsTargetValidAndInRange(*Soldier, Target))
		{
			const float YawError = FaceTargetSmooth(*Soldier, *Target, DeltaSeconds, TurnInterpSpeed);
			if (YawError > OpeningMaxYawErrorDegrees && Memory->TurnAlignElapsed < MaxTurnAlignSeconds)
			{
				Memory->TurnAlignElapsed += DeltaSeconds;
				return;
			}

			Memory->TurnAlignElapsed = 0.f;
			if (Soldier->TryExecuteNextPlannedMeleeStep())
			{
				return;
			}
		}

		Soldier->ClearMeleeComboPlan();
		Memory->Phase = EIFMeleeAttackPhase::Breather;
		Memory->BreatherRemaining = IsValid(Combat) ? Combat->GetAIComboBreatherSeconds() : 0.f;
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

EBTNodeResult::Type UIFBTTask_MeleeAttack::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const FBTTask_MeleeAttackMemory* const Memory = CastInstanceNodeMemory<FBTTask_MeleeAttackMemory>(NodeMemory);
	if (AIFSoldierCharacter* const Soldier = Memory->Soldier.Get())
	{
		const UIFMeleeCombatComponent* const Combat = Soldier->GetMeleeCombatComponent();
		if (IsValid(Combat) && Combat->GetPlannedAIExecutedHits() > 0)
		{
			Soldier->ClearMeleeComboPlan();
		}
	}

	return Super::AbortTask(OwnerComp, NodeMemory);
}

void UIFBTTask_MeleeAttack::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	if (TaskResult != EBTNodeResult::Succeeded)
	{
		const FBTTask_MeleeAttackMemory* const Memory = CastInstanceNodeMemory<FBTTask_MeleeAttackMemory>(NodeMemory);
		if (AIFSoldierCharacter* const Soldier = Memory->Soldier.Get())
		{
			const UIFMeleeCombatComponent* const Combat = Soldier->GetMeleeCombatComponent();
			if (IsValid(Combat) && Combat->GetPlannedAIExecutedHits() > 0)
			{
				Soldier->ClearMeleeComboPlan();
			}
		}
	}

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}
