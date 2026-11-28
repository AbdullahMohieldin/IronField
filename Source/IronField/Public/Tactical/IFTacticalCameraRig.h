#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IFTacticalCameraRig.generated.h"

class APawn;
class UCameraComponent;
class USceneComponent;
class USpringArmComponent;

/**
 * Overhead RTS camera rig used while Tactical Command Mode is active.
 * Keeps tick disabled in Action Mode and uses undilated delta time while active
 * (and during slow-mo exit ramps) so panning, zoom, rotation and time dilation stay smooth.
 */
UCLASS()
class IRONFIELD_API AIFTacticalCameraRig : public AActor
{
	GENERATED_BODY()

public:
	AIFTacticalCameraRig();

	virtual void Tick(float DeltaSeconds) override;

	void ActivateTacticalCamera(const APawn* CommanderPawn, float InitialYaw, float InTargetTimeDilation, float InRampDurationSeconds);
	void DeactivateTacticalCamera(float InRampDurationSeconds);
	void ForceResetTimeDilation();

	void SetPanInput(const FVector2D& InPanInput);
	void AddZoomInput(float WheelDelta);
	void SetYawInput(float InYawInput);
	void ClearControlInputs();

	bool IsTacticalCameraActive() const { return bIsTacticalActive; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Tactical|Camera")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Tactical|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Tactical|Camera")
	TObjectPtr<UCameraComponent> TacticalCamera;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Zoom", meta = (ClampMin = "100.0"))
	float MinArmLength = 900.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Zoom", meta = (ClampMin = "100.0"))
	float MaxArmLength = 2500.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Zoom", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultZoomAlpha = 0.45f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Zoom", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float ZoomStep = 0.10f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Zoom", meta = (ClampMin = "0.1"))
	float ZoomInterpSpeed = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Zoom")
	bool bInvertZoom = false;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Zoom", meta = (ClampMin = "-89.0", ClampMax = "-10.0"))
	float NearPitch = -48.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Zoom", meta = (ClampMin = "-89.0", ClampMax = "-10.0"))
	float FarPitch = -65.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Movement", meta = (ClampMin = "0.0"))
	float MinPanSpeed = 1500.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Movement", meta = (ClampMin = "0.0"))
	float MaxPanSpeed = 2800.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Movement", meta = (ClampMin = "0.0"))
	float YawRotationSpeed = 110.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Movement", meta = (ClampMin = "1.0"))
	float PanAccelerationInterpSpeed = 15.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Movement", meta = (ClampMin = "1.0"))
	float PanDecelerationInterpSpeed = 9.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Movement", meta = (ClampMin = "1.0"))
	float YawAccelerationInterpSpeed = 14.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Movement", meta = (ClampMin = "1.0"))
	float YawDecelerationInterpSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Bounds", meta = (ClampMin = "0.0"))
	float MaxPanRadiusFromCommander = 12000.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Bounds")
	bool bUseWorldBoundsClamp = false;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Bounds", meta = (EditCondition = "bUseWorldBoundsClamp"))
	FVector2D WorldBoundsMin = FVector2D(-20000.f, -20000.f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Bounds", meta = (EditCondition = "bUseWorldBoundsClamp"))
	FVector2D WorldBoundsMax = FVector2D(20000.f, 20000.f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Terrain", meta = (ClampMin = "0.0"))
	float GroundTraceUpOffset = 350.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Terrain", meta = (ClampMin = "0.0"))
	float GroundTraceDownOffset = 600.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Terrain", meta = (ClampMin = "0.0"))
	float MaxGroundStepHeightDelta = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinWalkableNormalZ = 0.65f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|Camera|Terrain", meta = (ClampMin = "0.1"))
	float GroundZInterpSpeed = 6.f;

private:
	void UpdateTimeDilationRamp(float RealDeltaSeconds);
	void ApplyCameraZoomAndPitch();
	void UpdateTerrainFollowZ(float XYLocationX, float XYLocationY, float RealDeltaSeconds);
	FVector ClampLocationToBounds(const FVector& DesiredLocation) const;

	TWeakObjectPtr<const APawn> TrackedCommander;
	FVector ActivationOrigin = FVector::ZeroVector;

	FVector2D ActivePanInput = FVector2D::ZeroVector;
	FVector CurrentPanVelocity = FVector::ZeroVector;
	float ActiveYawInput = 0.f;
	float CurrentYawVelocity = 0.f;

	float CurrentZoomAlpha = 0.45f;
	float TargetZoomAlpha = 0.45f;

	float CurrentGroundZ = 0.f;
	float TargetGroundZ = 0.f;

	float CurrentTimeDilation = 1.f;
	float TargetTimeDilation = 1.f;
	float TimeDilationRampRate = 0.f;

	bool bIsTacticalActive = false;
	bool bNeedsInitialGroundTrace = false;
};
