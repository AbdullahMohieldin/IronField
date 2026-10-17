#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IFWaveRewardComponent.generated.h"

class AIFHealPickup;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWaveRewardClearHeal, int32, WaveNumber, float, PlayerHealed, float, GateRepaired);

/**
 * Handles wave-clear healing for player/stronghold and enemy death heal pickup drops.
 */
UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFWaveRewardComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFWaveRewardComponent();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnWaveRewardClearHeal OnWaveClearHeal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Heal", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float PlayerHealPercent = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Heal", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StrongholdRepairPercent = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Drops", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float DropChance = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Drops", meta = (ClampMin = "1.0"))
	float PickupHealAmount = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Drops")
	TSubclassOf<AIFHealPickup> HealPickupClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Feedback")
	TObjectPtr<USoundBase> HealSound;

	UFUNCTION(BlueprintCallable, Category = "IronField|Wave|Rewards")
	void ApplyWaveClearHeal(int32 WaveNumber);

	UFUNCTION(BlueprintCallable, Category = "IronField|Wave|Rewards")
	void TrySpawnHealPickup(AActor* DeadEnemy);
};
