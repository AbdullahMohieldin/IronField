#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Stats/IFStatUtils.h"
#include "IFStaminaComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStaminaDepleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStaminaChanged, float, Percent);

UCLASS()
class IRONFIELD_API UIFStaminaComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFStaminaComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Stamina|Events")
	FOnStaminaDepleted OnStaminaDepleted;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Stamina|Events")
	FOnStaminaChanged OnStaminaChanged;

	UFUNCTION(BlueprintCallable, Category = "IronField|Stamina|Actions")
	bool TryConsumeStamina(float Amount);

	UFUNCTION(BlueprintCallable, Category = "IronField|Stamina|Actions")
	void StartContinuousDrain(float DrainRate);

	UFUNCTION(BlueprintCallable, Category = "IronField|Stamina|Actions")
	void StopContinuousDrain();

	UFUNCTION(BlueprintPure, Category = "IronField|Stamina|State")
	float GetStaminaPercent() const { return IFStatUtils::ComputePercent(CurrentStamina, MaxStamina); }

	UFUNCTION(BlueprintPure, Category = "IronField|Stamina|State")
	bool HasStamina(float MinimumAmount) const { return CurrentStamina >= MinimumAmount; }

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Stamina|Attributes", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float MaxStamina = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Stamina|Attributes", meta = (AllowPrivateAccess = "true"))
	float StaminaRegenRate = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Stamina|Attributes", meta = (AllowPrivateAccess = "true"))
	float StaminaRegenDelay = 1.75f;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Stamina|Attributes", meta = (AllowPrivateAccess = "true"))
	float CurrentStamina = 100.f;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Stamina|Attributes", meta = (AllowPrivateAccess = "true"))
	float TimeSinceLastStaminaDrain = 0.f;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Stamina|Attributes", meta = (AllowPrivateAccess = "true"))
	float ContinuousDrainRate = 0.f;

	bool IsDrainingStamina() const { return ContinuousDrainRate > 0.f; }
	bool CanRegenerateStamina() const;
	void DrainStamina(float DeltaTime);
	void RegenerateStamina(float DeltaTime);
	void UpdateTickEnabled();

	void SetStaminaClamped(float NewStamina);
	void BroadcastStaminaDepletedIfNeeded(float PreviousStamina);

	friend class FIFStaminaComponentTest;
};
