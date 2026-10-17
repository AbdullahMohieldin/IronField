#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "IFWaveWarmupComponent.generated.h"

class APawn;
class USoundBase;
struct FStreamableHandle;

/**
 * Handles preloading enemy classes/assets and spawning brief warmup dummies to prevent hitches.
 */
UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFWaveWarmupComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIFWaveWarmupComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Warmup")
	bool bWarmupSpawns = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Warmup", meta = (ClampMin = "0.1"))
	float WarmupLifetimeSeconds = 0.35f;

	void StartWarmup(const TArray<TSubclassOf<APawn>>& EnemyClasses, TSubclassOf<AActor> HealPickupClass, USoundBase* WaveStartSound, USoundBase* HealSound);

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "IronField|Wave|Warmup")
	void CleanupWarmupActors();

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> WarmupActors;

	UPROPERTY(Transient)
	TArray<TSubclassOf<APawn>> CachedEnemyClasses;

	UPROPERTY(Transient)
	TSubclassOf<AActor> CachedHealPickupClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CachedWaveStartSound;

	bool bWarmedUp = false;
	FTimerHandle WarmupTimerHandle;
	TSharedPtr<FStreamableHandle> WarmupStreamableHandle;

	void HandleWarmupAssetsLoaded();
	void WarmupSpawnActors();
};
