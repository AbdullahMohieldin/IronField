#include "Core/IFPlayerController.h"

#include "Blueprint/UserWidget.h"
#include "Castle/IFBuildingComponent.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFPlayerCharacter.h"
#include "Core/IFGameState.h"
#include "Core/IFInteractable.h"
#include "Core/IFLog.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Tactical/IFTacticalCommandComponent.h"
#include "UI/IFTacticalHUDWidget.h"
#include "UI/IFVitalBarsWidget.h"

AIFPlayerController::AIFPlayerController()
{
	TacticalCommandComponent = CreateDefaultSubobject<UIFTacticalCommandComponent>(TEXT("TacticalCommand"));
	BuildingSystem = CreateDefaultSubobject<UIFBuildingComponent>(TEXT("BuildingSystem"));
}

void AIFPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}

	ApplyGameplayInputMode();

	if (APawn* const CurrentPawn = GetPawn())
	{
		EnsurePlayerHUDCreatedAndBound(CurrentPawn);
	}

	if (const UWorld* const World = GetWorld())
	{
		if (AIFGameState* const GameState = World->GetGameState<AIFGameState>(); IsValid(GameState))
		{
			ObservedGameState = GameState;
			GameState->OnGameResultChanged.AddUniqueDynamic(this, &AIFPlayerController::HandleMatchResultChanged);
			if (GameState->GetMatchResult() != EIFGameResult::None)
			{
				HandleMatchResultChanged(GameState->GetMatchResult());
			}
		}
	}
}

void AIFPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (AIFGameState* const GameState = ObservedGameState.Get(); IsValid(GameState))
	{
		GameState->OnGameResultChanged.RemoveDynamic(this, &AIFPlayerController::HandleMatchResultChanged);
	}
	ObservedGameState.Reset();
	NearbyInteractables.Reset();

	if (IsValid(PlayerHUDWidget))
	{
		PlayerHUDWidget->UnbindVitalComponents();
		PlayerHUDWidget->RemoveFromParent();
		PlayerHUDWidget = nullptr;
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

void AIFPlayerController::OnUnPossess()
{
	Super::OnUnPossess();

	if (IsValid(PlayerHUDWidget))
	{
		PlayerHUDWidget->UnbindVitalComponents();
	}

	NearbyInteractables.Reset();

	if (IsValid(TacticalCommandComponent))
	{
		if (UIFTacticalHUDWidget* const HUD = TacticalCommandComponent->GetTacticalHUDWidget())
		{
			HUD->SetInteractionPrompt(FText::GetEmpty());
		}
	}
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

	InputComponent->SetTickableWhenPaused(true);

	if (!IsValid(PauseInputAction))
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Input] PauseInputAction is not assigned on %s."), *GetName());
	}
	else if (UEnhancedInputComponent* const EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EnhancedInput->BindAction(PauseInputAction, ETriggerEvent::Started, this, &AIFPlayerController::TogglePause);
	}

	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AIFPlayerController::HandleInteractKeyPressed);

	if (IsValid(TacticalCommandComponent))
	{
		TacticalCommandComponent->SetupControllerInput(InputComponent);
	}
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
#if !UE_BUILD_SHIPPING
	TogglePause();
#endif
}

void AIFPlayerController::SetMatchPaused(bool bPaused)
{
	if (IsGameOverActive())
	{
		return;
	}

	if (IsValid(TacticalCommandComponent))
	{
		TacticalCommandComponent->HandleMatchPausedChanged(bPaused);
	}

	if (bPaused)
	{
		ShowPauseMenu();
		ApplyPauseInputMode();
	}
	else
	{
		ClosePauseMenu();
		if (IsValid(TacticalCommandComponent) && TacticalCommandComponent->IsTacticalModeActive())
		{
			ApplyTacticalInputMode();
		}
		else
		{
			ApplyGameplayInputMode();
		}
	}

	UGameplayStatics::SetGamePaused(this, bPaused);
}

void AIFPlayerController::ShowPauseMenu()
{
	if (!IsLocalController() || IsValid(PauseMenuWidget) || !PauseMenuWidgetClass)
	{
		return;
	}

	PauseMenuWidget = CreateWidget<UUserWidget>(this, PauseMenuWidgetClass);
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

void AIFPlayerController::HandleMatchResultChanged(EIFGameResult NewResult)
{
	if (NewResult != EIFGameResult::None)
	{
		ShowGameOverScreen(NewResult);
	}
}

void AIFPlayerController::ShowGameOverScreen(EIFGameResult Result)
{
	if (!IsLocalController() || bIsGameOver)
	{
		return;
	}

	bIsGameOver = true;
	if (IsValid(TacticalCommandComponent))
	{
		TacticalCommandComponent->ExitTacticalMode(true);
	}
	ClosePauseMenu();
	ApplyUIInputMode();
	UGameplayStatics::SetGamePaused(this, true);

	if (GameOverScreenWidgetClass)
	{
		GameOverScreenWidget = CreateWidget<UUserWidget>(this, GameOverScreenWidgetClass);
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
	if (IsValid(TacticalCommandComponent))
	{
		TacticalCommandComponent->ExitTacticalMode(true);
	}
	UGameplayStatics::SetGamePaused(this, false);
	const FName LevelName = FName(*UGameplayStatics::GetCurrentLevelName(this, true));
	UGameplayStatics::OpenLevel(this, LevelName);
}

void AIFPlayerController::ApplyGameplayInputMode(bool bFlushKeys)
{
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
	if (bFlushKeys)
	{
		FlushPressedKeys();
	}
}

void AIFPlayerController::ApplyTacticalInputMode()
{
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockInFullscreen);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
}

void AIFPlayerController::ApplyPauseInputMode()
{
	// GameAndUI keeps player input alive so IA_Pause can resume; UIOnly would swallow it.
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

void AIFPlayerController::RegisterNearbyInteractable(AActor* Interactable)
{
	if (!IsValid(Interactable) || !Interactable->Implements<UIFInteractable>())
	{
		return;
	}

	NearbyInteractables.AddUnique(Interactable);
	RefreshInteractionPrompt();
}

void AIFPlayerController::UnregisterNearbyInteractable(AActor* Interactable)
{
	NearbyInteractables.RemoveAll([Interactable](const TWeakObjectPtr<AActor>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == Interactable;
	});
	RefreshInteractionPrompt();
}

AActor* AIFPlayerController::GetNearestInteractable() const
{
	APawn* const ControlledPawn = GetPawn();
	if (!IsValid(ControlledPawn))
	{
		return nullptr;
	}

	if (const AIFBaseCharacter* const BaseChar = Cast<AIFBaseCharacter>(ControlledPawn); IsValid(BaseChar) && BaseChar->IsDead())
	{
		return nullptr;
	}

	const FVector PawnLoc = ControlledPawn->GetActorLocation();
	float BestDistSq = MAX_flt;
	AActor* BestInteractable = nullptr;

	for (const TWeakObjectPtr<AActor>& Ptr : NearbyInteractables)
	{
		AActor* const Actor = Ptr.Get();
		if (!IsValid(Actor))
		{
			continue;
		}
		const IIFInteractable* const Interactable = Cast<IIFInteractable>(Actor);
		if (Interactable == nullptr || !Interactable->CanInteract(ControlledPawn))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(PawnLoc, Actor->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestInteractable = Actor;
		}
	}

	if (!IsValid(BestInteractable))
	{
		TArray<AActor*> OverlappingActors;
		ControlledPawn->GetOverlappingActors(OverlappingActors);
		for (AActor* const Actor : OverlappingActors)
		{
			if (!IsValid(Actor))
			{
				continue;
			}
			const IIFInteractable* const Interactable = Cast<IIFInteractable>(Actor);
			if (Interactable == nullptr || !Interactable->CanInteract(ControlledPawn))
			{
				continue;
			}

			const float DistSq = FVector::DistSquared(PawnLoc, Actor->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestInteractable = Actor;
			}
		}
	}

	return BestInteractable;
}

void AIFPlayerController::RefreshInteractionPrompt()
{
	NearbyInteractables.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr)
	{
		return !Ptr.IsValid();
	});

	AActor* const Nearest = GetNearestInteractable();
	APawn* const ControlledPawn = GetPawn();
	const FText PromptText = (IsValid(Nearest) && IsValid(ControlledPawn))
		? Cast<IIFInteractable>(Nearest)->GetInteractPromptText(ControlledPawn)
		: FText::GetEmpty();

	if (IsValid(TacticalCommandComponent))
	{
		if (UIFTacticalHUDWidget* const HUD = TacticalCommandComponent->GetTacticalHUDWidget())
		{
			HUD->SetInteractionPrompt(PromptText);
		}
	}
}

void AIFPlayerController::HandleInteractKeyPressed()
{
	if (bIsGameOver || UGameplayStatics::IsGamePaused(this))
	{
		return;
	}
	if (IsValid(TacticalCommandComponent) && TacticalCommandComponent->IsTacticalModeActive())
	{
		return;
	}

	if (AActor* const Nearest = GetNearestInteractable())
	{
		Cast<IIFInteractable>(Nearest)->Interact(GetPawn());
		RefreshInteractionPrompt();
	}
}