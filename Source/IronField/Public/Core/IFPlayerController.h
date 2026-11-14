#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/PlayerController.h"
#include "IFPlayerController.generated.h"

class UUserWidget;
struct FStreamableHandle;

UCLASS()
class IRONFIELD_API AIFPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void SetMatchPaused(bool bPaused);

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void RestartMatch();

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void ReturnToMainMenu();

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void TogglePause();

	void ShowGameOverScreen(EIFGameResult Result);

	UFUNCTION(Exec)
	void DebugTogglePause();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetupInputComponent() override;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI")
	TSoftClassPtr<UUserWidget> HUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI")
	TSoftClassPtr<UUserWidget> PauseMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI")
	TSoftClassPtr<UUserWidget> GameOverScreenWidgetClass;

private:
	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> PauseMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> GameOverScreenWidget;

	TSharedPtr<FStreamableHandle> HUDLoadHandle;
	TSharedPtr<FStreamableHandle> MenuWidgetsLoadHandle;

	void HandleHUDClassLoaded();
	void HandleMenuWidgetsPreloaded();
	void ShowPauseMenu();
	void ClosePauseMenu();
	void ApplyGameplayInputMode();
	void ApplyUIInputMode();
	bool IsGameOverActive() const;
};