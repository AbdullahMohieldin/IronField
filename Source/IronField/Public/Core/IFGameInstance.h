#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "IFGameInstance.generated.h"

UCLASS()
class IRONFIELD_API UIFGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Levels")
	FName GameplayLevelName = TEXT("MainLevel");
};
