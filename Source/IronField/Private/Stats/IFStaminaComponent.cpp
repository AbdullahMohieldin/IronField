#include "Stats/IFStaminaComponent.h"

UIFStaminaComponent::UIFStaminaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UIFStaminaComponent::TryConsumeStamina(float Amount)
{
	if (Amount <= 0.f)
	{
		return true;
	}

	if (!HasStamina(Amount))
	{
		return false;
	}

	SetStaminaClamped(CurrentStamina - Amount);
	TimeSinceLastStaminaDrain = 0.f;

	if (StaminaRegenRate > 0.f)
	{
		SetComponentTickEnabled(true);
	}

	return true;
}

void UIFStaminaComponent::StartContinuousDrain(float DrainRate)
{
	ContinuousDrainRate = FMath::Max(0.f, DrainRate);

	if (IsDrainingStamina() && CurrentStamina > 0.f)
	{
		SetComponentTickEnabled(true);
	}
}

void UIFStaminaComponent::StopContinuousDrain()
{
	ContinuousDrainRate = 0.f;
	UpdateTickEnabled();
}

void UIFStaminaComponent::BeginPlay()
{
	Super::BeginPlay();

	MaxStamina = FMath::Max(1.f, MaxStamina);
	StaminaRegenRate = FMath::Max(0.f, StaminaRegenRate);
	StaminaRegenDelay = FMath::Max(0.f, StaminaRegenDelay);

	CurrentStamina = MaxStamina;

	// Pretend the regen delay already elapsed so a full bar does not start ticking on spawn.
	TimeSinceLastStaminaDrain = StaminaRegenDelay;
	ContinuousDrainRate = 0.f;

	OnStaminaChanged.Broadcast(GetStaminaPercent());
}

void UIFStaminaComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsDrainingStamina())
	{
		DrainStamina(DeltaTime);
		return;
	}

	RegenerateStamina(DeltaTime);
}

bool UIFStaminaComponent::CanRegenerateStamina() const
{
	return CurrentStamina < MaxStamina && StaminaRegenRate > 0.f;
}

void UIFStaminaComponent::DrainStamina(float DeltaTime)
{
	SetStaminaClamped(CurrentStamina - ContinuousDrainRate * DeltaTime);
	TimeSinceLastStaminaDrain = 0.f;

	if (CurrentStamina <= 0.f)
	{
		StopContinuousDrain();
	}
}

void UIFStaminaComponent::RegenerateStamina(float DeltaTime)
{
	TimeSinceLastStaminaDrain += DeltaTime;
	if (TimeSinceLastStaminaDrain < StaminaRegenDelay)
	{
		return;
	}

	SetStaminaClamped(CurrentStamina + StaminaRegenRate * DeltaTime);

	if (CurrentStamina >= MaxStamina)
	{
		SetComponentTickEnabled(false);
	}
}

void UIFStaminaComponent::UpdateTickEnabled()
{
	SetComponentTickEnabled(CanRegenerateStamina());
}

void UIFStaminaComponent::SetStaminaClamped(float NewStamina)
{
	const float PreviousStamina = CurrentStamina;
	if (IFStatUtils::ApplyClampedValue(CurrentStamina, MaxStamina, NewStamina))
	{
		OnStaminaChanged.Broadcast(GetStaminaPercent());
	}
	BroadcastStaminaDepletedIfNeeded(PreviousStamina);
}

void UIFStaminaComponent::BroadcastStaminaDepletedIfNeeded(float PreviousStamina)
{
	if (PreviousStamina > 0.f && CurrentStamina <= 0.f)
	{
		OnStaminaDepleted.Broadcast();
	}
}
