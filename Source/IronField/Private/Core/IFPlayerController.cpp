#include "Core/IFPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Character/IFPlayerCharacter.h"
#include "Core/IFGameInstance.h"
#include "Core/IFLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "UI/IFVitalBarsWidget.h"

void AIFPlayerController::BeginPlay()
{
	Super::BeginPlay();

	ApplyGameplayInputMode();

	if (APawn* const CurrentPawn = GetPawn())
	{
		EnsurePlayerHUDCreatedAndBound(CurrentPawn);
	}

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

	if (!PauseInputAction.IsNull() && !IsValid(PauseInputAction.Get()))
	{
		PauseActionLoadHandle = Streamable.RequestAsyncLoad(PauseInputAction.ToSoftObjectPath(),
			FStreamableDelegate::CreateUObject(this, &AIFPlayerController::HandlePauseActionLoaded));
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
	if (PauseActionLoadHandle.IsValid())
	{
		PauseActionLoadHandle->CancelHandle();
		PauseActionLoadHandle.Reset();
	}

	if (IsValid(PlayerHUDWidget))
	{
		PlayerHUDWidget->UnbindVitalComponents();
		PlayerHUDWidget->RemoveFromParent();
		PlayerHUDWidget = nullptr;
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

void AIFPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	EnsurePlayerHUDCreatedAndBound(InPawn);
}

void AIFPlayerController::EnsurePlayerHUDCreatedAndBound(APawn* InPawn)
{
	if (!IsLocalController())
	{
		return;
	}

	if (!IsValid(PlayerHUDWidget) && PlayerHUDWidgetClass)
	{
		PlayerHUDWidget = CreateWidget<UIFVitalBarsWidget>(this, PlayerHUDWidgetClass);
		if (IsValid(PlayerHUDWidget))
		{
			PlayerHUDWidget->AddToViewport();
		}
	}

	if (IsValid(PlayerHUDWidget))
	{
		if (const AIFPlayerCharacter* const PlayerChar = Cast<AIFPlayerCharacter>(InPawn))
		{
			PlayerHUDWidget->BindToVitalComponents(PlayerChar->GetHealthComponent(), PlayerChar->GetStaminaComponent());
		}
	}
}

void AIFPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsValid(InputComponent))
	{
		return;
	}

	// P must also fire while paused so the same key resumes; Escape is reserved by PIE.
	InputComponent->SetTickableWhenPaused(true);

	// Raw keys are the fallback until IA_Pause is assigned; binding both would double-toggle.
	if (PauseInputAction.IsNull())
	{
		InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AIFPlayerController::TogglePause);
		InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this, &AIFPlayerController::TogglePause);
	}

	BindPauseAction();
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

void AIFPlayerController::HandlePauseActionLoaded()
{
	PauseActionLoadHandle.Reset();
	BindPauseAction();
}

void AIFPlayerController::BindPauseAction()
{
	UInputAction* const Action = PauseInputAction.Get();
	UEnhancedInputComponent* const EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!IsValid(Action) || !IsValid(EnhancedInput))
	{
		return;
	}

	EnhancedInput->BindAction(Action, ETriggerEvent::Started, this, &AIFPlayerController::TogglePause);
}

void AIFPlayerController::TogglePause()
{
	if (IsGameOverActive())
	{
		return;
	}

	SetMatchPaused(!UGameplayStatics::IsGamePaused(this));
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

	// Pause state and input mode never depend on widget availability; the menu is optional.
	if (bPaused)
	{
		ShowPauseMenu();
		ApplyPauseInputMode();
	}
	else
	{
		ClosePauseMenu();
		ApplyGameplayInputMode();
	}

	UGameplayStatics::SetGamePaused(this, bPaused);
}

void AIFPlayerController::ShowPauseMenu()
{
	if (IsValid(PauseMenuWidget))
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
	if (IsValid(PauseMenuWidget))
	{
		PauseMenuWidget->AddToViewport();
	}
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
	if (bIsGameOver)
	{
		return;
	}

	bIsGameOver = true;
	ClosePauseMenu();
	ApplyUIInputMode();
	UGameplayStatics::SetGamePaused(this, true);

	UClass* WidgetClass = GameOverScreenWidgetClass.Get();
	if (!IsValid(WidgetClass))
	{
		WidgetClass = GameOverScreenWidgetClass.LoadSynchronous();
	}
	if (IsValid(WidgetClass))
	{
		GameOverScreenWidget = CreateWidget<UUserWidget>(this, WidgetClass);
		if (IsValid(GameOverScreenWidget))
		{
			GameOverScreenWidget->AddToViewport();
		}
	}

	UE_LOG(LogIronField, Log, TEXT("[IF-Flow] Game over (result=%d)."), static_cast<int32>(Result));
}

void AIFPlayerController::RestartMatch()
{
	bIsGameOver = false;
	UGameplayStatics::SetGamePaused(this, false);
	const FName LevelName = FName(*UGameplayStatics::GetCurrentLevelName(this, true));
	UGameplayStatics::OpenLevel(this, LevelName);
}

void AIFPlayerController::ReturnToMainMenu()
{
	RestartMatch();
}

void AIFPlayerController::ApplyGameplayInputMode()
{
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
	FlushPressedKeys();
}

void AIFPlayerController::ApplyPauseInputMode()
{
	// GameAndUI keeps player input alive so IA_Pause can resume; UIOnly sets IgnoreInput
	// and would swallow the unpause press along with every other key.
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void AIFPlayerController::ApplyUIInputMode()
{
	FInputModeUIOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

bool AIFPlayerController::IsGameOverActive() const
{
	return bIsGameOver;
}