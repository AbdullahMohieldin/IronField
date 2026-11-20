#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "IFBTTask_MeleeAttack.generated.h"

class AIFSoldierAIController;
class AIFSoldierCharacter;

enum class EIFMeleeAttackPhase : uint8
{
	WaitingForStamina,
	ExecutingCombo,
	Breather
};

struct FBTTask_MeleeAttackMemory
{
	TWeakObjectPtr<AIFSoldierAIController> Controller;
	TWeakObjectPtr<AIFSoldierCharacter> Soldier;
	EIFMeleeAttackPhase Phase = EIFMeleeAttackPhase::WaitingForStamina;
	float ElapsedSeconds = 0.f;
	float StaminaWaitElapsed = 0.f;
	float TurnAlignElapsed = 0.f;
	float BreatherRemaining = 0.f;
};

/**
 * Plans a melee combo on the controller's current target, waits for required stamina while
 * smoothly facing the target, executes the planned steps, then holds a data-driven breather.
 * Range and facing are validated here, not in BP.
 */
UCLASS()
class IRONFIELD_API UIFBTTask_MeleeAttack : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UIFBTTask_MeleeAttack();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;

protected:
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;

	UPROPERTY(EditAnywhere, Category = "Soldier", meta = (ClampMin = "0.1"))
	float MaxAttackSeconds = 10.f;

	UPROPERTY(EditAnywhere, Category = "Soldier", meta = (ClampMin = "0.1"))
	float TurnInterpSpeed = 10.f;

	UPROPERTY(EditAnywhere, Category = "Soldier", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float OpeningMaxYawErrorDegrees = 30.f;

	UPROPERTY(EditAnywhere, Category = "Soldier", meta = (ClampMin = "0.0"))
	float MaxTurnAlignSeconds = 0.45f;
};
