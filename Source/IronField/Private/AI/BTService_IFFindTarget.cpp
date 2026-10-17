#include "AI/BTService_IFFindTarget.h"

#include "AI/IFBTUtils.h"
#include "AI/IFEnemyAIData.h"
#include "AI/IFEnemyController.h"
#include "AI/IFTargetScoring.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/IFEnemyCharacter.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Core/IFLog.h"
#include "Stats/IFHealthComponent.h"

namespace
{
	bool IsUsableTarget(AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return false;
		}

		if (const UIFHealthComponent* const Health = Actor->FindComponentByClass<UIFHealthComponent>())
		{
			return !Health->IsDead();
		}

		return true;
	}
}

UBTService_IFFindTarget::UBTService_IFFindTarget()
{
	NodeName = TEXT("Find Target");
	Interval = 0.25f;
	RandomDeviation = 0.05f;
	bNotifyTick = true;
	bCallTickOnSearchStart = true;

	TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_IFFindTarget, TargetActorKey), AActor::StaticClass());
}

void UBTService_IFFindTarget::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
	ChooseTarget(OwnerComp);
}

void UBTService_IFFindTarget::ChooseTarget(UBehaviorTreeComponent& OwnerComp) const
{
	const APawn* const Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	UBlackboardComponent* const BB = OwnerComp.GetBlackboardComponent();
	const UWorld* const World = OwnerComp.GetWorld();
	const UIFActorRegistrySubsystem* const Registry = World ? World->GetSubsystem<UIFActorRegistrySubsystem>() : nullptr;
	if (!Pawn || !BB || !Registry)
	{
		return;
	}

	AIFEnemyController* const Controller = Cast<AIFEnemyController>(OwnerComp.GetAIOwner());
	const UIFEnemyAIData* const AIData = Controller ? Controller->GetAIData() : GetDefault<UIFEnemyAIData>();
	if (!AIData)
	{
		return;
	}

	AActor* const Player = Registry->GetActor(IFRegistryRoles::Player);
	AActor* const Stronghold = Registry->GetActor(IFRegistryRoles::Stronghold);
	AActor* const Current = GetBlackboardTargetActor(OwnerComp, TargetActorKey);

	const float DetectionRange = AIData->PlayerDetectionRange;
	const bool bPlayerOk = IsUsableTarget(Player)
		&& FVector::DistSquared(Pawn->GetActorLocation(), Player->GetActorLocation()) <= FMath::Square(DetectionRange);
	const bool bStrongholdOk = IsUsableTarget(Stronghold);

	AIFEnemyCharacter* const Enemy = Controller ? Cast<AIFEnemyCharacter>(Controller->GetPawn()) : nullptr;
	const float Aggression = Enemy ? Enemy->GetAggression() : 0.5f;
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	FIFTargetScoreInput Input;
	Input.bPlayerValid = bPlayerOk;
	Input.bStrongholdValid = bStrongholdOk;
	Input.bCurrentValid = IsUsableTarget(Current);
	Input.PlayerDistance = (bPlayerOk && Player) ? FVector::Dist(Pawn->GetActorLocation(), Player->GetActorLocation()) : 0.f;
	Input.StrongholdDistance = (bStrongholdOk && Stronghold) ? FVector::Dist(Pawn->GetActorLocation(), Stronghold->GetActorLocation()) : 0.f;
	Input.Aggression = Aggression;
	Input.bHasGrudge = Enemy && (Now - Enemy->GetLastPlayerHitTime() < AIData->RetaliationSeconds);
	Input.TimeSinceLastSwitch = Enemy ? (Now - Enemy->GetLastTargetSwitchTime()) : 0.f;

	if (Current == Player)
	{
		Input.CurrentTarget = EIFTargetCandidate::Player;
	}
	else if (Current == Stronghold)
	{
		Input.CurrentTarget = EIFTargetCandidate::Stronghold;
	}
	else
	{
		Input.CurrentTarget = EIFTargetCandidate::None;
	}

	Input.DistanceFalloffRange = AIData->DistanceFalloffRange;
	Input.PlayerWeightBase = AIData->PlayerWeightBase;
	Input.StrongholdWeightBase = AIData->StrongholdWeightBase;
	Input.RetaliationBonus = AIData->RetaliationBonus;
	Input.CommitScoreBonus = AIData->CommitScoreBonus;
	Input.SwitchThreshold = AIData->SwitchThreshold;
	Input.CommitLockSeconds = AIData->CommitLockSeconds;

	const FIFTargetScoreResult Result = FIFTargetScoring::ScoreTarget(Input);

	AActor* Desired = Current;
	switch (Result.DesiredTarget)
	{
	case EIFTargetCandidate::Player:
		Desired = Player;
		break;
	case EIFTargetCandidate::Stronghold:
		Desired = Stronghold;
		break;
	case EIFTargetCandidate::None:
	default:
		Desired = nullptr;
		break;
	}

	if (Desired != Current)
	{
		BB->SetValueAsObject(TargetActorKey.SelectedKeyName, Desired);
		if (Enemy)
		{
			Enemy->NotifyTargetSwitched();
		}
		UE_LOG(LogIronField, Log, TEXT("[IF-AI] %s retarget %s -> %s"), *GetNameSafe(Pawn),
			*GetNameSafe(Current), *GetNameSafe(Desired));
	}

	if (Controller)
	{
		Controller->UpdateFocusOnTarget(Desired);
	}
}
