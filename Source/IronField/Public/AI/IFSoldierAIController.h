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
class UAISenseConfig_Damage;
class UAISenseConfig_Sight;
class UBehaviorTree;
class UIFHealthComponent;
struct FStreamableHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSoldierActionChanged, EIFSoldierAction, NewAction);

/**
 * Decision brain for one soldier.
 *
 * Owns sight + damage perception, target memory, leash clamping, Part 08 orders and the
 * single move goal that UIFBTTask_SoldierMove consumes. The BT only sequences work; it
 * never decides *who* to fight. All runtime state is per controller, so one BT asset can
 * be shared by every soldier.
 */
UCLASS()
class IRONFIELD_API AIFSoldierAIController : public AAIController
{
	GENERATED_BODY()

public:
	AIFSoldierAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(BlueprintAssignable, Category = "IronField|Soldier|Events")
	FOnSoldierActionChanged OnActionChanged;

	// --- Part 08 order API (semantics: MoveToLocation/AttackTarget/FollowActor/HoldPosition).
	//     Names are prefixed because AAIController already reflects MoveToLocation.
	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Orders")
	bool IssueMoveToLocation(const FVector& Location, float AcceptanceRadius = 20.f, float DesiredFacingYaw = 0.f, bool bUseFacingYaw = false);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Orders")
	bool IssueAttackTarget(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Orders")
	bool IssueFollowActor(AActor* Actor, float FollowDistance = 250.f);

	// Re-anchors the soldier at its current spot; Priority 2 defends it from there.
	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Orders")
	void IssueHoldPosition();

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Orders")
	void ClearOrder();

	// --- State read by BT nodes -------------------------------------------------
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

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|State")
	int32 GetDecisionSerial() const { return DecisionSerial; }

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|State")
	void RefreshDecision();

	void NotifyMoveOrderBlockedOrStalled();

	AIFSoldierCharacter* GetSoldier() const;

	// IGenericTeamAgentInterface — perception filters on the listener's attitude.
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	// UIFHealthComponent does not emit UAISense_Damage on its own; this bridge both
	// records the attacker and reports the stimulus for other listeners.
	UFUNCTION()
	void HandleOwnerDamaged(AActor* InstigatorActor, float DamageAmount);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Perception")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Perception")
	TObjectPtr<UAISenseConfig_Damage> DamageConfig;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI")
	TSoftObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float TargetMemorySeconds = 8.0f;

	// Hysteresis on the autonomous engagement radius; prevents border flicker.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "1.0"))
	float LeashHysteresisMultiplier = 1.15f;

	// Extra distance beyond FollowDistance before an escorting soldier breaks to catch up.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float FollowResumeBuffer = 100.f;

	// Max extra distance beyond FollowDistance allowed while defending Greystone before breaking off.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float FollowLeashBreakBuffer = 450.f;

	// Cooldown between autonomous target switches to prevent ping-ponging in dense melee.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float TargetSwitchCooldownSeconds = 1.0f;

	// Candidate must be closer than this fraction of CurrentTarget's distance to break target stickiness.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TargetSwitchDistanceRatio = 0.65f;

	// Distance from the soldier within which a closer threat or recent attacker can interrupt CurrentTarget.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "50.0"))
	float TargetSwitchInterceptRadius = 280.f;

	// Recency window during which a melee damage instigator can pull aggro from a distant target.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float DamageRetaliationWindowSeconds = 2.0f;

	// Move-order en-route combat interrupt settings (temporary pause, resumes MoveToLocation once clear).
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float MoveOrderDisengageGraceSeconds = 0.75f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "50.0"))
	float MoveOrderRetaliationRadius = 280.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "50.0"))
	float MoveOrderInterceptRadius = 185.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "100.0"))
	float AttackOrderClusterRadius = 450.f;

	// Distance from anchor that triggers walking back to post, and tight arrival tolerance for HoldPosition.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "10.0"))
	float AnchorReturnTriggerDistance = 45.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|AI", meta = (ClampMin = "10.0"))
	float AnchorReturnAcceptanceRadius = 20.f;

private:
	void RequestBehaviorTree();
	void HandleBehaviorTreeLoaded();
	void QueueTreeRestart();
	void ApplyPendingTreeRestart();

	void SetCurrentAction(EIFSoldierAction NewAction);
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
	TObjectPtr<UAIPerceptionComponent> Perception;

	UPROPERTY(Transient)
	TObjectPtr<UBehaviorTree> LoadedBehaviorTree;

	// Retained until EndPlay so the BT cannot be GC'd mid-fight.
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
	float MoveGoalAcceptanceRadius = 75.f;
	bool bHasMoveGoal = false;

	EIFSoldierAction CurrentAction = EIFSoldierAction::None;
	int32 DecisionSerial = 0;
	bool bRestartQueued = false;
};
