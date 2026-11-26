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

	TimeSinceLastDrain = 0.f;
	ApplyStaminaChange(CurrentStamina - Amount, false);
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

void UIFStaminaComponent::ConfigureStaminaDefaults(float InMaxStamina, float InRegenRate, float InRegenDelay)
{
	MaxStamina = FMath::Max(1.f, InMaxStamina);
	CurrentStamina = MaxStamina;
	StaminaRegenRate = FMath::Max(0.f, InRegenRate);
	StaminaRegenDelay = FMath::Max(0.f, InRegenDelay);
}

void UIFStaminaComponent::ResetStamina()
{
	ContinuousDrainRate = 0.f;
	RegenRateMultiplier = 1.f;
	CurrentStamina = MaxStamina;
	TimeSinceLastDrain = StaminaRegenDelay;
	SetComponentTickEnabled(false);
	OnStaminaChanged.Broadcast(GetStaminaPercent());
}

void UIFStaminaComponent::SetRegenRateMultiplier(float InMultiplier)
{
	const float ClampedMultiplier = FMath::Max(0.f, InMultiplier);
	if (FMath::IsNearlyEqual(RegenRateMultiplier, ClampedMultiplier))
	{
		return;
	}

	RegenRateMultiplier = ClampedMultiplier;
	UpdateTickEnabled();
}

void UIFStaminaComponent::ResetRegenDelay()
{
	TimeSinceLastDrain = 0.f;
	UpdateTickEnabled();
}

float UIFStaminaComponent::GetStaminaPercent() const
{
	return MaxStamina > 0.f ? CurrentStamina / MaxStamina : 0.f;
}

bool UIFStaminaComponent::CanRegenerate() const
{
	return CurrentStamina < MaxStamina && StaminaRegenRate > 0.f && RegenRateMultiplier > 0.f;
}

void UIFStaminaComponent::DrainStamina(float DeltaTime)
{
	TimeSinceLastDrain = 0.f;
	ApplyStaminaChange(CurrentStamina - ContinuousDrainRate * DeltaTime, true);
}

void UIFStaminaComponent::RegenerateStamina(float DeltaTime)
{
	if (RegenRateMultiplier <= 0.f || StaminaRegenRate <= 0.f)
	{
		UpdateTickEnabled();
		return;
	}

	TimeSinceLastDrain += DeltaTime;
	if (TimeSinceLastDrain < StaminaRegenDelay)
	{
		return;
	}

	ApplyStaminaChange(CurrentStamina + (StaminaRegenRate * RegenRateMultiplier) * DeltaTime, false);
}

void UIFStaminaComponent::UpdateTickEnabled()
{
	const bool bNeedsTick = (IsDraining() && CurrentStamina > 0.f) || CanRegenerate();
	SetComponentTickEnabled(bNeedsTick);
}

void UIFStaminaComponent::ApplyStaminaChange(float NewStamina, bool bClearDrainOnDeplete)
{
	const float PreviousStamina = CurrentStamina;
	const float Clamped = FMath::Clamp(NewStamina, 0.f, MaxStamina);
	const bool bChanged = !FMath::IsNearlyEqual(CurrentStamina, Clamped);

	CurrentStamina = Clamped;
	if (bClearDrainOnDeplete && CurrentStamina <= 0.f)
	{
		ContinuousDrainRate = 0.f;
	}
	UpdateTickEnabled();

	if (bChanged)
	{
		OnStaminaChanged.Broadcast(GetStaminaPercent());
	}

	if (PreviousStamina > 0.f && CurrentStamina <= 0.f)
	{
		OnStaminaDepleted.Broadcast();
	}
}