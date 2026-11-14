#include "Stats/IFHealthComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

UIFHealthComponent::UIFHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIFHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bHealthInitialized)
	{
		InitializeHealth(MaxHealth);
	}

	if (AActor* const Owner = GetOwner())
	{
		if (IsValid(Owner))
		{
			Owner->OnTakeAnyDamage.AddUniqueDynamic(this, &UIFHealthComponent::HandleOwnerTakeAnyDamage);
		}
	}
}

void UIFHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* const Owner = GetOwner())
	{
		if (IsValid(Owner))
		{
			Owner->OnTakeAnyDamage.RemoveAll(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UIFHealthComponent::InitializeHealth(float NewMaxHealth)
{
	MaxHealth = FMath::Max(1.f, NewMaxHealth);
	ReviveHealth = FMath::Clamp(ReviveHealth, 1.f, MaxHealth);
	CurrentHealth = MaxHealth;
	bIsDead = false;
	bHealthInitialized = true;
	OnHealthChanged.Broadcast(GetHealthPercent());
}

float UIFHealthComponent::ApplyDamage(float Amount)
{
	return ApplyDamageFromInstigator(Amount, nullptr);
}

float UIFHealthComponent::ApplyDamageFromInstigator(float Amount, AActor* InstigatorActor)
{
	if (bIsDead || bIsInvincible || Amount <= 0.f)
	{
		return 0.f;
	}

	const float PreviousHealth = CurrentHealth;
	SetHealthClamped(CurrentHealth - Amount);
	const float ActualDamage = PreviousHealth - CurrentHealth;

	if (ActualDamage > 0.f)
	{
		OnDamaged.Broadcast(InstigatorActor, ActualDamage);
	}

	if (CurrentHealth <= 0.f)
	{
		bIsDead = true;
		OnDeath.Broadcast(GetOwner());
	}

	return ActualDamage;
}

void UIFHealthComponent::ApplyHealing(float Amount)
{
	if (bIsDead || Amount <= 0.f)
	{
		return;
	}

	SetHealthClamped(CurrentHealth + Amount);
}

void UIFHealthComponent::HealToFull()
{
	if (bIsDead)
	{
		return;
	}

	SetHealthClamped(MaxHealth);
}

void UIFHealthComponent::Revive()
{
	if (!bIsDead)
	{
		return;
	}

	bIsDead = false;
	SetHealthClamped(FMath::Clamp(ReviveHealth, 1.f, MaxHealth));
}

float UIFHealthComponent::GetHealthPercent() const
{
	return MaxHealth > 0.f ? CurrentHealth / MaxHealth : 0.f;
}

void UIFHealthComponent::HandleOwnerTakeAnyDamage(AActor*, float Damage, const UDamageType*, AController* InstigatedBy, AActor* DamageCauser)
{
	AActor* const InstigatorActor = IsValid(InstigatedBy) ? InstigatedBy->GetPawn() : DamageCauser;
	ApplyDamageFromInstigator(Damage, InstigatorActor);
}

void UIFHealthComponent::SetHealthClamped(float NewHealth)
{
	const float Clamped = FMath::Clamp(NewHealth, 0.f, MaxHealth);
	if (!FMath::IsNearlyEqual(CurrentHealth, Clamped))
	{
		CurrentHealth = Clamped;
		OnHealthChanged.Broadcast(GetHealthPercent());
	}
}