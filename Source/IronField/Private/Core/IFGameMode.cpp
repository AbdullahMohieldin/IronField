#include "Core/IFGameMode.h"

#include "Character/IFPlayerCharacter.h"
#include "Core/IFGameState.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

AIFGameMode::AIFGameMode()
{
	GameStateClass = AIFGameState::StaticClass();
	PlayerControllerClass = AIFPlayerController::StaticClass();
	DefaultPawnClass = AIFPlayerCharacter::StaticClass();
}

void AIFGameMode::BeginPlay()
{
	Super::BeginPlay();

	IFGameState = GetGameState<AIFGameState>();
}

EIFGameResult AIFGameMode::GetMatchResult() const
{
	return IsValid(IFGameState) ? IFGameState->GetMatchResult() : EIFGameResult::None;
}

void AIFGameMode::EndMatch(EIFGameResult Result)
{
	if (Result == EIFGameResult::None || !IsValid(IFGameState) || IFGameState->GetMatchResult() != EIFGameResult::None)
	{
		return;
	}

	IFGameState->ApplyMatchResult(Result);

	if (UWorld* const World = GetWorld())
	{
		AIFPlayerController* const PC = Cast<AIFPlayerController>(World->GetFirstPlayerController());
		if (IsValid(PC))
		{
			PC->ShowGameOverScreen(Result);
		}
	}
}

void AIFGameMode::DebugVictory()
{
	EndMatch(EIFGameResult::Victory);
}

void AIFGameMode::DebugDefeat()
{
	EndMatch(EIFGameResult::Defeat);
}