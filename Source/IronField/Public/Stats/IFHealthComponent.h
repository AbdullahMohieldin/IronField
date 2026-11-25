#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IFHealthComponent.generated.h"

class AActor;
class AController;
class UDamageType;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, float, Percent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeath, AActor*, DeadActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamaged, AActor*, InstigatorActor, float, Damage);

UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFHealthComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Health|Events")
	FOnHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Health|Events")
	FOnDeath OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Health|Events")
	FOnDamaged OnDamaged;

	UFUNCTION(BlueprintCallable, Category = "IronField|Health")
	void InitializeHealth(float NewMaxHealth);

	UFUNCTION(BlueprintCallable, Category = "IronField|Health")
	float ApplyDamage(float Amount, AActor* InstigatorActor = nullptr);

	UFUNCTION(BlueprintCallable, Category = "IronField|Health")
	void ApplyHealing(float Amount);

	UFUNCTION(BlueprintCallable, Category = "IronField|Health")
	void Revive();

	UFUNCTION(BlueprintPure, Category = "IronField|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "IronField|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "IronField|Health")
	float GetHealthPercent() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Health")
	bool IsDead() const { return bIsDead; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Health", meta = (ClampMin = "1.0"))
	float ReviveHealth = 60.f;

private:
	UFUNCTION()
	void HandleOwnerTakeAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

	void SetHealthClamped(float NewHealth);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Health", meta = (AllowPrivateAccess = "true"))
	float CurrentHealth = 150.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Health", meta = (AllowPrivateAccess = "true"))
	bool bIsDead = false;

	bool bHealthInitialized = false;
};