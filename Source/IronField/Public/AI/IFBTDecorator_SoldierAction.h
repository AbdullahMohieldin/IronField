#pragma once

#include "CoreMinimal.h"
#include "AI/IFSoldierAITypes.h"
#include "BehaviorTree/BTDecorator.h"
#include "IFBTDecorator_SoldierAction.generated.h"

/**
 * Gates a branch on the controller's selected action. Reads the controller directly:
 * no blackboard key, no observer registration, no string lookup.
 */
UCLASS()
class IRONFIELD_API UIFBTDecorator_SoldierAction : public UBTDecorator
{
	GENERATED_BODY()

public:
	UIFBTDecorator_SoldierAction();

	virtual FString GetStaticDescription() const override;

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

	UPROPERTY(EditAnywhere, Category = "Soldier")
	TArray<EIFSoldierAction> AllowedActions;
};
