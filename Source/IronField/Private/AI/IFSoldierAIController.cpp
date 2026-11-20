#include "AI/IFSoldierAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Core/IFLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Damage.h"
#include "Perception/AISense_Sight.h"
#include "Stats/IFHealthComponent.h"
#include "TimerManager.h"

AIFSoldierAIController::AIFSoldierAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	SetPerceptionComponent(*Perception);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 1400.f;
	SightConfig->LoseSightRadius = 1800.f;
	SightConfig->PeripheralVisionAngleDegrees = 75.f;
	SightConfig->AutoSuccessRangeFromLastSeenLocation = 300.f;
	SightConfig->SetMaxAge(4.f);
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));
	DamageConfig->SetMaxAge(6.f);

	Perception->ConfigureSense(*SightConfig);
	Perception->ConfigureSense(*DamageConfig);
	Perception->SetDominantSense(SightConfig->GetSenseImplementation());
	Perception->OnTargetPerceptionUpdated.AddDynamic(this, &AIFSoldierAIController::HandleTargetPerceptionUpdated);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void AIFSoldierAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AIFSoldierCharacter* const Soldier = Cast<AIFSoldierCharacter>(InPawn);
	if (!IsValid(Soldier))
	{
		UE_LOG(LogIronField, Warning,
			TEXT("[IF-AI] %s possessed %s, which is not an AIFSoldierCharacter. AI left idle."),
			*GetNameSafe(this), *GetNameSafe(InPawn));
		return;
	}

	// Sync the controller's team so perception filters inside the sight sense work.
	SetGenericTeamId(Soldier->GetGenericTeamId());
	if (IsValid(Perception))
	{
		// AAIController::SetGenericTeamId does not notify the perception system.
		Perception->RequestStimuliListenerUpdate();
	}

	if (UIFHealthComponent* const Health = Soldier->GetHealthComponent())
	{
		Health->OnDamaged.AddUniqueDynamic(this, &AIFSoldierAIController::HandleOwnerDamaged);
		BoundHealthComponent = Health;
	}

	CurrentAction = EIFSoldierAction::None;
	DecisionSerial = 0;
	CurrentTarget.Reset();
	CurrentOrder.Reset();
	KnownHostiles.Reset();
	ClearMoveGoal();

	RequestBehaviorTree();
	RefreshDecision();
}

void AIFSoldierAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	bRestartQueued = false;

	if (UIFHealthComponent* const Health = BoundHealthComponent.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &AIFSoldierAIController::HandleOwnerDamaged);
	}
	BoundHealthComponent.Reset();

	CurrentTarget.Reset();
	KnownHostiles.Reset();
	ClearMoveGoal();

	// The controller may be reused by another pawn; drop the residency request.
	if (BehaviorTreeLoadHandle.IsValid())
	{
		BehaviorTreeLoadHandle->CancelHandle();
		BehaviorTreeLoadHandle.Reset();
	}
	LoadedBehaviorTree = nullptr;

	Super::OnUnPossess();
}

void AIFSoldierAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	bRestartQueued = false;

	if (UIFHealthComponent* const Health = BoundHealthComponent.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &AIFSoldierAIController::HandleOwnerDamaged);
	}
	BoundHealthComponent.Reset();

	if (BehaviorTreeLoadHandle.IsValid())
	{
		BehaviorTreeLoadHandle->CancelHandle();
		BehaviorTreeLoadHandle.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

ETeamAttitude::Type AIFSoldierAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
	return IFTeamUtils::SolveAttitude(GetGenericTeamId(), IFTeamUtils::GetActorTeamId(&Other));
}

AIFSoldierCharacter* AIFSoldierAIController::GetSoldier() const
{
	return Cast<AIFSoldierCharacter>(GetPawn());
}

// ---------------------------------------------------------------------------
// Behavior tree bootstrap
// ---------------------------------------------------------------------------

void AIFSoldierAIController::RequestBehaviorTree()
{
	if (BehaviorTreeAsset.IsNull())
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-AI] %s has no BehaviorTreeAsset assigned."), *GetNameSafe(GetPawn()));
		return;
	}

	if (UBehaviorTree* const Loaded = BehaviorTreeAsset.Get())
	{
		LoadedBehaviorTree = Loaded;
		HandleBehaviorTreeLoaded();
		return;
	}

	BehaviorTreeLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		BehaviorTreeAsset.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &AIFSoldierAIController::HandleBehaviorTreeLoaded));
}

void AIFSoldierAIController::HandleBehaviorTreeLoaded()
{
	LoadedBehaviorTree = BehaviorTreeAsset.Get();
	if (!IsValid(LoadedBehaviorTree))
	{
		UE_LOG(LogIronField, Error, TEXT("[IF-AI] %s failed to load its behavior tree."), *GetNameSafe(GetPawn()));
		return;
	}

	// The load may finish after the pawn was unpossessed or killed.
	if (!IsValid(GetPawn()))
	{
		return;
	}

	if (!RunBehaviorTree(LoadedBehaviorTree))
	{
		UE_LOG(LogIronField, Error, TEXT("[IF-AI] %s failed to start %s."),
			*GetNameSafe(GetPawn()), *GetNameSafe(LoadedBehaviorTree));
		return;
	}

	// Reflect the decision made during possession into the freshly started tree.
	RefreshDecision();
}

// ---------------------------------------------------------------------------
// Decision
// ---------------------------------------------------------------------------

void AIFSoldierAIController::RefreshDecision()
{
	AIFSoldierCharacter* const Soldier = GetSoldier();
	if (!IsValid(Soldier) || Soldier->IsDead())
	{
		ClearMoveGoal();
		SetCurrentAction(EIFSoldierAction::Idle);
		return;
	}

	PruneKnownHostiles();

	// ---- 1. Explicit orders (ignore the leash) ---------------------------
	// A completed or impossible order resets itself and breaks out of the switch
	// so Priority 2 evaluates the new situation in the same call.
	if (CurrentOrder.IsValid())
	{
		switch (CurrentOrder.Type)
		{
		case EIFSoldierOrderType::MoveToLocation:
		{
			CurrentTarget.Reset();
			const float Radius = FMath::Max(CurrentOrder.Radius, 10.f);
			if (FVector::DistSquared2D(Soldier->GetActorLocation(), CurrentOrder.Location) > FMath::Square(Radius))
			{
				bHasMoveGoal = true;
				MoveGoalActor.Reset();
				MoveGoalLocation = CurrentOrder.Location;
				MoveGoalAcceptanceRadius = Radius;
				SetCurrentAction(EIFSoldierAction::MoveTo);
				return;
			}

			// Arrived: the destination becomes the new post; combat is Priority 2's job.
			Soldier->SetAnchorLocation(CurrentOrder.Location, /*bRefreshControllerDecision*/ false);
			CurrentOrder.Reset();
			break;
		}

		case EIFSoldierOrderType::AttackTarget:
		{
			AActor* const Ordered = CurrentOrder.TargetActor.Get();
			if (!IsTargetValidForCombat(Ordered))
			{
				// Target gone: drop the order and let Priority 2 pick the next threat now.
				CurrentOrder.Reset();
				CurrentTarget.Reset();
				break;
			}

			CurrentTarget = Ordered;
			KnownHostiles.Add(Ordered, GetWorld()->GetTimeSeconds());
			UpdateMoveGoalForAction();
			SetCurrentAction(EIFSoldierAction::AttackTarget);
			return;
		}

		case EIFSoldierOrderType::FollowActor:
		{
			AActor* const Followed = CurrentOrder.TargetActor.Get();
			if (!IsValid(Followed))
			{
				CurrentOrder.Reset();
				CurrentTarget.Reset();
				break;
			}

			CurrentTarget.Reset();
			const float FollowDistance = FMath::Max(CurrentOrder.Radius, 50.f);
			if (FVector::DistSquared2D(Soldier->GetActorLocation(), Followed->GetActorLocation())
				> FMath::Square(FollowDistance))
			{
				bHasMoveGoal = true;
				MoveGoalActor = Followed;
				MoveGoalAcceptanceRadius = FollowDistance;
			}
			else
			{
				ClearMoveGoal();
			}
			SetCurrentAction(EIFSoldierAction::FollowActor);
			return;
		}

		default:
			break;
		}
	}

	// ---- 2. Priority 2: autonomous self-defence --------------------------
	AActor* const Target = AcquireBestTarget();
	CurrentTarget = Target;

	if (IsValid(Target))
	{
		if (Soldier->IsLeashedToAnchor())
		{
			const float LeashRadius = GetEngageRadius() * LeashHysteresisMultiplier;
			const bool bOutsideLeash = FVector::DistSquared2D(
				Soldier->GetAnchorLocation(), Target->GetActorLocation()) > FMath::Square(LeashRadius);

			if (bOutsideLeash)
			{
				// Kited past the post: drop the target and walk back.
				KnownHostiles.Remove(Target);
				CurrentTarget.Reset();
			}
		}

		if (IsValid(CurrentTarget.Get()))
		{
			UpdateMoveGoalForAction();
			SetCurrentAction(Soldier->IsLeashedToAnchor()
				? EIFSoldierAction::Defend
				: EIFSoldierAction::AttackTarget);
			return;
		}
	}

	// ---- 3. Advance ------------------------------------------------------
	if (Soldier->ShouldAdvanceWhenIdle())
	{
		AActor* AdvanceActor = nullptr;
		FVector AdvanceLocation = FVector::ZeroVector;

		if (Soldier->ShouldAdvanceTowardsPlayer())
		{
			AdvanceActor = UGameplayStatics::GetPlayerPawn(this, 0);
		}
		else if (AActor* const Configured = Soldier->GetAdvanceGoalActor())
		{
			AdvanceActor = Configured;
		}
		else
		{
			AdvanceLocation = Soldier->GetAdvanceGoalLocation();
		}

		if (IsValid(AdvanceActor) && AdvanceActor == Soldier)
		{
			AdvanceActor = nullptr;
		}

		const bool bHasGoal = IsValid(AdvanceActor) || !AdvanceLocation.IsNearlyZero();
		const FVector GoalLocation = IsValid(AdvanceActor) ? AdvanceActor->GetActorLocation() : AdvanceLocation;
		const float StopDistance = FMath::Max(Soldier->GetAdvanceStopDistance(), 0.f);

		if (bHasGoal && FVector::DistSquared2D(Soldier->GetActorLocation(), GoalLocation) > FMath::Square(StopDistance))
		{
			bHasMoveGoal = true;
			MoveGoalActor = AdvanceActor;
			MoveGoalLocation = GoalLocation;
			MoveGoalAcceptanceRadius = FMath::Max(StopDistance * 0.5f, 50.f);
			SetCurrentAction(EIFSoldierAction::Advance);
			return;
		}
	}

	// ---- 4. Return to post / idle ---------------------------------------
	ClearMoveGoal();
	if (Soldier->IsLeashedToAnchor()
		&& FVector::DistSquared2D(Soldier->GetActorLocation(), Soldier->GetAnchorLocation()) > FMath::Square(125.f))
	{
		bHasMoveGoal = true;
		MoveGoalActor.Reset();
		MoveGoalLocation = Soldier->GetAnchorLocation();
		MoveGoalAcceptanceRadius = 75.f;
		SetCurrentAction(EIFSoldierAction::HoldPosition);
		return;
	}

	SetCurrentAction(EIFSoldierAction::Idle);
}

void AIFSoldierAIController::SetCurrentAction(EIFSoldierAction NewAction)
{
	if (CurrentAction == NewAction)
	{
		return;
	}

	CurrentAction = NewAction;
	++DecisionSerial;
	OnActionChanged.Broadcast(NewAction);
	QueueTreeRestart();
}

void AIFSoldierAIController::QueueTreeRestart()
{
	if (bRestartQueued)
	{
		return;
	}

	bRestartQueued = true;
	// Next-tick deferral makes this safe even when the call came from a BT service tick.
	GetWorldTimerManager().SetTimerForNextTick(this, &AIFSoldierAIController::ApplyPendingTreeRestart);
}

void AIFSoldierAIController::ApplyPendingTreeRestart()
{
	bRestartQueued = false;

	UBehaviorTreeComponent* const BehaviorTree = Cast<UBehaviorTreeComponent>(GetBrainComponent());
	if (IsValid(BehaviorTree) && BehaviorTree->IsRunning())
	{
		BehaviorTree->RestartTree(EBTRestartMode::ForceReevaluateRootNode);
	}
}

// ---------------------------------------------------------------------------
// Targeting
// ---------------------------------------------------------------------------

AActor* AIFSoldierAIController::AcquireBestTarget() const
{
	const AIFSoldierCharacter* const Soldier = GetSoldier();
	if (!IsValid(Soldier))
	{
		return nullptr;
	}

	const float EngageRadius = GetEngageRadius();
	const float EngageRadiusSq = FMath::Square(EngageRadius);
	const FVector Reference = Soldier->IsLeashedToAnchor()
		? Soldier->GetAnchorLocation()
		: Soldier->GetActorLocation();
	const float Now = GetWorld()->GetTimeSeconds();

	// Keep the current target while it is valid, hostile and inside the hysteresis ring.
	if (AActor* const Existing = CurrentTarget.Get())
	{
		if (IsTargetValidForCombat(Existing))
		{
			const float HysteresisSq = FMath::Square(EngageRadius * LeashHysteresisMultiplier);
			if (FVector::DistSquared2D(Reference, Existing->GetActorLocation()) <= HysteresisSq)
			{
				return Existing;
			}
		}
	}

	AActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	TArray<AActor*> PerceivedActors;
	if (IsValid(Perception))
	{
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);
	}

	for (AActor* const Candidate : PerceivedActors)
	{
		if (!IsTargetValidForCombat(Candidate))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared2D(Reference, Candidate->GetActorLocation());
		if (DistSq <= EngageRadiusSq && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}

	// Damage-sensed targets the eyes have lost, still inside target memory.
	if (!IsValid(Best))
	{
		for (const TPair<TWeakObjectPtr<AActor>, float>& Pair : KnownHostiles)
		{
			AActor* const Candidate = Pair.Key.Get();
			if (!IsTargetValidForCombat(Candidate) || (Now - Pair.Value) > TargetMemorySeconds)
			{
				continue;
			}

			const float DistSq = FVector::DistSquared2D(Reference, Candidate->GetActorLocation());
			if (DistSq <= EngageRadiusSq && DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Candidate;
			}
		}
	}

	return Best;
}

bool AIFSoldierAIController::IsTargetValidForCombat(AActor* Target) const
{
	if (!IsValid(Target) || Target->IsActorBeingDestroyed())
	{
		return false;
	}

	if (const AIFBaseCharacter* const CharacterTarget = Cast<AIFBaseCharacter>(Target))
	{
		if (CharacterTarget->IsDead())
		{
			return false;
		}
	}

	return IFTeamUtils::AreHostile(GetPawn(), Target);
}

float AIFSoldierAIController::GetEngageRadius() const
{
	const AIFSoldierCharacter* const Soldier = GetSoldier();
	return IsValid(Soldier) ? Soldier->GetAutonomousEngageRadius() : 900.f;
}

void AIFSoldierAIController::PruneKnownHostiles()
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (auto It = KnownHostiles.CreateIterator(); It; ++It)
	{
		AActor* const Actor = It.Key().Get();
		if (!IsTargetValidForCombat(Actor) || (Now - It.Value()) > TargetMemorySeconds)
		{
			It.RemoveCurrent();
		}
	}
}

void AIFSoldierAIController::UpdateMoveGoalForAction()
{
	AIFSoldierCharacter* const Soldier = GetSoldier();
	AActor* const Target = CurrentTarget.Get();
	if (!IsValid(Soldier) || !IsValid(Target))
	{
		ClearMoveGoal();
		return;
	}

	// One distance space for movement and melee: centers, radii included once.
	const float EffectiveRange = Soldier->GetEffectiveAttackRange(Target);
	const float ChaseAcceptanceRadius = FMath::Max(25.f, EffectiveRange * 0.78f);
	const bool bAlreadyChasingTarget = bHasMoveGoal && MoveGoalActor.Get() == Target;
	const float InReachThreshold = bAlreadyChasingTarget ? ChaseAcceptanceRadius : (EffectiveRange * 0.9f);
	if (FVector::DistSquared2D(Soldier->GetActorLocation(), Target->GetActorLocation())
		<= FMath::Square(InReachThreshold))
	{
		// Already in reach: no move goal means UIFBTTask_SoldierMove succeeds instantly
		// and the attack task runs without shoving the pawn around.
		ClearMoveGoal();
		return;
	}

	bHasMoveGoal = true;
	MoveGoalActor = Target;
	MoveGoalLocation = Target->GetActorLocation();
	MoveGoalAcceptanceRadius = ChaseAcceptanceRadius;
}

void AIFSoldierAIController::ClearMoveGoal()
{
	bHasMoveGoal = false;
	MoveGoalActor.Reset();
	MoveGoalLocation = FVector::ZeroVector;
	MoveGoalAcceptanceRadius = 75.f;
}

void AIFSoldierAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!IsValid(Actor) || !IFTeamUtils::AreHostile(GetPawn(), Actor))
	{
		return;
	}

	// Sight loss is deliberately ignored: memory expiry is time-based in PruneKnownHostiles.
	if (Stimulus.WasSuccessfullySensed())
	{
		KnownHostiles.Add(Actor, GetWorld()->GetTimeSeconds());
		RefreshDecision();
	}
}

void AIFSoldierAIController::HandleOwnerDamaged(AActor* InstigatorActor, float DamageAmount)
{
	APawn* const SoldierPawn = GetPawn();
	if (!IsValid(InstigatorActor) || !IsValid(SoldierPawn)
		|| !IFTeamUtils::AreHostile(SoldierPawn, InstigatorActor))
	{
		return;
	}

	KnownHostiles.Add(InstigatorActor, GetWorld()->GetTimeSeconds());

	// Health applies damage directly; without this call no listener (including this one)
	// would ever receive a UAISense_Damage stimulus.
	UAISense_Damage::ReportDamageEvent(
		this,
		SoldierPawn,
		InstigatorActor,
		DamageAmount,
		InstigatorActor->GetActorLocation(),
		SoldierPawn->GetActorLocation());

	RefreshDecision();
}

FVector AIFSoldierAIController::GetAnchorLocation() const
{
	const AIFSoldierCharacter* const Soldier = GetSoldier();
	return IsValid(Soldier) ? Soldier->GetAnchorLocation() : FVector::ZeroVector;
}

// ---------------------------------------------------------------------------
// Orders (Part 08 surface)
// ---------------------------------------------------------------------------

bool AIFSoldierAIController::IssueMoveToLocation(const FVector& Location, float AcceptanceRadius)
{
	if (!IsValid(GetSoldier()))
	{
		return false;
	}

	CurrentOrder.Type = EIFSoldierOrderType::MoveToLocation;
	CurrentOrder.TargetActor.Reset();
	CurrentOrder.Location = Location;
	CurrentOrder.Radius = FMath::Max(AcceptanceRadius, 10.f);
	RefreshDecision();
	return true;
}

bool AIFSoldierAIController::IssueAttackTarget(AActor* Target)
{
	if (!IsValid(GetSoldier()) || !IsTargetValidForCombat(Target))
	{
		return false;
	}

	CurrentOrder.Type = EIFSoldierOrderType::AttackTarget;
	CurrentOrder.TargetActor = Target;
	CurrentOrder.Location = Target->GetActorLocation();
	CurrentOrder.Radius = FMath::Max(25.f, GetSoldier()->GetEffectiveAttackRange(Target) * 0.78f);
	RefreshDecision();
	return true;
}

bool AIFSoldierAIController::IssueFollowActor(AActor* Actor, float FollowDistance)
{
	if (!IsValid(GetSoldier()) || !IsValid(Actor) || Actor == GetPawn())
	{
		return false;
	}

	CurrentOrder.Type = EIFSoldierOrderType::FollowActor;
	CurrentOrder.TargetActor = Actor;
	CurrentOrder.Location = Actor->GetActorLocation();
	CurrentOrder.Radius = FMath::Max(FollowDistance, 50.f);
	RefreshDecision();
	return true;
}

void AIFSoldierAIController::IssueHoldPosition()
{
	AIFSoldierCharacter* const Soldier = GetSoldier();
	if (!IsValid(Soldier))
	{
		return;
	}

	// Holding means "make this spot the post"; Priority 2 defends it from here.
	Soldier->SetAnchorLocation(Soldier->GetActorLocation(), /*bRefreshControllerDecision*/ false);
	CurrentOrder.Reset();
	RefreshDecision();
}

void AIFSoldierAIController::ClearOrder()
{
	CurrentOrder.Reset();
	RefreshDecision();
}
