#pragma once

#include "CoreMinimal.h"
#include "Character/IFBaseCharacter.h"
#include "Combat/IFCombatTypes.h"
#include "IFEnemyCharacter.generated.h"

/**
 * Shared enemy base. CombatRange is the extra gap between collision surfaces
 * at which AI attacks; configure it per enemy Blueprint to match its weapon.
 */
UCLASS(Abstract)
class IRONFIELD_API AIFEnemyCharacter : public AIFBaseCharacter
{
	GENERATED_BODY()

public:
	AIFEnemyCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "IronField|Enemy|Combat")
	float GetCombatRange() const { return CombatRange; }

	/** Rolled aggression: 0 = sieger soul, 1 = hunter soul. */
	UFUNCTION(BlueprintPure, Category = "IronField|Enemy|Targeting")
	float GetAggression() const { return Aggression; }

	UFUNCTION(BlueprintPure, Category = "IronField|Enemy|Targeting")
	float GetLastPlayerHitTime() const { return LastPlayerHitTime; }

	UFUNCTION(BlueprintPure, Category = "IronField|Enemy|Targeting")
	float GetLastTargetSwitchTime() const { return LastTargetSwitchTime; }

	/** Rolls personality around BaseAggression, shifted by wave bias. Called on possess. */
	void RollAggression(float Bias, float Spread);

	/** Records a player hit so the AI can hold a grudge. Called from combat. */
	void NotifyHitByPlayer();

	/** Stamps a target switch for commitment locking. Called from the AI target service. */
	void NotifyTargetSwitched();

	void ApplyMovementSpeedForState(ECombatState State);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleDamaged(AActor* InstigatorActor, float Damage);

protected:
	/** Population mean personality; the roll adds variance around this. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Enemy|Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaseAggression = 0.5f;

	/** Rolled per spawn; Transient so BPs never serialize a stale roll. */
	UPROPERTY(Transient)
	float Aggression = 0.5f;

	/** Sentinel: large-negative so the (Now - last < window) test stays false until a real stamp is recorded. */
	static constexpr float NeverTimestamp = -1000.f;

	/** World time of the last player-caused hit; NeverTimestamp = never. */
	UPROPERTY(Transient)
	float LastPlayerHitTime = NeverTimestamp;

	/** World time of the last target switch; NeverTimestamp = never. */
	UPROPERTY(Transient)
	float LastTargetSwitchTime = NeverTimestamp;
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Enemy|Combat", meta = (ClampMin = "1.0", ToolTip = "Extra distance allowed between the enemy and target collision surfaces before attacking. Set this to the weapon's actual reach in the enemy Blueprint."))
	float CombatRange = 80.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Enemy|Movement", meta = (ClampMin = "0.0"))
	float ChaseSpeed = 360.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Enemy|Movement", meta = (ClampMin = "0.0"))
	float AttackingSpeed = 180.f;
};
