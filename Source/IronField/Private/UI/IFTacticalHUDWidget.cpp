#include "UI/IFTacticalHUDWidget.h"

#include "Castle/IFBuildingComponent.h"
#include "Castle/IFGateDoor.h"
#include "Castle/IFStructure.h"
#include "Castle/IFStructureDataAsset.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Core/IFPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Tactical/IFTacticalCommandComponent.h"
#include "TimerManager.h"

void UIFBuildSlotButtonProxy::HandleClicked()
{
	if (UIFTacticalHUDWidget* const Owner = OwnerWidget.Get())
	{
		Owner->HandleDynamicBuildSlotClicked(SlotIndex);
	}
}

void UIFTacticalHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	ConfigureFormationButtons();
	ConfigureBuildButtons();
	ConfigureStructureActionButtons();
	EnsureInteractionPromptCreated();

	if (IsValid(ModeStatusText))
	{
		ModeStatusText->SetText(NSLOCTEXT("IronFieldTactical", "ModeTitle", "TACTICAL COMMAND  ·  [Tab] Return"));
	}

	if (const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
	{
		const UIFBuildingComponent* const Building = BoundBuildingComponent.Get();
		RefreshBuildBar();
		UpdateSelectionState(Tactical->GetSelectedSoldierCount());
		UpdateSelectedStructureState(IsValid(Building) ? Building->GetSelectedStructure() : nullptr);
		UpdateFormationHighlight(Tactical->GetActiveFormationType());
		UpdateBuildHighlight(
			IsValid(Building) && Building->IsInBuildPlacementMode() && !Building->IsRelocatingStructure(),
			IsValid(Building) ? Building->GetActiveBuildData() : nullptr);
	}
	else
	{
		if (IsValid(BuildKeepHighlight))
		{
			BuildKeepHighlight->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (IsValid(BuildGuardTowerHighlight))
		{
			BuildGuardTowerHighlight->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (IsValid(BuildWallHighlight))
		{
			BuildWallHighlight->SetVisibility(ESlateVisibility::Collapsed);
		}
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
	UnbindFromBuildingComponent();
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

	if (const AIFPlayerController* const PC = Cast<AIFPlayerController>(InTacticalComponent->GetOwner()))
	{
		if (UIFBuildingComponent* const Building = PC->GetBuildingComponent())
		{
			BindToBuildingComponent(Building);
		}
	}

	if (IsValid(ModeStatusText))
	{
		ModeStatusText->SetText(NSLOCTEXT("IronFieldTactical", "ModeTitle", "TACTICAL COMMAND  ·  [Tab] Return"));
	}

	RefreshBuildBar();
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

void UIFTacticalHUDWidget::BindToBuildingComponent(UIFBuildingComponent* InBuildingComponent)
{
	UnbindFromBuildingComponent();

	if (!IsValid(InBuildingComponent))
	{
		return;
	}

	BoundBuildingComponent = InBuildingComponent;
	InBuildingComponent->OnBuildPlacementModeChanged.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementModeChanged);
	InBuildingComponent->OnBuildPlacementValidityChanged.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementValidityChanged);
	InBuildingComponent->OnBuildPlacementRejected.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementRejected);
	InBuildingComponent->OnSelectedStructureChanged.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleSelectedStructureChanged);
	InBuildingComponent->OnStructureActionCompleted.AddUniqueDynamic(this, &UIFTacticalHUDWidget::HandleOrderIssued);

	RefreshBuildBar();
	UpdateSelectedStructureState(InBuildingComponent->GetSelectedStructure());
	UpdateBuildHighlight(
		InBuildingComponent->IsInBuildPlacementMode() && !InBuildingComponent->IsRelocatingStructure(),
		InBuildingComponent->GetActiveBuildData());
}

void UIFTacticalHUDWidget::UnbindFromBuildingComponent()
{
	EndStatusFlash();
	if (UIFBuildingComponent* const Building = BoundBuildingComponent.Get())
	{
		Building->OnBuildPlacementModeChanged.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementModeChanged);
		Building->OnBuildPlacementValidityChanged.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementValidityChanged);
		Building->OnBuildPlacementRejected.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleBuildPlacementRejected);
		Building->OnSelectedStructureChanged.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleSelectedStructureChanged);
		Building->OnStructureActionCompleted.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleOrderIssued);
	}
	BoundBuildingComponent.Reset();
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
		RefreshBuildBar();
		if (const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get())
		{
			const UIFBuildingComponent* const Building = BoundBuildingComponent.Get();
			UpdateSelectionState(Tactical->GetSelectedSoldierCount());
			UpdateSelectedStructureState(IsValid(Building) ? Building->GetSelectedStructure() : nullptr);
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
	const UIFBuildingComponent* const Building = BoundBuildingComponent.Get();
	const bool bIsPlacementActive = IsValid(Building) && Building->IsInBuildPlacementMode();

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

	const UIFBuildingComponent* const Building = BoundBuildingComponent.Get();
	const bool bIsRelocating = bIsPlacementActive && IsValid(Building) && Building->IsRelocatingStructure();
	const AIFStructure* const SelectedStruct = IsValid(Building) ? Building->GetSelectedStructure() : nullptr;

	if (bIsRelocating)
	{
		ControlsHintText->SetText(NSLOCTEXT(
			"IronFieldTactical",
			"HintRelocatePlacement",
			"[LMB] Confirm Relocate  ·  [R] Rotate 90°  ·  [RMB / Esc] Cancel"));
	}
	else if (bIsPlacementActive)
	{
		const UIFStructureDataAsset* const ActiveDA = IsValid(Building) ? Building->GetActiveBuildData() : nullptr;
		if (IsValid(ActiveDA))
		{
			switch (ActiveDA->PlacementRule)
			{
			case EIFPlacementRule::CornerWalkwayOnly:
				ControlsHintText->SetText(NSLOCTEXT(
					"IronFieldTactical",
					"HintBuildCornerWalkway",
					"[LMB] Place on Wall Corner  ·  [RMB / Esc] Cancel"));
				break;
			case EIFPlacementRule::GateOpeningOnly:
				ControlsHintText->SetText(NSLOCTEXT(
					"IronFieldTactical",
					"HintBuildGateDoor",
					"[LMB] Place inside Gate Archway  ·  [RMB / Esc] Cancel"));
				break;
			case EIFPlacementRule::WallWalkwayOnly:
				ControlsHintText->SetText(NSLOCTEXT(
					"IronFieldTactical",
					"HintBuildWalkway",
					"[LMB] Place on Wall  ·  Move Cursor for Side  ·  [RMB / Esc] Cancel"));
				break;
			case EIFPlacementRule::StairMountOrFree:
				ControlsHintText->SetText(NSLOCTEXT(
					"IronFieldTactical",
					"HintBuildStairs",
					"[LMB] Place  ·  [R] Cycle Orientation  ·  [RMB / Esc] Cancel"));
				break;
			case EIFPlacementRule::FreeOrEndSnap:
			default:
				if (!ActiveDA->Connectors.IsEmpty())
				{
					ControlsHintText->SetText(NSLOCTEXT(
						"IronFieldTactical",
						"HintBuildModular",
						"[LMB] Place  ·  [R] Rotate / Flip  ·  [RMB / Esc] Cancel"));
				}
				else
				{
					ControlsHintText->SetText(NSLOCTEXT(
						"IronFieldTactical",
						"HintBuildStandalone",
						"[LMB] Place  ·  [R] Rotate 90°  ·  [RMB / Esc] Cancel"));
				}
				break;
			}
		}
		else
		{
			ControlsHintText->SetText(NSLOCTEXT(
				"IronFieldTactical",
				"HintBuildPlacement",
				"[LMB] Place  ·  [R] Rotate  ·  [RMB / Esc] Cancel"));
		}
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
	const UIFBuildingComponent* const Building = BoundBuildingComponent.Get();
	const bool bIsRelocating = bIsPlacementActive && IsValid(Building) && Building->IsRelocatingStructure();
	UpdateBuildHighlight(bIsPlacementActive && !bIsRelocating, ActiveStructureData);

	const int32 SelectedCount = IsValid(Tactical) ? Tactical->GetSelectedSoldierCount() : 0;
	UpdateSelectionState(SelectedCount);
	UpdateSelectedStructureState(IsValid(Building) ? Building->GetSelectedStructure() : nullptr);

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
	if (IsValid(ToggleGateButton))
	{
		ToggleGateButton->OnClicked.RemoveDynamic(this, &UIFTacticalHUDWidget::HandleToggleGateClicked);
	}
}

void UIFTacticalHUDWidget::HandleMoveStructureClicked()
{
	if (UIFBuildingComponent* const Building = BoundBuildingComponent.Get())
	{
		Building->BeginRelocateSelectedStructure();
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UIFTacticalHUDWidget::HandleDemolishStructureClicked()
{
	if (UIFBuildingComponent* const Building = BoundBuildingComponent.Get())
	{
		Building->DemolishSelectedStructure();
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UIFTacticalHUDWidget::UpdateSelectedStructureState(AIFStructure* SelectedStructure)
{
	const UIFTacticalCommandComponent* const Tactical = BoundTacticalComponent.Get();
	const UIFBuildingComponent* const Building = BoundBuildingComponent.Get();
	const bool bIsPlacementActive = IsValid(Building) && Building->IsInBuildPlacementMode();
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

		if (AIFGateDoor* const GateDoor = ResolveSelectedGateDoor(SelectedStructure))
		{
			EnsureToggleGateButtonCreated();
			if (IsValid(ToggleGateButton))
			{
				ToggleGateButton->SetVisibility(ESlateVisibility::Visible);
				RefreshToggleGateButtonLabel(GateDoor);
			}
		}
		else if (IsValid(ToggleGateButton))
		{
			ToggleGateButton->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	else if (IsValid(ToggleGateButton))
	{
		ToggleGateButton->SetVisibility(ESlateVisibility::Collapsed);
	}

	const int32 SelectedCount = IsValid(Tactical) ? Tactical->GetSelectedSoldierCount() : 0;
	UpdateControlsHintText(SelectedCount, bIsPlacementActive);
}

void UIFTacticalHUDWidget::EnsureToggleGateButtonCreated()
{
	if (IsValid(ToggleGateButton) || !IsValid(MoveStructureButton))
	{
		return;
	}

	UPanelWidget* const ParentPanel = MoveStructureButton->GetParent();
	if (!IsValid(ParentPanel))
	{
		return;
	}

	if (!bHasCachedBuildStyle)
	{
		if (IsValid(BuildWallButton))
		{
			CachedBuildButtonStyle = BuildWallButton->GetStyle();
		}
		if (IsValid(LineFormationLabel))
		{
			CachedBuildLabelFont = LineFormationLabel->GetFont();
		}
		bHasCachedBuildStyle = true;
	}

	ToggleGateButton = NewObject<UButton>(this);
	ToggleGateButton->SetStyle(MoveStructureButton->GetStyle());
	ToggleGateButton->SetClickMethod(EButtonClickMethod::DownAndUp);
	ToggleGateButton->OnClicked.AddDynamic(this, &UIFTacticalHUDWidget::HandleToggleGateClicked);

	ToggleGateButtonLabel = NewObject<UTextBlock>(this);
	FSlateFontInfo LabelFont = CachedBuildLabelFont;
	LabelFont.Size = 11;
	ToggleGateButtonLabel->SetFont(LabelFont);
	ToggleGateButtonLabel->SetJustification(ETextJustify::Center);
	ToggleGateButtonLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.88f, 0.62f, 1.0f)));
	ToggleGateButton->AddChild(ToggleGateButtonLabel);

	if (UHorizontalBox* const HBox = Cast<UHorizontalBox>(ParentPanel))
	{
		if (UHorizontalBoxSlot* const NewSlot = HBox->AddChildToHorizontalBox(ToggleGateButton))
		{
			NewSlot->SetVerticalAlignment(VAlign_Center);
			NewSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		}
	}
	else
	{
		ParentPanel->AddChild(ToggleGateButton);
	}
}

void UIFTacticalHUDWidget::RefreshToggleGateButtonLabel(const AIFGateDoor* GateDoor)
{
	if (!IsValid(ToggleGateButtonLabel) || !IsValid(GateDoor))
	{
		return;
	}

	ToggleGateButtonLabel->SetText(GateDoor->IsGateOpen()
		? NSLOCTEXT("IronFieldTactical", "CloseGateAction", "CLOSE GATE")
		: NSLOCTEXT("IronFieldTactical", "OpenGateAction", "OPEN GATE"));
}

AIFGateDoor* UIFTacticalHUDWidget::ResolveSelectedGateDoor(AIFStructure* SelectedStructure) const
{
	if (!IsValid(SelectedStructure) || !SelectedStructure->IsStructureAlive())
	{
		return nullptr;
	}

	if (AIFGateDoor* const DirectDoor = Cast<AIFGateDoor>(SelectedStructure))
	{
		return DirectDoor;
	}

	const UIFStructureDataAsset* const StructDA = SelectedStructure->GetStructureData();
	if (IsValid(StructDA) && StructDA->bSupportsGateDoor)
	{
		if (const UWorld* const World = GetWorld())
		{
			for (TActorIterator<AIFGateDoor> It(World); It; ++It)
			{
				AIFGateDoor* const Door = *It;
				if (!IsValid(Door) || !Door->IsStructureAlive())
				{
					continue;
				}
				if (Door->GetHostGate() == SelectedStructure
					|| FVector::DistSquared2D(Door->GetActorLocation(), SelectedStructure->GetActorLocation()) < FMath::Square(60.f))
				{
					return Door;
				}
			}
		}
	}

	return nullptr;
}

void UIFTacticalHUDWidget::HandleToggleGateClicked()
{
	if (const UIFBuildingComponent* const Building = BoundBuildingComponent.Get())
	{
		if (AIFGateDoor* const GateDoor = ResolveSelectedGateDoor(Building->GetSelectedStructure()))
		{
			GateDoor->ToggleGateOpen();
			RefreshToggleGateButtonLabel(GateDoor);
		}
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UIFTacticalHUDWidget::EnsureInteractionPromptCreated()
{
	if (IsValid(InteractionPromptBorder))
	{
		return;
	}

	UCanvasPanel* const RootCanvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!IsValid(RootCanvas))
	{
		return;
	}

	if (!bHasCachedBuildStyle && IsValid(LineFormationLabel))
	{
		CachedBuildLabelFont = LineFormationLabel->GetFont();
	}

	InteractionPromptBorder = NewObject<UBorder>(this);
	InteractionPromptBorder->SetBrushColor(FLinearColor(0.05f, 0.06f, 0.08f, 0.88f));
	InteractionPromptBorder->SetPadding(FMargin(18.f, 8.f));
	InteractionPromptBorder->SetVisibility(ESlateVisibility::Collapsed);

	InteractionPromptText = NewObject<UTextBlock>(this);
	FSlateFontInfo PromptFont = CachedBuildLabelFont;
	PromptFont.Size = 14;
	InteractionPromptText->SetFont(PromptFont);
	InteractionPromptText->SetJustification(ETextJustify::Center);
	InteractionPromptText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.90f, 0.56f, 1.0f)));
	InteractionPromptBorder->AddChild(InteractionPromptText);

	if (UCanvasPanelSlot* const CanvasSlot = RootCanvas->AddChildToCanvas(InteractionPromptBorder))
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		CanvasSlot->SetPosition(FVector2D(0.f, -110.f));
		CanvasSlot->SetAutoSize(true);
	}
}

void UIFTacticalHUDWidget::SetInteractionPrompt(const FText& PromptText)
{
	ActiveInteractionPromptText = PromptText;
	if (!PromptText.IsEmpty())
	{
		EnsureInteractionPromptCreated();
		if (IsValid(InteractionPromptText))
		{
			InteractionPromptText->SetText(PromptText);
		}
	}

	RefreshTacticalAndPromptVisibility(bIsTacticalOverlayActive);
}

void UIFTacticalHUDWidget::RefreshTacticalAndPromptVisibility(bool bTacticalActive)
{
	bIsTacticalOverlayActive = bTacticalActive;

	const bool bShowInteractionPrompt = !bTacticalActive && !ActiveInteractionPromptText.IsEmpty();
	if (IsValid(InteractionPromptBorder))
	{
		InteractionPromptBorder->SetVisibility(bShowInteractionPrompt ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (!bTacticalActive)
	{
		auto CollapseIfValid = [](UWidget* Widget)
		{
			if (IsValid(Widget))
			{
				Widget->SetVisibility(ESlateVisibility::Collapsed);
			}
		};

		CollapseIfValid(TopModeBorder);
		CollapseIfValid(BuildBarContainer);
		CollapseIfValid(HintBannerBorder);
		CollapseIfValid(FormationBarContainer);
		CollapseIfValid(SelectionBadgeBorder);
		CollapseIfValid(StructureActionBarContainer);
		CollapseIfValid(BuildPlacementStatusBorder);
		CollapseIfValid(OrderToastBorder);
	}
	else
	{
		if (IsValid(TopModeBorder))
		{
			TopModeBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		if (IsValid(BuildBarContainer))
		{
			BuildBarContainer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		if (IsValid(HintBannerBorder))
		{
			HintBannerBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}

	SetVisibility((bTacticalActive || bShowInteractionPrompt) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void UIFTacticalHUDWidget::HandleBuildPlacementValidityChanged(bool bIsValid, EIFPlacementInvalidReason InvalidReason)
{
	if (!IsValid(BuildPlacementStatusBorder))
	{
		return;
	}

	const UIFBuildingComponent* const Building = BoundBuildingComponent.Get();
	if (!IsValid(Building) || !Building->IsInBuildPlacementMode())
	{
		EndStatusFlash();
		BuildPlacementStatusBorder->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	if (IsValid(BuildPlacementStatusText))
	{
		BuildPlacementStatusText->SetText(Building->GetPlacementStatusText());
		BuildPlacementStatusText->SetColorAndOpacity(FSlateColor(
			bIsValid ? FLinearColor(0.68f, 1.0f, 0.74f, 1.0f) : FLinearColor(1.0f, 0.84f, 0.78f, 1.0f)));
	}

	if (UBorder* const StatusBorder = Cast<UBorder>(BuildPlacementStatusBorder.Get()))
	{
		StatusBorder->SetBrushColor(bIsValid
			? FLinearColor(0.08f, 0.30f, 0.14f, 0.92f)
			: FLinearColor(0.42f, 0.10f, 0.08f, 0.94f));
	}

	BuildPlacementStatusBorder->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UIFTacticalHUDWidget::HandleBuildPlacementRejected(EIFPlacementInvalidReason)
{
	if (!IsValid(BuildPlacementStatusBorder))
	{
		return;
	}

	if (UBorder* const StatusBorder = Cast<UBorder>(BuildPlacementStatusBorder.Get()))
	{
		StatusBorder->SetBrushColor(FLinearColor(0.85f, 0.14f, 0.10f, 1.0f));
	}
	BuildPlacementStatusBorder->SetRenderScale(FVector2D(1.08f, 1.08f));
	BuildPlacementStatusBorder->SetVisibility(ESlateVisibility::HitTestInvisible);

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StatusFlashTimerHandle);
		const float EffectiveDilation = FMath::Max(UGameplayStatics::GetGlobalTimeDilation(this), 0.1f);
		const float FlashDelay = FMath::Max(0.04f, 0.18f * EffectiveDilation);
		World->GetTimerManager().SetTimer(
			StatusFlashTimerHandle,
			this,
			&UIFTacticalHUDWidget::EndStatusFlash,
			FlashDelay,
			false);
	}
}

void UIFTacticalHUDWidget::EndStatusFlash()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StatusFlashTimerHandle);
	}

	if (IsValid(BuildPlacementStatusBorder))
	{
		BuildPlacementStatusBorder->SetRenderScale(FVector2D(1.f, 1.f));
		if (const UIFBuildingComponent* const Building = BoundBuildingComponent.Get(); IsValid(Building) && Building->IsInBuildPlacementMode())
		{
			if (UBorder* const StatusBorder = Cast<UBorder>(BuildPlacementStatusBorder.Get()))
			{
				StatusBorder->SetBrushColor(Building->IsCurrentPlacementValid()
					? FLinearColor(0.08f, 0.30f, 0.14f, 0.92f)
					: FLinearColor(0.42f, 0.10f, 0.08f, 0.94f));
			}
		}
	}
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
	if (UIFBuildingComponent* const Building = BoundBuildingComponent.Get())
	{
		Building->BeginBuildPlacementByType(StructureType);
	}

	// Always return focus to game viewport so clicking build buttons never steals WASD/R/Tab keys.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UIFTacticalHUDWidget::RefreshBuildBar()
{
	UHorizontalBox* const BuildBox = Cast<UHorizontalBox>(BuildBarContainer.Get());
	UIFBuildingComponent* const Building = BoundBuildingComponent.Get();
	if (!IsValid(BuildBox) || !IsValid(Building))
	{
		return;
	}

	if (!bHasCachedBuildStyle)
	{
		if (IsValid(BuildWallButton))
		{
			CachedBuildButtonStyle = BuildWallButton->GetStyle();
		}
		if (IsValid(LineFormationLabel))
		{
			CachedBuildLabelFont = LineFormationLabel->GetFont();
		}
		bHasCachedBuildStyle = true;
	}

	// Always clear legacy static buttons (KEEP/TOWER/WALL) so they never appear in PIE.
	BuildBox->ClearChildren();
	DynamicBuildProxies.Reset();
	DynamicBuildBorders.Reset();
	DynamicBuildLabels.Reset();
	DynamicBuildRoleLabels.Reset();
	DynamicBuildDataAssets.Reset();

	Building->EnsureBuildableStructuresLoaded();
	const TArray<TObjectPtr<UIFStructureDataAsset>>& Buildables = Building->GetLoadedBuildableStructures();
	if (Buildables.IsEmpty())
	{
		return;
	}

	FButtonStyle CardButtonStyle = CachedBuildButtonStyle;
	CardButtonStyle.NormalPadding = FMargin(10.f, 5.f);
	CardButtonStyle.PressedPadding = FMargin(10.f, 5.f);
	CardButtonStyle.Hovered.TintColor = FSlateColor(FLinearColor(0.24f, 0.22f, 0.16f, 0.96f));
	CardButtonStyle.Pressed.TintColor = FSlateColor(FLinearColor(0.36f, 0.28f, 0.12f, 1.0f));

	FSlateFontInfo TitleFont = CachedBuildLabelFont;
	TitleFont.Size = 12;

	FSlateFontInfo RoleFont = CachedBuildLabelFont;
	RoleFont.Size = 8;

	for (int32 i = 0; i < Buildables.Num(); ++i)
	{
		UIFStructureDataAsset* const Data = Buildables[i].Get();
		if (!IsValid(Data))
		{
			continue;
		}

		if (DynamicBuildBorders.Num() > 0)
		{
			USpacer* const Spacer = NewObject<USpacer>(this);
			Spacer->SetSize(FVector2D(6.f, 1.f));
			BuildBox->AddChildToHorizontalBox(Spacer);
		}

		UBorder* const Border = NewObject<UBorder>(this);
		Border->SetPadding(FMargin(2.f));
		Border->SetBrushColor(InactiveFormationBrushColor);

		UButton* const Button = NewObject<UButton>(this);
		Button->SetStyle(CardButtonStyle);
		Button->SetIsEnabled(true);
		Button->SetClickMethod(EButtonClickMethod::DownAndUp);

		UVerticalBox* const CardVBox = NewObject<UVerticalBox>(this);

		UTextBlock* const Label = NewObject<UTextBlock>(this);
		Label->SetFont(TitleFont);
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(InactiveFormationTextColor));
		Label->SetText(Data->DisplayName);
		if (UVerticalBoxSlot* const TitleSlot = CardVBox->AddChildToVerticalBox(Label))
		{
			TitleSlot->SetHorizontalAlignment(HAlign_Center);
		}

		UTextBlock* const RoleLabel = NewObject<UTextBlock>(this);
		RoleLabel->SetFont(RoleFont);
		RoleLabel->SetJustification(ETextJustify::Center);
		RoleLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.64f, 0.90f)));
		RoleLabel->SetText(Data->RoleTag);
		RoleLabel->SetVisibility(Data->RoleTag.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		if (UVerticalBoxSlot* const RoleSlot = CardVBox->AddChildToVerticalBox(RoleLabel))
		{
			RoleSlot->SetHorizontalAlignment(HAlign_Center);
			RoleSlot->SetPadding(FMargin(0.f, 1.f, 0.f, 0.f));
		}

		Button->AddChild(CardVBox);
		Border->AddChild(Button);

		if (UHorizontalBoxSlot* const HSlot = BuildBox->AddChildToHorizontalBox(Border))
		{
			HSlot->SetVerticalAlignment(VAlign_Center);
		}

		UIFBuildSlotButtonProxy* const Proxy = NewObject<UIFBuildSlotButtonProxy>(this);
		Proxy->SlotIndex = DynamicBuildDataAssets.Num();
		Proxy->OwnerWidget = this;
		Button->OnClicked.AddDynamic(Proxy, &UIFBuildSlotButtonProxy::HandleClicked);

		DynamicBuildProxies.Add(Proxy);
		DynamicBuildBorders.Add(Border);
		DynamicBuildLabels.Add(Label);
		DynamicBuildRoleLabels.Add(RoleLabel);
		DynamicBuildDataAssets.Add(Data);
	}

	UpdateBuildHighlight(Building->IsInBuildPlacementMode() && !Building->IsRelocatingStructure(), Building->GetActiveBuildData());
}

void UIFTacticalHUDWidget::HandleDynamicBuildSlotClicked(int32 SlotIndex)
{
	if (UIFBuildingComponent* const Building = BoundBuildingComponent.Get())
	{
		if (DynamicBuildDataAssets.IsValidIndex(SlotIndex))
		{
			if (UIFStructureDataAsset* const Data = DynamicBuildDataAssets[SlotIndex].Get())
			{
				if (Building->IsInBuildPlacementMode() && !Building->IsRelocatingStructure() && Building->GetActiveBuildData() == Data)
				{
					Building->CancelBuildPlacement();
				}
				else
				{
					Building->BeginBuildPlacement(Data);
				}
			}
		}
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UIFTacticalHUDWidget::UpdateBuildHighlight(bool bIsPlacementActive, const UIFStructureDataAsset* ActiveData)
{
	const bool bHasActive = bIsPlacementActive && IsValid(ActiveData);

	if (!DynamicBuildBorders.IsEmpty())
	{
		for (int32 i = 0; i < DynamicBuildBorders.Num(); ++i)
		{
			const bool bActive = bHasActive && DynamicBuildDataAssets.IsValidIndex(i) && (DynamicBuildDataAssets[i].Get() == ActiveData);
			if (UBorder* const Highlight = DynamicBuildBorders[i].Get())
			{
				Highlight->SetBrushColor(bActive ? FLinearColor(0.82f, 0.64f, 0.18f, 0.98f) : InactiveFormationBrushColor);
			}
			if (UTextBlock* const Label = DynamicBuildLabels.IsValidIndex(i) ? DynamicBuildLabels[i].Get() : nullptr)
			{
				Label->SetColorAndOpacity(FSlateColor(bActive ? ActiveFormationTextColor : InactiveFormationTextColor));
			}
			if (UTextBlock* const RoleLabel = DynamicBuildRoleLabels.IsValidIndex(i) ? DynamicBuildRoleLabels[i].Get() : nullptr)
			{
				RoleLabel->SetColorAndOpacity(FSlateColor(bActive
					? FLinearColor(0.65f, 0.88f, 1.0f, 1.0f)
					: FLinearColor(0.55f, 0.58f, 0.64f, 0.90f)));
			}
		}
		return;
	}

	const EIFStructureType ActiveType = bHasActive ? ActiveData->StructureType : EIFStructureType::Keep;

	auto ApplyBorder = [this](UBorder* Highlight, bool bActive)
	{
		if (IsValid(Highlight))
		{
			Highlight->SetBrushColor(bActive ? ActiveFormationBrushColor : InactiveFormationBrushColor);
		}
	};

	ApplyBorder(BuildKeepHighlight, bHasActive && ActiveType == EIFStructureType::Keep);
	ApplyBorder(BuildGuardTowerHighlight, bHasActive && ActiveType == EIFStructureType::GuardTower);
	ApplyBorder(BuildWallHighlight, bHasActive && ActiveType == EIFStructureType::WallSegment);
}
