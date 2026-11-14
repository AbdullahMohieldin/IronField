#include "Stats/IFStaminaComponent.h"

UIFStaminaComponent::UIFStaminaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UIFStaminaComponent::BeginPlay()
{
	Super::BeginPlay();

	MaxStamina = FMath::Max(1.f, MaxStamina);
	StaminaRegenRate = FMath::Max(0.f, StaminaRegenRate);
	StaminaRegenDelay = FMath::Max(0.f, StaminaRegenDelay);

	ResetStamina();
}

void UIFStaminaComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (IsDraining())
	{
		DrainStamina(DeltaTime);
		return;
	}

	RegenerateStamina(DeltaTime);
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
	TimeSinceLastDrain = 0.f;
	UpdateTickEnabled();
	return true;
}

bool UIFStaminaComponent::StartContinuousDrain(float DrainRate)
{
	if (DrainRate <= 0.f || CurrentStamina <= 0.f)
	{
		ContinuousDrainRate = 0.f;
		UpdateTickEnabled();
		return false;
	}

	ContinuousDrainRate = DrainRate;
	UpdateTickEnabled();
	return true;
}

void UIFStaminaComponent::StopContinuousDrain()
{
	ContinuousDrainRate = 0.f;
	UpdateTickEnabled();
}

void UIFStaminaComponent::InitializeStamina(float NewMaxStamina)
{
	MaxStamina = FMath::Max(1.f, NewMaxStamina);
	ResetStamina();
}

void UIFStaminaComponent::ResetStamina()
{
	ContinuousDrainRate = 0.f;
	CurrentStamina = MaxStamina;
	TimeSinceLastDrain = StaminaRegenDelay;
	SetComponentTickEnabled(false);
	OnStaminaChanged.Broadcast(GetStaminaPercent());
}

float UIFStaminaComponent::GetStaminaPercent() const
{
	return MaxStamina > 0.f ? CurrentStamina / MaxStamina : 0.f;
}

bool UIFStaminaComponent::CanRegenerate() const
{
	return CurrentStamina < MaxStamina && StaminaRegenRate > 0.f;
}

void UIFStaminaComponent::DrainStamina(float DeltaTime)
{
	SetStaminaClamped(CurrentStamina - ContinuousDrainRate * DeltaTime);
	TimeSinceLastDrain = 0.f;

	if (CurrentStamina <= 0.f)
	{
		StopContinuousDrain();
	}
}

void UIFStaminaComponent::RegenerateStamina(float DeltaTime)
{
	TimeSinceLastDrain += DeltaTime;
	if (TimeSinceLastDrain < StaminaRegenDelay)
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
	const bool bNeedsTick = (IsDraining() && CurrentStamina > 0.f) || CanRegenerate();
	SetComponentTickEnabled(bNeedsTick);
}

void UIFStaminaComponent::SetStaminaClamped(float NewStamina)
{
	const float PreviousStamina = CurrentStamina;
	const float Clamped = FMath::Clamp(NewStamina, 0.f, MaxStamina);
	if (!FMath::IsNearlyEqual(CurrentStamina, Clamped))
	{
		CurrentStamina = Clamped;
		OnStaminaChanged.Broadcast(GetStaminaPercent());
	}

	BroadcastDepletedIfNeeded(PreviousStamina);
}

void UIFStaminaComponent::BroadcastDepletedIfNeeded(float PreviousStamina)
{
	if (PreviousStamina > 0.f && CurrentStamina <= 0.f)
	{
		OnStaminaDepleted.Broadcast();
	}
}