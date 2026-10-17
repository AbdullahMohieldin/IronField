#pragma once

#include "CoreMinimal.h"
#include "Stats/IFStatComponent.h"
#include "IFHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnHealthDepleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, float, Percent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamaged, AActor*, InstigatorActor, float, Damage);

UCLASS()
class IRONFIELD_API UIFHealthComponent : public UIFStatComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "IronField|Health|Events")
	FOnHealthDepleted OnHealthDepleted;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Health|Events")
	FOnHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Health|Events")
	FOnDamaged OnDamaged;

	UFUNCTION(BlueprintCallable, Category = "IronField|Health|Actions")
	void ApplyHealing(float Amount);

	/** Only valid while dead; reviving a living character does nothing. */
	UFUNCTION(BlueprintCallable, Category = "IronField|Health|Actions")
	void Revive();

	UFUNCTION(BlueprintCallable, Category = "IronField|Health|Actions")
	void SetInvincible(bool bNewInvincible) { bIsInvincible = bNewInvincible; }

	/** Sets MaxHealth. Callable in constructors (per-class HP pools) and at runtime (wave scaling). */
	UFUNCTION(BlueprintCallable, Category = "IronField|Health|Actions")
	void SetMaxHealth(float NewMaxHealth, bool bFillHealthToMax = true);

	/** Sets the health restored by Revive(). Used for the player revive tuning. */
	UFUNCTION(BlueprintCallable, Category = "IronField|Health|Actions")
	void SetReviveHealth(float NewReviveHealth);

	UFUNCTION(BlueprintPure, Category = "IronField|Health|State")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "IronField|Health|State")
	float GetHealthPercent() const { return ComputePercent(CurrentHealth, MaxHealth); }

	UFUNCTION(BlueprintPure, Category = "IronField|Health|State")
	bool IsDead() const { return bIsDead; }

	UFUNCTION(BlueprintPure, Category = "IronField|Health|State")
	bool IsInvincible() const { return bIsInvincible; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Health|Attributes", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float MaxHealth = 100.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Health|Attributes", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float ReviveHealth = 30.f;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Health|Attributes", meta = (AllowPrivateAccess = "true"))
	float CurrentHealth = 100.f;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Health|Attributes", meta = (AllowPrivateAccess = "true"))
	bool bIsInvincible = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Health|State", meta = (AllowPrivateAccess = "true"))
	bool bIsDead = false;

	// Internal only; damage must enter through TakeDamage so the instigator is tracked.
	void ApplyDamage(float Amount);

	UFUNCTION()
	void HandleOwnerTakeAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

	void SetHealthClamped(float NewHealth);

	friend class FIFHealPickupFullHealthTest;
};
