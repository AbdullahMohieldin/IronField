#include "UI/IFVitalBarsWidget.h"

#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Stats/IFHealthComponent.h"
#include "Stats/IFStaminaComponent.h"

void UIFVitalBarsWidget::BindToVitalComponents(UIFHealthComponent* InHealthComp, UIFStaminaComponent* InStaminaComp)
{
	UnbindVitalComponents();

	if (IsValid(InHealthComp))
	{
		BoundHealthComponent = InHealthComp;
		InHealthComp->OnHealthChanged.AddUniqueDynamic(this, &UIFVitalBarsWidget::HandleHealthChanged);
		UpdateHealthDisplay(InHealthComp->GetHealthPercent());
	}

	if (IsValid(InStaminaComp))
	{
		BoundStaminaComponent = InStaminaComp;
		InStaminaComp->OnStaminaChanged.AddUniqueDynamic(this, &UIFVitalBarsWidget::HandleStaminaChanged);
		if (IsValid(StaminaBar))
		{
			StaminaBar->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		if (IsValid(StaminaText))
		{
			StaminaText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		UpdateStaminaDisplay(InStaminaComp->GetStaminaPercent());
	}
	else
	{
		if (IsValid(StaminaBar))
		{
			StaminaBar->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (IsValid(StaminaText))
		{
			StaminaText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UIFVitalBarsWidget::SetHealthBarColor(const FLinearColor& Color)
{
	if (IsValid(HealthBar))
	{
		HealthBar->SetFillColorAndOpacity(Color);
	}
}

void UIFVitalBarsWidget::UnbindVitalComponents()
{
	UnsubscribeVitalDelegates();
	BoundHealthComponent.Reset();
	BoundStaminaComponent.Reset();
}

void UIFVitalBarsWidget::UnsubscribeVitalDelegates()
{
	if (UIFHealthComponent* const Health = BoundHealthComponent.Get())
	{
		Health->OnHealthChanged.RemoveDynamic(this, &UIFVitalBarsWidget::HandleHealthChanged);
	}

	if (UIFStaminaComponent* const Stamina = BoundStaminaComponent.Get())
	{
		Stamina->OnStaminaChanged.RemoveDynamic(this, &UIFVitalBarsWidget::HandleStaminaChanged);
	}
}

void UIFVitalBarsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Re-subscribe cleanly if a Screen-space UWidgetComponent ever removes and re-adds the widget.
	if (UIFHealthComponent* const Health = BoundHealthComponent.Get())
	{
		Health->OnHealthChanged.AddUniqueDynamic(this, &UIFVitalBarsWidget::HandleHealthChanged);
		UpdateHealthDisplay(Health->GetHealthPercent());
	}

	if (UIFStaminaComponent* const Stamina = BoundStaminaComponent.Get())
	{
		Stamina->OnStaminaChanged.AddUniqueDynamic(this, &UIFVitalBarsWidget::HandleStaminaChanged);
		if (IsValid(StaminaBar))
		{
			StaminaBar->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		if (IsValid(StaminaText))
		{
			StaminaText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		UpdateStaminaDisplay(Stamina->GetStaminaPercent());
	}
	else if (BoundHealthComponent.IsValid())
	{
		if (IsValid(StaminaBar))
		{
			StaminaBar->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (IsValid(StaminaText))
		{
			StaminaText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UIFVitalBarsWidget::NativeDestruct()
{
	// Keep weak component references intact so NativeConstruct can re-bind if re-added to viewport.
	UnsubscribeVitalDelegates();
	Super::NativeDestruct();
}

void UIFVitalBarsWidget::HandleHealthChanged(float Percent)
{
	UpdateHealthDisplay(Percent);
}

void UIFVitalBarsWidget::HandleStaminaChanged(float Percent)
{
	UpdateStaminaDisplay(Percent);
}

void UIFVitalBarsWidget::UpdateHealthDisplay(float Percent)
{
	if (IsValid(HealthBar))
	{
		HealthBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
	}

	if (IsValid(HealthText))
	{
		if (const UIFHealthComponent* const Health = BoundHealthComponent.Get())
		{
			HealthText->SetText(FText::FromString(FString::Printf(
				TEXT("%d / %d"),
				FMath::RoundToInt(Health->GetCurrentHealth()),
				FMath::RoundToInt(Health->GetMaxHealth()))));
		}
	}
}

void UIFVitalBarsWidget::UpdateStaminaDisplay(float Percent)
{
	if (IsValid(StaminaBar))
	{
		StaminaBar->SetPercent(FMath::Clamp(Percent, 0.f, 1.f));
	}

	if (IsValid(StaminaText))
	{
		if (const UIFStaminaComponent* const Stamina = BoundStaminaComponent.Get())
		{
			StaminaText->SetText(FText::FromString(FString::Printf(
				TEXT("%d / %d"),
				FMath::RoundToInt(Stamina->GetCurrentStamina()),
				FMath::RoundToInt(Stamina->GetMaxStamina()))));
		}
	}
}
