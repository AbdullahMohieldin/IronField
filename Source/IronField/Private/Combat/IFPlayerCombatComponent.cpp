#include "Combat/IFPlayerCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/IFPlayerCharacter.h"
#include "Combat/IFCombatDataAsset.h"
#include "Combat/IFTeamTypes.h"
#include "Core/IFLog.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "Stats/IFStaminaComponent.h"
#include "TimerManager.h"

void UIFPlayerCombatComponent::RequestAttack()
{
	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || Player->IsGettingUp() || IsStunned() || IsInHitReact())
	{
		return;
	}

	if (IsIdle() || GetCombatState() == EIFCombatState::Blocking)
	{
		const bool bWasBlocking = (GetCombatState() == EIFCombatState::Blocking);
		if (bWasBlocking)
		{
			SetCombatState(EIFCombatState::Idle);
		}

		bool bAttackStarted = false;
		if (bWasBlocking && IsValid(LoadedCombatData) && !LoadedCombatData->ShieldBlock.GuardAttack.Montage.IsNull())
		{
			bAttackStarted = PlayGuardAttack();
		}

		if (!bAttackStarted)
		{
			bAttackStarted = TryStartAttack();
		}

		if (!bAttackStarted)
		{
			if (bWasBlocking && bBlockInputHeld)
			{
				TryResumeBlock();
			}
		}
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

void UIFPlayerCombatComponent::StartBlock()
{
	bBlockInputHeld = true;

	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || Player->IsGettingUp() || !IsIdle())
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] StartBlock ignored (state=%d)."), static_cast<int32>(GetCombatState()));
		return;
	}

	if (!IsValid(LoadedCombatData))
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] StartBlock ignored: combat data not loaded."));
		return;
	}

	if (IsValid(Stamina) && !Stamina->HasStamina(LoadedCombatData->ShieldBlock.MinBlockEntryStamina))
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] StartBlock ignored: below minimum block entry stamina."));
		return;
	}

	bAttackBuffered = false;
	CurrentStepIndex = INDEX_NONE;
	SetCombatState(EIFCombatState::Blocking);
	UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Block started."));
}

void UIFPlayerCombatComponent::StopBlock()
{
	bBlockInputHeld = false;

	if (GetCombatState() == EIFCombatState::Blocking)
	{
		SetCombatState(EIFCombatState::Idle);
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Block stopped."));
	}
}

bool UIFPlayerCombatComponent::TryBlockIncomingDamage(float DamageAmount, AActor* DamageCauser, AController* EventInstigator)
{
	if (!IsBlocking())
	{
		return false;
	}

	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead())
	{
		return false;
	}

	if (!IsValid(LoadedCombatData))
	{
		return false;
	}

	const FIFShieldBlockConfig& BlockConfig = LoadedCombatData->ShieldBlock;

	const AActor* Attacker = DamageCauser;
	if (!IsValid(Attacker) && IsValid(EventInstigator))
	{
		Attacker = EventInstigator->GetPawn();
	}

	FVector OwnerForward2D = Player->GetActorForwardVector();
	OwnerForward2D.Z = 0.f;
	OwnerForward2D.Normalize();

	FVector ToAttacker2D = OwnerForward2D;
	if (IsValid(Attacker) && Attacker != Player)
	{
		ToAttacker2D = Attacker->GetActorLocation() - Player->GetActorLocation();
		ToAttacker2D.Z = 0.f;
		if (!ToAttacker2D.Normalize())
		{
			ToAttacker2D = OwnerForward2D;
		}
	}

	const float MinDot = FMath::Cos(FMath::DegreesToRadians(BlockConfig.MaxBlockAngleDegrees * 0.5f));
	if (FVector::DotProduct(OwnerForward2D, ToAttacker2D) < MinDot)
	{
		return false;
	}

	const float StaminaDrain = DamageAmount * BlockConfig.BlockStaminaCostMultiplier;
	const float CurrentStaminaVal = IsValid(Stamina) ? Stamina->GetCurrentStamina() : 0.f;

	if (CurrentStaminaVal > StaminaDrain)
	{
		if (IsValid(Stamina))
		{
			Stamina->TryConsumeStamina(StaminaDrain);
		}

		UAnimMontage* const DeflectMontage = BlockConfig.DeflectMontage.Get();
		if (IsValid(DeflectMontage) && IsValid(CachedAnimInstance))
		{
			CachedAnimInstance->Montage_Play(DeflectMontage, 1.f);
		}

		return true;
	}

	// Guard break
	if (IsValid(Stamina))
	{
		Stamina->TryConsumeStamina(CurrentStaminaVal);
	}

	TriggerGuardBreak();
	return true;
}

void UIFPlayerCombatComponent::TriggerGuardBreak()
{
	InvalidateMontageExecution();
	bBlockInputHeld = false;
	bAttackBuffered = false;
	ClearComboWindowState();
	CloseHitWindow();
	CurrentStepIndex = INDEX_NONE;
	bIsSpinAttacking = false;
	bIsExecutingGuardAttack = false;

	SetCombatState(EIFCombatState::Stunned);

	UAnimMontage* const GuardBreakMontage = IsValid(LoadedCombatData) ? LoadedCombatData->ShieldBlock.GuardBreakMontage.Get() : nullptr;
	float Duration = 0.f;
	if (IsValid(GuardBreakMontage) && IsValid(CachedAnimInstance))
	{
		Duration = CachedAnimInstance->Montage_Play(GuardBreakMontage, 1.f, EMontagePlayReturnType::Duration);
	}

	if (Duration <= 0.f)
	{
		Duration = 2.0f;
	}

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(GuardBreakTimerHandle, this, &UIFPlayerCombatComponent::HandleGuardBreakEnded, Duration, false);
	}
}

void UIFPlayerCombatComponent::HandleGuardBreakEnded()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GuardBreakTimerHandle);
	}

	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (IsValid(Player) && Player->IsDead())
	{
		return;
	}

	if (GetCombatState() == EIFCombatState::Stunned)
	{
		SetCombatState(EIFCombatState::Idle);
	}
}

bool UIFPlayerCombatComponent::RequestSpinAttack()
{
	AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || Player->IsGettingUp() || IsStunned() || IsInHitReact())
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin ignored: owner invalid, dead, getting up, stunned, or in hit react."));
		return false;
	}

	const EIFCombatState CurrentState = GetCombatState();
	if (CurrentState != EIFCombatState::Idle && CurrentState != EIFCombatState::Blocking)
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin ignored (state=%d)."), static_cast<int32>(CurrentState));
		return false;
	}

	if (!IsValid(LoadedCombatData))
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin ignored: combat data not loaded."));
		return false;
	}

	const FIFSpinAttackConfig& SpinConfig = LoadedCombatData->SpinAttack;
	const UWorld* const World = GetWorld();
	if (!IsValid(World) || World->GetTimeSeconds() < NextSpinAttackAllowedTime)
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin ignored: cooldown %.2fs remaining."), GetSpinAttackCooldownRemaining());
		return false;
	}

	if (IsValid(Stamina) && !Stamina->HasStamina(SpinConfig.StaminaCost))
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin ignored: stamina below cost %.0f."), SpinConfig.StaminaCost);
		return false;
	}

	UAnimMontage* const SpinMontage = SpinConfig.Montage.Get();
	if (!IsValid(SpinMontage) || !IsValid(CachedAnimInstance))
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin ignored: montage or anim instance missing."));
		return false;
	}

	const EIFCombatState StartingState = GetCombatState();
	CloseHitWindow();

	if (!PlayAttackMontage(SpinMontage))
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin aborted: montage playback failed."));
		return false;
	}

	const int32 StartedInstanceID = CurrentMontageInstanceID;

	if (IsValid(Stamina))
	{
		Stamina->TryConsumeStamina(SpinConfig.StaminaCost);
	}

	if (CurrentMontageInstanceID != StartedInstanceID || GetCombatState() != StartingState)
	{
		return false;
	}

	NextSpinAttackAllowedTime = World->GetTimeSeconds() + SpinConfig.CooldownSeconds;

	if (const AController* const Controller = Player->GetController())
	{
		FRotator AimRotation = Player->GetActorRotation();
		AimRotation.Yaw = Controller->GetControlRotation().Yaw;
		Player->SetActorRotation(AimRotation);
	}

	bAttackBuffered = false;
	ClearComboWindowState();
	CurrentStepIndex = INDEX_NONE;
	bIsExecutingGuardAttack = false;

	LeapSlamAttackStep.Montage = SpinConfig.Montage;
	LeapSlamAttackStep.Damage = SpinConfig.Damage;
	LeapSlamAttackStep.StaminaCost = SpinConfig.StaminaCost;
	LeapSlamAttackStep.HitRadius = SpinConfig.LeapSlamTraceRadius;
	LeapSlamAttackStep.HitStartSocket = SpinConfig.HitStartSocket;
	LeapSlamAttackStep.HitEndSocket = SpinConfig.HitEndSocket;
	LeapSlamAttackStep.TraceSampleCount = SpinConfig.TraceSampleCount;
	LeapSlamAttackStep.HitVFX = SpinConfig.HitVFX;
	LeapSlamAttackStep.HitSound = SpinConfig.HitSound;

	bIsSpinAttacking = true;
	SetCombatState(EIFCombatState::Attacking);
	if (CurrentMontageInstanceID != StartedInstanceID || !IsAttacking())
	{
		return false;
	}

	UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin attack started."));
	return true;
}

float UIFPlayerCombatComponent::GetSpinAttackCooldownRemaining() const
{
	const UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return 0.f;
	}

	return FMath::Max(0.f, NextSpinAttackAllowedTime - World->GetTimeSeconds());
}

void UIFPlayerCombatComponent::OpenComboWindow()
{
	if (!IsAttacking() || bIsSpinAttacking)
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
	if (!IsAttacking() || bIsSpinAttacking || bComboWindowNotifySeen || bComboWindowOpen)
	{
		return;
	}

	bComboWindowOpen = true;
	TryBufferedAdvance();
}

bool UIFPlayerCombatComponent::TryBufferedAdvance()
{
	if (!bAttackBuffered || bIsSpinAttacking || CurrentStepIndex == INDEX_NONE)
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

void UIFPlayerCombatComponent::GatherRequiredAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const
{
	Super::GatherRequiredAssetPaths(OutAssetPaths);

	if (!IsValid(LoadedCombatData))
	{
		return;
	}

	if (!LoadedCombatData->ShieldBlock.DeflectMontage.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->ShieldBlock.DeflectMontage.ToSoftObjectPath());
	}
	if (!LoadedCombatData->ShieldBlock.GuardBreakMontage.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->ShieldBlock.GuardBreakMontage.ToSoftObjectPath());
	}
	if (!LoadedCombatData->ShieldBlock.GuardAttack.Montage.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->ShieldBlock.GuardAttack.Montage.ToSoftObjectPath());
	}
	if (!LoadedCombatData->ShieldBlock.GuardAttack.HitVFX.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->ShieldBlock.GuardAttack.HitVFX.ToSoftObjectPath());
	}
	if (!LoadedCombatData->ShieldBlock.GuardAttack.HitSound.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->ShieldBlock.GuardAttack.HitSound.ToSoftObjectPath());
	}
	if (!LoadedCombatData->SpinAttack.Montage.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->SpinAttack.Montage.ToSoftObjectPath());
	}
	if (!LoadedCombatData->SpinAttack.HitVFX.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->SpinAttack.HitVFX.ToSoftObjectPath());
	}
	if (!LoadedCombatData->SpinAttack.HitSound.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->SpinAttack.HitSound.ToSoftObjectPath());
	}
}

const FIFMeleeAttackStep* UIFPlayerCombatComponent::GetCurrentStep() const
{
	if (bIsSpinAttacking && IsValid(LoadedCombatData))
	{
		return &LeapSlamAttackStep;
	}

	return Super::GetCurrentStep();
}

void UIFPlayerCombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (bIsSpinAttacking)
	{
		bIsSpinAttacking = false;
		FinishCombo();
		TryResumeBlock();
		return;
	}

	Super::HandleMontageBlendingOut(Montage, bInterrupted);

	if (IsIdle())
	{
		TryResumeBlock();
	}
}

void UIFPlayerCombatComponent::PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator)
{
	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || IsStunned())
	{
		return;
	}

	const EIFCombatState CurrentState = GetCombatState();
	const bool bCanHitReact = (CurrentState == EIFCombatState::Idle)
		|| (CurrentState == EIFCombatState::Blocking)
		|| (CurrentState == EIFCombatState::HitReact)
		|| (CurrentState == EIFCombatState::Attacking && !bIsSpinAttacking);

	if (!bCanHitReact)
	{
		return;
	}

	if (CurrentState == EIFCombatState::Attacking)
	{
		bAttackBuffered = false;
		ClearComboWindowState();
	}

	Super::PlayDirectionalHitReact(DamageCauser, EventInstigator);
	if (IsIdle())
	{
		TryResumeBlock();
	}
}

void UIFPlayerCombatComponent::OnHitReactFinished()
{
	Super::OnHitReactFinished();
	if (IsIdle())
	{
		TryResumeBlock();
	}
}

void UIFPlayerCombatComponent::TryResumeBlock()
{
	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || Player->IsGettingUp() || !IsIdle())
	{
		return;
	}

	if (!bBlockInputHeld)
	{
		return;
	}

	if (!IsValid(LoadedCombatData))
	{
		return;
	}

	if (IsValid(Stamina) && !Stamina->HasStamina(LoadedCombatData->ShieldBlock.MinBlockEntryStamina))
	{
		return;
	}

	bAttackBuffered = false;
	CurrentStepIndex = INDEX_NONE;
	bIsExecutingGuardAttack = false;
	SetCombatState(EIFCombatState::Blocking);
	UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Block resumed."));
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
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GuardBreakTimerHandle);
	}

	bBlockInputHeld = false;
	bAttackBuffered = false;
	bIsSpinAttacking = false;
	bIsExecutingGuardAttack = false;
	ClearComboWindowState();
	Super::HandleOwnerDeath();
}

void UIFPlayerCombatComponent::HandleOwnerRevived()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GuardBreakTimerHandle);
	}

	bBlockInputHeld = false;
	bAttackBuffered = false;
	bIsSpinAttacking = false;
	bIsExecutingGuardAttack = false;
	ClearComboWindowState();
	Super::HandleOwnerRevived();
}

void UIFPlayerCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GuardBreakTimerHandle);
	}

	ClearComboWindowState();
	Super::EndPlay(EndPlayReason);
}
