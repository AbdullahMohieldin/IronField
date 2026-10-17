#include "Wave/IFWaveManager.h"

#include "Building/IFStronghold.h"
#include "Character/IFPlayerCharacter.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Core/IFFeedbackUtils.h"
#include "Core/IFGameInstance.h"
#include "Core/IFLog.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Pickup/IFHealPickup.h"
#include "Stats/IFHealthComponent.h"
#include "TimerManager.h"
#include "Wave/IFEnemySpawnPoint.h"
#include "Wave/IFWaveRewardComponent.h"
#include "Wave/IFWaveSpawnable.h"
#include "Wave/IFWaveWarmupComponent.h"

namespace
{
	constexpr float NextFrameTimerDelay = 0.01f;
}

AIFWaveManager::AIFWaveManager()
{
	PrimaryActorTick.bCanEverTick = false;

	RewardComponent = CreateDefaultSubobject<UIFWaveRewardComponent>(TEXT("WaveRewards"));
	WarmupComponent = CreateDefaultSubobject<UIFWaveWarmupComponent>(TEXT("WaveWarmup"));
}

AActor* AIFWaveManager::GetPlayerActor() const
{
	return CachedPlayer;
}

AActor* AIFWaveManager::GetStrongholdActor() const
{
	if (const UWorld* const World = GetWorld())
	{
		if (const UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			return Registry->GetActor(IFRegistryRoles::Stronghold);
		}
	}
	return nullptr;
}

void AIFWaveManager::StartNextWave()
{
	if (bIsWaveActive)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Wave] Cannot start next wave while a wave is already active."));
		return;
	}

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InterWaveTimerHandle);
	}

	CurrentWaveIndex++;

	if (IsUnlimitedRunMode())
	{
		BeginWaveFromDefinition(BuildUnlimitedWave(CurrentWaveIndex));
		return;
	}

	if (CurrentWaveIndex >= Waves.Num())
	{
		if (UWorld* const World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SpawnTimerHandle);
			World->GetTimerManager().ClearTimer(ValidateTimerHandle);
		}
		UE_LOG(LogIronField, Log, TEXT("[IF-Wave] All waves completed."));
		OnAllWavesCompleted.Broadcast();
		return;
	}

	BeginWaveFromDefinition(Waves[CurrentWaveIndex]);
}

void AIFWaveManager::BeginWaveFromDefinition(const FWaveDefinition& Wave)
{
	UnbindAllSpawnedEnemyDelegates();

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InterWaveTimerHandle);
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
		World->GetTimerManager().ClearTimer(ValidateTimerHandle);
	}

	WaveGeneration++;

	// Flush any pending corpse cleanup from the previous wave before clearing spawned lists.
	CleanupWaveCorpses();

	EnemiesSpawnedSoFar = 0;
	EnemiesAlive = 0;
	TotalEnemiesInWave = 0;
	SpawnedEnemies.Empty();
	SpawnQueue.Reset();
	bIsWaveActive = true;

	const int32 WaveNumber = GetCurrentWave();
	UE_LOG(LogIronField, Log, TEXT("[IF-Wave] Starting wave %d."), WaveNumber);
	OnWaveStarted.Broadcast(WaveNumber);
	PlayWaveStartFeedback();

	if (UWorld* const World = GetWorld())
	{
		if (WaveValidationIntervalSeconds > 0.f)
		{
			FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AIFWaveManager::HandleValidateTimer, WaveGeneration);
			World->GetTimerManager().SetTimer(ValidateTimerHandle, Delegate, WaveValidationIntervalSeconds, true);
		}
	}

	SpawnAllEnemiesInWave(Wave);
}

void AIFWaveManager::HandleInterWaveTimer(uint32 ScheduledGeneration)
{
	if (ScheduledGeneration != WaveGeneration)
	{
		return;
	}

	StartNextWave();
}

void AIFWaveManager::HandleSpawnTimerTick(uint32 ScheduledGeneration)
{
	if (ScheduledGeneration != WaveGeneration)
	{
		return;
	}

	TrySpawnPending();
}

void AIFWaveManager::HandleValidateTimer(uint32 ScheduledGeneration)
{
	if (ScheduledGeneration != WaveGeneration)
	{
		return;
	}

	ValidateWaveState();
}

void AIFWaveManager::ScheduleNextWave()
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!IsUnlimitedRunMode() && (CurrentWaveIndex + 1) >= Waves.Num())
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Wave] Last wave finished; no next wave to schedule."));
		OnAllWavesCompleted.Broadcast();
		return;
	}

	const float Delay = FMath::Max(NextFrameTimerDelay, InterWaveDelaySeconds);
	UE_LOG(LogIronField, Log, TEXT("[IF-Wave] Scheduling next wave in %.1fs."), Delay);

	FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AIFWaveManager::HandleInterWaveTimer, WaveGeneration);
	World->GetTimerManager().SetTimer(InterWaveTimerHandle, Delegate, Delay, false);
}

void AIFWaveManager::PlayWaveStartFeedback() const
{
	if (!WaveStartSound)
	{
		return;
	}

	IFFeedbackUtils::PlayAtLocation(GetWorld(), WaveStartSound, nullptr, GetActorLocation());
}

void AIFWaveManager::ValidateWaveState()
{
	int32 ActualAlive = 0;
	for (int32 Index = SpawnedEnemies.Num() - 1; Index >= 0; --Index)
	{
		AActor* const Enemy = SpawnedEnemies[Index].Get();
		if (!IsValid(Enemy))
		{
			SpawnedEnemies.RemoveAt(Index);
			continue;
		}

		const UIFHealthComponent* const Health = Enemy->FindComponentByClass<UIFHealthComponent>();
		if (!Health || !Health->IsDead())
		{
			ActualAlive++;
		}
	}

	if (ActualAlive != EnemiesAlive)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Wave] Wave %d alive-count mismatch: tracked=%d actual=%d; reconciling."),
			GetCurrentWave(), EnemiesAlive, ActualAlive);
		EnemiesAlive = ActualAlive;
		OnEnemiesAliveCountChanged.Broadcast(EnemiesAlive);
	}

	TrySpawnPending();
	CompleteWaveIfFinished();
}

FVector AIFWaveManager::PickSpawnLocation(FRotator& OutRotation) const
{
	OutRotation = GetActorRotation();
	FVector SpawnLocation = GetActorLocation();

	if (SpawnPoints.Num() > 0)
	{
		if (AActor* const SpawnPoint = SpawnPoints[FMath::RandRange(0, SpawnPoints.Num() - 1)])
		{
			SpawnLocation = SpawnPoint->GetActorLocation();
			OutRotation = SpawnPoint->GetActorRotation();
		}
	}

	if (SpawnLocationJitterRadius > 0.f)
	{
		SpawnLocation.X += FMath::RandRange(-SpawnLocationJitterRadius, SpawnLocationJitterRadius);
		SpawnLocation.Y += FMath::RandRange(-SpawnLocationJitterRadius, SpawnLocationJitterRadius);
	}

	return SpawnLocation;
}

bool AIFWaveManager::IsUnlimitedRunMode() const
{
	const UIFGameInstance* const GameInstance = Cast<UIFGameInstance>(GetGameInstance());
	return GameInstance && GameInstance->GetRunMode() == EIFRunMode::Unlimited;
}

FWaveDefinition AIFWaveManager::BuildUnlimitedWave(int32 WaveIndex) const
{
	FWaveDefinition Result = UnlimitedBaseWave;
	for (FEnemyGroupDefinition& Group : Result.EnemyGroups)
	{
		Group.EnemyCount = IFWaveScalingUtils::ScaleEnemyCount(Group.EnemyCount, WaveIndex, UnlimitedEnemyCountScaleFactor);
	}
	return Result;
}

void AIFWaveManager::CacheSpawnPoints()
{
	SpawnPoints.Empty();

	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> FoundPoints;
	UGameplayStatics::GetAllActorsOfClass(World, AIFEnemySpawnPoint::StaticClass(), FoundPoints);
	for (AActor* Point : FoundPoints)
	{
		SpawnPoints.Add(Point);
	}

	if (SpawnPoints.Num() <= 0)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Wave] No AIFEnemySpawnPoint actors found; spawning at manager location."));
	}
}

void AIFWaveManager::SpawnAllEnemiesInWave(const FWaveDefinition& Wave)
{
	BuildSpawnQueue(Wave);

	UWorld* const World = GetWorld();
	if (World && SpawnQueue.HasPending() && SpawnIntervalSeconds > 0.f)
	{
		FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AIFWaveManager::HandleSpawnTimerTick, WaveGeneration);
		World->GetTimerManager().SetTimer(SpawnTimerHandle, Delegate, SpawnIntervalSeconds, true, SpawnStartDelaySeconds);
	}
	else
	{
		TrySpawnPending();
	}
}

void AIFWaveManager::BuildSpawnQueue(const FWaveDefinition& Wave)
{
	SpawnQueue.Reset();

	for (const FEnemyGroupDefinition& Group : Wave.EnemyGroups)
	{
		if (!Group.EnemyClass)
		{
			UE_LOG(LogIronField, Warning, TEXT("[IF-Wave] Wave %d has an enemy group with no Enemy Class — skipping."), GetCurrentWave());
			continue;
		}

		for (int32 i = 0; i < Group.EnemyCount; ++i)
		{
			SpawnQueue.Enqueue(Group.EnemyClass);
		}
	}

	TotalEnemiesInWave = SpawnQueue.TotalQueued();
}

void AIFWaveManager::TrySpawnPending()
{
	UWorld* const World = GetWorld();
	if (!World || !bIsWaveActive)
	{
		return;
	}

	const int32 CappedMax = FMath::Max(1, MaxConcurrentAlive);
	while (SpawnQueue.HasPending() && EnemiesAlive < CappedMax)
	{
		const TSubclassOf<APawn> EnemyClass = SpawnQueue.Dequeue();
		if (!EnemyClass)
		{
			TotalEnemiesInWave = FMath::Max(0, TotalEnemiesInWave - 1);
			continue;
		}

		FRotator SpawnRotation;
		const FVector SpawnLocation = PickSpawnLocation(SpawnRotation);
		const FTransform SpawnTransform(SpawnRotation, SpawnLocation);

		APawn* const SpawnedEnemy = World->SpawnActorDeferred<APawn>(
			EnemyClass, SpawnTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (SpawnedEnemy)
		{
			if (IIFWaveSpawnable* const Spawnable = Cast<IIFWaveSpawnable>(SpawnedEnemy))
			{
				FIFSpawnContext Context;
				Context.WaveNumber = GetCurrentWave();
				Context.SiegeBias = GetSiegeBiasForWave();
				Context.HealthScale = IFWaveScalingUtils::ComputeHealthScale(CurrentWaveIndex, WaveHealthMultiplier);
				Context.DamageScale = IFWaveScalingUtils::ComputeDamageScale(CurrentWaveIndex, WaveDamageMultiplier);
				Spawnable->SetSpawnContext(Context);
			}

			SpawnedEnemy->FinishSpawning(SpawnTransform);
			EnemiesSpawnedSoFar++;
			NotifyEnemySpawned(SpawnedEnemy);
		}
		else
		{
			TotalEnemiesInWave = FMath::Max(0, TotalEnemiesInWave - 1);
			UE_LOG(LogIronField, Warning, TEXT("[IF-Wave] Failed to spawn %s at %s"),
				*EnemyClass->GetName(), *SpawnLocation.ToString());
		}
	}

	if (!SpawnQueue.HasPending())
	{
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}

	CompleteWaveIfFinished();
}

void AIFWaveManager::NotifyEnemySpawned(APawn* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	SpawnedEnemies.Add(Enemy);
	EnemiesAlive++;
	OnEnemiesAliveCountChanged.Broadcast(EnemiesAlive);

	if (UIFHealthComponent* const Health = Enemy->FindComponentByClass<UIFHealthComponent>())
	{
		Health->OnDeath.AddDynamic(this, &AIFWaveManager::HandleEnemyDied);
	}
}

void AIFWaveManager::CompleteWaveIfFinished()
{
	if (!bIsWaveActive || EnemiesAlive > 0 || SpawnQueue.HasPending() || EnemiesSpawnedSoFar < TotalEnemiesInWave)
	{
		return;
	}

	if (TotalEnemiesInWave <= 0)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Wave] Wave %d has no spawnable enemies; completing immediately."), GetCurrentWave());
	}

	bIsWaveActive = false;
	const int32 WaveNumber = GetCurrentWave();
	UE_LOG(LogIronField, Log, TEXT("[IF-Wave] Wave %d complete."), WaveNumber);
	OnWaveCompleted.Broadcast(WaveNumber);

	if (RewardComponent)
	{
		RewardComponent->ApplyWaveClearHeal(WaveNumber);
	}

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ValidateTimerHandle);
	}

	ScheduleNextWave();
}

void AIFWaveManager::HandleEnemyDied(AActor* DeadActor)
{
	if (!DeadActor)
	{
		return;
	}

	if (UIFHealthComponent* const Health = DeadActor->FindComponentByClass<UIFHealthComponent>())
	{
		Health->OnDeath.RemoveDynamic(this, &AIFWaveManager::HandleEnemyDied);
	}

	EnemiesAlive = FMath::Max(0, EnemiesAlive - 1);
	OnEnemiesAliveCountChanged.Broadcast(EnemiesAlive);

	KillCount++;
	OnKillCountChanged.Broadcast(KillCount);

	PendingCorpses.Add(DeadActor);
	SpawnedEnemies.Remove(DeadActor);

	if (RewardComponent)
	{
		RewardComponent->TrySpawnHealPickup(DeadActor);
	}

	if (UWorld* const World = GetWorld())
	{
		if (CorpseDespawnDelaySeconds > 0.f)
		{
			FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AIFWaveManager::CleanupWaveCorpses);
			World->GetTimerManager().SetTimer(CorpseCleanupTimerHandle, Delegate, CorpseDespawnDelaySeconds, false);
		}
		else
		{
			CleanupWaveCorpses();
		}
	}

	TrySpawnPending();
	CompleteWaveIfFinished();
}

void AIFWaveManager::CleanupWaveCorpses()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CorpseCleanupTimerHandle);
	}

	for (TWeakObjectPtr<AActor>& CorpsePtr : PendingCorpses)
	{
		if (AActor* const Corpse = CorpsePtr.Get())
		{
			Corpse->Destroy();
		}
	}
	PendingCorpses.Empty();
}

void AIFWaveManager::UnbindAllSpawnedEnemyDelegates()
{
	for (const TWeakObjectPtr<AActor>& EnemyPtr : SpawnedEnemies)
	{
		if (AActor* const Enemy = EnemyPtr.Get())
		{
			if (UIFHealthComponent* const Health = Enemy->FindComponentByClass<UIFHealthComponent>())
			{
				Health->OnDeath.RemoveDynamic(this, &AIFWaveManager::HandleEnemyDied);
			}
		}
	}
}

void AIFWaveManager::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* const World = GetWorld())
	{
		if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			Registry->RegisterActor(IFRegistryRoles::WaveManager, this);
		}
	}

	if (RewardComponent)
	{
		RewardComponent->PlayerHealPercent = PlayerHealPercent;
		RewardComponent->StrongholdRepairPercent = StrongholdRepairPercent;
		RewardComponent->DropChance = DropChance;
		RewardComponent->PickupHealAmount = PickupHealAmount;
		RewardComponent->HealPickupClass = HealPickupClass;
		RewardComponent->HealSound = HealSound;
		RewardComponent->OnWaveClearHeal.AddDynamic(this, &AIFWaveManager::HandleRewardWaveClearHeal);
	}

	if (WarmupComponent)
	{
		WarmupComponent->bWarmupSpawns = bWarmupSpawns;
		WarmupComponent->WarmupLifetimeSeconds = WarmupLifetimeSeconds;
	}

	CacheSpawnPoints();

	if (const UWorld* const World = GetWorld())
	{
		if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			if (AIFPlayerCharacter* const Player = Registry->GetActor<AIFPlayerCharacter>(IFRegistryRoles::Player))
			{
				BindPlayer(Player);
			}
			else
			{
				Registry->OnActorRegistered.AddDynamic(this, &AIFWaveManager::HandleActorRegistered);
			}
		}
	}

	PreloadWaveAssets();

	if (bAutoStartOnBeginPlay)
	{
		if (UWorld* const World = GetWorld())
		{
			const float Delay = FMath::Max(NextFrameTimerDelay, InitialWaveStartDelaySeconds);
			FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AIFWaveManager::HandleInterWaveTimer, WaveGeneration);
			World->GetTimerManager().SetTimer(InterWaveTimerHandle, Delegate, Delay, false);
		}
	}
}

void AIFWaveManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			Registry->UnregisterActor(IFRegistryRoles::WaveManager, this);
			Registry->OnActorRegistered.RemoveDynamic(this, &AIFWaveManager::HandleActorRegistered);
		}

		World->GetTimerManager().ClearTimer(InterWaveTimerHandle);
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
		World->GetTimerManager().ClearTimer(ValidateTimerHandle);
		World->GetTimerManager().ClearTimer(CorpseCleanupTimerHandle);
	}

	WaveGeneration++;

	CleanupWaveCorpses();

	if (RewardComponent)
	{
		RewardComponent->OnWaveClearHeal.RemoveDynamic(this, &AIFWaveManager::HandleRewardWaveClearHeal);
	}

	UnbindPlayer();
	UnbindAllSpawnedEnemyDelegates();

	Super::EndPlay(EndPlayReason);
}

void AIFWaveManager::HandleActorRegistered(FName ActorRole, AActor* Actor)
{
	if (ActorRole == IFRegistryRoles::Player)
	{
		HandlePlayerRegistered(Cast<AIFPlayerCharacter>(Actor));
	}
}

void AIFWaveManager::HandlePlayerRegistered(AIFPlayerCharacter* Player)
{
	if (CachedPlayer || !Player)
	{
		return;
	}

	BindPlayer(Player);
}

void AIFWaveManager::BindPlayer(AIFPlayerCharacter* Player)
{
	if (!Player)
	{
		return;
	}

	CachedPlayer = Player;

	if (UIFHealthComponent* const Health = Player->GetHealthComponent())
	{
		Health->OnHealthDepleted.AddDynamic(this, &AIFWaveManager::HandlePlayerHealthDepleted);
	}
}

void AIFWaveManager::UnbindPlayer()
{
	if (!CachedPlayer)
	{
		return;
	}

	if (UIFHealthComponent* const Health = CachedPlayer->GetHealthComponent())
	{
		Health->OnHealthDepleted.RemoveDynamic(this, &AIFWaveManager::HandlePlayerHealthDepleted);
	}

	CachedPlayer = nullptr;
}

void AIFWaveManager::HandlePlayerHealthDepleted()
{
	OnPlayerDowned.Broadcast();
}

void AIFWaveManager::HandleRewardWaveClearHeal(int32 WaveNumber, float InPlayerHealed, float InGateRepaired)
{
	OnWaveClearHeal.Broadcast(WaveNumber, InPlayerHealed, InGateRepaired);
}

void AIFWaveManager::CollectWarmupEnemyClasses(TArray<TSubclassOf<APawn>>& OutClasses) const
{
	OutClasses.Reset();

	auto AddGroups = [&](const FWaveDefinition& Wave)
	{
		for (const FEnemyGroupDefinition& Group : Wave.EnemyGroups)
		{
			if (Group.EnemyClass)
			{
				OutClasses.AddUnique(Group.EnemyClass);
			}
		}
	};

	for (const FWaveDefinition& Wave : Waves)
	{
		AddGroups(Wave);
	}
	AddGroups(UnlimitedBaseWave);
}

void AIFWaveManager::PreloadWaveAssets()
{
	if (!WarmupComponent || !bWarmupSpawns)
	{
		return;
	}

	TArray<TSubclassOf<APawn>> Enemies;
	CollectWarmupEnemyClasses(Enemies);
	WarmupComponent->StartWarmup(Enemies, HealPickupClass, WaveStartSound, HealSound);
}
