#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/IFGameTypes.h"
#include "IFGameOverScreenWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * End-of-run screen shared by victory and defeat. Set the result before adding the
 * widget to the viewport. The Blueprint must contain a TextBlock named ResultTitleText
 * plus RestartButton and MainMenuButton; a missing or misnamed widget fails to compile.
	* StatsText is optional: an explicit stats line passed to SetResult wins, otherwise the
 * widget builds "WAVE N - KILLS M" from the wave manager subsystem.
 */
UCLASS()
class IRONFIELD_API UIFGameOverScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetResult(EIFGameResult InResult, const FText& InStats = FText::GetEmpty()) { Result = InResult; Stats = InStats; }

	UFUNCTION(BlueprintPure, Category = "IronField|UI|GameOver")
	EIFGameResult GetResult() const { return Result; }

protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> RestartButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> MainMenuButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ResultTitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatsText;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI|GameOver")
	FText VictoryText = FText::FromString(TEXT("VICTORY"));

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI|GameOver")
	FText DefeatText = FText::FromString(TEXT("DEFEAT"));

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI|GameOver")
	FLinearColor VictoryColor = FLinearColor(1.f, 0.84f, 0.2f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|UI|GameOver")
	FLinearColor DefeatColor = FLinearColor(0.85f, 0.25f, 0.25f);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void HandleRestartClicked();

	UFUNCTION()
	void HandleMainMenuClicked();

	void UnpauseAndOpenLevel(FName LevelName);

private:
	void ApplyResultTitle();
	void ApplyStatsLine();

	EIFGameResult Result = EIFGameResult::Defeat;
	FText Stats;
};
