#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/GameModeBase.h"
#include "IFGameMode.generated.h"

class AIFStronghold;
class AIFWaveManager;

UCLASS()
class IRONFIELD_API AIFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIFGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleActorRegistered(FName ActorRole, AActor* Actor);

	UFUNCTION()
	void HandleWaveManagerRegistered(AIFWaveManager* WaveManager);

	UFUNCTION()
	void HandleStrongholdRegistered(AIFStronghold* Stronghold);

	UFUNCTION()
	void HandleAllWavesCompleted();

	UFUNCTION()
	void HandleStrongholdDestroyed(AIFStronghold* Stronghold);

	void ShowGameOver(EIFGameResult Result);
	void SaveBestRun(EIFGameResult Result);

	// The game mode begins play before level actors register with the subsystems,
	// so game-flow delegates are bound either immediately or on registration.
	UPROPERTY(Transient)
	TObjectPtr<AIFWaveManager> BoundWaveManager;

	UPROPERTY(Transient)
	TObjectPtr<AIFStronghold> BoundStronghold;
};
