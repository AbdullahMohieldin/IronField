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

	if (AActor* const Owner = GetOwner(); IsValid(Owner))
	{
		Owner->OnTakeAnyDamage.AddUniqueDynamic(this, &UIFHealthComponent::HandleOwnerTakeAnyDamage);
	}
}

void UIFHealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* const Owner = GetOwner(); IsValid(Owner))
	{
		Owner->OnTakeAnyDamage.RemoveDynamic(this, &UIFHealthComponent::HandleOwnerTakeAnyDamage);
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

float UIFHealthComponent::ApplyDamage(float Amount, AActor* InstigatorActor)
{
	if (bIsDead || Amount <= 0.f)
	{
		return 0.f;
	}

	const float Clamped = FMath::Clamp(CurrentHealth - Amount, 0.f, MaxHealth);
	const float ActualDamage = CurrentHealth - Clamped;
	const bool bHealthChanged = !FMath::IsNearlyEqual(CurrentHealth, Clamped);
	CurrentHealth = Clamped;

	const bool bJustDied = (CurrentHealth <= 0.f);
	if (bJustDied)
	{
		bIsDead = true;
	}

	if (bHealthChanged)
	{
		OnHealthChanged.Broadcast(GetHealthPercent());
	}

	if (ActualDamage > 0.f)
	{
		OnDamaged.Broadcast(InstigatorActor, ActualDamage);
	}

	if (bJustDied)
	{
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
	AActor* InstigatorActor = IsValid(InstigatedBy) ? InstigatedBy->GetPawn() : nullptr;
	if (!IsValid(InstigatorActor))
	{
		InstigatorActor = DamageCauser;
	}
	ApplyDamage(Damage, InstigatorActor);
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