#pragma once

#include "CoreMinimal.h"
#include "Character/IFEnemyCharacter.h"
#include "IFMageEnemyCharacter.generated.h"

UCLASS()
class IRONFIELD_API AIFMageEnemyCharacter : public AIFEnemyCharacter
{
	GENERATED_BODY()

public:
	AIFMageEnemyCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
