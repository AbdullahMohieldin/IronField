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

void UIFHealthComponent::HandleOwnerTakeAnyDamage(AActor*, float Damage, const UDamageType*, AController* InstigatedBy, AActor* DamageCauser)
{
	if (bIsDead || bIsInvincible || Damage <= 0.f)
	{
		return;
	}

	ApplyDamage(Damage);

	AActor* const InstigatorActor = InstigatedBy ? InstigatedBy->GetPawn() : DamageCauser;
	OnDamaged.Broadcast(InstigatorActor, Damage);
}

void UIFHealthComponent::SetHealthClamped(float NewHealth)
{
	if (IFStatUtils::ApplyClampedValue(CurrentHealth, MaxHealth, NewHealth))
	{
		OnHealthChanged.Broadcast(GetHealthPercent());
	}
}
