#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "IFBTService_SoldierStatus.generated.h"

/**
 * Heartbeat for the soldier tree: refreshes the controller's decision (target memory,
 * leash, moving advance/follow goals) and mirrors debug keys into the blackboard.
 */
UCLASS()
class IRONFIELD_API UIFBTService_SoldierStatus : public UBTService
{
	GENERATED_BODY()

public:
	UIFBTService_SoldierStatus();

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "IronField|AI|Blackboard")
	FBlackboardKeySelector TargetActorKey;

	UPROPERTY(EditAnywhere, Category = "IronField|AI|Blackboard")
	FBlackboardKeySelector AnchorLocationKey;
};
