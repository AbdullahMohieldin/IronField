#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/PlayerController.h"
#include "IFPlayerController.generated.h"

class APawn;
class UInputAction;
class UIFTacticalCommandComponent;
class UUserWidget;
class UIFVitalBarsWidget;
struct FStreamableHandle;

UCLASS()
class IRONFIELD_API AIFPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AIFPlayerController();

	UFUNCTION(BlueprintPure, Category = "IronField|Tactical")
	UIFTacticalCommandComponent* GetTacticalCommandComponent() const { return TacticalCommandComponent; }

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void SetMatchPaused(bool bPaused);

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void RestartMatch();

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void ReturnToMainMenu();

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void TogglePause();

	void ShowGameOverScreen(EIFGameResult Result);
	void ApplyGameplayInputMode(bool bFlushKeys = true);
	void ApplyTacticalInputMode();
	bool IsGameOverActive() const;

	UFUNCTION(Exec)
	void DebugTogglePause();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void SetupInputComponent() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|UI")
	TSubclassOf<UIFVitalBarsWidget> PlayerHUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI")
	TSoftClassPtr<UUserWidget> HUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI")
	TSoftClassPtr<UUserWidget> PauseMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI")
	TSoftClassPtr<UUserWidget> GameOverScreenWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Input")
	TSoftObjectPtr<UInputAction> PauseInputAction;

private:
	UPROPERTY(Transient)
	TObjectPtr<UIFVitalBarsWidget> PlayerHUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> PauseMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> GameOverScreenWidget;

	TSharedPtr<FStreamableHandle> HUDLoadHandle;
	TSharedPtr<FStreamableHandle> MenuWidgetsLoadHandle;
	TSharedPtr<FStreamableHandle> PauseActionLoadHandle;

	void HandleHUDClassLoaded();
	void EnsurePlayerHUDCreatedAndBound(APawn* InPawn);
	void HandleMenuWidgetsPreloaded();
	void HandlePauseActionLoaded();
	void BindPauseAction();
	void ShowPauseMenu();
	void ClosePauseMenu();
	void ApplyPauseInputMode();
	void ApplyUIInputMode();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Tactical", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFTacticalCommandComponent> TacticalCommandComponent;

	bool bIsGameOver = false;
};