#pragma once

#include "CoreMinimal.h"
#include "Combat/IFTeamTypes.h"
#include "GameFramework/Character.h"
#include "IFBaseCharacter.generated.h"

class UIFCombatComponent;
class UIFHealthComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharacterDied, AIFBaseCharacter*, Character);

UCLASS()
class IRONFIELD_API AIFBaseCharacter : public ACharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AIFBaseCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static const FName CombatComponentName;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Character|Events")
	FOnCharacterDied OnCharacterDied;

	UFUNCTION(BlueprintPure, Category = "IronField|Character|Components")
	UIFHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Character|Components")
	UIFCombatComponent* GetCombatComponent() const { return CombatComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Character|State")
	bool IsDead() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Character|State")
	bool IsAlive() const { return !IsDead(); }

	UFUNCTION(BlueprintPure, Category = "IronField|Character|Combat")
	FVector GetLastDamageDirection() const { return LastDamageDirection; }

	void ApplyLocalHitstop(float DurationSeconds, float TimeDilation = 0.05f);
	void ClearLocalHitstop();

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override { TeamID = InTeamID; }
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamID; }
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

	virtual void Landed(const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Team")
	FGenericTeamId TeamID = FGenericTeamId(IFTeamIds::Neutral);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Character|Animation", meta = (ClampMin = "0.0"))
	float DeathMontageBlendOutTime = 0.15f;

	virtual void OnDeathStarted() {}

	UFUNCTION()
	virtual void HandleDeath(AActor* DeadActor);

	void RestoreAliveState();

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Character|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Character|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFCombatComponent> CombatComponent;

	void StopMovementForDeath();
	void DisableCollisionForDeath();

	bool bHasDied = false;

	FVector LastDamageDirection = FVector::ZeroVector;
	FTimerHandle HitstopTimerHandle;
	bool bInLocalHitstop = false;
};