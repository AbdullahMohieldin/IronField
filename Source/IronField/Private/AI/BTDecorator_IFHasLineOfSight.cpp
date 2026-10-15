#include "AI/BTDecorator_IFHasLineOfSight.h"

#include "AIController.h"
#include "AI/IFBTUtils.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Engine/World.h"

namespace
{
	struct FIFLineOfSightMemory : public FIFBTConditionMemory
	{
		float TimeUntilNextTrace = 0.f;
	};
}

UBTDecorator_IFHasLineOfSight::UBTDecorator_IFHasLineOfSight()
{
	NodeName = TEXT("Has Line Of Sight");
	bNotifyTick = true;
	bAllowAbortLowerPri = true;
	bAllowAbortNone = true;
	FlowAbortMode = EBTFlowAbortMode::Both;

	TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_IFHasLineOfSight, TargetActorKey), AActor::StaticClass());
}

uint16 UBTDecorator_IFHasLineOfSight::GetInstanceMemorySize() const
{
	return sizeof(FIFLineOfSightMemory);
}

void UBTDecorator_IFHasLineOfSight::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	FIFLineOfSightMemory* const Memory = reinterpret_cast<FIFLineOfSightMemory*>(NodeMemory);
	if (Memory)
	{
		Memory->TimeUntilNextTrace -= DeltaSeconds;
		if (Memory->TimeUntilNextTrace > 0.f && Memory->bInitialized)
		{
			return;
		}
		Memory->TimeUntilNextTrace = 0.2f;
	}

	UpdateConditionDecoratorAbort(OwnerComp, this, NodeMemory, CalculateRawConditionValue(OwnerComp, NodeMemory));
}

bool UBTDecorator_IFHasLineOfSight::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const AAIController* const AIController = OwnerComp.GetAIOwner();
	const APawn* const ControlledPawn = AIController ? AIController->GetPawn() : nullptr;
	const AActor* const TargetActor = GetBlackboardTargetActor(OwnerComp, TargetActorKey);
	const UWorld* const World = OwnerComp.GetWorld();
	if (!ControlledPawn || !TargetActor || !World)
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IFHasLineOfSight), false, ControlledPawn);
	Params.AddIgnoredActor(TargetActor);

	const FVector HeightOffset(0.f, 0.f, TraceHeightOffset);
	const FVector Start = ControlledPawn->GetActorLocation() + HeightOffset;
	const FVector End = TargetActor->GetActorLocation() + HeightOffset;

	return !World->LineTraceSingleByChannel(Hit, Start, End, TraceChannel, Params);
}
