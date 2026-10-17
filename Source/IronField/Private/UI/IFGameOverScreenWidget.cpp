#include "UI/IFGameOverScreenWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Core/IFGameInstance.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerControllerUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Wave/IFWaveManager.h"

void UIFGameOverScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (RestartButton)
	{
		RestartButton->OnClicked.AddDynamic(this, &UIFGameOverScreenWidget::HandleRestartClicked);
	}

	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.AddDynamic(this, &UIFGameOverScreenWidget::HandleMainMenuClicked);
	}

	ApplyResultTitle();
	ApplyStatsLine();
}

void UIFGameOverScreenWidget::NativeDestruct()
{
	if (RestartButton)
	{
		RestartButton->OnClicked.RemoveAll(this);
	}

	if (MainMenuButton)
	{
		MainMenuButton->OnClicked.RemoveAll(this);
	}

	Super::NativeDestruct();
}

void UIFGameOverScreenWidget::ApplyResultTitle()
{
	if (!ResultTitleText)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-UI] ResultTitleText BindWidget is missing; the result title will not update."));
		return;
	}

	const bool bVictory = Result == EIFGameResult::Victory;
	ResultTitleText->SetText(bVictory ? VictoryText : DefeatText);
	ResultTitleText->SetColorAndOpacity(bVictory ? VictoryColor : DefeatColor);
}

void UIFGameOverScreenWidget::ApplyStatsLine()
{
	if (!StatsText)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-UI] StatsText BindWidget is missing; the run stats line will not update."));
		return;
	}

	// Explicit stats passed to SetResult always win (keeps future callers flexible).
	if (!Stats.IsEmpty())
	{
		StatsText->SetText(Stats);
		return;
	}

	// Default path: build "WAVE N - KILLS M" from the live wave manager so the
	// existing ShowGameOverScreen(Result) caller needs no change.
	const UWorld* const World = GetWorld();
	const UIFActorRegistrySubsystem* const Subsystem = World ? World->GetSubsystem<UIFActorRegistrySubsystem>() : nullptr;
	const AIFWaveManager* const WaveManager = Subsystem ? Subsystem->GetActor<AIFWaveManager>(IFRegistryRoles::WaveManager) : nullptr;
	if (!WaveManager)
	{
		return;
	}

	const int32 WaveNumber = FMath::Max(1, WaveManager->GetCurrentWave());
	StatsText->SetText(FText::FromString(FString::Printf(TEXT("WAVE %d  •  KILLS %d"), WaveNumber, WaveManager->GetKillCount())));
}

void UIFGameOverScreenWidget::UnpauseAndOpenLevel(FName LevelName)
{
	IFPlayerControllerUtils::OpenLevelUnpaused(this, LevelName);
}

void UIFGameOverScreenWidget::HandleRestartClicked()
{
	UnpauseAndOpenLevel(FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}

void UIFGameOverScreenWidget::HandleMainMenuClicked()
{
	const UIFGameInstance* const GameInstance = Cast<UIFGameInstance>(GetGameInstance());
	if (!GameInstance)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-UI] Game over screen has no IFGameInstance; cannot return to the main menu."));
		return;
	}

	UnpauseAndOpenLevel(GameInstance->MainMenuLevelName);
}
