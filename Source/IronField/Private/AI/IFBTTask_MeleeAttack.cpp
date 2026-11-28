#include "AI/IFBTTask_MeleeAttack.h"

#include "AI/IFSoldierAIController.h"
#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFCombatDataAsset.h"
#include "GameFramework/Actor.h"
#include "Stats/IFStaminaComponent.h"

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

		// Face the target actor directly (never the surround ring slot, which can point away from the target).
		FVector ToTarget = Target.GetActorLocation() - Soldier.GetActorLocation();
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

	void BuildComboPlan(const UIFCombatComponent& Combat, const UIFStaminaComponent* Stamina, FIFPlannedAICombo& OutPlan)
	{
		const UIFCombatDataAsset* const CombatData = Combat.GetLoadedCombatData();
		if (OutPlan.bHasPlan || !Combat.IsCombatReady() || !IsValid(CombatData) || CombatData->ComboSteps.IsEmpty())
		{
			return;
		}

		const int32 StepCount = CombatData->ComboSteps.Num();
		OutPlan.Reset();
		OutPlan.bHasPlan = true;
		OutPlan.OpenerStepIndex = CombatData->bRandomizeAIComboOpener
			? FMath::RandRange(0, StepCount - 1)
			: 0;
		OutPlan.PlannedHitCount = 1;
		OutPlan.ExecutedHits = 0;

		const TArray<float>& Chances = CombatData->AIComboContinuationChances;
		for (int32 ChainIndex = 0; ChainIndex < Chances.Num() && OutPlan.PlannedHitCount < StepCount; ++ChainIndex)
		{
			const float Chance = Chances[ChainIndex];
			if (Chance > 0.f && (Chance >= 1.f || FMath::FRand() < Chance))
			{
				++OutPlan.PlannedHitCount;
			}
			else
			{
				break;
			}
		}

		float TotalStamina = 0.f;
		for (int32 Offset = 0; Offset < OutPlan.PlannedHitCount; ++Offset)
		{
			const int32 StepIndex = (OutPlan.OpenerStepIndex + Offset) % StepCount;
			TotalStamina += FMath::Max(0.f, CombatData->ComboSteps[StepIndex].StaminaCost);
		}

		if (IsValid(Stamina))
		{
			TotalStamina = FMath::Min(TotalStamina, Stamina->GetMaxStamina());
		}
		OutPlan.RequiredStamina = TotalStamina;
	}

	bool HasEnoughStaminaForPlan(const UIFStaminaComponent* Stamina, const FIFPlannedAICombo& Plan)
	{
		if (!IsValid(Stamina))
		{
			return true;
		}
		if (!Plan.bHasPlan)
		{
			return false;
		}
		return Stamina->GetCurrentStamina() + KINDA_SMALL_NUMBER >= Plan.RequiredStamina;
	}

	bool TryExecuteNextPlannedStep(UIFCombatComponent& Combat, FIFPlannedAICombo& Plan)
	{
		const UIFCombatDataAsset* const CombatData = Combat.GetLoadedCombatData();
		if (!Combat.IsCombatReady() || !IsValid(CombatData) || CombatData->ComboSteps.IsEmpty()
			|| !Plan.bHasPlan || Plan.ExecutedHits >= Plan.PlannedHitCount || !Combat.IsIdle())
		{
			return false;
		}

		const int32 StepIndex = (Plan.OpenerStepIndex + Plan.ExecutedHits) % CombatData->ComboSteps.Num();
		if (Combat.TryStartAttackStep(StepIndex))
		{
			++Plan.ExecutedHits;
			return true;
		}

		return false;
	}

	bool HasRemainingPlannedSteps(const FIFPlannedAICombo& Plan)
	{
		return Plan.bHasPlan && Plan.ExecutedHits < Plan.PlannedHitCount;
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
	Memory->ComboPlan.Reset();
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

	const UIFStaminaComponent* const Stamina = Soldier->GetStaminaComponent();
	BuildComboPlan(*Combat, Stamina, Memory->ComboPlan);

	Memory->Controller = Controller;
	Memory->Soldier = Soldier;
	Memory->Phase = EIFMeleeAttackPhase::WaitingForStamina;

	if (HasEnoughStaminaForPlan(Stamina, Memory->ComboPlan)
		&& GetYawErrorDegrees(*Soldier, *Target) <= OpeningMaxYawErrorDegrees
		&& TryExecuteNextPlannedStep(*Combat, Memory->ComboPlan))
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

	const UIFCombatDataAsset* const CombatData = Combat->GetLoadedCombatData();
	const UIFStaminaComponent* const Stamina = Soldier->GetStaminaComponent();

	switch (Memory->Phase)
	{
	case EIFMeleeAttackPhase::WaitingForStamina:
	{
		if (!IsTargetValidAndInRange(*Soldier, Target))
		{
			FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
			return;
		}

		const float YawError = FaceTargetSmooth(*Soldier, *Target, DeltaSeconds, TurnInterpSpeed);

		const bool bHasStamina = HasEnoughStaminaForPlan(Stamina, Memory->ComboPlan);
		if (!bHasStamina)
		{
			Memory->StaminaWaitElapsed += DeltaSeconds;
		}

		// Timed-out stamina wait falls through so a low-stamina unit attempts an opener or resets its combo plan instead of deadlocking.
		const float MaxStaminaWait = IsValid(CombatData) ? CombatData->MaxStaminaWaitSeconds : 3.5f;
		const bool bStaminaTimedOut = Memory->StaminaWaitElapsed >= MaxStaminaWait;
		if (!bHasStamina && !bStaminaTimedOut)
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
		if (TryExecuteNextPlannedStep(*Combat, Memory->ComboPlan))
		{
			Memory->Phase = EIFMeleeAttackPhase::ExecutingCombo;
			return;
		}

		if (bStaminaTimedOut)
		{
			Memory->ComboPlan.Reset();
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

		if (HasRemainingPlannedSteps(Memory->ComboPlan) && IsTargetValidAndInRange(*Soldier, Target))
		{
			const float YawError = FaceTargetSmooth(*Soldier, *Target, DeltaSeconds, TurnInterpSpeed);
			if (YawError > OpeningMaxYawErrorDegrees && Memory->TurnAlignElapsed < MaxTurnAlignSeconds)
			{
				Memory->TurnAlignElapsed += DeltaSeconds;
				return;
			}

			Memory->TurnAlignElapsed = 0.f;
			if (TryExecuteNextPlannedStep(*Combat, Memory->ComboPlan))
			{
				return;
			}
		}

		Memory->ComboPlan.Reset();
		Memory->Phase = EIFMeleeAttackPhase::Breather;
		Memory->BreatherRemaining = IsValid(CombatData) ? CombatData->AIComboBreatherSeconds : 0.f;
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
	FBTTask_MeleeAttackMemory* const Memory = CastInstanceNodeMemory<FBTTask_MeleeAttackMemory>(NodeMemory);
	Memory->ComboPlan.Reset();

	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}
