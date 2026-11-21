#include "UI/IFTacticalHUDWidget.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Tactical/IFTacticalCommandComponent.h"
#include "TimerManager.h"

void UIFTacticalHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ConfigureFormationButtons();

	if (IsValid(ModeStatusText))
	{
		ModeStatusText->SetText(NSLOCTEXT("IronFieldTactical", "ModeTitle", "TACTICAL COMMAND  ·  [Tab] Return"));
	}

	if (const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
	{
		UpdateSelectionState(Tactical->GetSelectedSoldierCount());
		UpdateFormationHighlight(Tactical->GetActiveFormationType());
	}
	else
	{
		UpdateSelectionState(0);
		UpdateFormationHighlight(EIFTacticalFormationType::Line);
	}

	HideOrderToast();
}

void UIFTacticalHUDWidget::NativeDestruct()
{
	HideOrderToast();
	UnbindFormationButtons();
	UnbindFromTacticalComponent();
	Super::NativeDestruct();
}

void UIFTacticalHUDWidget::BindToTacticalComponent(UIFTacticalCommandComponent* InTacticalComponent)
{
	UnbindFromTacticalComponent();

	if (!IsValid(InTacticalComponent))
	{
		return;
	}

	BoundTacticalComponent = InTacticalComponent;
	InTacticalComponent->OnTacticalModeChanged.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleTacticalModeChanged);
	InTacticalComponent->OnSelectionChanged.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleSelectionChanged);
	InTacticalComponent->OnOrderIssued.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleOrderIssued);
	InTacticalComponent->OnFormationTypeChanged.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleFormationTypeChanged);

	if (IsValid(ModeStatusText))
	{
		ModeStatusText->SetText(NSLOCTEXT("IronFieldTactical", "ModeTitle", "TACTICAL COMMAND  ·  [Tab] Return"));
	}

	UpdateSelectionState(InTacticalComponent->GetSelectedSoldierCount());
	UpdateFormationHighlight(InTacticalComponent->GetActiveFormationType());
	HideOrderToast();
}

void UIFTacticalHUDWidget::UnbindFromTacticalComponent()
{
	HideOrderToast();

	if (UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
	{
		Tactical->OnTacticalModeChanged.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleTacticalModeChanged);
		Tactical->OnSelectionChanged.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleSelectionChanged);
		Tactical->OnOrderIssued.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleOrderIssued);
		Tactical->OnFormationTypeChanged.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleFormationTypeChanged);
	}
	BoundTacticalComponent.Reset();
}

void UIFTacticalHUDWidget::ConfigureFormationButtons()
{
	SetIsFocusable(false);

	if (IsValid(LineFormationButton))
	{
		LineFormationButton->OnClicked.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleLineFormationClicked);
	}
	if (IsValid(SquareFormationButton))
	{
		SquareFormationButton->OnClicked.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleSquareFormationClicked);
	}
	if (IsValid(CircleFormationButton))
	{
		CircleFormationButton->OnClicked.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleCircleFormationClicked);
	}
}

void UIFTacticalHUDWidget::UnbindFormationButtons()
{
	if (IsValid(LineFormationButton))
	{
		LineFormationButton->OnClicked.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleLineFormationClicked);
	}
	if (IsValid(SquareFormationButton))
	{
		SquareFormationButton->OnClicked.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleSquareFormationClicked);
	}
	if (IsValid(CircleFormationButton))
	{
		CircleFormationButton->OnClicked.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleCircleFormationClicked);
	}
}

void UIFTacticalHUDWidget::HandleTacticalModeChanged(bool bIsTacticalModeActive)
{
	if (bIsTacticalModeActive)
	{
		if (const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
		{
			UpdateSelectionState(Tactical->GetSelectedSoldierCount());
			UpdateFormationHighlight(Tactical->GetActiveFormationType());
		}
		HideOrderToast();
	}
	else
	{
		HideOrderToast();
	}
}

void UIFTacticalHUDWidget::HandleSelectionChanged(int32 SelectedUnitCount)
{
	UpdateSelectionState(SelectedUnitCount);
}

void UIFTacticalHUDWidget::HandleOrderIssued(EIFTacticalOrderType OrderType, FVector)
{
	ShowOrderToast(OrderType);
}

void UIFTacticalHUDWidget::HandleFormationTypeChanged(EIFTacticalFormationType NewFormationType)
{
	UpdateFormationHighlight(NewFormationType);
}

void UIFTacticalHUDWidget::HandleLineFormationClicked()
{
	SelectFormationFromButton(EIFTacticalFormationType::Line);
}

void UIFTacticalHUDWidget::HandleSquareFormationClicked()
{
	SelectFormationFromButton(EIFTacticalFormationType::Square);
}

void UIFTacticalHUDWidget::HandleCircleFormationClicked()
{
	SelectFormationFromButton(EIFTacticalFormationType::Circle);
}

void UIFTacticalHUDWidget::SelectFormationFromButton(EIFTacticalFormationType FormationType)
{
	if (UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
	{
		Tactical->SetActiveFormationType(FormationType);
	}

	// Always return focus to the game viewport so clicking formation buttons never steals WASD/Tab/1-3 keys.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UIFTacticalHUDWidget::UpdateSelectionState(int32 SelectedUnitCount)
{
	const bool bHasSelection = SelectedUnitCount > 0;

	if (IsValid(SelectionBadgeBorder))
	{
		SelectionBadgeBorder->SetVisibility(bHasSelection ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (IsValid(SelectedSquadText))
	{
		if (bHasSelection)
		{
			const FText UnitLabel = (SelectedUnitCount == 1)
				? NSLOCTEXT("IronFieldTactical", "SelectedSingular", "1 Soldier Selected")
				: FText::Format(NSLOCTEXT("IronFieldTactical", "SelectedPlural", "{0} Soldiers Selected"), FText::AsNumber(SelectedUnitCount));
			SelectedSquadText->SetText(UnitLabel);
		}
		else
		{
			SelectedSquadText->SetText(FText::GetEmpty());
		}
	}

	if (IsValid(FormationBarContainer))
	{
		FormationBarContainer->SetVisibility(bHasSelection ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (IsValid(ControlsHintText))
	{
		if (bHasSelection)
		{
			ControlsHintText->SetText(NSLOCTEXT(
				"IronFieldTactical",
				"HintSelected",
				"[RMB] Move (Drag to Size & Face)  ·  [RMB Enemy] Attack  ·  [RMB Hero] Escort  ·  [1/2/3] Formation"));
		}
		else
		{
			ControlsHintText->SetText(NSLOCTEXT(
				"IronFieldTactical",
				"HintEmpty",
				"[LMB / Drag] Select Squad  ·  [WASD / Wheel / Q·E] Camera"));
		}
	}
}

void UIFTacticalHUDWidget::UpdateFormationHighlight(EIFTacticalFormationType ActiveType)
{
	auto ApplyStyle = [this](UBorder* HighlightBorder, UTextBlock* Label, bool bIsActive)
	{
		if (IsValid(HighlightBorder))
		{
			HighlightBorder->SetBrushColor(bIsActive ? ActiveFormationBrushColor : InactiveFormationBrushColor);
		}
		if (IsValid(Label))
		{
			Label->SetColorAndOpacity(FSlateColor(bIsActive ? ActiveFormationTextColor : InactiveFormationTextColor));
		}
	};

	ApplyStyle(LineFormationHighlight, LineFormationLabel, ActiveType == EIFTacticalFormationType::Line);
	ApplyStyle(SquareFormationHighlight, SquareFormationLabel, ActiveType == EIFTacticalFormationType::Square);
	ApplyStyle(CircleFormationHighlight, CircleFormationLabel, ActiveType == EIFTacticalFormationType::Circle);
}

void UIFTacticalHUDWidget::ShowOrderToast(EIFTacticalOrderType OrderType)
{
	if (OrderType == EIFTacticalOrderType::None)
	{
		HideOrderToast();
		return;
	}

	if (IsValid(LastOrderText))
	{
		switch (OrderType)
		{
		case EIFTacticalOrderType::MoveTo:
			LastOrderText->SetText(NSLOCTEXT("IronFieldTactical", "OrderMoveTo", "Moving in Formation"));
			break;
		case EIFTacticalOrderType::AttackTarget:
			LastOrderText->SetText(NSLOCTEXT("IronFieldTactical", "OrderAttack", "Attacking Target"));
			break;
		case EIFTacticalOrderType::FollowCommander:
			LastOrderText->SetText(NSLOCTEXT("IronFieldTactical", "OrderFollow", "Escorting Commander"));
			break;
		default:
			HideOrderToast();
			return;
		}
	}

	if (IsValid(OrderToastBorder))
	{
		OrderToastBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OrderToastTimerHandle);
		const float EffectiveDilation = FMath::Max(UGameplayStatics::GetGlobalTimeDilation(this), 0.1f);
		const float TimerDelay = FMath::Max(0.05f, OrderToastDurationSeconds * EffectiveDilation);
		World->GetTimerManager().SetTimer(
			OrderToastTimerHandle,
			this,
			&UIFTacticalHUDWidget::HideOrderToast,
			TimerDelay,
			false);
	}
}

void UIFTacticalHUDWidget::HideOrderToast()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OrderToastTimerHandle);
	}

	if (IsValid(OrderToastBorder))
	{
		OrderToastBorder->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (IsValid(LastOrderText))
	{
		LastOrderText->SetText(FText::GetEmpty());
	}
}
