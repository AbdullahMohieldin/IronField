#include "Stats/IFHealthComponent.h"

#include "Core/IFLog.h"
#include "GameFramework/Actor.h"

namespace
{
	// Minimum valid floor for MaxHealth and ReviveHealth.
	constexpr float MinHealthValue = 1.f;
}

void UIFHealthComponent::ApplyDamage(float Amount)
{
	if (bIsDead || bIsInvincible || Amount <= 0.f)
	{
		// Narrow post-revive diagnosis: invincible blocks are unexpected outside the get-up window.
		if (bIsInvincible && !bIsDead && Amount > 0.f)
		{
			UE_LOG(LogIronField, Log, TEXT("[IF-Revive] %s blocked %.0f damage while invincible."),
				*GetNameSafe(GetOwner()), Amount);
		}
		return;
	}

	SetHealthClamped(CurrentHealth - Amount);

	if (CurrentHealth <= 0.f)
	{
		bIsDead = true;
		OnHealthDepleted.Broadcast();
	}
}

void UIFHealthComponent::ApplyHealing(float Amount)
{
	if (bIsDead || Amount <= 0.f)
	{
		return;
	}

	SetHealthClamped(CurrentHealth + Amount);
}

void UIFHealthComponent::Revive()
{
	if (!bIsDead)
	{
		return;
	}

	bIsDead = false;
	SetHealthClamped(FMath::Clamp(ReviveHealth, MinHealthValue, MaxHealth));
}

void UIFHealthComponent::SetMaxHealth(float NewMaxHealth, bool bFillHealthToMax)
{
	MaxHealth = FMath::Max(MinHealthValue, NewMaxHealth);
	ReviveHealth = FMath::Min(ReviveHealth, MaxHealth);

	if (bFillHealthToMax || CurrentHealth > MaxHealth)
	{
		SetHealthClamped(bFillHealthToMax ? MaxHealth : CurrentHealth);
	}
}

void UIFHealthComponent::SetReviveHealth(float NewReviveHealth)
{
	ReviveHealth = FMath::Clamp(NewReviveHealth, MinHealthValue, MaxHealth);
}

void UIFHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	MaxHealth = FMath::Max(MinHealthValue, MaxHealth);
	CurrentHealth = MaxHealth;
	bIsDead = false;

	if (AActor* const Owner = GetOwner())
	{
		Owner->OnTakeAnyDamage.AddDynamic(this, &UIFHealthComponent::HandleOwnerTakeAnyDamage);
	}

	OnHealthChanged.Broadcast(GetHealthPercent());
}

void UIFHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* const Owner = GetOwner())
	{
		Owner->OnTakeAnyDamage.RemoveAll(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UIFHealthComponent::HandleOwnerTakeAnyDamage(AActor*, float Damage, const UDamageType*, AController*, AActor*)
{
	ApplyDamage(Damage);
}

void UIFHealthComponent::SetHealthClamped(float NewHealth)
{
	if (ApplyClampedValue(CurrentHealth, MaxHealth, NewHealth))
	{
		OnHealthChanged.Broadcast(GetHealthPercent());
	}
}
