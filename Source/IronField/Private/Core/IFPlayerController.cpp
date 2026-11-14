#include "Core/IFPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Core/IFGameInstance.h"
#include "Core/IFLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

void AIFPlayerController::BeginPlay()
{
	Super::BeginPlay();

	ApplyGameplayInputMode();

	FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

	if (!HUDWidgetClass.IsNull())
	{
		HUDLoadHandle = Streamable.RequestAsyncLoad(HUDWidgetClass.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &AIFPlayerController::HandleHUDClassLoaded));
	}

	TArray<FSoftObjectPath> MenuWidgetPaths;
	if (!PauseMenuWidgetClass.IsNull())
	{
		MenuWidgetPaths.Add(PauseMenuWidgetClass.ToSoftObjectPath());
	}
	if (!GameOverScreenWidgetClass.IsNull())
	{
		MenuWidgetPaths.Add(GameOverScreenWidgetClass.ToSoftObjectPath());
	}
	if (MenuWidgetPaths.Num() > 0)
	{
		MenuWidgetsLoadHandle = Streamable.RequestAsyncLoad(MenuWidgetPaths, FStreamableDelegate::CreateUObject(this, &AIFPlayerController::HandleMenuWidgetsPreloaded));
	}
}

void AIFPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HUDLoadHandle.IsValid())
	{
		HUDLoadHandle->CancelHandle();
		HUDLoadHandle.Reset();
	}
	if (MenuWidgetsLoadHandle.IsValid())
	{
		MenuWidgetsLoadHandle->CancelHandle();
		MenuWidgetsLoadHandle.Reset();
	}

	if (IsValid(HUDWidget))
	{
		HUDWidget->RemoveFromParent();
		HUDWidget = nullptr;
	}

	ClosePauseMenu();

	if (IsValid(GameOverScreenWidget))
	{
		GameOverScreenWidget->RemoveFromParent();
		GameOverScreenWidget = nullptr;
	}

	Super::EndPlay(Reason);
}

void AIFPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (IsValid(InputComponent))
	{
		// P must also fire while paused so the same key resumes; Escape is reserved by PIE.
		InputComponent->SetTickableWhenPaused(true);
		InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AIFPlayerController::TogglePause);
		InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this, &AIFPlayerController::TogglePause);
	}
}

void AIFPlayerController::HandleHUDClassLoaded()
{
	HUDLoadHandle.Reset();

	UClass* const WidgetClass = HUDWidgetClass.Get();
	if (!IsValid(WidgetClass))
	{
		return;
	}

	HUDWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (IsValid(HUDWidget))
	{
		HUDWidget->AddToViewport();
	}
}

void AIFPlayerController::HandleMenuWidgetsPreloaded()
{
	MenuWidgetsLoadHandle.Reset();
}

void AIFPlayerController::TogglePause()
{
	if (IsGameOverActive())
	{
		return;
	}

	SetMatchPaused(!IsValid(PauseMenuWidget));
}

void AIFPlayerController::DebugTogglePause()
{
	TogglePause();
}

void AIFPlayerController::SetMatchPaused(bool bPaused)
{
	if (IsGameOverActive())
	{
		return;
	}

	if (bPaused)
	{
		ShowPauseMenu();
	}
	else
	{
		ClosePauseMenu();
		UGameplayStatics::SetGamePaused(this, false);
		ApplyGameplayInputMode();
	}
}

void AIFPlayerController::ShowPauseMenu()
{
	if (IsGameOverActive() || IsValid(PauseMenuWidget))
	{
		return;
	}

	UClass* WidgetClass = PauseMenuWidgetClass.Get();
	if (!IsValid(WidgetClass))
	{
		WidgetClass = PauseMenuWidgetClass.LoadSynchronous();
	}
	if (!IsValid(WidgetClass))
	{
		return;
	}

	PauseMenuWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (!IsValid(PauseMenuWidget))
	{
		return;
	}

	PauseMenuWidget->AddToViewport();
	ApplyUIInputMode();
	UGameplayStatics::SetGamePaused(this, true);
}

void AIFPlayerController::ClosePauseMenu()
{
	if (IsValid(PauseMenuWidget))
	{
		PauseMenuWidget->RemoveFromParent();
		PauseMenuWidget = nullptr;
	}
}

void AIFPlayerController::ShowGameOverScreen(EIFGameResult Result)
{
	if (IsValid(GameOverScreenWidget))
	{
		return;
	}

	ClosePauseMenu();

	UClass* WidgetClass = GameOverScreenWidgetClass.Get();
	if (!IsValid(WidgetClass))
	{
		WidgetClass = GameOverScreenWidgetClass.LoadSynchronous();
	}
	if (!IsValid(WidgetClass))
	{
		return;
	}

	GameOverScreenWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (!IsValid(GameOverScreenWidget))
	{
		return;
	}

	GameOverScreenWidget->AddToViewport();
	ApplyUIInputMode();
	UGameplayStatics::SetGamePaused(this, true);

	UE_LOG(LogIronField, Log, TEXT("[IF-Flow] Game over (result=%d)."), static_cast<int32>(Result));
}

void AIFPlayerController::RestartMatch()
{
	UGameplayStatics::SetGamePaused(this, false);
	const FName LevelName = FName(*UGameplayStatics::GetCurrentLevelName(this, true));
	UGameplayStatics::OpenLevel(this, LevelName);
}

void AIFPlayerController::ReturnToMainMenu()
{
	UGameplayStatics::SetGamePaused(this, false);
	UIFGameInstance* const GameInstance = Cast<UIFGameInstance>(GetGameInstance());
	if (IsValid(GameInstance))
	{
		UGameplayStatics::OpenLevel(this, GameInstance->MainMenuLevelName);
	}
}

void AIFPlayerController::ApplyGameplayInputMode()
{
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
	FlushPressedKeys();
}

void AIFPlayerController::ApplyUIInputMode()
{
	FInputModeUIOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

bool AIFPlayerController::IsGameOverActive() const
{
	return IsValid(GameOverScreenWidget);
}