#include "AI/IFSoldierAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Core/IFLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/CrowdManager.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Damage.h"
#include "Perception/AISense_Sight.h"
#include "Stats/IFHealthComponent.h"
#include "TimerManager.h"

AIFSoldierAIController::AIFSoldierAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
	PrimaryActorTick.bCanEverTick = false;

	if (UCrowdFollowingComponent* const CrowdComp = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		CrowdComp->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Good);
		CrowdComp->SetCrowdSeparation(true);
		CrowdComp->SetCrowdSeparationWeight(2.0f);
		CrowdComp->SetCrowdObstacleAvoidance(true);
		CrowdComp->SetCrowdAnticipateTurns(true);
		CrowdComp->SetCrowdOptimizeVisibility(true);
		CrowdComp->SetCrowdCollisionQueryRange(250.f);
		CrowdComp->SetCrowdPathOptimizationRange(600.f);
	}

	Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	SetPerceptionComponent(*Perception);

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->SightRadius = 900.f;
	SightConfig->LoseSightRadius = 1150.f;
	SightConfig->PeripheralVisionAngleDegrees = 75.f;
	SightConfig->AutoSuccessRangeFromLastSeenLocation = 150.f;
	SightConfig->SetMaxAge(8.f);
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

	if (UCrowdFollowingComponent* const CrowdComp = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		if (CrowdComp->GetCrowdSimulationState() != ECrowdSimulationState::Enabled
			&& IsValid(UCrowdManager::GetCurrent(GetWorld())))
		{
			CrowdComp->SetCrowdSimulationState(ECrowdSimulationState::Enabled);
		}
	}

	if (UIFHealthComponent* const Health = Soldier->GetHealthComponent())
	{
		Health->OnDamaged.AddUniqueDynamic(this, &AIFSoldierAIController::HandleOwnerDamaged);
		BoundHealthComponent = Health;
	}

	CurrentAction = EIFSoldierAction::None;
	DecisionSerial = 0;
	CurrentTarget.Reset();
	RecentDamageInstigator.Reset();
	RecentDamageTime = -1000.f;
	LastTargetSwitchTime = -1000.f;
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

	StopMovement();
	if (UCrowdFollowingComponent* const CrowdComp = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		CrowdComp->SetCrowdSimulationState(ECrowdSimulationState::Disabled);
	}

	if (UIFHealthComponent* const Health = BoundHealthComponent.Get())
	{
		Health->OnDamaged.RemoveDynamic(this, &AIFSoldierAIController::HandleOwnerDamaged);
	}
	BoundHealthComponent.Reset();

	CurrentTarget.Reset();
	RecentDamageInstigator.Reset();
	RecentDamageTime = -1000.f;
	LastTargetSwitchTime = -1000.f;
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

	StopMovement();
	if (UCrowdFollowingComponent* const CrowdComp = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		CrowdComp->SetCrowdSimulationState(ECrowdSimulationState::Disabled);
	}

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

	AActor* const PreviousTarget = CurrentTarget.Get();
	const float Now = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() : 0.f;
	auto RecordTargetSwitchIfChanged = [this, PreviousTarget, Now](AActor* NewTarget)
	{
		if (IsValid(NewTarget) && NewTarget != PreviousTarget)
		{
			LastTargetSwitchTime = Now;
		}
	};

	// ---- 1. Explicit orders (ignore the leash) ---------------------------
	// A completed or impossible order resets itself and breaks out of the switch
	// so Priority 2 evaluates the new situation in the same call.
	if (CurrentOrder.IsValid())
	{
		switch (CurrentOrder.Type)
		{
		case EIFSoldierOrderType::MoveToLocation:
		{
			const float Radius = FMath::Max(CurrentOrder.Radius, 10.f);
			if (FVector::DistSquared2D(Soldier->GetActorLocation(), CurrentOrder.Location) > FMath::Square(Radius))
			{
				// Engage blocking or attacking enemies en route while keeping CurrentOrder intact so march resumes once clear.
				if (AActor* const InterruptTarget = AcquireMoveOrderInterruptTarget())
				{
					CurrentTarget = InterruptTarget;
					RecordTargetSwitchIfChanged(InterruptTarget);
					UpdateMoveGoalForAction();
					Soldier->CancelAnchorFacingAlignment();
					SetCurrentAction(EIFSoldierAction::AttackTarget);
					return;
				}

				CurrentTarget.Reset();
				bHasMoveGoal = true;
				MoveGoalActor.Reset();
				MoveGoalLocation = CurrentOrder.Location;
				MoveGoalAcceptanceRadius = Radius;
				Soldier->CancelAnchorFacingAlignment();
				SetCurrentAction(EIFSoldierAction::MoveTo);
				return;
			}

			// Arrived: the destination becomes the new post; combat is Priority 2's job.
			Soldier->SetAnchorLocation(CurrentOrder.Location, /*bRefreshControllerDecision*/ false);
			if (CurrentOrder.bHasFacingYaw)
			{
				Soldier->SetAnchorFacingYaw(CurrentOrder.FacingYaw);
			}
			else
			{
				Soldier->ClearAnchorFacing();
			}
			CurrentOrder.Reset();
			break;
		}

		case EIFSoldierOrderType::AttackTarget:
		{
			AActor* Ordered = CurrentOrder.TargetActor.Get();
			if (!IsTargetValidForCombat(Ordered))
			{
				// Target gone: anchor near current ground so Priority 2 fights remaining surrounding enemies instead of retreating home.
				Soldier->SetAnchorLocation(Soldier->GetActorLocation(), /*bRefreshControllerDecision*/ false);
				Soldier->ClearAnchorFacing();
				CurrentOrder.Reset();
				CurrentTarget.Reset();
				break;
			}

			// En-route intercept & anti-dogpile peel: before reaching Ordered, allow switching to a closer threatening hostile.
			const float OrderedDist = FVector::Dist2D(Soldier->GetActorLocation(), Ordered->GetActorLocation());
			const float OrderedInReachDist = Soldier->GetMaxMeleeEngagementRange(Ordered);
			if (OrderedDist > OrderedInReachDist && (Now - LastTargetSwitchTime) >= TargetSwitchCooldownSeconds)
			{
				AActor* BetterTarget = nullptr;
				if (AActor* const Attacker = RecentDamageInstigator.Get())
				{
					if ((Now - RecentDamageTime) <= DamageRetaliationWindowSeconds && IsTargetValidForCombat(Attacker)
						&& Attacker != Ordered
						&& FVector::DistSquared2D(Soldier->GetActorLocation(), Attacker->GetActorLocation())
							<= FMath::Square(FMath::Max(TargetSwitchInterceptRadius, 50.f)))
					{
						BetterTarget = Attacker;
					}
				}

				if (!IsValid(BetterTarget))
				{
					TArray<AActor*> Candidates;
					GatherValidCombatCandidates(Candidates);
					const FVector OrderedLoc = Ordered->GetActorLocation();
					const float ClusterRadiusSq = FMath::Square(FMath::Max(AttackOrderClusterRadius, 100.f));
					float BestCandidateDist = OrderedDist * FMath::Clamp(TargetSwitchDistanceRatio, 0.1f, 1.0f);

					for (AActor* const Candidate : Candidates)
					{
						if (Candidate == Ordered)
						{
							continue;
						}

						const FVector CandidateLoc = Candidate->GetActorLocation();
						const float DistToSelf = FVector::Dist2D(Soldier->GetActorLocation(), CandidateLoc);
						const float ImmediateInterceptDist = Soldier->GetMaxMeleeEngagementRange(Candidate);
						const bool bInCluster = FVector::DistSquared2D(OrderedLoc, CandidateLoc) <= ClusterRadiusSq;
						const bool bBlocksPath = (DistToSelf <= ImmediateInterceptDist);

						if ((bBlocksPath || bInCluster) && DistToSelf < BestCandidateDist)
						{
							BestCandidateDist = DistToSelf;
							BetterTarget = Candidate;
						}
					}
				}

				if (IsValid(BetterTarget))
				{
					CurrentOrder.TargetActor = BetterTarget;
					Ordered = BetterTarget;
				}
			}

			Soldier->SetAnchorLocation(Ordered->GetActorLocation(), /*bRefreshControllerDecision*/ false);
			Soldier->ClearAnchorFacing();
			CurrentTarget = Ordered;
			RecordTargetSwitchIfChanged(Ordered);
			KnownHostiles.Add(Ordered, Now);
			UpdateMoveGoalForAction();
			Soldier->CancelAnchorFacingAlignment();
			SetCurrentAction(EIFSoldierAction::AttackTarget);
			return;
		}

		case EIFSoldierOrderType::FollowActor:
		{
			AActor* const Followed = CurrentOrder.TargetActor.Get();
			const AIFBaseCharacter* const FollowedCharacter = Cast<AIFBaseCharacter>(Followed);
			if (!IsValid(Followed) || (IsValid(FollowedCharacter) && FollowedCharacter->IsDead()))
			{
				CurrentOrder.Reset();
				CurrentTarget.Reset();
				break;
			}

			const float FollowDistance = FMath::Max(CurrentOrder.Radius, 50.f);
			const float ResumeDistance = FollowDistance + FollowResumeBuffer;
			const float LeashBreakDistance = GetMaxFollowLeashDistance();
			const float DistToFollowedSq = FVector::DistSquared2D(Soldier->GetActorLocation(), Followed->GetActorLocation());

			if (DistToFollowedSq > FMath::Square(LeashBreakDistance))
			{
				CurrentTarget.Reset();
				bHasMoveGoal = true;
				MoveGoalActor = Followed;
				MoveGoalAcceptanceRadius = FollowDistance;
				Soldier->CancelAnchorFacingAlignment();
				SetCurrentAction(EIFSoldierAction::FollowActor);
				return;
			}

			const bool bWasCatchingUp = (CurrentAction == EIFSoldierAction::FollowActor) && bHasMoveGoal;
			const float CatchUpThreshold = bWasCatchingUp ? FollowDistance : ResumeDistance;
			const bool bInEscortBand = (DistToFollowedSq <= FMath::Square(CatchUpThreshold))
				|| (CurrentAction == EIFSoldierAction::Defend && DistToFollowedSq <= FMath::Square(LeashBreakDistance));

			if (bInEscortBand)
			{
				AActor* const EscortTarget = AcquireBestTarget();
				if (IsValid(EscortTarget))
				{
					CurrentTarget = EscortTarget;
					RecordTargetSwitchIfChanged(EscortTarget);
					UpdateMoveGoalForAction();
					Soldier->CancelAnchorFacingAlignment();
					SetCurrentAction(EIFSoldierAction::Defend);
					return;
				}
			}

			CurrentTarget.Reset();
			if (DistToFollowedSq > FMath::Square(CatchUpThreshold))
			{
				bHasMoveGoal = true;
				MoveGoalActor = Followed;
				MoveGoalAcceptanceRadius = FollowDistance;
			}
			else
			{
				ClearMoveGoal();
			}
			Soldier->CancelAnchorFacingAlignment();
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
			RecordTargetSwitchIfChanged(CurrentTarget.Get());
			UpdateMoveGoalForAction();
			Soldier->CancelAnchorFacingAlignment();
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
			Soldier->CancelAnchorFacingAlignment();
			SetCurrentAction(EIFSoldierAction::Advance);
			return;
		}
	}

	// ---- 4. Return to post / idle ---------------------------------------
	ClearMoveGoal();
	const float ReturnTriggerDist = FMath::Max(AnchorReturnTriggerDistance, 15.f);
	const float ReturnAcceptance = FMath::Max(AnchorReturnAcceptanceRadius, 10.f);
	if (Soldier->IsLeashedToAnchor()
		&& FVector::DistSquared2D(Soldier->GetActorLocation(), Soldier->GetAnchorLocation()) > FMath::Square(ReturnTriggerDist))
	{
		bHasMoveGoal = true;
		MoveGoalActor.Reset();
		MoveGoalLocation = Soldier->GetAnchorLocation();
		MoveGoalAcceptanceRadius = ReturnAcceptance;
		Soldier->CancelAnchorFacingAlignment();
		SetCurrentAction(EIFSoldierAction::HoldPosition);
		return;
	}

	Soldier->BeginAnchorFacingAlignment();
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

void AIFSoldierAIController::GatherValidCombatCandidates(TArray<AActor*>& OutCandidates) const
{
	OutCandidates.Reset();

	if (IsValid(Perception))
	{
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), OutCandidates);
	}

	const float Now = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() : 0.f;
	for (const TPair<TWeakObjectPtr<AActor>, float>& Pair : KnownHostiles)
	{
		AActor* const KnownActor = Pair.Key.Get();
		if (IsValid(KnownActor) && (Now - Pair.Value) <= TargetMemorySeconds)
		{
			OutCandidates.AddUnique(KnownActor);
		}
	}

	if (AActor* const Attacker = RecentDamageInstigator.Get())
	{
		if ((Now - RecentDamageTime) <= DamageRetaliationWindowSeconds)
		{
			OutCandidates.AddUnique(Attacker);
		}
	}

	OutCandidates.RemoveAll([this](AActor* Candidate)
	{
		return !IsTargetValidForCombat(Candidate);
	});
}

AActor* AIFSoldierAIController::AcquireMoveOrderInterruptTarget() const
{
	const AIFSoldierCharacter* const Soldier = GetSoldier();
	if (!IsValid(Soldier))
	{
		return nullptr;
	}

	const float Now = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() : 0.f;
	const FVector SoldierLoc = Soldier->GetActorLocation();
	const FVector ToGoal = (CurrentOrder.Location - SoldierLoc).GetSafeNormal2D();
	const bool bInDisengageGrace = (Now - CurrentOrder.IssuedTime) < MoveOrderDisengageGraceSeconds;

	TArray<AActor*> Candidates;
	GatherValidCombatCandidates(Candidates);
	if (Candidates.IsEmpty())
	{
		return nullptr;
	}

	const float RetaliationRadius = FMath::Max(MoveOrderRetaliationRadius, 50.f);
	if (!bInDisengageGrace)
	{
		if (AActor* const Attacker = RecentDamageInstigator.Get())
		{
			if ((Now - RecentDamageTime) <= DamageRetaliationWindowSeconds
				&& IsTargetValidForCombat(Attacker)
				&& FVector::DistSquared2D(SoldierLoc, Attacker->GetActorLocation()) <= FMath::Square(RetaliationRadius))
			{
				return Attacker;
			}
		}

		// Keep fighting an already-engaged interrupt target until dead or outside hysteresis band.
		if (AActor* const Existing = CurrentTarget.Get())
		{
			if (CurrentAction == EIFSoldierAction::AttackTarget
				&& IsTargetValidForCombat(Existing)
				&& FVector::DistSquared2D(SoldierLoc, Existing->GetActorLocation()) <= FMath::Square(RetaliationRadius * 1.15f))
			{
				return Existing;
			}
		}
	}

	AActor* BestBlockingHostile = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (AActor* const Candidate : Candidates)
	{
		const FVector CandidateLoc = Candidate->GetActorLocation();
		const float DistSq = FVector::DistSquared2D(SoldierLoc, CandidateLoc);
		const float EffectiveRange = Soldier->GetEffectiveAttackRange(Candidate);

		bool bShouldInterrupt = false;
		if (bInDisengageGrace)
		{
			// During fresh retreat grace window, only stop for an enemy physically body-blocking the path ahead.
			const float BodyBlockRadius = EffectiveRange * 0.90f;
			const FVector ToEnemy = (CandidateLoc - SoldierLoc).GetSafeNormal2D();
			bShouldInterrupt = (DistSq <= FMath::Square(BodyBlockRadius))
				&& (ToGoal.IsNearlyZero() || FVector::DotProduct(ToGoal, ToEnemy) >= 0.35f);
		}
		else
		{
			const float InterceptDist = FMath::Max(MoveOrderInterceptRadius, EffectiveRange + 35.f);
			bShouldInterrupt = (DistSq <= FMath::Square(InterceptDist));
		}

		if (bShouldInterrupt && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestBlockingHostile = Candidate;
		}
	}

	return BestBlockingHostile;
}

AActor* AIFSoldierAIController::AcquireBestTarget() const
{
	const AIFSoldierCharacter* const Soldier = GetSoldier();
	if (!IsValid(Soldier))
	{
		return nullptr;
	}

	const bool bIsEscortOrder = (CurrentOrder.Type == EIFSoldierOrderType::FollowActor);
	const float EngageRadius = bIsEscortOrder
		? FMath::Min(GetEngageRadius(), GetMaxFollowLeashDistance())
		: GetEngageRadius();
	const float EngageRadiusSq = FMath::Square(EngageRadius);
	const FVector SoldierLoc = Soldier->GetActorLocation();
	const FVector Reference = (Soldier->IsLeashedToAnchor() || bIsEscortOrder)
		? GetAnchorLocation()
		: SoldierLoc;
	const float Now = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() : 0.f;

	TArray<AActor*> CandidatePool;
	GatherValidCombatCandidates(CandidatePool);

	AActor* BestOverall = nullptr;
	float BestOverallRefDistSq = TNumericLimits<float>::Max();
	AActor* ClosestToSoldier = nullptr;
	float ClosestToSoldierDistSq = TNumericLimits<float>::Max();

	for (AActor* const Candidate : CandidatePool)
	{
		const FVector CandidateLoc = Candidate->GetActorLocation();
		const float RefDistSq = FVector::DistSquared2D(Reference, CandidateLoc);
		if (RefDistSq > EngageRadiusSq)
		{
			continue;
		}

		if (RefDistSq < BestOverallRefDistSq)
		{
			BestOverallRefDistSq = RefDistSq;
			BestOverall = Candidate;
		}

		const float SelfDistSq = FVector::DistSquared2D(SoldierLoc, CandidateLoc);
		if (SelfDistSq < ClosestToSoldierDistSq)
		{
			ClosestToSoldierDistSq = SelfDistSq;
			ClosestToSoldier = Candidate;
		}
	}

	// Check whether a recent melee damage instigator is threatening this soldier within intercept range.
	const float InterceptRadiusSq = FMath::Square(FMath::Max(TargetSwitchInterceptRadius, 50.f));
	AActor* ValidRecentAttacker = nullptr;
	if (AActor* const Attacker = RecentDamageInstigator.Get())
	{
		if ((Now - RecentDamageTime) <= DamageRetaliationWindowSeconds && IsTargetValidForCombat(Attacker))
		{
			const FVector AttackerLoc = Attacker->GetActorLocation();
			if (FVector::DistSquared2D(Reference, AttackerLoc) <= EngageRadiusSq
				&& FVector::DistSquared2D(SoldierLoc, AttackerLoc) <= InterceptRadiusSq)
			{
				ValidRecentAttacker = Attacker;
			}
		}
	}

	// Keep the current target inside its hysteresis ring unless a much closer threat or melee attacker interrupts it.
	if (AActor* const Existing = CurrentTarget.Get())
	{
		if (IsTargetValidForCombat(Existing))
		{
			const float HysteresisSq = bIsEscortOrder
				? EngageRadiusSq
				: FMath::Square(EngageRadius * LeashHysteresisMultiplier);
			if (FVector::DistSquared2D(Reference, Existing->GetActorLocation()) <= HysteresisSq)
			{
				const bool bSwitchCooldownReady = (Now - LastTargetSwitchTime) >= TargetSwitchCooldownSeconds;
				if (bSwitchCooldownReady)
				{
					const float ExistingSelfDist = FVector::Dist2D(SoldierLoc, Existing->GetActorLocation());
					const float ExistingMeleeThreshold = Soldier->GetMaxMeleeEngagementRange(Existing);

					// 1. Retaliate against a recent melee attacker if the current target is not already in melee reach.
					if (IsValid(ValidRecentAttacker) && ValidRecentAttacker != Existing && ExistingSelfDist > ExistingMeleeThreshold)
					{
						return ValidRecentAttacker;
					}

					// 2. Break target stickiness if a much closer hostile intercepts the soldier.
					if (IsValid(ClosestToSoldier) && ClosestToSoldier != Existing && ClosestToSoldierDistSq <= InterceptRadiusSq)
					{
						const float CandidateSelfDist = FMath::Sqrt(ClosestToSoldierDistSq);
						if (CandidateSelfDist < ExistingSelfDist * TargetSwitchDistanceRatio)
						{
							return ClosestToSoldier;
						}
					}
				}

				return Existing;
			}
		}
	}

	if (IsValid(ValidRecentAttacker))
	{
		if (!IsValid(ClosestToSoldier)
			|| FVector::DistSquared2D(SoldierLoc, ValidRecentAttacker->GetActorLocation()) <= ClosestToSoldierDistSq * 1.25f)
		{
			return ValidRecentAttacker;
		}
	}

	return BestOverall;
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
	else if (const AIFStructure* const StructureTarget = Cast<AIFStructure>(Target))
	{
		if (!StructureTarget->IsStructureAlive())
		{
			return false;
		}
	}
	else if (const UIFHealthComponent* const TargetHealth = Target->FindComponentByClass<UIFHealthComponent>())
	{
		if (TargetHealth->IsDead())
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

float AIFSoldierAIController::GetMaxFollowLeashDistance() const
{
	const float FollowDistance = FMath::Max(CurrentOrder.Radius, 50.f);
	return FollowDistance + FMath::Max(FollowLeashBreakBuffer, FollowResumeBuffer);
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

	// Stop chasing at StopDistance and hold stationary combat until target exceeds MaxHoldDistance.
	// Uses exact box-surface distance for AIFStructure targets so soldiers stop flush against wall faces.
	const float StopDistance = Soldier->GetEffectiveAttackRange(Target);
	const float MaxHoldDistance = Soldier->GetMaxMeleeEngagementRange(Target);
	const bool bActivelyChasingTarget = bHasMoveGoal && (MoveGoalActor.Get() == Target) && !Soldier->IsAttacking();
	const float InReachThreshold = bActivelyChasingTarget ? StopDistance : MaxHoldDistance;
	if (Soldier->GetHorizontalDistanceToTarget(Target) <= InReachThreshold)
	{
		// Already in reach: no move goal means UIFBTTask_SoldierMove succeeds instantly
		// and the attack task runs without shoving the pawn around.
		ClearMoveGoal();
		return;
	}

	bHasMoveGoal = true;
	MoveGoalActor = Target;
	MoveGoalLocation = Soldier->GetCombatApproachPoint(Target);
	MoveGoalAcceptanceRadius = StopDistance;
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

	const float Now = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() : 0.f;
	KnownHostiles.Add(InstigatorActor, Now);
	RecentDamageInstigator = InstigatorActor;
	RecentDamageTime = Now;

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
	if (CurrentOrder.Type == EIFSoldierOrderType::FollowActor)
	{
		if (const AActor* const Followed = CurrentOrder.TargetActor.Get())
		{
			return Followed->GetActorLocation();
		}
	}

	const AIFSoldierCharacter* const Soldier = GetSoldier();
	return IsValid(Soldier) ? Soldier->GetAnchorLocation() : FVector::ZeroVector;
}

// ---------------------------------------------------------------------------
// Orders (Part 08 surface)
// ---------------------------------------------------------------------------

bool AIFSoldierAIController::IssueMoveToLocation(const FVector& Location, float AcceptanceRadius, float DesiredFacingYaw, bool bUseFacingYaw)
{
	if (!IsValid(GetSoldier()))
	{
		return false;
	}

	CurrentOrder.Type = EIFSoldierOrderType::MoveToLocation;
	CurrentOrder.TargetActor.Reset();
	CurrentOrder.Location = Location;
	CurrentOrder.Radius = FMath::Max(AcceptanceRadius, 10.f);
	CurrentOrder.FacingYaw = FRotator::NormalizeAxis(DesiredFacingYaw);
	CurrentOrder.bHasFacingYaw = bUseFacingYaw;
	CurrentOrder.IssuedTime = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() : 0.f;
	RefreshDecision();
	return true;
}

void AIFSoldierAIController::NotifyMoveOrderBlockedOrStalled()
{
	AIFSoldierCharacter* const Soldier = GetSoldier();
	if (!IsValid(Soldier) || Soldier->IsDead())
	{
		return;
	}

	if (CurrentOrder.Type == EIFSoldierOrderType::MoveToLocation)
	{
		Soldier->SetAnchorLocation(Soldier->GetActorLocation(), /*bRefreshControllerDecision*/ false);
		if (CurrentOrder.bHasFacingYaw)
		{
			Soldier->SetAnchorFacingYaw(CurrentOrder.FacingYaw);
		}
		else
		{
			Soldier->ClearAnchorFacing();
		}
		CurrentOrder.Reset();
		RefreshDecision();
	}
	else if (CurrentAction == EIFSoldierAction::HoldPosition)
	{
		Soldier->SetAnchorLocation(Soldier->GetActorLocation(), /*bRefreshControllerDecision*/ false);
		RefreshDecision();
	}
}

bool AIFSoldierAIController::IssueAttackTarget(AActor* Target)
{
	AIFSoldierCharacter* const Soldier = GetSoldier();
	if (!IsValid(Soldier) || !IsTargetValidForCombat(Target))
	{
		return false;
	}

	Soldier->SetAnchorLocation(Target->GetActorLocation(), /*bRefreshControllerDecision*/ false);
	Soldier->ClearAnchorFacing();

	CurrentOrder.Type = EIFSoldierOrderType::AttackTarget;
	CurrentOrder.TargetActor = Target;
	CurrentOrder.Location = Target->GetActorLocation();
	CurrentOrder.Radius = Soldier->GetEffectiveAttackRange(Target);
	CurrentOrder.IssuedTime = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() : 0.f;
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
