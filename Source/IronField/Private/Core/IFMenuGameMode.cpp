#include "Core/IFMenuGameMode.h"

#include "Blueprint/UserWidget.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

AIFMenuGameMode::AIFMenuGameMode()
{
	PlayerControllerClass = APlayerController::StaticClass();
	DefaultPawnClass = nullptr;
}

void AIFMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!MainMenuWidgetClass.IsNull())
	{
		FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
		MenuLoadHandle = Streamable.RequestAsyncLoad(MainMenuWidgetClass.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &AIFMenuGameMode::HandleMenuClassLoaded));
	}
}

void AIFMenuGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (MenuLoadHandle.IsValid())
	{
		MenuLoadHandle->CancelHandle();
		MenuLoadHandle.Reset();
	}

	if (IsValid(MainMenuWidget))
	{
		MainMenuWidget->RemoveFromParent();
		MainMenuWidget = nullptr;
	}

	Super::EndPlay(Reason);
}

void AIFMenuGameMode::HandleMenuClassLoaded()
{
	MenuLoadHandle.Reset();

	UClass* const WidgetClass = MainMenuWidgetClass.Get();
	if (!IsValid(WidgetClass))
	{
		return;
	}

	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	APlayerController* const PC = World->GetFirstPlayerController();
	if (!IsValid(PC))
	{
		return;
	}

	MainMenuWidget = CreateWidget<UUserWidget>(PC, WidgetClass);
	if (!IsValid(MainMenuWidget))
	{
		return;
	}

	MainMenuWidget->AddToViewport();

	FInputModeUIOnly InputMode;
	PC->SetInputMode(InputMode);
	PC->bShowMouseCursor = true;
}