#pragma once

#include "CoreMinimal.h"
#include "Character/IFBaseCharacter.h"
#include "Tactical/IFTacticalTypes.h"
#include "IFSoldierCharacter.generated.h"

class AActor;
class AIFSoldierAIController;
class UIFStaminaComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UWidgetComponent;

UCLASS()
class IRONFIELD_API AIFSoldierCharacter : public AIFBaseCharacter
{
	GENERATED_BODY()

public:
	AIFSoldierCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Components")
	UIFStaminaComponent* GetStaminaComponent() const { return StaminaComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	AIFSoldierAIController* GetSoldierController() const;

	float GetEffectiveAttackRange(const AActor* Target) const;
	float GetMaxMeleeEngagementRange(const AActor* Target) const;
	float GetHorizontalDistanceToTarget(const AActor* Target) const;
	FVector GetCombatApproachPoint(const AActor* Target) const;

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	FVector GetAnchorLocation() const { return bHasAnchorLocation ? AnchorLocation : GetActorLocation(); }

	// Pass false when called from controller decisions to avoid recursion.
	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|AI")
	void SetAnchorLocation(const FVector& NewAnchorLocation, bool bRefreshControllerDecision = true);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Formation")
	void SetAnchorFacingYaw(float NewFacingYaw);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Formation")
	void ClearAnchorFacing();

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Formation")
	bool HasAnchorFacing() const { return bHasAnchorFacing; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Formation")
	float GetAnchorFacingYaw() const { return AnchorFacingYaw; }

	void BeginAnchorFacingAlignment();
	void CancelAnchorFacingAlignment();

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	bool IsLeashedToAnchor() const { return bLeashToAnchor; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	float GetAutonomousEngageRadius() const { return AutonomousEngageRadius; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	bool ShouldAdvanceWhenIdle() const { return bAdvanceWhenIdle; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	bool ShouldAdvanceTowardsPlayer() const { return bAdvanceTowardsPlayer; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	AActor* GetAdvanceGoalActor() const { return AdvanceGoalActor; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	FVector GetAdvanceGoalLocation() const { return AdvanceGoalLocation; }

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|AI")
	float GetAdvanceStopDistance() const { return AdvanceStopDistance; }

	virtual void GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Tactical")
	void SetTacticalSelected(bool bSelected, bool bShowRingNow = true);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Tactical")
	void SetSelectionRingDisplayVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "IronField|Soldier|Tactical")
	void SetHostileTargetHighlighted(bool bHighlighted);

	UFUNCTION(BlueprintPure, Category = "IronField|Soldier|Tactical")
	bool IsTacticalSelected() const { return bIsTacticalSelected; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleDeath(AActor* DeadActor) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Combat", meta = (ClampMin = "0.0"))
	float AttackRange = 60.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Combat", meta = (ClampMin = "0.0"))
	float AttackRangeBuffer = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Death", meta = (ClampMin = "0.0"))
	float CorpseLifeSpan = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Death")
	bool bEnableRagdollDeath = true;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Death", meta = (ClampMin = "0.0"))
	float RagdollTransitionDelaySeconds = 0.28f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Death", meta = (ClampMin = "0.0"))
	float RagdollDeathImpulse = 220.f;

	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI")
	bool bLeashToAnchor = true;

	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float AutonomousEngageRadius = 400.f;

	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI")
	bool bAdvanceWhenIdle = false;

	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI")
	bool bAdvanceTowardsPlayer = true;

	UPROPERTY(EditInstanceOnly, Category = "IronField|Soldier|AI")
	TObjectPtr<AActor> AdvanceGoalActor;

	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI")
	FVector AdvanceGoalLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "IronField|Soldier|AI", meta = (ClampMin = "0.0"))
	float AdvanceStopDistance = 350.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Soldier|UI")
	FLinearColor AllyHealthBarColor = FLinearColor(0.15f, 0.55f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Soldier|UI")
	FLinearColor EnemyHealthBarColor = FLinearColor(0.90f, 0.18f, 0.18f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Tactical")
	TObjectPtr<UStaticMesh> SelectionRingMesh;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Tactical")
	TObjectPtr<UMaterialInterface> SelectionRingMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Tactical")
	TObjectPtr<UMaterialInterface> HostileTargetRingMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Tactical", meta = (ClampMin = "0.0"))
	float SelectionRingGroundClearance = 2.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Formation", meta = (ClampMin = "1.0"))
	float AnchorFacingTurnSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Soldier|Formation", meta = (ClampMin = "0.2", ClampMax = "15.0"))
	float AnchorFacingToleranceDegrees = 1.5f;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFStaminaComponent> StaminaComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> OverheadWidgetComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Tactical", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> SelectionRingComponent;

	void UpdateSelectionRingTransform();
	void RefreshTacticalRingVisual();
	void UpdateOverheadWidgetVisibility();

	UFUNCTION()
	void HandleHealthChanged(float HealthPercent);

	EIFTacticalRingMode TacticalRingMode = EIFTacticalRingMode::None;
	bool bIsTacticalSelected = false;
	bool bSelectionRingDisplayEnabled = false;

	FVector AnchorLocation = FVector::ZeroVector;
	float AnchorFacingYaw = 0.f;
	bool bHasAnchorFacing = false;
	bool bIsAligningToAnchorFacing = false;
	bool bHasAnchorLocation = false;

	void ActivateRagdollDeath();
	FTimerHandle RagdollTransitionTimerHandle;
};
