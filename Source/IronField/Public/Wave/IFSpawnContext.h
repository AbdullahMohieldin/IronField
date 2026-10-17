#pragma once

#include "CoreMinimal.h"
#include "IFSpawnContext.generated.h"

USTRUCT(BlueprintType)
struct IRONFIELD_API FIFSpawnContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave")
	int32 WaveNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave")
	float SiegeBias = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave")
	float HealthScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Wave")
	float DamageScale = 1.f;
};
