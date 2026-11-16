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
#include "Kismet/KismetSystemLibrary.h"
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
	bBlockInputHeld = false;
	bAttackBuffered = false;
	ClearComboWindowState();
	CloseHitWindow();
	CurrentStepIndex = INDEX_NONE;
	bIsSpinAttacking = false;
	bIsExecutingLeapSlam = false;
	bIsExecutingGuardAttack = false;

	SetCombatState(EIFCombatState::Stunned);

	UAnimMontage* const GuardBreakMontage = IsValid(LoadedCombatData) ? LoadedCombatData->ShieldBlock.GuardBreakMontage.Get() : nullptr;
	float Duration = 0.f;
	if (IsValid(GuardBreakMontage) && IsValid(CachedAnimInstance))
	{
		Duration = CachedAnimInstance->Montage_Play(GuardBreakMontage, 1.f);
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
	if (!IsValid(Player) || Player->IsDead() || Player->IsGettingUp())
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin ignored: owner invalid, dead, or getting up."));
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

	if (IsValid(Stamina) && !Stamina->TryConsumeStamina(SpinConfig.StaminaCost))
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

	bAttackBuffered = false;
	ClearComboWindowState();
	CloseHitWindow();
	CurrentStepIndex = INDEX_NONE;

	if (const AController* const Controller = Player->GetController())
	{
		FRotator AimRotation = Player->GetActorRotation();
		AimRotation.Yaw = Controller->GetControlRotation().Yaw;
		Player->SetActorRotation(AimRotation);
	}

	NextSpinAttackAllowedTime = World->GetTimeSeconds() + SpinConfig.CooldownSeconds;
	SpinHitActors.Reset();

	CurrentMontage = SpinMontage;
	const float PlayLength = CachedAnimInstance->Montage_Play(SpinMontage, 1.f);
	if (PlayLength <= 0.f)
	{
		CurrentMontage = nullptr;
		SetCombatState(EIFCombatState::Idle);
		UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin aborted: montage playback failed."));
		return false;
	}

	bIsSpinAttacking = true;
	bIsExecutingLeapSlam = true;
	SetCombatState(EIFCombatState::Attacking);
	UE_LOG(LogIronField, Log, TEXT("[IF-Combat] Spin attack started."));
	return true;
}

void UIFPlayerCombatComponent::ExecuteSpinAttackHit()
{
	if (!IsValid(LoadedCombatData))
	{
		return;
	}

	const FIFSpinAttackConfig& SpinConfig = LoadedCombatData->SpinAttack;
	AActor* const OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(OwnerActor);
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	UKismetSystemLibrary::SphereOverlapActors(
		this,
		OwnerActor->GetActorLocation(),
		SpinConfig.Radius,
		ObjectTypes,
		AActor::StaticClass(),
		ActorsToIgnore,
		OverlappingActors
	);

	AController* InstigatorController = nullptr;
	if (const APawn* const OwnerPawn = Cast<APawn>(OwnerActor))
	{
		InstigatorController = OwnerPawn->GetController();
	}

	UWorld* const World = GetWorld();

	for (AActor* const Target : OverlappingActors)
	{
		if (!IsValid(Target) || Target == OwnerActor || SpinHitActors.Contains(Target))
		{
			continue;
		}

		if (!IFTeamUtils::AreHostile(OwnerActor, Target))
		{
			continue;
		}

		SpinHitActors.Add(Target);

		const FVector TargetLocation = Target->GetActorLocation();
		UGameplayStatics::ApplyDamage(
			Target,
			SpinConfig.Damage,
			InstigatorController,
			OwnerActor,
			UDamageType::StaticClass()
		);

		if (IsValid(World) && IsValid(SpinConfig.HitVFX))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, SpinConfig.HitVFX, TargetLocation);
		}

		if (IsValid(SpinConfig.HitSound))
		{
			UGameplayStatics::PlaySoundAtLocation(this, SpinConfig.HitSound, TargetLocation);
		}
	}
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

void UIFPlayerCombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (GetCombatState() == EIFCombatState::Stunned)
	{
		HandleGuardBreakEnded();
		return;
	}

	if (IsValid(LoadedCombatData) && Montage == LoadedCombatData->ShieldBlock.DeflectMontage.Get())
	{
		return;
	}

	if (bIsSpinAttacking && Montage == (LoadedCombatData ? LoadedCombatData->SpinAttack.Montage.Get() : nullptr))
	{
		bIsSpinAttacking = false;
		bIsExecutingLeapSlam = false;
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
		|| (CurrentState == EIFCombatState::Attacking && !bIsExecutingLeapSlam);

	if (!bCanHitReact)
	{
		return;
	}

	if (CurrentState == EIFCombatState::Attacking)
	{
		if (IsValid(CachedAnimInstance) && IsValid(CurrentMontage))
		{
			CachedAnimInstance->Montage_Stop(0.1f, CurrentMontage);
		}
		CloseHitWindow();
		bAttackBuffered = false;
		CurrentStepIndex = INDEX_NONE;
		bIsExecutingGuardAttack = false;
		ClearComboWindowState();
	}

	Super::PlayDirectionalHitReact(DamageCauser, EventInstigator);
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
	bIsExecutingLeapSlam = false;
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
	bIsExecutingLeapSlam = false;
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
