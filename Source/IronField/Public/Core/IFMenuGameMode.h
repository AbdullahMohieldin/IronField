#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "IFMenuGameMode.generated.h"

class UUserWidget;
struct FStreamableHandle;

UCLASS()
class IRONFIELD_API AIFMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AIFMenuGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI")
	TSoftClassPtr<UUserWidget> MainMenuWidgetClass;

private:
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> MainMenuWidget;

	TSharedPtr<FStreamableHandle> MenuLoadHandle;

	void HandleMenuClassLoaded();
};