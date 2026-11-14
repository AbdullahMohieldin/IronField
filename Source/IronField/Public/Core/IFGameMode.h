#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "IFGameMode.generated.h"

class AIFGameState;

UCLASS()
class IRONFIELD_API AIFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIFGameMode();

	UFUNCTION(BlueprintPure, Category = "IronField|Match")
	EIFMatchState GetCurrentMatchState() const;

	void TransitionTo(EIFMatchState NewState);

	UFUNCTION(Exec)
	void DebugNextState();

	UFUNCTION(Exec)
	void DebugVictory();

	UFUNCTION(Exec)
	void DebugDefeat();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Match", meta = (ClampMin = "0.0"))
	float PreparationSeconds = 5.f;

private:
	UPROPERTY(Transient)
	TObjectPtr<AIFGameState> IFGameState;

	FTimerHandle PreparationTimerHandle;

	void StartPreparation();
	void HandlePreparationComplete();
	bool IsTransitionAllowed(EIFMatchState From, EIFMatchState To) const;
};