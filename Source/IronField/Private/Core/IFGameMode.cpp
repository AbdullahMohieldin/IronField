#include "Core/IFGameMode.h"

#include "Character/IFPlayerCharacter.h"
#include "Core/IFGameState.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

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
	StartPreparation();
}

void AIFGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PreparationTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

EIFMatchState AIFGameMode::GetCurrentMatchState() const
{
	return IsValid(IFGameState) ? IFGameState->GetCurrentMatchState() : EIFMatchState::Preparation;
}

void AIFGameMode::StartPreparation()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PreparationTimerHandle, this, &AIFGameMode::HandlePreparationComplete, PreparationSeconds, false);
	}
}

void AIFGameMode::HandlePreparationComplete()
{
	TransitionTo(EIFMatchState::WaveActive);
}

void AIFGameMode::TransitionTo(EIFMatchState NewState)
{
	if (!IsValid(IFGameState))
	{
		return;
	}

	const EIFMatchState From = IFGameState->GetCurrentMatchState();
	if (!IsTransitionAllowed(From, NewState))
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Flow] Ignored transition %d -> %d."), static_cast<int32>(From), static_cast<int32>(NewState));
		return;
	}

	IFGameState->ApplyMatchState(NewState);

	if (NewState == EIFMatchState::Victory || NewState == EIFMatchState::Defeat)
	{
		const EIFGameResult Result = (NewState == EIFMatchState::Victory) ? EIFGameResult::Victory : EIFGameResult::Defeat;
		IFGameState->ApplyMatchResult(Result);

		if (UWorld* const World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PreparationTimerHandle);
			AIFPlayerController* const PC = Cast<AIFPlayerController>(World->GetFirstPlayerController());
			if (IsValid(PC))
			{
				PC->ShowGameOverScreen(Result);
			}
		}
	}
}

bool AIFGameMode::IsTransitionAllowed(EIFMatchState From, EIFMatchState To) const
{
	if (From == To || From == EIFMatchState::Victory || From == EIFMatchState::Defeat)
	{
		return false;
	}

	switch (To)
	{
	case EIFMatchState::Defeat:
		return true;
	case EIFMatchState::Victory:
		return From == EIFMatchState::WaveActive || From == EIFMatchState::Intermission;
	case EIFMatchState::WaveActive:
		return From == EIFMatchState::Preparation || From == EIFMatchState::Intermission;
	case EIFMatchState::Intermission:
		return From == EIFMatchState::WaveActive;
	default:
		return false;
	}
}

void AIFGameMode::DebugNextState()
{
	switch (GetCurrentMatchState())
	{
	case EIFMatchState::Preparation:
		TransitionTo(EIFMatchState::WaveActive);
		break;
	case EIFMatchState::WaveActive:
		TransitionTo(EIFMatchState::Intermission);
		break;
	case EIFMatchState::Intermission:
		TransitionTo(EIFMatchState::WaveActive);
		break;
	default:
		break;
	}
}

void AIFGameMode::DebugVictory()
{
	TransitionTo(EIFMatchState::Victory);
}

void AIFGameMode::DebugDefeat()
{
	TransitionTo(EIFMatchState::Defeat);
}