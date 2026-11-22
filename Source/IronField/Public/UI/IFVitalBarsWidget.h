#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "IFVitalBarsWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UIFHealthComponent;
class UIFStaminaComponent;

/**
 * Reusable event-driven vital bars widget for both the player HUD and soldier overhead bars.
 * Subscribes directly to health/stamina change delegates with zero tick or property bindings.
 */
UCLASS(Abstract)
class IRONFIELD_API UIFVitalBarsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "IronField|UI")
	void BindToVitalComponents(UIFHealthComponent* InHealthComp, UIFStaminaComponent* InStaminaComp);

	UFUNCTION(BlueprintCallable, Category = "IronField|UI")
	void SetHealthBarColor(const FLinearColor& Color);

	UFUNCTION(BlueprintCallable, Category = "IronField|UI")
	void UnbindVitalComponents();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> StaminaBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StaminaText;

private:
	UFUNCTION()
	void HandleHealthChanged(float Percent);

	UFUNCTION()
	void HandleStaminaChanged(float Percent);

	void UnsubscribeVitalDelegates();
	void UpdateHealthDisplay(float Percent);
	void UpdateStaminaDisplay(float Percent);

	TWeakObjectPtr<UIFHealthComponent> BoundHealthComponent;
	TWeakObjectPtr<UIFStaminaComponent> BoundStaminaComponent;
};
