#include "Combat/IFPlayerCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/IFPlayerCharacter.h"
#include "Combat/IFCombatDataAsset.h"
#include "Core/IFLog.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Stats/IFStaminaComponent.h"
#include "TimerManager.h"

void UIFPlayerCombatComponent::RequestAttack()
{
	const AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player) || Player->IsDead() || IsStunned() || IsInHitReact())
	{
		return;
	}

	if (IsIdle() || IsBlocking())
	{
		if (IsBlocking() && IsValid(LoadedCombatData) && !LoadedCombatData->ShieldBlock.GuardAttack.Montage.IsNull())
		{
			if (PlayGuardAttack())
			{
				return;
			}
		}

		TryStartAttack();
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
	LastFinishedStepIndex = INDEX_NONE;
	SetCombatState(EIFCombatState::Blocking);
	UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] Block started."));
}

void UIFPlayerCombatComponent::StopBlock()
{
	bBlockInputHeld = false;

	if (IsBlocking())
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
	if (!IsValid(Player) || Player->IsDead() || !IsValid(LoadedCombatData))
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

	// Guard-breaking hit is still health-absorbed by the shield while zeroing stamina into Stunned.
	if (IsValid(Stamina))
	{
		Stamina->TryConsumeStamina(CurrentStaminaVal);
	}

	TriggerGuardBreak();
	return true;
}

bool UIFPlayerCombatComponent::PlayGuardAttack()
{
	if (!bCombatReady || !IsValid(LoadedCombatData))
	{
		return false;
	}

	LastFinishedStepIndex = INDEX_NONE;
	bIsExecutingGuardAttack = true;
	if (!ExecuteAttackStep(LoadedCombatData->ShieldBlock.GuardAttack, INDEX_NONE))
	{
		bIsExecutingGuardAttack = false;
		return false;
	}

	return true;
}

void UIFPlayerCombatComponent::TriggerGuardBreak()
{
	InvalidateMontageExecution();
	bAttackBuffered = false;
	ClearComboWindowState();
	CloseHitWindow();
	CurrentStepIndex = INDEX_NONE;
	LastFinishedStepIndex = INDEX_NONE;
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
		World->GetTimerManager().SetTimer(
			GuardBreakTimerHandle,
			this,
			&UIFPlayerCombatComponent::HandleGuardBreakEnded,
			Duration,
			false);
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

	if (IsStunned())
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

	if (!IsIdle() && !IsBlocking())
	{
		UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam ignored (state=%d)."), static_cast<int32>(GetCombatState()));
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

	LastFinishedStepIndex = INDEX_NONE;
	bAttackBuffered = false;
	bIsExecutingGuardAttack = false;
	bIsLeapSlamming = true;

	if (!ExecuteAttackStep(LeapSlamConfig.Attack, INDEX_NONE))
	{
		bIsLeapSlamming = false;
		return false;
	}

	NextLeapSlamAllowedTime = World->GetTimeSeconds() + LeapSlamConfig.CooldownSeconds;
	UE_LOG(LogIronField, Verbose, TEXT("[IF-Combat] LeapSlam attack started."));
	return true;
}

float UIFPlayerCombatComponent::GetLeapSlamCooldownRemaining() const
{
	const UWorld* const World = GetWorld();
	return IsValid(World) ? FMath::Max(0.f, NextLeapSlamAllowedTime - World->GetTimeSeconds()) : 0.f;
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

void UIFPlayerCombatComponent::SnapOwnerYawToController()
{
	AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(GetOwner());
	if (!IsValid(Player))
	{
		return;
	}

	if (const AController* const Controller = Player->GetController())
	{
		FRotator AimRotation = Player->GetActorRotation();
		AimRotation.Yaw = Controller->GetControlRotation().Yaw;
		Player->SetActorRotation(AimRotation);
	}
}

void UIFPlayerCombatComponent::HandleStepStarted()
{
	ClearComboWindowState();

	// Snap facing to camera yaw so swings align with aim.
	SnapOwnerYawToController();

	if (bIsLeapSlamming)
	{
		return;
	}

	const FIFMeleeAttackStep* const Step = GetCurrentStep();
	const UAnimMontage* const Montage = GetCurrentMontage();
	UWorld* const World = GetWorld();
	if (!Step || !IsValid(Montage) || !IsValid(World))
	{
		return;
	}

	const float EffectiveDuration = Montage->GetPlayLength() / FMath::Max(KINDA_SMALL_NUMBER, Montage->RateScale);
	const float FallbackDelay = EffectiveDuration * FMath::Clamp(Step->FallbackComboWindowStartNormalized, 0.f, 1.f);
	if (FallbackDelay > 0.f)
	{
		World->GetTimerManager().SetTimer(
			FallbackComboWindowHandle,
			this,
			&UIFPlayerCombatComponent::HandleFallbackComboWindow,
			FallbackDelay,
			false);
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

	if (!LoadedCombatData->BlockCascadeVFX.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->BlockCascadeVFX.ToSoftObjectPath());
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
	if (!LoadedCombatData->LeapSlam.Attack.Montage.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->LeapSlam.Attack.Montage.ToSoftObjectPath());
	}
	if (!LoadedCombatData->LeapSlam.Attack.HitVFX.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->LeapSlam.Attack.HitVFX.ToSoftObjectPath());
	}
	if (!LoadedCombatData->LeapSlam.Attack.HitCascadeVFX.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->LeapSlam.Attack.HitCascadeVFX.ToSoftObjectPath());
	}
	if (!LoadedCombatData->LeapSlam.Attack.HitSound.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->LeapSlam.Attack.HitSound.ToSoftObjectPath());
	}
}

void UIFPlayerCombatComponent::HandleCombatStateTransition(EIFCombatState PreviousState, EIFCombatState NewState)
{
	Super::HandleCombatStateTransition(PreviousState, NewState);

	if (PreviousState == EIFCombatState::Attacking && NewState != EIFCombatState::Attacking)
	{
		bAttackBuffered = false;
		bIsLeapSlamming = false;
		bIsExecutingGuardAttack = false;
		ClearComboWindowState();
	}

	if (NewState == EIFCombatState::Blocking && IsValid(Stamina))
	{
		Stamina->SetRegenRateMultiplier(BlockingStaminaRegenMultiplier);
	}
}

const FIFMeleeAttackStep* UIFPlayerCombatComponent::GetCurrentStep() const
{
	if (!IsValid(LoadedCombatData))
	{
		return nullptr;
	}

	if (bIsLeapSlamming)
	{
		return &LoadedCombatData->LeapSlam.Attack;
	}
	if (bIsExecutingGuardAttack)
	{
		return &LoadedCombatData->ShieldBlock.GuardAttack;
	}

	return Super::GetCurrentStep();
}

void UIFPlayerCombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (bIsLeapSlamming || bIsExecutingGuardAttack)
	{
		bIsLeapSlamming = false;
		bIsExecutingGuardAttack = false;
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

	if (!bIsHeavyStagger || (IsAttacking() && bIsLeapSlamming))
	{
		Super::PlayDirectionalHitReact(DamageCauser, EventInstigator, false);
		return;
	}

	Super::PlayDirectionalHitReact(DamageCauser, EventInstigator, true);

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
	if (!IsValid(Player) || Player->IsDead() || !IsIdle() || !bBlockInputHeld || !IsValid(LoadedCombatData))
	{
		return;
	}

	if (IsValid(Stamina) && !Stamina->HasStamina(LoadedCombatData->ShieldBlock.MinBlockEntryStamina))
	{
		return;
	}

	bAttackBuffered = false;
	CurrentStepIndex = INDEX_NONE;
	LastFinishedStepIndex = INDEX_NONE;
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
	NextLeapSlamAllowedTime = 0.f;
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
