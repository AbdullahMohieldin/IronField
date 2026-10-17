#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "IFWaveManager.generated.h"

class AIFPlayerCharacter;
class AIFBaseCharacter;
class AIFHealPickup;
class USoundBase;
struct FStreamableHandle;

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
	TSubclassOf<AIFBaseCharacter> EnemyClass;

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

	// Broadcast each time the player's health is depleted. The player revives afterwards,
	// so enemies use this to drop their current target, not to end the run.
	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnPlayerDowned OnPlayerDowned;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnWaveStarted OnWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Wave|Events")
	FOnWaveCompleted OnWaveCompleted;

	// Broadcast after wave-clear healing is applied so HUD can show exact amounts.
	// Zero amounts mean that side was skipped (dead/missing) — listeners show a plain banner.
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

	// Single base definition for Unlimited mode. Every wave is this definition scaled by wave index.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config")
	FWaveDefinition UnlimitedBaseWave;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config")
	bool bAutoStartOnBeginPlay = true;

	// Multiplier applied to UnlimitedBaseWave enemy counts per wave (wave 1 = factor^0, wave 2 = factor^1, ...).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Config", meta = (ClampMin = "1.0"))
	float UnlimitedEnemyCountScaleFactor = 1.15f;

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

	// Empty until assigned in Blueprint; guarded at play time.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Wave|Feedback")
	TObjectPtr<USoundBase> WaveStartSound;

	// Wave-clear heal & drops
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

	// First-spawn hitch warmup: during the opening grace, preload every class the run
	// can spawn and render one of each for a few frames so shader/AnimBP/disk costs
	// land before first contact instead of mid-fight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Warmup")
	bool bWarmupSpawns = true;

	// How long warmup actors stay alive; must cover several rendered frames.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave|Warmup", meta = (ClampMin = "0.1"))
	float WarmupLifetimeSeconds = 0.35f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State")
	int32 TotalEnemiesInWave = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State")
	int32 EnemiesSpawnedSoFar = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State")
	int32 EnemiesAlive = 0;

	// Cumulative kills for this run. Never reset per wave (a new run is a new
	// manager actor, so the initializer is the reset). Drives HUD KILLS + stats.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State")
	int32 KillCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Wave|State")
	bool bIsWaveActive = false;

	// 1-based wave number for UI. Derived from the zero-based CurrentWaveIndex.
	UFUNCTION(BlueprintPure, Category = "IronField|Wave|State")
	int32 GetCurrentWave() const { return CurrentWaveIndex + 1; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|State")
	int32 GetKillCount() const { return KillCount; }

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|Targeting")
	AActor* GetPlayerActor() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Wave|Targeting")
	AActor* GetStrongholdActor() const;

	UFUNCTION(BlueprintCallable, Category = "IronField|Wave|Actions")
	void StartNextWave();

	/** Personality shift for the current wave: 1 = full siege (opening), 0 = even split. */
	UFUNCTION(BlueprintPure, Category = "IronField|Wave|Targeting")
	float GetSiegeBiasForWave() const { return CurrentWaveIndex <= 0 ? 1.f : 0.f; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AIFPlayerCharacter> CachedPlayer;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AIFBaseCharacter>> SpawnedEnemies;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnPoints;

	int32 CurrentWaveIndex = -1;

	/** Flattened spawn queue for the active wave; trickled out by the spawn timer. */
	UPROPERTY(Transient)
	TArray<TSubclassOf<AIFBaseCharacter>> PendingSpawns;

	// Warmup dummies: spawned visible for a few frames during the opening grace,
	// never tracked as wave enemies, destroyed by CleanupWarmupActors.
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> WarmupActors;

	bool bWarmedUp = false;

	FTimerHandle InterWaveTimerHandle;
	FTimerHandle SpawnTimerHandle;
	FTimerHandle ValidateTimerHandle;
	FTimerHandle WarmupTimerHandle;
	FTimerHandle CorpseCleanupTimerHandle;
	TSharedPtr<FStreamableHandle> WarmupStreamableHandle;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AIFBaseCharacter>> PendingCorpses;

	// Generation counter for async identity: bumped on every BeginWaveFromDefinition
	// and EndPlay; timer delegates capture the scheduling generation and drop stale firings.
	uint32 WaveGeneration = 0;

	void NotifyEnemySpawned(AIFBaseCharacter* Enemy);
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
	void ApplyWaveScaling(AIFBaseCharacter* Enemy) const;
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
	void HandlePlayerRegistered(AIFPlayerCharacter* Player);

	UFUNCTION()
	void HandlePlayerHealthDepleted();

	UFUNCTION()
	void HandleEnemyDied(AIFBaseCharacter* DeadEnemy);

	UFUNCTION()
	void HandleWaveClearHeal(int32 WaveNumber);

	void TrySpawnHealPickup(AIFBaseCharacter* DeadEnemy);

	// First-spawn hitch warmup (see bWarmupSpawns).
	void PreloadWaveAssets();
	void CollectWarmupEnemyClasses(TArray<TSubclassOf<AIFBaseCharacter>>& OutClasses) const;
	UFUNCTION()
	void HandleWarmupAssetsLoaded();
	void WarmupSpawnActors();
	UFUNCTION()
	void CleanupWarmupActors();

	void BindPlayer(AIFPlayerCharacter* Player);
	void UnbindPlayer();

	UPROPERTY(Transient)
	bool bWaveCompletedBound = false;
};
