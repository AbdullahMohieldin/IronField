#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/GameModeBase.h"
#include "IFGameMode.generated.h"

class AIFGameState;

UCLASS()
class IRONFIELD_API AIFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIFGameMode();

	UFUNCTION(BlueprintPure, Category = "IronField|Match")
	EIFGameResult GetMatchResult() const;

	void EndMatch(EIFGameResult Result);

	UFUNCTION(Exec)
	void DebugVictory();

	UFUNCTION(Exec)
	void DebugDefeat();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<AIFGameState> IFGameState;
};