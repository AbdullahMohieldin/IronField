#pragma once

#include "CoreMinimal.h"
#include "Character/IFBaseCharacter.h"
#include "Combat/IFCombatTypes.h"
#include "InputActionValue.h"
#include "UObject/SoftObjectPtr.h"
#include "IFPlayerCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UIFStaminaComponent;
struct FStreamableHandle;

UCLASS()
class IRONFIELD_API AIFPlayerCharacter : public AIFBaseCharacter
{
	GENERATED_BODY()

public:
	AIFPlayerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "IronField|Player|Components")
	UIFStaminaComponent* GetStaminaComponent() const { return StaminaComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Player|Movement")
	bool IsSprinting() const { return bIsSprinting; }

	UFUNCTION(BlueprintCallable, Category = "IronField|Player|Respawn")
	void RespawnAtCastle(const FVector& SpawnLocation, const FRotator& SpawnRotation);

	UFUNCTION(BlueprintCallable, Category = "IronField|Player|Combat")
	void StartBlock();

	UFUNCTION(BlueprintCallable, Category = "IronField|Player|Combat")
	void StopBlock();

	UFUNCTION(BlueprintCallable, Category = "IronField|Player|Combat")
	void RequestLeapSlam();

	UFUNCTION(BlueprintCallable, Category = "IronField|Player|Tactical")
	void EnterTacticalStandby();

	UFUNCTION(BlueprintCallable, Category = "IronField|Player|Tactical")
	void ExitTacticalStandby();

	UFUNCTION(BlueprintPure, Category = "IronField|Player|Tactical")
	bool IsInTacticalStandby() const { return bIsInTacticalStandby; }

	UFUNCTION(Exec)
	void DebugDamage(float Amount);

	virtual void Jump() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void OnDeathStarted() override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TSoftObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input", meta = (ClampMin = "0"))
	int32 DefaultMappingPriority = 0;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TSoftObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TSoftObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TSoftObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TSoftObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Input")
	TSoftObjectPtr<UInputAction> AttackAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> BlockAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LeapSlamAction;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 475.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement", meta = (ClampMin = "0.0"))
	float SprintSpeed = 720.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement", meta = (ClampMin = "0.0"))
	float BlockingSpeed = 260.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement", meta = (ClampMin = "0.0"))
	float AttackMoveSpeed = 325.f;

	// Minimum stick/keys deflection magnitude to sprint, in any direction.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SprintInputThreshold = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Camera", meta = (ClampMin = "-89.0", ClampMax = "89.0"))
	float CameraMinPitch = -65.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Camera", meta = (ClampMin = "-89.0", ClampMax = "89.0"))
	float CameraMaxPitch = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Camera")
	bool bInvertLookPitch = false;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Stamina", meta = (ClampMin = "0.0"))
	float SprintStaminaDrainRate = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Player|Stamina", meta = (ClampMin = "0.0"))
	float MinimumStaminaToStartSprint = 20.f;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Player|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFStaminaComponent> StaminaComponent;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Player|Movement")
	bool bIsSprinting = false;

	bool bSprintRequested = false;
	bool bIsInTacticalStandby = false;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Player|Movement")
	FVector2D CachedMovementInput = FVector2D::ZeroVector;

	TSharedPtr<FStreamableHandle> InputAssetsLoadHandle;

	void Move(const FInputActionValue& Value);
	void StopMoving();
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();
	void Attack();

	UFUNCTION()
	void HandleStaminaDepleted();

	UFUNCTION()
	void HandleCombatStateChanged(EIFCombatState PreviousState, EIFCombatState NewState);

	void RequestInputAssets();
	void HandleInputAssetsLoaded();
	void BindInputActions();
	void AddDefaultMappingContext();
	void RemoveDefaultMappingContext();

	void UpdateMovementSpeed();
	void UpdateSprintState();
	void UpdateRotationMode();
	bool HasSprintInput() const;
	float CalculateDesiredMovementSpeed() const;

};