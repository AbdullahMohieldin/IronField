#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/GameModeBase.h"
#include "IFGameMode.generated.h"

class AIFPlayerCharacter;

UCLASS()
class IRONFIELD_API AIFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIFGameMode();

	void EndMatch(EIFGameResult Result);
	void RequestHeroRespawn(AIFPlayerCharacter* DeadHero);

	UFUNCTION(Exec)
	void DebugVictory();

	UFUNCTION(Exec)
	void DebugDefeat();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Match", meta = (ClampMin = "0.5"))
	float HeroRespawnDelaySeconds = 4.f;

private:
	void ExecuteHeroRespawn(TWeakObjectPtr<AIFPlayerCharacter> DeadHero);

	FTimerHandle HeroRespawnTimerHandle;
};