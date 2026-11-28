#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Castle/IFBuildingTypes.h"
#include "Castle/IFCastleTypes.h"
#include "Tactical/IFTacticalTypes.h"
#include "IFTacticalHUDWidget.generated.h"

class AIFGateDoor;
class AIFStructure;
class UBorder;
class UButton;
class UIFBuildingComponent;
class UIFStructureDataAsset;
class UIFTacticalCommandComponent;
class UIFTacticalHUDWidget;
class UTextBlock;
class UWidget;

UCLASS()
class IRONFIELD_API UIFBuildSlotButtonProxy : public UObject
{
	GENERATED_BODY()

public:
	int32 SlotIndex = INDEX_NONE;
	TWeakObjectPtr<UIFTacticalHUDWidget> OwnerWidget;

	UFUNCTION()
	void HandleClicked();
};

/**
 * Contextual, event-driven Tactical Mode overlay (zero tick, zero Blueprint logic).
 * Supports progressive disclosure for squad badge, transient order confirmation toast,
 * adaptive control hints, and non-focusable clickable formation selector buttons.
 */
UCLASS(Abstract)
class IRONFIELD_API UIFTacticalHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|UI")
	void BindToTacticalComponent(UIFTacticalCommandComponent* InTacticalComponent);

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|UI")
	void UnbindFromTacticalComponent();

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|UI")
	void BindToBuildingComponent(UIFBuildingComponent* InBuildingComponent);

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|UI")
	void UnbindFromBuildingComponent();

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|UI")
	void RefreshBuildBar();

	UFUNCTION(BlueprintCallable, Category = "IronField|Tactical|UI")
	void SetInteractionPrompt(const FText& PromptText);

	void RefreshTacticalAndPromptVisibility(bool bTacticalActive);

	void HandleDynamicBuildSlotClicked(int32 SlotIndex);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> TopModeBorder;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ModeStatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SelectionBadgeBorder;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SelectedSquadText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> OrderToastBorder;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LastOrderText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> HintBannerBorder;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ControlsHintText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> FormationBarContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> LineFormationButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> SquareFormationButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CircleFormationButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> LineFormationHighlight;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SquareFormationHighlight;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> CircleFormationHighlight;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LineFormationLabel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SquareFormationLabel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CircleFormationLabel;

	// --- Part 09 Contextual Build Bar (Keep, GuardTower, WallSegment) -------
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BuildBarContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BuildKeepButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BuildGuardTowerButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BuildWallButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BuildKeepHighlight;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BuildGuardTowerHighlight;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BuildWallHighlight;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BuildPlacementStatusBorder;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BuildPlacementStatusText;

	// --- Part 09 Contextual Selected Structure Action Bar ------------------
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> StructureActionBarContainer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SelectedStructureNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> MoveStructureButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> DemolishStructureButton;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|UI", meta = (ClampMin = "0.25"))
	float OrderToastDurationSeconds = 1.75f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|UI")
	FLinearColor ActiveFormationBrushColor = FLinearColor(0.32f, 0.24f, 0.08f, 0.95f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|UI")
	FLinearColor InactiveFormationBrushColor = FLinearColor(0.05f, 0.05f, 0.06f, 0.84f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|UI")
	FLinearColor ActiveFormationTextColor = FLinearColor(1.0f, 0.88f, 0.45f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|UI")
	FLinearColor InactiveFormationTextColor = FLinearColor(0.76f, 0.75f, 0.72f, 1.0f);

private:
	UFUNCTION()
	void HandleTacticalModeChanged(bool bIsTacticalModeActive);

	UFUNCTION()
	void HandleSelectionChanged(int32 SelectedUnitCount);

	UFUNCTION()
	void HandleOrderIssued(EIFTacticalOrderType OrderType, FVector TargetLocation);

	UFUNCTION()
	void HandleFormationTypeChanged(EIFTacticalFormationType NewFormationType);

	UFUNCTION()
	void HandleBuildPlacementModeChanged(bool bIsPlacementActive, UIFStructureDataAsset* ActiveStructureData);

	UFUNCTION()
	void HandleBuildPlacementValidityChanged(bool bIsValid, EIFPlacementInvalidReason InvalidReason);

	UFUNCTION()
	void HandleBuildPlacementRejected(EIFPlacementInvalidReason InvalidReason);

	UFUNCTION()
	void HandleSelectedStructureChanged(AIFStructure* SelectedStructure);

	UFUNCTION()
	void HandleLineFormationClicked();

	UFUNCTION()
	void HandleSquareFormationClicked();

	UFUNCTION()
	void HandleCircleFormationClicked();

	UFUNCTION()
	void HandleBuildKeepClicked();

	UFUNCTION()
	void HandleBuildGuardTowerClicked();

	UFUNCTION()
	void HandleBuildWallClicked();

	UFUNCTION()
	void HandleMoveStructureClicked();

	UFUNCTION()
	void HandleDemolishStructureClicked();

	UFUNCTION()
	void HandleToggleGateClicked();

	void ConfigureFormationButtons();
	void UnbindFormationButtons();
	void ConfigureBuildButtons();
	void UnbindBuildButtons();
	void ConfigureStructureActionButtons();
	void UnbindStructureActionButtons();
	void SelectFormationFromButton(EIFTacticalFormationType FormationType);
	void SelectBuildStructureFromButton(EIFStructureType StructureType);
	void UpdateSelectionState(int32 SelectedUnitCount);
	void UpdateSelectedStructureState(AIFStructure* SelectedStructure);
	void UpdateFormationHighlight(EIFTacticalFormationType ActiveType);
	void UpdateBuildHighlight(bool bIsPlacementActive, const UIFStructureDataAsset* ActiveData);
	void UpdateControlsHintText(int32 SelectedUnitCount, bool bIsPlacementActive);
	void ShowOrderToast(EIFTacticalOrderType OrderType);
	void HideOrderToast();
	void EndStatusFlash();

	TWeakObjectPtr<UIFTacticalCommandComponent> BoundTacticalComponent;
	TWeakObjectPtr<UIFBuildingComponent> BoundBuildingComponent;
	FTimerHandle OrderToastTimerHandle;
	FTimerHandle StatusFlashTimerHandle;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UIFBuildSlotButtonProxy>> DynamicBuildProxies;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> DynamicBuildBorders;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> DynamicBuildLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> DynamicBuildRoleLabels;

	TArray<TWeakObjectPtr<UIFStructureDataAsset>> DynamicBuildDataAssets;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> InteractionPromptBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> InteractionPromptText;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ToggleGateButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ToggleGateButtonLabel;

	FText ActiveInteractionPromptText;
	bool bIsTacticalOverlayActive = false;

	void EnsureInteractionPromptCreated();
	void EnsureToggleGateButtonCreated();
	void RefreshToggleGateButtonLabel(const AIFGateDoor* GateDoor);
	AIFGateDoor* ResolveSelectedGateDoor(AIFStructure* SelectedStructure) const;

	bool bHasCachedBuildStyle = false;
	FButtonStyle CachedBuildButtonStyle;
	FSlateFontInfo CachedBuildLabelFont;
};
