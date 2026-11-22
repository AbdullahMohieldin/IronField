#include "UI/IFTacticalHUDWidget.h"

#include "Castle/IFStructure.h"
#include "Castle/IFStructureDataAsset.h"
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
	ConfigureBuildButtons();
	ConfigureStructureActionButtons();

	if (IsValid(ModeStatusText))
	{
		ModeStatusText->SetText(NSLOCTEXT("IronFieldTactical", "ModeTitle", "TACTICAL COMMAND  ·  [Tab] Return"));
	}

	if (const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
	{
		UpdateSelectionState(Tactical->GetSelectedSoldierCount());
		UpdateSelectedStructureState(Tactical->GetSelectedStructure());
		UpdateFormationHighlight(Tactical->GetActiveFormationType());
		UpdateBuildHighlight(Tactical->IsInBuildPlacementMode() && !Tactical->IsRelocatingStructure(), Tactical->GetActiveBuildData());
	}
	else
	{
		UpdateSelectionState(0);
		UpdateSelectedStructureState(nullptr);
		UpdateFormationHighlight(EIFTacticalFormationType::Line);
		UpdateBuildHighlight(false, nullptr);
	}

	if (IsValid(BuildPlacementStatusBorder))
	{
		BuildPlacementStatusBorder->SetVisibility(ESlateVisibility::Collapsed);
	}

	HideOrderToast();
}

void UIFTacticalHUDWidget::NativeDestruct()
{
	HideOrderToast();
	UnbindFormationButtons();
	UnbindBuildButtons();
	UnbindStructureActionButtons();
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
	InTacticalComponent->OnBuildPlacementModeChanged.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementModeChanged);
	InTacticalComponent->OnBuildPlacementValidityChanged.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementValidityChanged);
	InTacticalComponent->OnSelectedStructureChanged.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleSelectedStructureChanged);

	if (IsValid(ModeStatusText))
	{
		ModeStatusText->SetText(NSLOCTEXT("IronFieldTactical", "ModeTitle", "TACTICAL COMMAND  ·  [Tab] Return"));
	}

	UpdateSelectionState(InTacticalComponent->GetSelectedSoldierCount());
	UpdateSelectedStructureState(InTacticalComponent->GetSelectedStructure());
	UpdateFormationHighlight(InTacticalComponent->GetActiveFormationType());
	UpdateBuildHighlight(InTacticalComponent->IsInBuildPlacementMode() && !InTacticalComponent->IsRelocatingStructure(), InTacticalComponent->GetActiveBuildData());
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
		Tactical->OnBuildPlacementModeChanged.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementModeChanged);
		Tactical->OnBuildPlacementValidityChanged.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementValidityChanged);
		Tactical->OnSelectedStructureChanged.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleSelectedStructureChanged);
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
			UpdateSelectedStructureState(Tactical->GetSelectedStructure());
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
	const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get();
	const bool bIsPlacementActive = IsValid(Tactical) && Tactical->IsInBuildPlacementMode();

	if (IsValid(SelectionBadgeBorder))
	{
		SelectionBadgeBorder->SetVisibility((bHasSelection && !bIsPlacementActive) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
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
		FormationBarContainer->SetVisibility((bHasSelection && !bIsPlacementActive) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	UpdateControlsHintText(SelectedUnitCount, bIsPlacementActive);
}

void UIFTacticalHUDWidget::UpdateControlsHintText(int32 SelectedUnitCount, bool bIsPlacementActive)
{
	if (!IsValid(ControlsHintText))
	{
		return;
	}

	const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get();
	const bool bIsRelocating = bIsPlacementActive && IsValid(Tactical) && Tactical->IsRelocatingStructure();
	const AIFStructure* const SelectedStruct = IsValid(Tactical) ? Tactical->GetSelectedStructure() : nullptr;

	if (bIsRelocating)
	{
		ControlsHintText->SetText(NSLOCTEXT(
			"IronFieldTactical",
			"HintRelocatePlacement",
			"[LMB] Confirm Relocate  ·  [R] Rotate 45°  ·  [RMB] Cancel"));
	}
	else if (bIsPlacementActive)
	{
		ControlsHintText->SetText(NSLOCTEXT(
			"IronFieldTactical",
			"HintBuildPlacement",
			"[LMB] Place  ·  [R] Rotate 45°  ·  [Shift+LMB] Chain  ·  [RMB] Cancel"));
	}
	else if (IsValid(SelectedStruct) && SelectedStruct->IsStructureAlive())
	{
		ControlsHintText->SetText(NSLOCTEXT(
			"IronFieldTactical",
			"HintStructureSelected",
			"[MOVE] Relocate Structure  ·  [DEMOLISH] Remove  ·  [LMB] Deselect"));
	}
	else if (SelectedUnitCount > 0)
	{
		ControlsHintText->SetText(NSLOCTEXT(
			"IronFieldTactical",
			"HintSelected",
			"[RMB] Move (Drag to Size & Face)  ·  [RMB Enemy/Structure] Attack  ·  [RMB Hero] Escort  ·  [1/2/3] Formation"));
	}
	else
	{
		ControlsHintText->SetText(NSLOCTEXT(
			"IronFieldTactical",
			"HintEmpty",
			"[LMB / Drag] Select Squad or Structure  ·  [Build Bar] Construct  ·  [WASD / Wheel / Q·E] Camera"));
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
		case EIFTacticalOrderType::BuildStructure:
			LastOrderText->SetText(NSLOCTEXT("IronFieldTactical", "OrderBuild", "Structure Constructed"));
			break;
		case EIFTacticalOrderType::RelocateStructure:
			LastOrderText->SetText(NSLOCTEXT("IronFieldTactical", "OrderRelocate", "Structure Relocated"));
			break;
		case EIFTacticalOrderType::DemolishStructure:
			LastOrderText->SetText(NSLOCTEXT("IronFieldTactical", "OrderDemolish", "Structure Demolished"));
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

void UIFTacticalHUDWidget::ConfigureBuildButtons()
{
	if (IsValid(BuildKeepButton))
	{
		BuildKeepButton->OnClicked.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleBuildKeepClicked);
	}
	if (IsValid(BuildGuardTowerButton))
	{
		BuildGuardTowerButton->OnClicked.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleBuildGuardTowerClicked);
	}
	if (IsValid(BuildWallButton))
	{
		BuildWallButton->OnClicked.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleBuildWallClicked);
	}
}

void UIFTacticalHUDWidget::UnbindBuildButtons()
{
	if (IsValid(BuildKeepButton))
	{
		BuildKeepButton->OnClicked.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleBuildKeepClicked);
	}
	if (IsValid(BuildGuardTowerButton))
	{
		BuildGuardTowerButton->OnClicked.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleBuildGuardTowerClicked);
	}
	if (IsValid(BuildWallButton))
	{
		BuildWallButton->OnClicked.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleBuildWallClicked);
	}
}

void UIFTacticalHUDWidget::HandleBuildPlacementModeChanged(bool bIsPlacementActive, UIFStructureDataAsset* ActiveStructureData)
{
	const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get();
	const bool bIsRelocating = bIsPlacementActive && IsValid(Tactical) && Tactical->IsRelocatingStructure();
	UpdateBuildHighlight(bIsPlacementActive && !bIsRelocating, ActiveStructureData);

	const int32 SelectedCount = IsValid(Tactical) ? Tactical->GetSelectedSoldierCount() : 0;
	UpdateSelectionState(SelectedCount);
	UpdateSelectedStructureState(IsValid(Tactical) ? Tactical->GetSelectedStructure() : nullptr);

	if (!bIsPlacementActive && IsValid(BuildPlacementStatusBorder))
	{
		BuildPlacementStatusBorder->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UIFTacticalHUDWidget::HandleSelectedStructureChanged(AIFStructure* SelectedStructure)
{
	UpdateSelectedStructureState(SelectedStructure);
}

void UIFTacticalHUDWidget::ConfigureStructureActionButtons()
{
	if (IsValid(MoveStructureButton))
	{
		MoveStructureButton->OnClicked.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleMoveStructureClicked);
	}
	if (IsValid(DemolishStructureButton))
	{
		DemolishStructureButton->OnClicked.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleDemolishStructureClicked);
	}
}

void UIFTacticalHUDWidget::UnbindStructureActionButtons()
{
	if (IsValid(MoveStructureButton))
	{
		MoveStructureButton->OnClicked.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleMoveStructureClicked);
	}
	if (IsValid(DemolishStructureButton))
	{
		DemolishStructureButton->OnClicked.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleDemolishStructureClicked);
	}
}

void UIFTacticalHUDWidget::HandleMoveStructureClicked()
{
	if (UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
	{
		Tactical->BeginRelocateSelectedStructure();
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UIFTacticalHUDWidget::HandleDemolishStructureClicked()
{
	if (UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
	{
		Tactical->DemolishSelectedStructure();
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UIFTacticalHUDWidget::UpdateSelectedStructureState(AIFStructure* SelectedStructure)
{
	const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get();
	const bool bIsPlacementActive = IsValid(Tactical) && Tactical->IsInBuildPlacementMode();
	const bool bShowBar = IsValid(SelectedStructure) && SelectedStructure->IsStructureAlive() && !bIsPlacementActive;

	if (IsValid(StructureActionBarContainer))
	{
		StructureActionBarContainer->SetVisibility(bShowBar ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (bShowBar)
	{
		if (IsValid(SelectedStructureNameText))
		{
			const UIFStructureDataAsset* const Data = SelectedStructure->GetStructureData();
			SelectedStructureNameText->SetText(IsValid(Data) ? Data->DisplayName : NSLOCTEXT("IronFieldTactical", "SelectedStructureDefault", "Structure"));
		}

		if (IsValid(DemolishStructureButton))
		{
			DemolishStructureButton->SetIsEnabled(!SelectedStructure->IsKeep());
		}
	}

	const int32 SelectedCount = IsValid(Tactical) ? Tactical->GetSelectedSoldierCount() : 0;
	UpdateControlsHintText(SelectedCount, bIsPlacementActive);
}

void UIFTacticalHUDWidget::HandleBuildPlacementValidityChanged(bool bIsValid, EIFPlacementInvalidReason InvalidReason)
{
	if (!IsValid(BuildPlacementStatusBorder))
	{
		return;
	}

	if (bIsValid || InvalidReason == EIFPlacementInvalidReason::None)
	{
		BuildPlacementStatusBorder->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	if (IsValid(BuildPlacementStatusText))
	{
		FText ReasonText;
		switch (InvalidReason)
		{
		case EIFPlacementInvalidReason::OutsideOwnedTerritory:
			ReasonText = NSLOCTEXT("IronFieldTactical", "BuildOutsideTerritory", "Place inside an owned Territory");
			break;
		case EIFPlacementInvalidReason::HeroNotInTerritory:
			ReasonText = NSLOCTEXT("IronFieldTactical", "BuildHeroOutside", "Move Greystone into this Territory to build");
			break;
		case EIFPlacementInvalidReason::KeepAlreadyExists:
			ReasonText = NSLOCTEXT("IronFieldTactical", "BuildKeepExists", "Territory already has a Keep (max 1)");
			break;
		case EIFPlacementInvalidReason::TooCloseToFlag:
			ReasonText = NSLOCTEXT("IronFieldTactical", "BuildTooCloseFlag", "Cannot obstruct Capture Flag zone");
			break;
		case EIFPlacementInvalidReason::BlockedByObstacle:
			ReasonText = NSLOCTEXT("IronFieldTactical", "BuildBlocked", "Blocked by structure or unit");
			break;
		default:
			ReasonText = NSLOCTEXT("IronFieldTactical", "BuildInvalid", "Invalid placement location");
			break;
		}
		BuildPlacementStatusText->SetText(ReasonText);
	}

	BuildPlacementStatusBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UIFTacticalHUDWidget::HandleBuildKeepClicked()
{
	SelectBuildStructureFromButton(EIFStructureType::Keep);
}

void UIFTacticalHUDWidget::HandleBuildGuardTowerClicked()
{
	SelectBuildStructureFromButton(EIFStructureType::GuardTower);
}

void UIFTacticalHUDWidget::HandleBuildWallClicked()
{
	SelectBuildStructureFromButton(EIFStructureType::WallSegment);
}

void UIFTacticalHUDWidget::SelectBuildStructureFromButton(EIFStructureType StructureType)
{
	if (UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
	{
		Tactical->BeginBuildPlacementByType(StructureType);
	}

	// Always return focus to game viewport so clicking build buttons never steals WASD/R/Tab keys.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UIFTacticalHUDWidget::UpdateBuildHighlight(bool bIsPlacementActive, const UIFStructureDataAsset* ActiveData)
{
	const bool bHasActiveType = bIsPlacementActive && IsValid(ActiveData);
	const EIFStructureType ActiveType = bHasActiveType ? ActiveData->StructureType : EIFStructureType::Keep;

	auto ApplyBorder = [this](UBorder* Highlight, bool bActive)
	{
		if (IsValid(Highlight))
		{
			Highlight->SetBrushColor(bActive ? ActiveFormationBrushColor : InactiveFormationBrushColor);
		}
	};

	ApplyBorder(BuildKeepHighlight, bHasActiveType && ActiveType == EIFStructureType::Keep);
	ApplyBorder(BuildGuardTowerHighlight, bHasActiveType && ActiveType == EIFStructureType::GuardTower);
	ApplyBorder(BuildWallHighlight, bHasActiveType && ActiveType == EIFStructureType::WallSegment);
}
