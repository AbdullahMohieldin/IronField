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
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Stats/IFStaminaComponent.h"
#include "TimerManager.h"

void UIFPlayerCombatComponent::RequestAttack()
{
	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || IsStunned() || IsInHitReact())
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
	if (!IsValid(Player) || Player->IsDead() || !IsIdle())
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] StartBlock ignored (state=%d)."), static_cast<int32>(GetCombatState()));
		return;
	}

	if (!IsValid(LoadedCombatData))
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] StartBlock ignored: combat data not loaded."));
		return;
	}

	if (IsValid(Stamina) && !Stamina->HasStamina(LoadedCombatData->ShieldBlock.MinBlockEntryStamina))
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] StartBlock ignored: below minimum block entry stamina."));
		return;
	}

	bAttackBuffered = false;
	CurrentStepIndex = INDEX_NONE;
	SetCombatState(EIFCombatState::Blocking);
	UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] Block started."));
}

void UIFPlayerCombatComponent::StopBlock()
{
	bBlockInputHeld = false;

	if (GetCombatState() == EIFCombatState::Blocking)
	{
		SetCombatState(EIFCombatState::Idle);
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] Block stopped."));
	}
}

void UIFPlayerCombatComponent::CancelPendingInputsForTacticalMode()
{
	bAttackBuffered = false;
	StopBlock();
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

	const FVector OwnerForward2D = Player->GetActorForwardVector().GetSafeNormal2D();

	FVector ToAttacker2D = OwnerForward2D;
	if (IsValid(Attacker) && Attacker != Player)
	{
		const FVector Candidate = (Attacker->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D();
		if (!Candidate.IsNearlyZero())
		{
			ToAttacker2D = Candidate;
		}
	}

	const float MinDot = FMath::Cos(FMath::DegreesToRadians(BlockConfig.MaxBlockAngleDegrees * 0.5f));
	if (FVector::DotProduct(OwnerForward2D, ToAttacker2D) < MinDot)
	{
		return false;
	}

	const float StaminaDrain = DamageAmount * BlockConfig.BlockStaminaCostMultiplier;
	const float CurrentStaminaVal = IsValid(Stamina) ? Stamina->GetCurrentStamina() : 0.f;

	if (UParticleSystem* const BlockVFX = LoadedCombatData->BlockCascadeVFX.Get())
	{
		if (UWorld* const World = GetWorld())
		{
			const FVector BlockLocation = Player->GetActorLocation()
				+ ToAttacker2D * BlockConfig.BlockVFXOffset.X
				+ FVector(0.f, 0.f, BlockConfig.BlockVFXOffset.Z);
			UGameplayStatics::SpawnEmitterAtLocation(World, BlockVFX, BlockLocation, ToAttacker2D.Rotation(), true);
		}
	}

	if (CurrentStaminaVal >= StaminaDrain)
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
	bAttackBuffered = false;
	ClearComboWindowState();
	CloseHitWindow();
	CurrentStepIndex = INDEX_NONE;
	bIsLeapSlamming = false;
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
		Duration = IsValid(LoadedCombatData) ? LoadedCombatData->ShieldBlock.FallbackGuardBreakDurationSeconds : 2.0f;
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
		TryResumeBlock();
	}
}

bool UIFPlayerCombatComponent::RequestLeapSlam()
{
	AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || IsStunned() || IsInHitReact())
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam ignored: owner invalid, dead, stunned, or in hit react."));
		return false;
	}

	const EIFCombatState CurrentState = GetCombatState();
	if (CurrentState != EIFCombatState::Idle && CurrentState != EIFCombatState::Blocking)
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam ignored (state=%d)."), static_cast<int32>(CurrentState));
		return false;
	}

	if (!IsValid(LoadedCombatData))
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam ignored: combat data not loaded."));
		return false;
	}

	const FIFLeapSlamConfig& LeapSlamConfig = LoadedCombatData->LeapSlam;
	const UWorld* const World = GetWorld();
	if (!IsValid(World) || World->GetTimeSeconds() < NextLeapSlamAllowedTime)
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam ignored: cooldown %.2fs remaining."), GetLeapSlamCooldownRemaining());
		return false;
	}

	if (IsValid(Stamina) && !Stamina->HasStamina(LeapSlamConfig.StaminaCost))
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam ignored: stamina below cost %.0f."), LeapSlamConfig.StaminaCost);
		return false;
	}

	UAnimMontage* const LeapSlamMontage = LeapSlamConfig.Montage.Get();
	if (!IsValid(LeapSlamMontage) || !IsValid(CachedAnimInstance))
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam ignored: montage or anim instance missing."));
		return false;
	}

	const EIFCombatState StartingState = GetCombatState();
	CloseHitWindow();

	if (!PlayAttackMontage(LeapSlamMontage))
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam aborted: montage playback failed."));
		return false;
	}

	const int32 StartedInstanceID = CurrentMontageInstanceID;

	if (IsValid(Stamina))
	{
		Stamina->TryConsumeStamina(LeapSlamConfig.StaminaCost);
	}

	if (CurrentMontageInstanceID != StartedInstanceID || GetCombatState() != StartingState)
	{
		return false;
	}

	NextLeapSlamAllowedTime = World->GetTimeSeconds() + LeapSlamConfig.CooldownSeconds;

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

	LeapSlamAttackStep = LeapSlamConfig.ToAttackStep();

	bIsLeapSlamming = true;
	SetCombatState(EIFCombatState::Attacking);
	if (CurrentMontageInstanceID != StartedInstanceID || !IsAttacking())
	{
		return false;
	}

	UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam attack started."));
	return true;
}

float UIFPlayerCombatComponent::GetLeapSlamCooldownRemaining() const
{
	const UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return 0.f;
	}

	return FMath::Max(0.f, NextLeapSlamAllowedTime - World->GetTimeSeconds());
}

void UIFPlayerCombatComponent::OpenComboWindow()
{
	if (!IsAttacking() || bIsLeapSlamming)
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

	// Snap facing to camera yaw so swings align with aim.
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
	if (!IsAttacking() || bIsLeapSlamming || bComboWindowNotifySeen || bComboWindowOpen)
	{
		return;
	}

	bComboWindowOpen = true;
	TryBufferedAdvance();
}

bool UIFPlayerCombatComponent::TryBufferedAdvance()
{
	if (!bAttackBuffered || bIsLeapSlamming || CurrentStepIndex == INDEX_NONE)
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
	if (!LoadedCombatData->ShieldBlock.GuardAttack.HitCascadeVFX.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->ShieldBlock.GuardAttack.HitCascadeVFX.ToSoftObjectPath());
	}
	if (!LoadedCombatData->ShieldBlock.GuardAttack.HitSound.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->ShieldBlock.GuardAttack.HitSound.ToSoftObjectPath());
	}
	if (!LoadedCombatData->LeapSlam.Montage.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->LeapSlam.Montage.ToSoftObjectPath());
	}
	if (!LoadedCombatData->LeapSlam.HitVFX.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->LeapSlam.HitVFX.ToSoftObjectPath());
	}
	if (!LoadedCombatData->LeapSlam.HitCascadeVFX.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->LeapSlam.HitCascadeVFX.ToSoftObjectPath());
	}
	if (!LoadedCombatData->LeapSlam.HitSound.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->LeapSlam.HitSound.ToSoftObjectPath());
	}
}

const FIFMeleeAttackStep* UIFPlayerCombatComponent::GetCurrentStep() const
{
	if (bIsLeapSlamming && IsValid(LoadedCombatData))
	{
		return &LeapSlamAttackStep;
	}

	return Super::GetCurrentStep();
}

void UIFPlayerCombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (bIsLeapSlamming)
	{
		bIsLeapSlamming = false;
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

void UIFPlayerCombatComponent::PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator, bool bIsHeavyStagger)
{
	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || IsStunned())
	{
		return;
	}

	if (!bIsHeavyStagger || (GetCombatState() == EIFCombatState::Attacking && bIsLeapSlamming))
	{
		Super::PlayDirectionalHitReact(DamageCauser, EventInstigator, false);
		return;
	}

	const EIFCombatState CurrentState = GetCombatState();
	const bool bCanHitReact = (CurrentState == EIFCombatState::Idle)
		|| (CurrentState == EIFCombatState::Blocking)
		|| (CurrentState == EIFCombatState::HitReact)
		|| (CurrentState == EIFCombatState::Attacking && !bIsLeapSlamming);

	if (!bCanHitReact)
	{
		return;
	}

	Super::PlayDirectionalHitReact(DamageCauser, EventInstigator, true);

	if (!IsAttacking())
	{
		bAttackBuffered = false;
		ClearComboWindowState();
	}

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
	if (!IsValid(Player) || Player->IsDead() || !IsIdle())
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
	UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] Block resumed."));
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
	bIsLeapSlamming = false;
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
	bIsLeapSlamming = false;
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
