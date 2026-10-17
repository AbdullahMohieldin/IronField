#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "IFEnemyAIData.generated.h"

/** Shared enemy AI tunables, referenced by AIFEnemyController. */
UCLASS(Blueprintable)
class IRONFIELD_API UIFEnemyAIData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Combat", meta = (ClampMin = "0.0"))
	float MinReattackCooldownSeconds = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Combat", meta = (ClampMin = "0.0"))
	float MaxReattackCooldownSeconds = 1.6f;

	/** Player farther than this → target the stronghold instead. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "0.0"))
	float PlayerDetectionRange = 2000.f;

	/** Seconds an enemy stays locked on a target before it may switch (death/invalid bypasses). */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "0.0"))
	float CommitLockSeconds = 3.f;

	/** Score multiplier for the current target (stickiness). */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "1.0"))
	float CommitScoreBonus = 1.6f;

	/** Challenger must beat the champion by this factor to steal the target. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "1.0"))
	float SwitchThreshold = 1.35f;

	/** Player-score multiplier while holding a grudge. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "1.0"))
	float RetaliationBonus = 3.f;

	/** Seconds after a player hit during which the enemy holds its grudge. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "0.0"))
	float RetaliationSeconds = 4.f;

	/** Random spread applied around base aggression at spawn. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AggressionSpread = 0.35f;

	/** Distance falloff range used by target scoring curve. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "1.0"))
	float DistanceFalloffRange = 800.f;

	/** Base weight applied to player candidate in target scoring. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "0.0"))
	float PlayerWeightBase = 0.5f;

	/** Base weight applied to stronghold candidate in target scoring. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|AI|Targeting", meta = (ClampMin = "0.0"))
	float StrongholdWeightBase = 1.5f;
};
