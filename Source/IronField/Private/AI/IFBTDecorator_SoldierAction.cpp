#include "AI/IFBTDecorator_SoldierAction.h"

#include "AI/IFSoldierAIController.h"

UIFBTDecorator_SoldierAction::UIFBTDecorator_SoldierAction()
{
	NodeName = TEXT("IF Soldier Action");
	FlowAbortMode = EBTFlowAbortMode::None;
}

bool UIFBTDecorator_SoldierAction::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const AIFSoldierAIController* const Controller = Cast<AIFSoldierAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(Controller))
	{
		return false;
	}

	return AllowedActions.Contains(Controller->GetCurrentAction());
}

FString UIFBTDecorator_SoldierAction::GetStaticDescription() const
{
	TArray<FString> Names;
	for (const EIFSoldierAction Action : AllowedActions)
	{
		if (const UEnum* const Enum = StaticEnum<EIFSoldierAction>())
		{
			Names.Add(Enum->GetNameStringByValue(static_cast<int64>(Action)));
		}
	}

	return FString::Printf(TEXT("Action is one of: %s"), *FString::Join(Names, TEXT(", ")));
}
