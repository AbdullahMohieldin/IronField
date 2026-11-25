#pragma once

#include "CoreMinimal.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/PlayerController.h"
#include "IFPlayerController.generated.h"

class APawn;
class AIFGameState;
class UInputAction;
class UIFBuildingComponent;
class UIFTacticalCommandComponent;
class UUserWidget;
class UIFVitalBarsWidget;

UCLASS()
class IRONFIELD_API AIFPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AIFPlayerController();

	UFUNCTION(BlueprintPure, Category = "IronField|Tactical")
	UIFTacticalCommandComponent* GetTacticalCommandComponent() const { return TacticalCommandComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Build")
	UIFBuildingComponent* GetBuildingComponent() const { return BuildingSystem; }

	void SetInteractionPromptText(const FText& PromptText);

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void SetMatchPaused(bool bPaused);

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void RestartMatch();

	UFUNCTION(BlueprintCallable, Category = "IronField|Flow")
	void TogglePause();

	void ApplyGameplayInputMode(bool bFlushKeys = true);
	void ApplyTacticalInputMode();
	bool IsGameOverActive() const;

	UFUNCTION(Exec)
	void DebugTogglePause();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void SetupInputComponent() override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|UI")
	TSubclassOf<UIFVitalBarsWidget> PlayerHUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|UI")
	TSubclassOf<UUserWidget> PauseMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|UI")
	TSubclassOf<UUserWidget> GameOverScreenWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Input")
	TObjectPtr<UInputAction> PauseInputAction;

private:
	UPROPERTY(Transient)
	TObjectPtr<UIFVitalBarsWidget> PlayerHUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> PauseMenuWidget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> GameOverScreenWidget;

	void EnsurePlayerHUDCreatedAndBound(APawn* InPawn);
	void ShowPauseMenu();
	void ClosePauseMenu();
	void ShowGameOverScreen(EIFGameResult Result);
	void ApplyPauseInputMode();
	void ApplyUIInputMode();

	UFUNCTION()
	void HandleMatchResultChanged(EIFGameResult NewResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Tactical", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFTacticalCommandComponent> TacticalCommandComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Build", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFBuildingComponent> BuildingSystem;

	TWeakObjectPtr<AIFGameState> ObservedGameState;

	bool bIsGameOver = false;
};