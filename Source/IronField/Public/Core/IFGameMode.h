#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/GameModeBase.h"
#include "IFGameMode.generated.h"

UCLASS()
class IRONFIELD_API AIFGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIFGameMode();

	void EndMatch(EIFGameResult Result);

	UFUNCTION(Exec)
	void DebugVictory();

	UFUNCTION(Exec)
	void DebugDefeat();
};