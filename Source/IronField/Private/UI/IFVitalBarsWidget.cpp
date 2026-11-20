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
		UpdateStaminaDisplay(InStaminaComp->GetStaminaPercent());
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
	if (UIFHealthComponent* const Health = BoundHealthComponent.Get())
	{
		Health->OnHealthChanged.RemoveDynamic(this, &UIFVitalBarsWidget::HandleHealthChanged);
	}
	BoundHealthComponent.Reset();

	if (UIFStaminaComponent* const Stamina = BoundStaminaComponent.Get())
	{
		Stamina->OnStaminaChanged.RemoveDynamic(this, &UIFVitalBarsWidget::HandleStaminaChanged);
	}
	BoundStaminaComponent.Reset();
}

void UIFVitalBarsWidget::NativeDestruct()
{
	UnbindVitalComponents();
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
