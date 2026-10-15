#include "Core/IFPlayerControllerUtils.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace IFPlayerControllerUtils
{
	void FocusWidgetWithUIOnlyInput(APlayerController* Controller, UUserWidget* Widget)
	{
		if (!Controller || !Widget)
		{
			return;
		}

		Widget->AddToViewport();

		TSharedPtr<SWidget> FocusWidget = Widget->TakeWidget();
		if (!FocusWidget.IsValid())
		{
			return;
		}

		FInputModeUIOnly InputMode;
		if (Widget->IsFocusable())
		{
			InputMode.SetWidgetToFocus(FocusWidget.ToSharedRef());
		}
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Controller->SetInputMode(InputMode);
		Controller->bShowMouseCursor = true;
	}

	void OpenLevelUnpaused(const UObject* WorldContext, FName LevelName)
	{
		if (!WorldContext || LevelName.IsNone())
		{
			return;
		}

		UGameplayStatics::SetGamePaused(WorldContext, false);
		UGameplayStatics::OpenLevel(WorldContext, LevelName);
	}
}
