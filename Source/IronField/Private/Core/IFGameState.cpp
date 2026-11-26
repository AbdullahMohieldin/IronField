#include "Core/IFGameState.h"

#include "Castle/IFStructure.h"
#include "Castle/IFTerritory.h"

void AIFGameState::ApplyMatchResult(EIFGameResult NewResult)
{
	if (NewResult == EIFGameResult::None || MatchResult != EIFGameResult::None)
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

	RegisteredTerritories.AddUnique(Territory);
}

void AIFGameState::UnregisterTerritory(AIFTerritory* Territory)
{
	RegisteredTerritories.RemoveSingleSwap(Territory);
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

AIFTerritory* AIFGameState::FindNearestOwnedTerritoryWithKeep(FGenericTeamId TeamID, const FVector& ReferenceLocation) const
{
	AIFTerritory* BestTerritory = nullptr;
	float BestDistSq = MAX_flt;

	for (const TWeakObjectPtr<AIFTerritory>& Ptr : RegisteredTerritories)
	{
		AIFTerritory* const Territory = Ptr.Get();
		if (!IsValid(Territory) || Territory->GetGenericTeamId() != TeamID || !Territory->HasActiveKeep())
		{
			continue;
		}

		const AIFStructure* const Keep = Territory->GetActiveKeep();
		const FVector KeepLocation = IsValid(Keep) ? Keep->GetActorLocation() : Territory->GetHeroRespawnTransform().GetLocation();
		const float DistSq = FVector::DistSquared(ReferenceLocation, KeepLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestTerritory = Territory;
		}
	}

	return BestTerritory;
}