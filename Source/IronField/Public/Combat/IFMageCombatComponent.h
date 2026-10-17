#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatComponent.h"
#include "IFMageCombatComponent.generated.h"

class AIFProjectile;

/**
 * Ranged combat for the mage: cast montage + projectile fire.
 * The inherited melee combo categories are hidden because they do not apply.
 */
UCLASS(HideCategories = ("IronField|Combat|Combo", "IronField|Combat|State"))
class IRONFIELD_API UIFMageCombatComponent : public UIFCombatComponent
{
	GENERATED_BODY()

public:
	virtual void StartAttack() override;
	virtual void LaunchProjectileAttack() override;
	virtual void BeginAttackCollision() override {}
	virtual void EndAttackCollision() override {}

	/** Multiplies per-instance damage for wave scaling. */
	void ApplyDamageScale(float Multiplier) { AttackDamage = FMath::Max(0.f, AttackDamage * Multiplier); }

	UFUNCTION(BlueprintPure, Category = "IronField|Enemy|Projectile")
	TSubclassOf<AIFProjectile> GetProjectileClass() const { return ProjectileClass; }

protected:
	virtual float GetCurrentAttackDamage() const override { return AttackDamage; }
	virtual TSubclassOf<UDamageType> GetCurrentDamageTypeClass() const override { return DamageTypeClass; }
	virtual bool CanQueueComboAttack() const override { return false; }

private:
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Enemy|Cast", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> CastMontage;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Enemy|Projectile", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AIFProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Enemy|Projectile", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float ProjectileSpawnForwardOffset = 60.f;

	// Live blackboard target first, attack-start target as fallback.
	AActor* ResolveLiveTarget() const;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Enemy|Damage", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float AttackDamage = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Enemy|Damage", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UDamageType> DamageTypeClass;
};
