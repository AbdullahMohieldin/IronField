#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "IFTestAutomationTypes.generated.h"

/**
 * Helper listener object for capturing dynamic multicast delegate broadcasts in automation tests.
 */
UCLASS()
class IFSYSTEMS_API UIFTestAutomationListener : public UObject
{
	GENERATED_BODY()

public:
	int32 DeathCount = 0;
	int32 HealthDepletedCount = 0;
	int32 StaminaDepletedCount = 0;

	UFUNCTION()
	void HandleDeath(AActor* DeadActor)
	{
		++DeathCount;
	}

	UFUNCTION()
	void HandleHealthDepleted()
	{
		++HealthDepletedCount;
	}

	UFUNCTION()
	void HandleStaminaDepleted()
	{
		++StaminaDepletedCount;
	}
};
