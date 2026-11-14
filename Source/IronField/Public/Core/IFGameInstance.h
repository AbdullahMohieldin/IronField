#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "Engine/GameInstance.h"
#include "IFGameInstance.generated.h"

UCLASS()
class IRONFIELD_API UIFGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "IronField|Run")
	EIFRunMode GetRunMode() const { return RunMode; }

	UFUNCTION(BlueprintCallable, Category = "IronField|Run")
	void SetRunMode(EIFRunMode NewMode);

	UFUNCTION(BlueprintCallable, Category = "IronField|Run")
	void StartGameRun(EIFRunMode Mode);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Levels")
	FName MainMenuLevelName = TEXT("MainMenu");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Levels")
	FName GameplayLevelName = TEXT("MainLevel");

private:
	UPROPERTY(Transient)
	EIFRunMode RunMode = EIFRunMode::Normal;
};