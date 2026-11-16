#include "Core/IFGameState.h"

void AIFGameState::ApplyMatchResult(EIFGameResult NewResult)
{
	if (MatchResult == NewResult)
	{
		return;
	}

	MatchResult = NewResult;
	OnGameResultChanged.Broadcast(NewResult);
}