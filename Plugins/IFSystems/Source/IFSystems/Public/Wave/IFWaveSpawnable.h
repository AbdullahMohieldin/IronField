#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Wave/IFSpawnContext.h"
#include "IFWaveSpawnable.generated.h"

UINTERFACE(MinimalAPI, BlueprintType)
class UIFWaveSpawnable : public UInterface
{
	GENERATED_BODY()
};

class IFSYSTEMS_API IIFWaveSpawnable
{
	GENERATED_BODY()

public:
	virtual void SetSpawnContext(const FIFSpawnContext& InContext) = 0;
	virtual const FIFSpawnContext& GetSpawnContext() const = 0;
};
