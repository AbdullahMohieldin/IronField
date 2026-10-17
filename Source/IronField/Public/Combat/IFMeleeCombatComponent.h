#pragma once

#include "CoreMinimal.h"
#include "Combat/IFComboCombatComponent.h"
#include "IFMeleeCombatComponent.generated.h"

UCLASS()
class IRONFIELD_API UIFMeleeCombatComponent : public UIFComboCombatComponent
{
	GENERATED_BODY()

public:
	virtual float GetComboContinueChance(int32 ComboIndex) const override;

protected:
	virtual bool RequiresWeaponCollisionBox() const override { return true; }
	virtual float GetCurrentAttackDamage() const override;

private:
	/** Per-combo-step chance (0-1) to auto-continue after that step ends. Index matches ComboSteps. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|AI", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", ClampMax = "1.0"))
	TArray<float> ComboContinueChances = { 0.45f, 0.3f };

	/** Scales BP-authored ComboStep damage (0.8 x 20 = 16 for default steps). */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Combat|AI", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float DamageMultiplier = 0.8f;
};
