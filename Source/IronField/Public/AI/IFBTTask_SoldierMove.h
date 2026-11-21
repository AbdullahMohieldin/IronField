#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "IFBTTask_SoldierMove.generated.h"

class AActor;
class AIFSoldierAIController;

/** Per-execution state, kept in NodeMemory because BT nodes are shared across pawns. */
struct FBTTask_SoldierMoveMemory
{
	TWeakObjectPtr<AIFSoldierAIController> Controller;
	TWeakObjectPtr<AActor> TrackedGoalActor;
	FVector TrackedGoalLocation = FVector::ZeroVector;
	FVector LastProgressLocation = FVector::ZeroVector;
	float ElapsedSeconds = 0.f;
	float StallElapsedSeconds = 0.f;
	bool bTrackedActorGoal = false;
};

/**
 * Drives the controller's current move goal (actor or static point) and finishes on
 * arrival, on an early-ended path, or on timeout. Retargets when the decision changes
 * the goal and always stops movement when it finishes so melee wind-ups do not slide.
 */
UCLASS()
class IRONFIELD_API UIFBTTask_SoldierMove : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UIFBTTask_SoldierMove();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override;

protected:
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;

	UPROPERTY(EditAnywhere, Category = "Soldier", meta = (ClampMin = "1.0"))
	float MaxMoveSeconds = 20.f;

	UPROPERTY(EditAnywhere, Category = "Soldier", meta = (ClampMin = "0.3"))
	float StallTimeoutSeconds = 1.85f;

	UPROPERTY(EditAnywhere, Category = "Soldier", meta = (ClampMin = "5.0"))
	float MinStallProgressDistance = 25.f;

private:
	// Issues one request for the controller's current goal and records what was requested.
	EBTNodeResult::Type IssueMove(AIFSoldierAIController& Controller, FBTTask_SoldierMoveMemory& Memory);
};
