#include "AI/IFSoldierAIController.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFTeamTypes.h"
#include "Core/IFLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/CrowdManager.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "Stats/IFHealthComponent.h"

const FName AIFSoldierAIController::SoldierBranchBlackboardKey(TEXT("SoldierBranch"));

AIFSoldierAIController::AIFSoldierAIController(const FObjectInitializer& ObjectInitializer)
	// Detour Crowd replaces default path following; bUseRVOAvoidance stays false on AIFSoldierCharacter so the two systems never fight over velocity.
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
	PrimaryActorTick.bCanEverTick = false;

	ConfigureCrowdFollowing();

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

	Perception->ConfigureSense(*SightConfig);
	Perception->SetDominantSense(SightConfig->GetSenseImplementation());
	Perception->OnTargetPerceptionUpdated.AddDynamic(this, &AIFSoldierAIController::HandleTargetPerceptionUpdated);
}

void AIFSoldierAIController::ConfigureCrowdFollowing()
{
	if (UCrowdFollowingComponent* const CrowdComp = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		CrowdComp->SetAvoidanceGroup(1, true);
		CrowdComp->SetGroupsToAvoid(MAX_int32);
		CrowdComp->SetGroupsToIgnore(0);
		CrowdComp->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Good);
		CrowdComp->SetCrowdSeparation(true);
		CrowdComp->SetCrowdSeparationWeight(1.35f);
		CrowdComp->SetCrowdObstacleAvoidance(true);
		CrowdComp->SetCrowdAnticipateTurns(true);
		CrowdComp->SetCrowdOptimizeVisibility(true);
		CrowdComp->SetCrowdCollisionQueryRange(200.f);
		CrowdComp->SetCrowdPathOptimizationRange(600.f);
		CrowdComp->UpdateCrowdAgentParams();
	}
}

void AIFSoldierAIController::ApplyIdleAnchorFacing(AIFSoldierCharacter& Soldier)
{
	if (UCharacterMovementComponent* const Movement = Soldier.GetCharacterMovement())
	{
		// PhysicsRotation prioritizes bOrientRotationToMovement over bUseControllerDesiredRotation.
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = true;
	}

	const float DesiredYaw = Soldier.GetAnchorFacingYaw();
	const FVector FacingDir = FRotator(0.f, DesiredYaw, 0.f).Vector();
	SetFocalPoint(Soldier.GetAnchorLocation() + FacingDir * 500.f, EAIFocusPriority::Gameplay);
	SetControlRotation(FRotator(0.f, DesiredYaw, 0.f));
}

void AIFSoldierAIController::ClearIdleAnchorFacing(AIFSoldierCharacter& Soldier)
{
	ClearFocus(EAIFocusPriority::Gameplay);
	if (UCharacterMovementComponent* const Movement = Soldier.GetCharacterMovement())
	{
		Movement->bUseControllerDesiredRotation = false;
		Movement->bOrientRotationToMovement = true;
	}
}

bool AIFSoldierAIController::IsValidCombatTarget(const AActor* SourceActor, const AActor* Target)
{
	if (!IsValid(SourceActor) || !IsValid(Target) || Target->IsActorBeingDestroyed())
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

	return IFTeamUtils::AreHostile(SourceActor, Target);
}

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

	SetGenericTeamId(Soldier->GetGenericTeamId());
	if (IsValid(Perception))
	{
		Perception->RequestStimuliListenerUpdate();
	}

	// OnUnPossess disables crowd simulation to keep inactive controllers out of Detour's MaxAgents budget, so OnPossess re-enables and syncs params.
	if (UCrowdFollowingComponent* const CrowdComp = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		if (CrowdComp->GetCrowdSimulationState() != ECrowdSimulationState::Enabled
			&& IsValid(UCrowdManager::GetCurrent(GetWorld())))
		{
			CrowdComp->SetCrowdSimulationState(ECrowdSimulationState::Enabled);
		}
	}
	ConfigureCrowdFollowing();

	if (UIFHealthComponent* const Health = Soldier->GetHealthComponent())
	{
		Health->OnDamaged.AddUniqueDynamic(this, &AIFSoldierAIController::HandleOwnerDamaged);
		BoundHealthComponent = Health;
	}

	CurrentAction = EIFSoldierAction::None;
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

void AIFSoldierAIController::CleanupActiveSession()
{
	if (AIFSoldierCharacter* const Soldier = GetSoldier())
	{
		ClearIdleAnchorFacing(*Soldier);
	}
	else
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}

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
	LoadedBehaviorTree = nullptr;
}

void AIFSoldierAIController::OnUnPossess()
{
	CleanupActiveSession();
	CurrentTarget.Reset();
	RecentDamageInstigator.Reset();
	RecentDamageTime = -1000.f;
	LastTargetSwitchTime = -1000.f;
	KnownHostiles.Reset();
	ClearMoveGoal();

	Super::OnUnPossess();
}

void AIFSoldierAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CleanupActiveSession();
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

	RefreshDecision();
	SyncBlackboardBranch();
}

void AIFSoldierAIController::RefreshDecision()
{
	AIFSoldierCharacter* const Soldier = GetSoldier();
	if (!IsValid(Soldier) || Soldier->IsDead())
	{
		if (IsValid(Soldier))
		{
			ClearIdleAnchorFacing(*Soldier);
		}
		ClearMoveGoal();
		SetCurrentAction(EIFSoldierAction::Idle);
		return;
	}

	PruneKnownHostiles();

	AActor* const PreviousTarget = CurrentTarget.Get();
	const float Now = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() : 0.f;

	if (CurrentOrder.IsValid())
	{
		switch (CurrentOrder.Type)
		{
		case EIFSoldierOrderType::MoveToLocation:
			if (EvaluateMoveOrder(*Soldier, Now, PreviousTarget))
			{
				return;
			}
			break;

		case EIFSoldierOrderType::AttackTarget:
			if (EvaluateAttackOrder(*Soldier, Now, PreviousTarget))
			{
				return;
			}
			break;

		case EIFSoldierOrderType::FollowActor:
			if (EvaluateFollowOrder(*Soldier, Now, PreviousTarget))
			{
				return;
			}
			break;

		default:
			break;
		}
	}

	if (EvaluateAutonomousCombat(*Soldier, Now, PreviousTarget))
	{
		return;
	}

	if (EvaluateAdvance(*Soldier))
	{
		return;
	}

	EvaluateReturnOrIdle(*Soldier);
}

bool AIFSoldierAIController::EvaluateMoveOrder(AIFSoldierCharacter& Soldier, float Now, AActor* PreviousTarget)
{
	const float Radius = FMath::Max(CurrentOrder.Radius, 10.f);
	if (FVector::DistSquared2D(Soldier.GetActorLocation(), CurrentOrder.Location) > FMath::Square(Radius))
	{
		if (AActor* const InterruptTarget = AcquireMoveOrderInterruptTarget())
		{
			CurrentTarget = InterruptTarget;
			RecordTargetSwitchIfChanged(InterruptTarget, PreviousTarget, Now);
			UpdateMoveGoalForAction();
			ClearIdleAnchorFacing(Soldier);
			SetCurrentAction(EIFSoldierAction::AttackTarget);
			return true;
		}

		CurrentTarget.Reset();
		bHasMoveGoal = true;
		MoveGoalActor.Reset();
		MoveGoalLocation = CurrentOrder.Location;
		MoveGoalAcceptanceRadius = Radius;
		ClearIdleAnchorFacing(Soldier);
		SetCurrentAction(EIFSoldierAction::MoveTo);
		return true;
	}

	Soldier.SetAnchorLocation(CurrentOrder.Location, false);
	if (CurrentOrder.bHasFacingYaw)
	{
		Soldier.SetAnchorFacingYaw(CurrentOrder.FacingYaw);
	}
	else
	{
		Soldier.ClearAnchorFacing();
	}
	CurrentOrder.Reset();
	return false;
}

bool AIFSoldierAIController::EvaluateAttackOrder(AIFSoldierCharacter& Soldier, float Now, AActor* PreviousTarget)
{
	AActor* Ordered = CurrentOrder.TargetActor.Get();
	if (!IsTargetValidForCombat(Ordered))
	{
		// Anchor near current ground so autonomous defence fights surrounding enemies instead of retreating home.
		Soldier.SetAnchorLocation(Soldier.GetActorLocation(), false);
		Soldier.ClearAnchorFacing();
		CurrentOrder.Reset();
		CurrentTarget.Reset();
		return false;
	}

	const float OrderedDist = FVector::Dist2D(Soldier.GetActorLocation(), Ordered->GetActorLocation());
	const float OrderedInReachDist = Soldier.GetMaxMeleeEngagementRange(Ordered);
	if (OrderedDist > OrderedInReachDist && (Now - LastTargetSwitchTime) >= TargetSwitchCooldownSeconds)
	{
		AActor* BetterTarget = nullptr;
		if (AActor* const Attacker = RecentDamageInstigator.Get())
		{
			if ((Now - RecentDamageTime) <= DamageRetaliationWindowSeconds && IsTargetValidForCombat(Attacker)
				&& Attacker != Ordered
				&& FVector::DistSquared2D(Soldier.GetActorLocation(), Attacker->GetActorLocation())
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
				const float DistToSelf = FVector::Dist2D(Soldier.GetActorLocation(), CandidateLoc);
				const float ImmediateInterceptDist = Soldier.GetMaxMeleeEngagementRange(Candidate);
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

	Soldier.SetAnchorLocation(Ordered->GetActorLocation(), false);
	Soldier.ClearAnchorFacing();
	CurrentTarget = Ordered;
	RecordTargetSwitchIfChanged(Ordered, PreviousTarget, Now);
	KnownHostiles.Add(Ordered, Now);
	UpdateMoveGoalForAction();
	ClearIdleAnchorFacing(Soldier);
	SetCurrentAction(EIFSoldierAction::AttackTarget);
	return true;
}

bool AIFSoldierAIController::EvaluateFollowOrder(AIFSoldierCharacter& Soldier, float Now, AActor* PreviousTarget)
{
	AActor* const Followed = CurrentOrder.TargetActor.Get();
	const AIFBaseCharacter* const FollowedCharacter = Cast<AIFBaseCharacter>(Followed);
	if (!IsValid(Followed) || (IsValid(FollowedCharacter) && FollowedCharacter->IsDead()))
	{
		CurrentOrder.Reset();
		CurrentTarget.Reset();
		return false;
	}

	const float FollowDistance = FMath::Max(CurrentOrder.Radius, 50.f);
	const float ResumeDistance = FollowDistance + FollowResumeBuffer;
	const float LeashBreakDistance = GetMaxFollowLeashDistance();
	const float DistToFollowedSq = FVector::DistSquared2D(Soldier.GetActorLocation(), Followed->GetActorLocation());

	if (DistToFollowedSq > FMath::Square(LeashBreakDistance))
	{
		CurrentTarget.Reset();
		bHasMoveGoal = true;
		MoveGoalActor = Followed;
		MoveGoalAcceptanceRadius = FollowDistance;
		ClearIdleAnchorFacing(Soldier);
		SetCurrentAction(EIFSoldierAction::FollowActor);
		return true;
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
			RecordTargetSwitchIfChanged(EscortTarget, PreviousTarget, Now);
			UpdateMoveGoalForAction();
			ClearIdleAnchorFacing(Soldier);
			SetCurrentAction(EIFSoldierAction::Defend);
			return true;
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
	ClearIdleAnchorFacing(Soldier);
	SetCurrentAction(EIFSoldierAction::FollowActor);
	return true;
}

bool AIFSoldierAIController::EvaluateAutonomousCombat(AIFSoldierCharacter& Soldier, float Now, AActor* PreviousTarget)
{
	AActor* const Target = AcquireBestTarget();
	CurrentTarget = Target;

	if (!IsValid(Target))
	{
		return false;
	}

	RecordTargetSwitchIfChanged(Target, PreviousTarget, Now);
	UpdateMoveGoalForAction();
	ClearIdleAnchorFacing(Soldier);
	SetCurrentAction(Soldier.IsLeashedToAnchor()
		? EIFSoldierAction::Defend
		: EIFSoldierAction::AttackTarget);
	return true;
}

bool AIFSoldierAIController::EvaluateAdvance(AIFSoldierCharacter& Soldier)
{
	if (!Soldier.ShouldAdvanceWhenIdle())
	{
		return false;
	}

	AActor* AdvanceActor = nullptr;
	FVector AdvanceLocation = FVector::ZeroVector;

	if (Soldier.ShouldAdvanceTowardsPlayer())
	{
		AdvanceActor = UGameplayStatics::GetPlayerPawn(this, 0);
	}
	else if (AActor* const Configured = Soldier.GetAdvanceGoalActor())
	{
		AdvanceActor = Configured;
	}
	else
	{
		AdvanceLocation = Soldier.GetAdvanceGoalLocation();
	}

	if (IsValid(AdvanceActor) && AdvanceActor == &Soldier)
	{
		AdvanceActor = nullptr;
	}

	const bool bHasGoal = IsValid(AdvanceActor) || !AdvanceLocation.IsNearlyZero();
	const FVector GoalLocation = IsValid(AdvanceActor) ? AdvanceActor->GetActorLocation() : AdvanceLocation;
	const float StopDistance = FMath::Max(Soldier.GetAdvanceStopDistance(), 0.f);

	if (bHasGoal && FVector::DistSquared2D(Soldier.GetActorLocation(), GoalLocation) > FMath::Square(StopDistance))
	{
		bHasMoveGoal = true;
		MoveGoalActor = AdvanceActor;
		MoveGoalLocation = GoalLocation;
		MoveGoalAcceptanceRadius = FMath::Max(StopDistance * 0.5f, 50.f);
		ClearIdleAnchorFacing(Soldier);
		SetCurrentAction(EIFSoldierAction::Advance);
		return true;
	}

	return false;
}

void AIFSoldierAIController::EvaluateReturnOrIdle(AIFSoldierCharacter& Soldier)
{
	ClearMoveGoal();
	const float ReturnTriggerDist = FMath::Max(AnchorReturnTriggerDistance, 15.f);
	const float ReturnAcceptance = FMath::Max(AnchorReturnAcceptanceRadius, 10.f);
	if (Soldier.IsLeashedToAnchor()
		&& FVector::DistSquared2D(Soldier.GetActorLocation(), Soldier.GetAnchorLocation()) > FMath::Square(ReturnTriggerDist))
	{
		bHasMoveGoal = true;
		MoveGoalActor.Reset();
		MoveGoalLocation = Soldier.GetAnchorLocation();
		MoveGoalAcceptanceRadius = ReturnAcceptance;
		ClearIdleAnchorFacing(Soldier);
		SetCurrentAction(EIFSoldierAction::HoldPosition);
		return;
	}

	if (Soldier.HasAnchorFacing())
	{
		ApplyIdleAnchorFacing(Soldier);
	}
	else
	{
		ClearIdleAnchorFacing(Soldier);
	}
	SetCurrentAction(EIFSoldierAction::Idle);
}

void AIFSoldierAIController::RecordTargetSwitchIfChanged(AActor* NewTarget, AActor* PreviousTarget, float Now)
{
	if (IsValid(NewTarget) && NewTarget != PreviousTarget)
	{
		LastTargetSwitchTime = Now;
	}
}

void AIFSoldierAIController::SetCurrentAction(EIFSoldierAction NewAction)
{
	if (CurrentAction == NewAction)
	{
		return;
	}

	CurrentAction = NewAction;
	SyncBlackboardBranch();
}

void AIFSoldierAIController::SyncBlackboardBranch()
{
	if (UBlackboardComponent* const BlackboardComp = GetBlackboardComponent())
	{
		BlackboardComp->SetValueAsEnum(
			SoldierBranchBlackboardKey,
			static_cast<uint8>(ResolveSoldierBranch(CurrentAction)));
	}
}

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
			const float BodyBlockRadius = EffectiveRange * 0.90f;
			const FVector ToEnemy = (CandidateLoc - SoldierLoc).GetSafeNormal2D();
			// ~70 degree forward half-angle cone so retreating units only stop for enemies physically blocking their escape path.
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

	if (AActor* const Existing = CurrentTarget.Get())
	{
		if (IsTargetValidForCombat(Existing))
		{
			const float HysteresisRadius = bIsEscortOrder
				? FMath::Min(EngageRadius * LeashHysteresisMultiplier, GetMaxFollowLeashDistance())
				: (EngageRadius * LeashHysteresisMultiplier);
			if (FVector::DistSquared2D(Reference, Existing->GetActorLocation()) <= FMath::Square(HysteresisRadius))
			{
				const bool bSwitchCooldownReady = (Now - LastTargetSwitchTime) >= TargetSwitchCooldownSeconds;
				if (bSwitchCooldownReady)
				{
					const float ExistingSelfDist = FVector::Dist2D(SoldierLoc, Existing->GetActorLocation());
					const float ExistingMeleeThreshold = Soldier->GetMaxMeleeEngagementRange(Existing);

					if (IsValid(ValidRecentAttacker) && ValidRecentAttacker != Existing && ExistingSelfDist > ExistingMeleeThreshold)
					{
						return ValidRecentAttacker;
					}

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
	return IsValidCombatTarget(GetPawn(), Target);
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

	const float StopDistance = Soldier->GetEffectiveAttackRange(Target);
	const UIFCombatComponent* const Combat = Soldier->GetCombatComponent();
	// Dual-threshold hysteresis: chase all the way to StopDistance, then tolerate AttackRangeBuffer before stepping again.
	const bool bActivelyChasingTarget = bHasMoveGoal && (MoveGoalActor.Get() == Target) && !(IsValid(Combat) && Combat->IsAttacking());
	if (Soldier->IsTargetInMeleeReach(Target, !bActivelyChasingTarget))
	{
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
	MoveGoalAcceptanceRadius = 20.f;
}

void AIFSoldierAIController::HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!IsValid(Actor) || !IFTeamUtils::AreHostile(GetPawn(), Actor))
	{
		return;
	}

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

bool AIFSoldierAIController::IssueAttackTarget(AActor* Target)
{
	AIFSoldierCharacter* const Soldier = GetSoldier();
	if (!IsValid(Soldier) || !IsTargetValidForCombat(Target))
	{
		return false;
	}

	Soldier->SetAnchorLocation(Target->GetActorLocation(), false);
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
