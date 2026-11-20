#include "AI/IFBTService_SoldierStatus.h"

#include "AI/IFSoldierAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"

UIFBTService_SoldierStatus::UIFBTService_SoldierStatus()
{
	NodeName = TEXT("IF Soldier Status");
	Interval = 0.10f;
	RandomDeviation = 0.02f;

	TargetActorKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UIFBTService_SoldierStatus, TargetActorKey), AActor::StaticClass());
	AnchorLocationKey.AddVectorFilter(this,
		GET_MEMBER_NAME_CHECKED(UIFBTService_SoldierStatus, AnchorLocationKey));
}

void UIFBTService_SoldierStatus::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	if (UBlackboardData* const BlackboardAsset = GetBlackboardAsset())
	{
		if (TargetActorKey.NeedsResolving())
		{
			TargetActorKey.ResolveSelectedKey(*BlackboardAsset);
		}
		if (AnchorLocationKey.NeedsResolving())
		{
			AnchorLocationKey.ResolveSelectedKey(*BlackboardAsset);
		}
	}
}

void UIFBTService_SoldierStatus::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	AIFSoldierAIController* const Controller = Cast<AIFSoldierAIController>(OwnerComp.GetAIOwner());
	if (!IsValid(Controller))
	{
		return;
	}

	// Authoritative decision refresh; idempotent, so the extra calls from perception
	// and orders simply collapse into no-ops.
	Controller->RefreshDecision();

	UBlackboardComponent* const Blackboard = OwnerComp.GetBlackboardComponent();
	if (!IsValid(Blackboard))
	{
		return;
	}

	if (TargetActorKey.IsSet())
	{
		Blackboard->SetValue<UBlackboardKeyType_Object>(
			TargetActorKey.GetSelectedKeyID(), Controller->GetCurrentTarget());
	}

	if (AnchorLocationKey.IsSet())
	{
		Blackboard->SetValue<UBlackboardKeyType_Vector>(
			AnchorLocationKey.GetSelectedKeyID(), Controller->GetAnchorLocation());
	}
}
