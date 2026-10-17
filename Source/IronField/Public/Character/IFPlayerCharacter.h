#pragma once

#include "CoreMinimal.h"
#include "Character/IFBaseCharacter.h"
#include "Combat/IFCombatTypes.h"
#include "InputActionValue.h"
#include "IFPlayerCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UIFPlayerCombatComponent;
class UIFStaminaComponent;

UCLASS()
class IRONFIELD_API AIFPlayerCharacter : public AIFBaseCharacter
{
	GENERATED_BODY()

public:
	AIFPlayerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "IronField|Player|Movement")
	bool IsSprinting() const { return bIsSprinting; }

	UFUNCTION(BlueprintPure, Category = "IronField|Player|Health")
	float GetHealthPercent() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Player|Stamina")
	float GetStaminaPercent() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Player|Components")
	UIFStaminaComponent* GetStaminaComponent() const { return StaminaComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Player|State")
	bool IsGettingUp() const { return bIsGettingUp; }

	UFUNCTION(BlueprintCallable, Category = "IronField|Player|State")
	void NotifyGetUpFinished();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Jump() override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TObjectPtr<UInputMappingContext> DefaultInputMappingContext;

	/** Priority for DefaultInputMappingContext; higher wins when several contexts are active. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input", meta = (ClampMin = "0"))
	int32 DefaultInputMappingPriority = 0;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TObjectPtr<UInputAction> MoveInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TObjectPtr<UInputAction> LookInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TObjectPtr<UInputAction> JumpInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TObjectPtr<UInputAction> SprintInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TObjectPtr<UInputAction> BlockInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TObjectPtr<UInputAction> AttackInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TObjectPtr<UInputAction> SpinAttackInputAction;

	// Game-level toggle bound beside the other actions; key choice lives in IA_Pause.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TObjectPtr<UInputAction> PauseInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement")
	float WalkSpeed = 375.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement")
	float BackpedalSpeed = 240.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement")
	float BlockingSpeed = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement")
	float SprintSpeed = 620.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement")
	float AttackMoveSpeed = 300.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement")
	float SprintInputThreshold = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement")
	float BackpedalInputThreshold = -0.1f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement")
	float SprintExitSpeedSquared = 10000.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Camera", meta = (ClampMin = "-89.0", ClampMax = "-10.0"))
	float CameraMinPitch = -75.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Camera", meta = (ClampMin = "-89.0", ClampMax = "-10.0"))
	float CameraMaxPitch = -40.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Camera")
	bool bInvertLookPitch = false;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Stamina")
	float SprintStaminaDrainRate = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Stamina")
	float MinimumStaminaToStartSprint = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Gameplay")
	float ReviveDelaySeconds = 6.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Gameplay")
	float GetUpDuration = 1.5f;

	virtual void OnDeathStarted() override;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFStaminaComponent> StaminaComponent;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Player|Movement", meta = (AllowPrivateAccess = "true"))
	bool bIsSprinting = false;

	bool bSprintRequested = false;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Player|Movement", meta = (AllowPrivateAccess = "true"))
	FVector2D CachedMovementInput = FVector2D::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Player|State", meta = (AllowPrivateAccess = "true"))
	bool bIsGettingUp = false;

	void Move(const FInputActionValue& Value);
	void StopMoving();
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void Attack();
	void StartBlock();
	void StopBlock();
	void StartSpinAttack();
	void StopSpinAttack();
	void RequestPauseToggle();

	UFUNCTION()
	void HandleStaminaDepleted();

	// Shared gate for attack/block/spin: rejects dead/get-up and cancels sprint first.
	bool TryPrepareCombatAction();

	UFUNCTION()
	void HandleCombatStateChanged(ECombatState PreviousState, ECombatState NewState);

	void UpdateMovementSpeed();
	void UpdateSprintState();
	bool HasSprintInput() const;
	float CalculateDesiredMovementSpeed() const;
	UIFPlayerCombatComponent* GetPlayerCombatComponent() const;

	void UpdateTickEnabled();

	void ClearReviveTimers();
	void StartReviveTimer();
	void AttemptRevive();
	void CompleteRevive();

	FTimerHandle ReviveTimerHandle;
	FTimerHandle GetUpFallbackTimerHandle;
};
