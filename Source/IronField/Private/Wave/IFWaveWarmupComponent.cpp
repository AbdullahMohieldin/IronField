#include "Wave/IFWaveWarmupComponent.h"

#include "Combat/IFCombatComponent.h"
#include "Combat/IFMageCombatComponent.h"
#include "Combat/IFProjectile.h"
#include "Core/IFFeedbackUtils.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace
{
	constexpr float MinWarmupLifetime = 0.1f;
}

UIFWaveWarmupComponent::UIFWaveWarmupComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIFWaveWarmupComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WarmupTimerHandle);
	}

	if (WarmupStreamableHandle.IsValid())
	{
		WarmupStreamableHandle->CancelHandle();
		WarmupStreamableHandle.Reset();
	}

	CleanupWarmupActors();

	Super::EndPlay(EndPlayReason);
}

void UIFWaveWarmupComponent::StartWarmup(const TArray<TSubclassOf<APawn>>& EnemyClasses, TSubclassOf<AActor> HealPickupClass, USoundBase* WaveStartSound, USoundBase* HealSound)
{
	if (!bWarmupSpawns)
	{
		return;
	}

	CachedEnemyClasses = EnemyClasses;
	CachedHealPickupClass = HealPickupClass;
	CachedWaveStartSound = WaveStartSound;

	TArray<FSoftObjectPath> Paths;
	for (const TSubclassOf<APawn>& EnemyClass : EnemyClasses)
	{
		if (EnemyClass)
		{
			Paths.AddUnique(FSoftObjectPath(EnemyClass));
		}
	}
	if (HealPickupClass)
	{
		Paths.AddUnique(FSoftObjectPath(HealPickupClass));
	}
	if (WaveStartSound)
	{
		Paths.AddUnique(FSoftObjectPath(WaveStartSound));
	}
	if (HealSound)
	{
		Paths.AddUnique(FSoftObjectPath(HealSound));
	}

	if (Paths.Num() <= 0)
	{
		return;
	}

	FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
	WarmupStreamableHandle = Streamable.RequestAsyncLoad(Paths, FStreamableDelegate::CreateUObject(this, &UIFWaveWarmupComponent::HandleWarmupAssetsLoaded));
}

void UIFWaveWarmupComponent::HandleWarmupAssetsLoaded()
{
	WarmupStreamableHandle.Reset();

	if (bWarmedUp || !GetWorld())
	{
		return;
	}

	bWarmedUp = true;
	WarmupSpawnActors();
}

void UIFWaveWarmupComponent::WarmupSpawnActors()
{
	UWorld* const World = GetWorld();
	AActor* const Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	const FVector Center = Owner->GetActorLocation() + FVector(0.f, 0.f, 100.f);
	TSubclassOf<AIFProjectile> WarmupProjectileClass = nullptr;

	for (const TSubclassOf<APawn>& EnemyClass : CachedEnemyClasses)
	{
		if (!EnemyClass)
		{
			continue;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		APawn* const Dummy = World->SpawnActor<APawn>(EnemyClass, Center, FRotator::ZeroRotator, SpawnParams);
		if (!Dummy)
		{
			continue;
		}

		if (AController* Ctrl = Dummy->GetController())
		{
			Ctrl->UnPossess();
			Ctrl->Destroy();
		}
		Dummy->SetActorEnableCollision(false);
		WarmupActors.Add(Dummy);

		if (!WarmupProjectileClass)
		{
			if (UIFCombatComponent* Combat = Dummy->FindComponentByClass<UIFCombatComponent>())
			{
				if (UIFMageCombatComponent* Mage = Cast<UIFMageCombatComponent>(Combat))
				{
					WarmupProjectileClass = Mage->GetProjectileClass();
				}
			}
		}
	}

	if (WarmupProjectileClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (AIFProjectile* const Shot = World->SpawnActor<AIFProjectile>(WarmupProjectileClass, Center + FVector(0.f, 0.f, 200.f), FRotator::ZeroRotator, SpawnParams))
		{
			FIFProjectileSpawnArgs Args;
			Args.SpawnLocation = Shot->GetActorLocation();
			Args.LaunchDirection = FVector::UpVector;
			Args.Damage = 0.f;
			Shot->InitializeProjectile(Args);
			WarmupActors.Add(Shot);
		}
	}

	if (CachedHealPickupClass)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		if (AActor* const Pickup = World->SpawnActor<AActor>(CachedHealPickupClass, Center, FRotator::ZeroRotator, SpawnParams))
		{
			WarmupActors.Add(Pickup);
		}
	}

	if (CachedWaveStartSound)
	{
		IFFeedbackUtils::PlayAtLocation(World, CachedWaveStartSound, nullptr, Owner->GetActorLocation());
	}

	FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &UIFWaveWarmupComponent::CleanupWarmupActors);
	World->GetTimerManager().SetTimer(WarmupTimerHandle, Delegate, FMath::Max(MinWarmupLifetime, WarmupLifetimeSeconds), false);
}

void UIFWaveWarmupComponent::CleanupWarmupActors()
{
	for (TObjectPtr<AActor>& Actor : WarmupActors)
	{
		if (Actor)
		{
			Actor->Destroy();
		}
	}
	WarmupActors.Empty();
}
