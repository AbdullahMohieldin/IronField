#include "Wave/IFWaveRewardComponent.h"

#include "Building/IFStronghold.h"
#include "Character/IFPlayerCharacter.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Core/IFFeedbackUtils.h"
#include "Core/IFLog.h"
#include "Engine/World.h"
#include "Pickup/IFHealPickup.h"
#include "Stats/IFHealthComponent.h"

namespace
{
	constexpr float PickupGroundTraceDistance = 500.f;
	constexpr float TraceMissGroundOffset = 10.f;
}

UIFWaveRewardComponent::UIFWaveRewardComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIFWaveRewardComponent::ApplyWaveClearHeal(int32 WaveNumber)
{
	UE_LOG(LogIronField, Log, TEXT("[IF-Heal] Wave %d clear - applying heal/repair via RewardComponent."), WaveNumber);

	float PlayerHealed = 0.f;
	float GateRepaired = 0.f;

	if (UWorld* const World = GetWorld())
	{
		auto ApplyClearHeal = [this, World](UIFHealthComponent* Health, float Percent, float& OutAmount,
			const TCHAR* HealLabel, const TCHAR* SkipMessage, const AActor* FxActor)
		{
			if (!Health)
			{
				return;
			}

			if (Health->IsDead())
			{
				UE_LOG(LogIronField, Log, TEXT("%s"), SkipMessage);
				return;
			}

			const float Amount = Health->GetMaxHealth() * Percent;
			Health->ApplyHealing(Amount);
			OutAmount = Amount;
			UE_LOG(LogIronField, Log, TEXT("[IF-Heal] %s %.0f (%.0f%% of %.0f max)."), HealLabel, Amount, Percent * 100.f, Health->GetMaxHealth());

			if (HealSound && FxActor)
			{
				IFFeedbackUtils::PlayAtLocation(World, HealSound, nullptr, FxActor->GetActorLocation());
			}
		};

		if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			if (AIFPlayerCharacter* const Player = Registry->GetActor<AIFPlayerCharacter>(IFRegistryRoles::Player))
			{
				ApplyClearHeal(Player->GetHealthComponent(), PlayerHealPercent, PlayerHealed,
					TEXT("Player healed"), TEXT("[IF-Heal] Player dead - skipping heal."), Player);
			}

			if (AActor* const StrongholdActor = Registry->GetActor(IFRegistryRoles::Stronghold))
			{
				ApplyClearHeal(StrongholdActor->FindComponentByClass<UIFHealthComponent>(), StrongholdRepairPercent, GateRepaired,
					TEXT("Stronghold repaired"), TEXT("[IF-Heal] Stronghold destroyed - skipping repair."), StrongholdActor);
			}
		}
	}

	OnWaveClearHeal.Broadcast(WaveNumber, PlayerHealed, GateRepaired);
}

void UIFWaveRewardComponent::TrySpawnHealPickup(AActor* DeadEnemy)
{
	if (!DeadEnemy || !HealPickupClass || DropChance <= 0.f)
	{
		return;
	}

	if (FMath::FRand() > DropChance)
	{
		return;
	}

	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	FVector SpawnLocation = DeadEnemy->GetActorLocation();
	FHitResult HitResult;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(PickupSpawnTrace), false, DeadEnemy);
	TraceParams.bReturnPhysicalMaterial = false;
	TraceParams.bTraceComplex = true;
	if (World->LineTraceSingleByChannel(HitResult, SpawnLocation, SpawnLocation - FVector(0.f, 0.f, PickupGroundTraceDistance), ECC_WorldStatic, TraceParams))
	{
		SpawnLocation = HitResult.Location;
	}
	else
	{
		SpawnLocation.Z += TraceMissGroundOffset;
	}

	const FTransform SpawnTransform(FRotator::ZeroRotator, SpawnLocation);
	AIFHealPickup* const Pickup = World->SpawnActorDeferred<AIFHealPickup>(
		HealPickupClass, SpawnTransform, GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (Pickup)
	{
		Pickup->HealAmount = PickupHealAmount;
		Pickup->FinishSpawning(SpawnTransform);
		UE_LOG(LogIronField, Log, TEXT("[IF-Heal] Spawned heal pickup at %s (amount=%.0f)."), *SpawnLocation.ToString(), PickupHealAmount);
	}
}
