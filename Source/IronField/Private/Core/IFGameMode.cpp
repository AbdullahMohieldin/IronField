#include "Core/IFGameMode.h"

#include "Character/IFPlayerCharacter.h"
#include "Core/IFGameState.h"
#include "Core/IFPlayerController.h"
#include "UI/IFTacticalHUD.h"

AIFGameMode::AIFGameMode()
{
	GameStateClass = AIFGameState::StaticClass();
	PlayerControllerClass = AIFPlayerController::StaticClass();
	DefaultPawnClass = AIFPlayerCharacter::StaticClass();
	HUDClass = AIFTacticalHUD::StaticClass();
}

void AIFGameMode::EndMatch(EIFGameResult Result)
{
	AIFGameState* const GS = GetGameState<AIFGameState>();
	if (Result == EIFGameResult::None || !IsValid(GS) || GS->GetMatchResult() != EIFGameResult::None)
	{
		return;
	}

	GS->ApplyMatchResult(Result);
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