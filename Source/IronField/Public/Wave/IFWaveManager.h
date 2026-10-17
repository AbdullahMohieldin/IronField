#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Wave/IFSpawnContext.h"
#include "Wave/IFSpawnQueue.h"
#include "Wave/IFWaveScalingUtils.h"
#include "IFWaveManager.generated.h"

class APawn;
class AIFPlayerCharacter;
class AIFHealPickup;
class UIFWaveRewardComponent;
class UIFWaveWarmupComponent;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerDowned);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveStarted, int32, WaveNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveCompleted, int32, WaveNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWaveClearHeal, int32, WaveNumber, float, PlayerHealed, float, GateRepaired);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemiesAliveCountChanged, int32, NewCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnKillCountChanged, int32, NewCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllWavesCompleted);

USTRUCT(BlueprintType)
struct FEnemyGroupDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Config")
	TSubclassOf<APawn> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Config", meta = (ClampMin = "1"))
	int32 EnemyCount = 5;
};

USTRUCT(BlueprintType)
struct FWaveDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Config")
	TArray<FEnemyGroupDefinition> EnemyGroups;
};

UCLASS()
class IRONFIELD_API AIFWaveManager : public AActor
{
	GENERATED_BODY()

public:
	AIFWaveManager();

	// Broadcast each time the player's health is depleted.
	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnPlayerDowned OnPlayerDowned;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnWaveStarted OnWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnWaveCompleted OnWaveCompleted;

	// Broadcast after wave-clear healing is applied so HUD can show exact amounts.
	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnWaveClearHeal OnWaveClearHeal;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnEnemiesAliveCountChanged OnEnemiesAliveCountChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnKillCountChanged OnKillCountChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnAllWavesCompleted OnAllWavesCompleted;

	// Authored sequence for Normal mode only. Unused in Unlimited mode.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config")
	TArray<FWaveDefinition> Waves;

	// Single base definition for Unlimited mode.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config")
	FWaveDefinition UnlimitedBaseWave;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config")
	bool bAutoStartOnBeginPlay = true;

	// Multiplier applied to UnlimitedBaseWave enemy counts per wave.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "1.0"))
	float UnlimitedEnemyCountScaleFactor = 1.15f;

	// Per-wave scaling multipliers (U1: default 1.0 preserves identical baseline gameplay).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Scaling", meta = (ClampMin = "1.0"))
	float WaveHealthMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Scaling", meta = (ClampMin = "1.0"))
	float WaveDamageMultiplier = 1.0f;

	/** Random XY offset applied around spawn points so enemies do not stack on one spot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "0.0"))
	float SpawnLocationJitterRadius = 150.f;

	/** Breather between waves so the run has rhythm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "0.0"))
	float InterWaveDelaySeconds = 8.f;

	/** Stagger between spawns so the whole wave never pops in one frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "0.05"))
	float SpawnIntervalSeconds = 0.5f;

	/** Delay before dead enemies are destroyed so death animations finish playing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "0.1"))
	float CorpseDespawnDelaySeconds = 1.0f;

	/** Wait before the first spawn of each wave so the banner reads before contact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "0.0"))
	float SpawnStartDelaySeconds = 2.5f;

	/** Grace after level load before wave 1 starts (auto-start only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "0.0"))
	float InitialWaveStartDelaySeconds = 5.f;

	/** Cap on simultaneous alive enemies; the queue trickles in as they die. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "1"))
	int32 MaxConcurrentAlive = 5;

	/** How often the active wave reconciles its alive-count against live enemies. 0 disables. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "0.0"))
	float WaveValidationIntervalSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Feedback")
	TObjectPtr<USoundBase> WaveStartSound;

	// Authored wave-clear heal & drops (forwarded to RewardComponent)
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

	// Authored warmup configuration (forwarded to WarmupComponent)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Warmup")
	bool bWarmupSpawns = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Warmup", meta = (ClampMin = "0.1"))
	float WarmupLifetimeSeconds = 0.35f;

	// Getters for encapsulated state
	UFUNCTION(BlueprintPure, Category = "IronField|Wave|State")
	int32 GetCurrentWave() const { return CurrentWaveIndex + 1; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|State")
	int32 GetKillCount() const { return KillCount; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|State")
	int32 GetEnemiesAlive() const { return EnemiesAlive; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|State")
	bool IsWaveActive() const { return bIsWaveActive; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|State")
	int32 GetTotalEnemiesInWave() const { return TotalEnemiesInWave; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|State")
	int32 GetEnemiesSpawnedSoFar() const { return EnemiesSpawnedSoFar; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|Targeting")
	AActor* GetPlayerActor() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|Targeting")
	AActor* GetStrongholdActor() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|Targeting")
	float GetSiegeBiasForWave() const { return CurrentWaveIndex <= 0 ? 1.f : 0.f; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|Components")
	UIFWaveRewardComponent* GetRewardComponent() const { return RewardComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|Components")
	UIFWaveWarmupComponent* GetWarmupComponent() const { return WarmupComponent; }

	UFUNCTION(BlueprintCallable, Category = "IronField|Wave|Actions")
	void StartNextWave();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFWaveRewardComponent> RewardComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFWaveWarmupComponent> WarmupComponent;

	// Encapsulated state properties
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State", meta = (AllowPrivateAccess = "true"))
	int32 TotalEnemiesInWave = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State", meta = (AllowPrivateAccess = "true"))
	int32 EnemiesSpawnedSoFar = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State", meta = (AllowPrivateAccess = "true"))
	int32 EnemiesAlive = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State", meta = (AllowPrivateAccess = "true"))
	int32 KillCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State", meta = (AllowPrivateAccess = "true"))
	bool bIsWaveActive = false;

	UPROPERTY(Transient)
	TObjectPtr<AIFPlayerCharacter> CachedPlayer;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> SpawnedEnemies;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnPoints;

	int32 CurrentWaveIndex = -1;

	UPROPERTY(Transient)
	FIFSpawnQueue SpawnQueue;

	FTimerHandle InterWaveTimerHandle;
	FTimerHandle SpawnTimerHandle;
	FTimerHandle ValidateTimerHandle;
	FTimerHandle CorpseCleanupTimerHandle;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> PendingCorpses;

	uint32 WaveGeneration = 0;

	void NotifyEnemySpawned(APawn* Enemy);
	void SpawnAllEnemiesInWave(const FWaveDefinition& Wave);
	void BuildSpawnQueue(const FWaveDefinition& Wave);

	UFUNCTION()
	void TrySpawnPending();

	UFUNCTION()
	void HandleInterWaveTimer(uint32 ScheduledGeneration);

	UFUNCTION()
	void HandleSpawnTimerTick(uint32 ScheduledGeneration);

	UFUNCTION()
	void HandleValidateTimer(uint32 ScheduledGeneration);

	void ScheduleNextWave();
	FVector PickSpawnLocation(FRotator& OutRotation) const;
	bool IsUnlimitedRunMode() const;
	FWaveDefinition BuildUnlimitedWave(int32 WaveIndex) const;
	void BeginWaveFromDefinition(const FWaveDefinition& Wave);
	void PlayWaveStartFeedback() const;
	void CompleteWaveIfFinished();
	void ValidateWaveState();
	void CleanupWaveCorpses();
	void UnbindAllSpawnedEnemyDelegates();
	void CacheSpawnPoints();

	UFUNCTION()
	void HandleActorRegistered(FName ActorRole, AActor* Actor);

	void HandlePlayerRegistered(AIFPlayerCharacter* Player);

	UFUNCTION()
	void HandlePlayerHealthDepleted();

	UFUNCTION()
	void HandleEnemyDied(AActor* DeadActor);

	UFUNCTION()
	void HandleRewardWaveClearHeal(int32 WaveNumber, float InPlayerHealed, float InGateRepaired);

	void PreloadWaveAssets();
	void CollectWarmupEnemyClasses(TArray<TSubclassOf<APawn>>& OutClasses) const;

	void BindPlayer(AIFPlayerCharacter* Player);
	void UnbindPlayer();

	friend class FIFWaveCorpseCleanupTest;
};
