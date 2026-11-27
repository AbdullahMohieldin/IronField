#include "Core/IFGameMode.h"

#include "Castle/IFTerritory.h"
#include "Character/IFPlayerCharacter.h"
#include "Core/IFGameState.h"
#include "Core/IFPlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UI/IFTacticalHUD.h"

AIFGameMode::AIFGameMode()
{
	GameStateClass = AIFGameState::StaticClass();
	PlayerControllerClass = AIFPlayerController::StaticClass();
	DefaultPawnClass = AIFPlayerCharacter::StaticClass();
	HUDClass = AIFTacticalHUD::StaticClass();
}

void AIFGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HeroRespawnTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void AIFGameMode::EndMatch(EIFGameResult Result)
{
	AIFGameState* const GS = GetGameState<AIFGameState>();
	if (Result == EIFGameResult::None || !IsValid(GS) || GS->GetMatchResult() != EIFGameResult::None)
	{
		return;
	}

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HeroRespawnTimerHandle);
	}

	GS->ApplyMatchResult(Result);
}

void AIFGameMode::RequestHeroRespawn(AIFPlayerCharacter* DeadHero)
{
	AIFGameState* const GS = GetGameState<AIFGameState>();
	UWorld* const World = GetWorld();
	if (!IsValid(DeadHero) || !IsValid(GS) || GS->GetMatchResult() != EIFGameResult::None || !IsValid(World))
	{
		return;
	}

	if (World->GetTimerManager().IsTimerActive(HeroRespawnTimerHandle))
	{
		return;
	}

	const TWeakObjectPtr<AIFPlayerCharacter> HeroWeak(DeadHero);
	World->GetTimerManager().SetTimer(
		HeroRespawnTimerHandle,
		FTimerDelegate::CreateUObject(this, &AIFGameMode::ExecuteHeroRespawn, HeroWeak),
		HeroRespawnDelaySeconds,
		false);
}

void AIFGameMode::ExecuteHeroRespawn(TWeakObjectPtr<AIFPlayerCharacter> DeadHero)
{
	AIFPlayerCharacter* const Hero = DeadHero.Get();
	AIFGameState* const GS = GetGameState<AIFGameState>();
	if (!IsValid(Hero) || !Hero->IsDead() || !IsValid(GS) || GS->GetMatchResult() != EIFGameResult::None)
	{
		return;
	}

	if (const AIFTerritory* const RespawnTerritory = GS->FindNearestOwnedTerritoryWithKeep(Hero->GetGenericTeamId(), Hero->GetActorLocation()))
	{
		const FTransform SpawnXf = RespawnTerritory->GetHeroRespawnTransform();
		Hero->RespawnAtCastle(SpawnXf.GetLocation(), SpawnXf.Rotator());
		return;
	}

	// Hero permadeath when no friendly territory has a standing Keep.
	EndMatch(EIFGameResult::Defeat);
}

void AIFGameMode::DebugVictory()
{
#if !UE_BUILD_SHIPPING
	EndMatch(EIFGameResult::Victory);
#endif
}

void AIFGameMode::DebugDefeat()
{
#if !UE_BUILD_SHIPPING
	EndMatch(EIFGameResult::Defeat);
#endif
}