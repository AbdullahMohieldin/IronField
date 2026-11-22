#include "Core/IFGameState.h"

#include "Castle/IFTerritory.h"

void AIFGameState::ApplyMatchResult(EIFGameResult NewResult)
{
	if (MatchResult == NewResult)
	{
		return;
	}

	MatchResult = NewResult;
	OnGameResultChanged.Broadcast(NewResult);
}

void AIFGameState::RegisterTerritory(AIFTerritory* Territory)
{
	if (!IsValid(Territory))
	{
		return;
	}

	const bool bExists = RegisteredTerritories.ContainsByPredicate(
		[Territory](const TWeakObjectPtr<AIFTerritory>& Ptr) { return Ptr.Get() == Territory; });
	if (!bExists)
	{
		RegisteredTerritories.Add(Territory);
	}
}

void AIFGameState::UnregisterTerritory(AIFTerritory* Territory)
{
	RegisteredTerritories.RemoveAll([Territory](const TWeakObjectPtr<AIFTerritory>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == Territory;
	});
}

void AIFGameState::GetTerritories(TArray<AIFTerritory*>& OutTerritories) const
{
	OutTerritories.Reset();
	for (const TWeakObjectPtr<AIFTerritory>& Ptr : RegisteredTerritories)
	{
		if (AIFTerritory* const Territory = Ptr.Get(); IsValid(Territory))
		{
			OutTerritories.Add(Territory);
		}
	}
}

AIFTerritory* AIFGameState::FindTerritoryAtLocation(const FVector& WorldLocation) const
{
	for (const TWeakObjectPtr<AIFTerritory>& Ptr : RegisteredTerritories)
	{
		if (AIFTerritory* const Territory = Ptr.Get(); IsValid(Territory) && Territory->ContainsPoint2D(WorldLocation))
		{
			return Territory;
		}
	}
	return nullptr;
}

AIFTerritory* AIFGameState::FindTerritoryContainingBox(const FVector& WorldCenter, const FVector2D& HalfExtents, float YawDegrees) const
{
	for (const TWeakObjectPtr<AIFTerritory>& Ptr : RegisteredTerritories)
	{
		if (AIFTerritory* const Territory = Ptr.Get();
			IsValid(Territory) && Territory->ContainsOrientedBox2D(WorldCenter, HalfExtents, YawDegrees))
		{
			return Territory;
		}
	}
	return nullptr;
}