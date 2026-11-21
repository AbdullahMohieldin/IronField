#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Tactical/IFTacticalTypes.h"
#include "IFTacticalHUDWidget.generated.h"

class UBorder;
class UButton;
class UIFTacticalCommandComponent;
class UTextBlock;
class UWidget;

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
	void HandleLineFormationClicked();

	UFUNCTION()
	void HandleSquareFormationClicked();

	UFUNCTION()
	void HandleCircleFormationClicked();

	void ConfigureFormationButtons();
	void UnbindFormationButtons();
	void SelectFormationFromButton(EIFTacticalFormationType FormationType);
	void UpdateSelectionState(int32 SelectedUnitCount);
	void UpdateFormationHighlight(EIFTacticalFormationType ActiveType);
	void ShowOrderToast(EIFTacticalOrderType OrderType);
	void HideOrderToast();

	TWeakObjectPtr<UIFTacticalCommandComponent> BoundTacticalComponent;
	FTimerHandle OrderToastTimerHandle;
};
