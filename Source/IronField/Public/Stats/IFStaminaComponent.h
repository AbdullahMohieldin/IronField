#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IFStaminaComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStaminaChanged, float, Percent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStaminaDepleted);

UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFStaminaComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFStaminaComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Stamina|Events")
	FOnStaminaChanged OnStaminaChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Stamina|Events")
	FOnStaminaDepleted OnStaminaDepleted;

	UFUNCTION(BlueprintCallable, Category = "IronField|Stamina")
	bool TryConsumeStamina(float Amount);

	UFUNCTION(BlueprintCallable, Category = "IronField|Stamina")
	bool StartContinuousDrain(float DrainRate);

	UFUNCTION(BlueprintCallable, Category = "IronField|Stamina")
	void StopContinuousDrain();

	UFUNCTION(BlueprintCallable, Category = "IronField|Stamina")
	void InitializeStamina(float NewMaxStamina);

	UFUNCTION(BlueprintCallable, Category = "IronField|Stamina")
	void ResetStamina();

	UFUNCTION(BlueprintPure, Category = "IronField|Stamina")
	float GetMaxStamina() const { return MaxStamina; }

	UFUNCTION(BlueprintPure, Category = "IronField|Stamina")
	float GetCurrentStamina() const { return CurrentStamina; }

	UFUNCTION(BlueprintPure, Category = "IronField|Stamina")
	float GetStaminaPercent() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Stamina")
	bool HasStamina(float MinimumAmount) const { return CurrentStamina >= MinimumAmount; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Stamina", meta = (ClampMin = "1.0"))
	float MaxStamina = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Stamina", meta = (ClampMin = "0.0"))
	float StaminaRegenRate = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Stamina", meta = (ClampMin = "0.0"))
	float StaminaRegenDelay = 1.75f;

private:
	bool IsDraining() const { return ContinuousDrainRate > 0.f; }
	bool CanRegenerate() const;
	void DrainStamina(float DeltaTime);
	void RegenerateStamina(float DeltaTime);
	void UpdateTickEnabled();
	void SetStaminaClamped(float NewStamina);
	void BroadcastDepletedIfNeeded(float PreviousStamina);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Stamina", meta = (AllowPrivateAccess = "true"))
	float CurrentStamina = 100.f;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Stamina")
	float TimeSinceLastDrain = 0.f;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Stamina")
	float ContinuousDrainRate = 0.f;
};