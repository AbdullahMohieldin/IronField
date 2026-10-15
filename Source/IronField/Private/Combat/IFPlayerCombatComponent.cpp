#include "Combat/IFPlayerCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Combat/IFCombatTargetingUtils.h"
#include "Core/IFAnimMontageUtils.h"
#include "Core/IFLog.h"
#include "Engine/World.h"
#include "Stats/IFStaminaComponent.h"

namespace
{
	constexpr float MinBlendOutTime = 0.01f;
}

void UIFPlayerCombatComponent::StartAttack()
{
	if (IsDead() || IsBlocking() || bIsSpinning)
	{
		return;
	}

	if (!IsAttacking())
	{
		TryPlayAttackMontage(0);
		return;
	}

	// One-slot input buffer: press during any active step (except the last) to queue the next.
	if (CanQueueComboAttack())
	{
		bComboQueued = true;
	}
}

void UIFPlayerCombatComponent::StartBlock()
{
	if (!IsIdle() || !HasUsableStamina(MinimumStaminaToStartBlock))
	{
		return;
	}

	if (!BlockMontage)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s StartBlock with no BlockMontage assigned — logic-only block."),
			*GetNameSafe(GetOwner()));
	}

	if (!TryPlayBlockMontage())
	{
		return;
	}

	SetCombatState(ECombatState::Blocking);

	if (StaminaComponent)
	{
		StaminaComponent->StartContinuousDrain(BlockStaminaDrainRate);
	}
}

void UIFPlayerCombatComponent::StopBlock()
{
	if (!IsBlocking())
	{
		return;
	}

	SetCombatState(ECombatState::Idle);

	UAnimInstance* const AnimInstance = GetAnimInstance();
	if (AnimInstance && ActiveBlockMontage)
	{
		AnimInstance->Montage_Stop(BlockBlendOutTime, ActiveBlockMontage);
	}

	ActiveBlockMontage = nullptr;

	if (StaminaComponent)
	{
		StaminaComponent->StopContinuousDrain();
	}
}

void UIFPlayerCombatComponent::ReceiveAttack(AActor* Instigator, float Damage, TSubclassOf<UDamageType> DamageTypeClass)
{
	if (IsDead())
	{
		return;
	}

	const bool bFacing = IsOwnerFacingTarget(Instigator);
	if (IsBlocking() && bFacing)
	{
		// Guard break: swarms force stamina pressure per blocked hit.
		if (StaminaComponent && BlockStaminaCostPerHit > 0.f && !StaminaComponent->TryConsumeStamina(BlockStaminaCostPerHit))
		{
			StopBlock();
			Super::ReceiveAttack(Instigator, Damage, DamageTypeClass);
			return;
		}

		// Chip through the guard so blocking is mitigation, not immunity.
		if (BlockChipFraction > 0.f && Damage > 0.f)
		{
			IFCombatTargetingUtils::ApplyDamageTo(GetOwner(), Instigator, Damage * BlockChipFraction, DamageTypeClass);
			if (IsDead())
			{
				return;
			}
		}

		PlayBlockReactionMontage();
		return;
	}

	Super::ReceiveAttack(Instigator, Damage, DamageTypeClass);
}

bool UIFPlayerCombatComponent::CanQueueComboAttack() const
{
	return !bIsSpinning && Super::CanQueueComboAttack();
}

void UIFPlayerCombatComponent::StartSpinAttack()
{
	if (bIsSpinning || !IsIdle() || !HasUsableStamina(MinimumStaminaToStartSpin))
	{
		return;
	}

	UAnimInstance* const AnimInstance = GetAnimInstance();
	if (!AnimInstance || !SpinAttackMontage)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s StartSpinAttack with no SpinAttackMontage assigned."),
			*GetNameSafe(GetOwner()));
		return;
	}

	const float PlayLength = AnimInstance->Montage_Play(SpinAttackMontage);
	if (PlayLength <= 0.f)
	{
		return;
	}

	bIsSpinning = true;
	ResetRegisteredAttackHits();
	SpinLastHitTimes.Reset();
	SetCombatState(ECombatState::Attacking);

	if (StaminaComponent)
	{
		StaminaComponent->StartContinuousDrain(SpinStaminaDrainRate);
	}

	AnimInstance->Montage_SetNextSection(SpinIntroSectionName, SpinLoopSectionName, SpinAttackMontage);
	AnimInstance->Montage_SetNextSection(SpinLoopSectionName, SpinLoopSectionName, SpinAttackMontage);
	AnimInstance->Montage_JumpToSection(SpinIntroSectionName, SpinAttackMontage);

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &UIFPlayerCombatComponent::HandleSpinMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, SpinAttackMontage);
}

void UIFPlayerCombatComponent::StopSpinAttack()
{
	if (!bIsSpinning)
	{
		return;
	}

	StopSpinGracefully();
}

void UIFPlayerCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (StaminaComponent)
	{
		StaminaComponent->OnStaminaDepleted.AddDynamic(this, &UIFPlayerCombatComponent::HandleStaminaDepleted);
	}
}

void UIFPlayerCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StaminaComponent)
	{
		StaminaComponent->OnStaminaDepleted.RemoveDynamic(this, &UIFPlayerCombatComponent::HandleStaminaDepleted);
		StaminaComponent->StopContinuousDrain();
	}

	StopSpinImmediately();

	UAnimInstance* const AnimInstance = GetAnimInstance();
	IFAnimMontageUtils::ClearMontageEndDelegate(AnimInstance, BlockReactionMontage);
	IFAnimMontageUtils::ClearMontageEndDelegate(AnimInstance, BlockMontage);
	ActiveBlockMontage = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UIFPlayerCombatComponent::ResetCombatState()
{
	StopSpinImmediately();
	ActiveBlockMontage = nullptr;

	if (StaminaComponent)
	{
		StaminaComponent->StopContinuousDrain();
	}

	Super::ResetCombatState();
}

void UIFPlayerCombatComponent::ClearReactionMontageDelegates()
{
	Super::ClearReactionMontageDelegates();

	UAnimInstance* const AnimInstance = GetAnimInstance();
	IFAnimMontageUtils::ClearMontageEndDelegate(AnimInstance, BlockReactionMontage);
}

void UIFPlayerCombatComponent::HandleHitReactionMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	Super::HandleHitReactionMontageEnded(Montage, bInterrupted);

	if (IsBlocking())
	{
		TryPlayBlockMontage();
	}
}

float UIFPlayerCombatComponent::GetCurrentAttackDamage() const
{
	return bIsSpinning ? SpinDamage : Super::GetCurrentAttackDamage();
}

bool UIFPlayerCombatComponent::TryRegisterAttackHit(AActor* TargetActor)
{
	if (!bIsSpinning)
	{
		return Super::TryRegisterAttackHit(TargetActor);
	}

	if (!TargetActor || !IsAttacking())
	{
		return false;
	}

	const UWorld* const World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if (const float* const LastHit = SpinLastHitTimes.Find(TargetActor))
	{
		if (Now - *LastHit < SpinRehitInterval)
		{
			return false;
		}
	}

	SpinLastHitTimes.Add(TargetActor, Now);
	return true;
}

TSubclassOf<UDamageType> UIFPlayerCombatComponent::GetCurrentDamageTypeClass() const
{
	return bIsSpinning ? SpinDamageTypeClass : Super::GetCurrentDamageTypeClass();
}

void UIFPlayerCombatComponent::ClearSpinState()
{
	bIsSpinning = false;
	SpinLastHitTimes.Reset();

	if (StaminaComponent)
	{
		StaminaComponent->StopContinuousDrain();
	}
}

void UIFPlayerCombatComponent::StopSpinGracefully()
{
	ClearSpinState();

	if (SpinLoopSectionName.IsNone() || SpinEndSectionName.IsNone())
	{
		StopSpinImmediately();
		RestoreIdleStateUnlessDead();
		return;
	}

	UAnimInstance* const AnimInstance = GetAnimInstance();
	if (AnimInstance && SpinAttackMontage)
	{
		AnimInstance->Montage_SetNextSection(SpinLoopSectionName, SpinEndSectionName, SpinAttackMontage);
	}
}

void UIFPlayerCombatComponent::StopSpinImmediately()
{
	ClearSpinState();
	EndAttackCollision();
	ResetRegisteredAttackHits();

	UAnimInstance* const AnimInstance = GetAnimInstance();
	IFAnimMontageUtils::ClearMontageEndDelegate(AnimInstance, SpinAttackMontage);

	if (AnimInstance && SpinAttackMontage && AnimInstance->Montage_IsPlaying(SpinAttackMontage))
	{
		AnimInstance->Montage_Stop(SpinBlendOutTime, SpinAttackMontage);
	}
}

void UIFPlayerCombatComponent::HandleSpinMontageEnded(UAnimMontage* Montage, bool)
{
	if (Montage != SpinAttackMontage)
	{
		return;
	}

	StopSpinImmediately();
	RestoreIdleStateUnlessDead();
}

void UIFPlayerCombatComponent::HandleBlockReactionMontageEnded(UAnimMontage* Montage, bool)
{
	if (Montage != BlockReactionMontage)
	{
		return;
	}

	IFAnimMontageUtils::ClearMontageEndDelegate(GetAnimInstance(), BlockReactionMontage);
	ActiveBlockMontage = nullptr;

	if (IsBlocking())
	{
		TryPlayBlockMontage();
	}
}

void UIFPlayerCombatComponent::HandleStaminaDepleted()
{
	if (bIsSpinning)
	{
		StopSpinGracefully();
	}
}

bool UIFPlayerCombatComponent::TryPlayBlockMontage()
{
	UAnimInstance* const AnimInstance = GetAnimInstance();
	if (!AnimInstance || !BlockMontage)
	{
		ActiveBlockMontage = nullptr;
		return true;
	}

	IFAnimMontageUtils::ClearMontageEndDelegate(AnimInstance, BlockMontage);

	const float PlayLength = AnimInstance->Montage_Play(BlockMontage);
	if (PlayLength <= 0.f)
	{
		ActiveBlockMontage = nullptr;
		return false;
	}

	ActiveBlockMontage = BlockMontage;
	return true;
}

void UIFPlayerCombatComponent::PlayBlockReactionMontage()
{
	UAnimInstance* const AnimInstance = GetAnimInstance();
	if (!AnimInstance || !BlockReactionMontage)
	{
		return;
	}

	if (BlockMontage && AnimInstance->Montage_IsPlaying(BlockMontage))
	{
		AnimInstance->Montage_Stop(FMath::Max(MinBlendOutTime, BlockBlendOutTime), BlockMontage);
	}

	IFAnimMontageUtils::ClearMontageEndDelegate(AnimInstance, BlockReactionMontage);

	const float PlayLength = AnimInstance->Montage_Play(BlockReactionMontage);
	if (PlayLength > 0.f)
	{
		ActiveBlockMontage = BlockReactionMontage;

		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &UIFPlayerCombatComponent::HandleBlockReactionMontageEnded);
		AnimInstance->Montage_SetEndDelegate(EndDelegate, BlockReactionMontage);
	}
}

bool UIFPlayerCombatComponent::IsOwnerFacingTarget(AActor* TargetActor) const
{
	const AActor* const Owner = GetOwner();
	if (!Owner || !TargetActor)
	{
		return false;
	}

	const FVector DirectionToTarget = (TargetActor->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D();
	if (DirectionToTarget.IsNearlyZero())
	{
		return true;
	}

	return FVector::DotProduct(Owner->GetActorForwardVector().GetSafeNormal2D(), DirectionToTarget) >= BlockFacingDotThreshold;
}
