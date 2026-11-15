#include "Combat/IFPlayerCombatComponent.h"

#include "Animation/AnimMontage.h"
#include "Character/IFPlayerCharacter.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "TimerManager.h"

void UIFPlayerCombatComponent::RequestAttack()
{
	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || Player->IsGettingUp())
	{
		return;
	}

	if (IsIdle())
	{
		TryStartAttack();
		return;
	}
	if (!IsAttacking())
	{
		return;
	}

	// One-slot buffer: consumed instantly inside the window, otherwise at window open.
	bAttackBuffered = true;
	if (bComboWindowOpen)
	{
		TryBufferedAdvance();
	}
}

void UIFPlayerCombatComponent::OpenComboWindow()
{
	if (!IsAttacking())
	{
		return;
	}

	bComboWindowOpen = true;
	bComboWindowNotifySeen = true;

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FallbackComboWindowHandle);
	}

	TryBufferedAdvance();
}

void UIFPlayerCombatComponent::CloseComboWindow()
{
	bComboWindowOpen = false;
}

void UIFPlayerCombatComponent::HandleStepStarted()
{
	ClearComboWindowState();

	// Free-look locomotion means the body may face away from the aim; snap to camera yaw
	// so every swing attacks where the player is looking.
	AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (IsValid(Player))
	{
		if (const AController* const Controller = Player->GetController())
		{
			FRotator AimRotation = Player->GetActorRotation();
			AimRotation.Yaw = Controller->GetControlRotation().Yaw;
			Player->SetActorRotation(AimRotation);
		}
	}

	const FIFMeleeAttackStep* const Step = GetCurrentStep();
	const UAnimMontage* const Montage = GetCurrentMontage();
	UWorld* const World = GetWorld();
	if (!Step || !IsValid(Montage) || !IsValid(World))
	{
		return;
	}

	const float FallbackDelay = Montage->GetPlayLength() * FMath::Clamp(Step->FallbackComboWindowStartNormalized, 0.f, 1.f);
	if (FallbackDelay > 0.f)
	{
		World->GetTimerManager().SetTimer(FallbackComboWindowHandle, this, &UIFPlayerCombatComponent::HandleFallbackComboWindow, FallbackDelay, false);
	}
	else
	{
		HandleFallbackComboWindow();
	}
}

void UIFPlayerCombatComponent::HandleFallbackComboWindow()
{
	if (!IsAttacking() || bComboWindowNotifySeen || bComboWindowOpen)
	{
		return;
	}

	bComboWindowOpen = true;
	TryBufferedAdvance();
}

bool UIFPlayerCombatComponent::TryBufferedAdvance()
{
	if (!bAttackBuffered)
	{
		return false;
	}

	if (PlayStep(GetNextStepIndex()))
	{
		bAttackBuffered = false;
		return true;
	}

	return false;
}

void UIFPlayerCombatComponent::ClearComboWindowState()
{
	bComboWindowOpen = false;
	bComboWindowNotifySeen = false;

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FallbackComboWindowHandle);
	}
}

void UIFPlayerCombatComponent::HandleOwnerDeath()
{
	bAttackBuffered = false;
	ClearComboWindowState();
	Super::HandleOwnerDeath();
}

void UIFPlayerCombatComponent::HandleOwnerRevived()
{
	bAttackBuffered = false;
	ClearComboWindowState();
	Super::HandleOwnerRevived();
}

void UIFPlayerCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearComboWindowState();
	Super::EndPlay(EndPlayReason);
}