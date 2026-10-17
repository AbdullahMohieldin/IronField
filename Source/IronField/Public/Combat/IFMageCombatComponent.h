#pragma once

#include "CoreMinimal.h"
#include "Combat/IFCombatComponent.h"
#include "IFMageCombatComponent.generated.h"

class AIFProjectile;
class UAnimMontage;

/**
 * Ranged combat for the mage: cast montage + projectile fire.
 */
UCLASS()
class IRONFIELD_API UIFMageCombatComponent : public UIFCombatComponent
{
	GENERATED_BODY()

public:
	virtual void StartAttack() override;
	virtual void CancelAttack() override;
	virtual void LaunchProjectileAttack() override;

	UFUNCTION(BlueprintPure, Category = "IronField|Enemy|Projectile")
	TSubclassOf<AIFProjectile> GetProjectileClass() const { return ProjectileClass; }

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

	void ClearCastMontageDelegate();

	UFUNCTION()
	void HandleCastMontageEnded(UAnimMontage* Montage, bool bInterrupted);
};
