#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/GameStateBase.h"
#include "IFGameState.generated.h"

UCLASS()
class IRONFIELD_API AIFGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "IronField|Match")
	EIFMatchState GetCurrentMatchState() const { return CurrentMatchState; }

	UFUNCTION(BlueprintPure, Category = "IronField|Match")
	EIFGameResult GetMatchResult() const { return MatchResult; }

	UPROPERTY(BlueprintAssignable, Category = "IronField|Match")
	FOnMatchStateChanged OnMatchStateChanged;

	void ApplyMatchState(EIFMatchState NewState);
	void ApplyMatchResult(EIFGameResult NewResult);

private:
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Match", meta = (AllowPrivateAccess = "true"))
	EIFMatchState CurrentMatchState = EIFMatchState::Preparation;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Match", meta = (AllowPrivateAccess = "true"))
	EIFGameResult MatchResult = EIFGameResult::None;
};