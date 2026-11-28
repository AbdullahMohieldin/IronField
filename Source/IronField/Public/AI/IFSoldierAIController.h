#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/IFSoldierAITypes.h"
#include "Perception/AIPerceptionTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "IFSoldierAIController.generated.h"

class AActor;
class AIFSoldierCharacter;
class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UBehaviorTree;
class UIFHealthComponent;
struct FStreamableHandle;

/**
 * Decision brain for one soldier.
 * Owns sight perception, target memory, leash clamping, tactical orders, and the active
 * move goal consumed by UIFBTTask_SoldierMove, mirroring branch state to the Blackboard.
 */
UCLASS()
class IRONFIELD_API AIFSoldierAIController : public AAIController
{
	GENERATED_BODY()

public:
	AIFSoldierAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static const FName SoldierBranchBlackboardKey;
	static bool IsValidCombatTarget(const AActor* SourceActor, const AActor* Target);

	// Prefixed with Issue to avoid colliding with AAIController::MoveToLocation.
	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Orders")
	bool IssueMoveToLocation(const FVector& Location, float AcceptanceRadius = 20.f, float DesiredFacingYaw = 0.f, bool bUseFacingYaw = false);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Orders")
	bool IssueAttackTarget(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Orders")
	bool IssueFollowActor(AActor* Actor, float FollowDistance = 250.f);

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|State")
	EIFSoldierAction GetCurrentAction() const { return CurrentAction; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|State")
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|State")
	bool HasMoveGoal() const { return bHasMoveGoal; }

	AActor* GetMoveGoalActor() const { return MoveGoalActor.Get(); }
	FVector GetMoveGoalLocation() const { return MoveGoalLocation; }
	float GetMoveGoalAcceptanceRadius() const { return MoveGoalAcceptanceRadius; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|State")
	FVector GetAnchorLocation() const;

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|State")
	void RefreshDecision();

	AIFSoldierCharacter* GetSoldier() const;

	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

protected:
	virtual void PostInitializeComponents() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	UFUNCTION()
	void HandleOwnerDamaged(AActor* InstigatorActor, float DamageAmount);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI")
	TSoftObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float TargetMemorySeconds = 8.0f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "1.0"))
	float LeashHysteresisMultiplier = 1.15f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float FollowResumeBuffer = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float FollowLeashBreakBuffer = 450.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float TargetSwitchCooldownSeconds = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TargetSwitchDistanceRatio = 0.65f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "50.0"))
	float TargetSwitchInterceptRadius = 280.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float DamageRetaliationWindowSeconds = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float MoveOrderDisengageGraceSeconds = 0.75f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "50.0"))
	float MoveOrderRetaliationRadius = 280.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "50.0"))
	float MoveOrderInterceptRadius = 185.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "100.0"))
	float AttackOrderClusterRadius = 450.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "10.0"))
	float AnchorReturnTriggerDistance = 45.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "10.0"))
	float AnchorReturnAcceptanceRadius = 20.f;

private:
	void EnableControllerRotationFacing(AIFSoldierCharacter& Soldier);
	void RestoreMovementRotationFacing(AIFSoldierCharacter& Soldier);
	void RequestBehaviorTree();
	void HandleBehaviorTreeLoaded();
	void CleanupActiveSession();

	bool EvaluateMoveOrder(AIFSoldierCharacter& Soldier, float Now, AActor* PreviousTarget);
	bool EvaluateAttackOrder(AIFSoldierCharacter& Soldier, float Now, AActor* PreviousTarget);
	bool EvaluateFollowOrder(AIFSoldierCharacter& Soldier, float Now, AActor* PreviousTarget);
	bool EvaluateAutonomousCombat(AIFSoldierCharacter& Soldier, float Now, AActor* PreviousTarget);
	bool EvaluateAdvance(AIFSoldierCharacter& Soldier);
	void EvaluateReturnOrIdle(AIFSoldierCharacter& Soldier);

	void SetCurrentAction(EIFSoldierAction NewAction);
	void SyncBlackboardBranch();
	void RecordTargetSwitchIfChanged(AActor* NewTarget, AActor* PreviousTarget, float Now);
	AActor* GetActiveRecentAttacker(float Now) const;
	void GatherValidCombatCandidates(TArray<AActor*>& OutCandidates) const;
	AActor* AcquireBestTarget() const;
	AActor* AcquireMoveOrderInterruptTarget() const;
	bool IsTargetValidForCombat(AActor* Target) const;
	float GetEngageRadius() const;
	float GetMaxFollowLeashDistance() const;
	void UpdateMoveGoalForAction();
	void PruneKnownHostiles();
	void ClearMoveGoal();

	UPROPERTY(Transient)
	TObjectPtr<UBehaviorTree> LoadedBehaviorTree;

	// LoadedBehaviorTree roots the asset against GC; this handle only keeps the async request cancellable until load completes or EndPlay.
	TSharedPtr<FStreamableHandle> BehaviorTreeLoadHandle;

	// Cached for a guaranteed unbind even if the pawn is already cleared.
	TWeakObjectPtr<UIFHealthComponent> BoundHealthComponent;

	TMap<TWeakObjectPtr<AActor>, float> KnownHostiles;
	TWeakObjectPtr<AActor> CurrentTarget;
	TWeakObjectPtr<AActor> RecentDamageInstigator;
	float RecentDamageTime = -1000.f;
	float LastTargetSwitchTime = -1000.f;
	FIFSoldierOrder CurrentOrder;

	TWeakObjectPtr<AActor> MoveGoalActor;
	FVector MoveGoalLocation = FVector::ZeroVector;
	float MoveGoalAcceptanceRadius = 20.f;
	bool bHasMoveGoal = false;

	EIFSoldierAction CurrentAction = EIFSoldierAction::None;
};
