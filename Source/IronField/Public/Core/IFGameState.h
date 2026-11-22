#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/GameStateBase.h"
#include "IFGameState.generated.h"

class AIFTerritory;

UCLASS()
class IRONFIELD_API AIFGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "IronField|Match")
	EIFGameResult GetMatchResult() const { return MatchResult; }

	UPROPERTY(BlueprintAssignable, Category = "IronField|Match")
	FOnGameResultChanged OnGameResultChanged;

	void ApplyMatchResult(EIFGameResult NewResult);

	// --- Typed Territory Registry (Zero stringly-typed lookups) -------------
	void RegisterTerritory(AIFTerritory* Territory);
	void UnregisterTerritory(AIFTerritory* Territory);
	void GetTerritories(TArray<AIFTerritory*>& OutTerritories) const;
	AIFTerritory* FindTerritoryAtLocation(const FVector& WorldLocation) const;
	AIFTerritory* FindTerritoryContainingBox(const FVector& WorldCenter, const FVector2D& HalfExtents, float YawDegrees) const;

private:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Match", meta = (AllowPrivateAccess = "true"))
	EIFGameResult MatchResult = EIFGameResult::None;

	TArray<TWeakObjectPtr<AIFTerritory>> RegisteredTerritories;
};