#include "AI/BTService_IFFindTarget.h"

#include "AI/IFBTUtils.h"
#include "AI/IFEnemyAIData.h"
#include "AI/IFEnemyController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Character/IFEnemyCharacter.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Core/IFLog.h"
#include "Stats/IFHealthComponent.h"

namespace
{
	// Targeting curve: distance falloff plus the aggression-shaped candidate weights.
	constexpr float DistanceFalloffRange = 800.f;
	constexpr float PlayerWeightBase = 0.5f;
	constexpr float StrongholdWeightBase = 1.5f;

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

	/** Soft distance desire: 1.0 in your face, fading with range. Same curve for both targets. */
	float DistanceDesire(float Dist)
	{
		return 1.f / (1.f + Dist / DistanceFalloffRange);
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

	const AIFEnemyController* const Controller = Cast<AIFEnemyController>(OwnerComp.GetAIOwner());
	const UIFEnemyAIData* const AIData = Controller ? Controller->GetAIData() : GetDefault<UIFEnemyAIData>();
	if (!AIData)
	{
		return;
	}
	const float DetectionRange = AIData->PlayerDetectionRange;

	AActor* const Player = Registry->GetActor(IFRegistryRoles::Player);
	AActor* const Stronghold = Registry->GetActor(IFRegistryRoles::Stronghold);
	AActor* const Current = GetBlackboardTargetActor(OwnerComp, TargetActorKey);

	const bool bPlayerOk = IsUsableTarget(Player)
		&& FVector::DistSquared(Pawn->GetActorLocation(), Player->GetActorLocation()) <= FMath::Square(DetectionRange);
	const bool bStrongholdOk = IsUsableTarget(Stronghold);

	AIFEnemyCharacter* const Enemy = OwnerComp.GetAIOwner() ? Cast<AIFEnemyCharacter>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
	const float Aggression = Enemy ? Enemy->GetAggression() : 0.5f;
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// One scorer for both candidates: distance desire shaped by personality.
	float PlayerScore = 0.f;
	const bool bGrudge = Enemy && (Now - Enemy->GetLastPlayerHitTime() < AIData->RetaliationSeconds);
	if (bPlayerOk)
	{
		PlayerScore = DistanceDesire(FVector::Dist(Pawn->GetActorLocation(), Player->GetActorLocation())) * (PlayerWeightBase + Aggression);

		// Grudge: a recent player hit multiplies player desire and breaks commitment.
		if (bGrudge)
		{
			PlayerScore *= AIData->RetaliationBonus;
		}
	}

	float StrongholdScore = 0.f;
	if (bStrongholdOk)
	{
		StrongholdScore = DistanceDesire(FVector::Dist(Pawn->GetActorLocation(), Stronghold->GetActorLocation())) * (StrongholdWeightBase - Aggression);
	}

	// Champion = highest raw score; nothing valid → null (clears dead targets).
	AActor* Champion = nullptr;
	float ChampionScore = 0.f;
	if (PlayerScore > ChampionScore)
	{
		Champion = Player;
		ChampionScore = PlayerScore;
	}
	if (StrongholdScore > ChampionScore)
	{
		Champion = Stronghold;
		ChampionScore = StrongholdScore;
	}

	// Commitment: the locked target scores a bonus, fresh switches are time-gated,
	// and a challenger must win by a clear margin. Invalid targets never lock.
	AActor* Desired = Current;
	if (Champion != Current)
	{
		const bool bCurrentValid = IsUsableTarget(Current);
		float CurrentScore = 0.f;
		if (bCurrentValid)
		{
			CurrentScore = (Current == Player ? PlayerScore : StrongholdScore) * AIData->CommitScoreBonus;
		}

		const bool bLocked = Enemy && bCurrentValid && !bGrudge && (Now - Enemy->GetLastTargetSwitchTime() < AIData->CommitLockSeconds);
		if (!bLocked && ChampionScore > CurrentScore * AIData->SwitchThreshold)
		{
			Desired = Champion;
		}
	}

	// Both candidates invalid: clear a dead locked target instead of keeping it.
	if (!Champion && !IsUsableTarget(Current))
	{
		Desired = nullptr;
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
}
