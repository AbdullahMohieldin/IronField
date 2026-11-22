#pragma once

#include "CoreMinimal.h"
#include "Castle/IFStructure.h"
#include "IFGuardTower.generated.h"

class AIFTowerProjectile;

/**
 * Defensive structure providing automated ranged fire at hostile characters within AttackRange.
 * Zero tick: driven solely by an FTimerHandle active only while the tower is Active.
 */
UCLASS()
class IRONFIELD_API AIFGuardTower : public AIFStructure
{
	GENERATED_BODY()

public:
	AIFGuardTower();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnStructureActivated() override;
	virtual void OnStructureDeactivated() override;
	virtual void OnTeamChanged(FGenericTeamId PreviousTeam, FGenericTeamId NewTeam) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|GuardTower", meta = (ClampMin = "100.0"))
	float AttackRange = 950.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|GuardTower", meta = (ClampMin = "0.1"))
	float FireIntervalSeconds = 1.25f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|GuardTower", meta = (ClampMin = "1.0"))
	float TowerDamage = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|GuardTower")
	FVector MuzzleOffset = FVector(0.f, 0.f, 420.f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|GuardTower")
	TSubclassOf<AIFTowerProjectile> ProjectileClass;

private:
	void StartFireTimer();
	void StopFireTimer();
	void TryFireAtNearestHostile();

	FTimerHandle FireTimerHandle;
};
