#include "Core/IFGameState.h"

void AIFGameState::ApplyMatchState(EIFMatchState NewState)
{
	if (CurrentMatchState == NewState)
	{
		return;
	}

	const EIFMatchState PreviousState = CurrentMatchState;
	CurrentMatchState = NewState;
	OnMatchStateChanged.Broadcast(PreviousState, NewState);
}

void AIFGameState::ApplyMatchResult(EIFGameResult NewResult)
{
	MatchResult = NewResult;
}