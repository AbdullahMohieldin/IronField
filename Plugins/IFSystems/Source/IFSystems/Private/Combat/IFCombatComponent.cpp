#include "Combat/IFCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/IFCombatTargetingUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IFAnimMontageUtils.h"
#include "Core/IFLog.h"
#include "GameFramework/Character.h"
#include "Stats/IFStaminaComponent.h"

UIFCombatComponent::UIFCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIFCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* const Owner = GetOwner();
	const ACharacter* const OwnerCharacter = Cast<ACharacter>(Owner);
	CachedMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
	StaminaComponent = Owner ? Owner->FindComponentByClass<UIFStaminaComponent>() : nullptr;
}

void UIFCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearReactionMontageDelegates();
	Super::EndPlay(EndPlayReason);
}

void UIFCombatComponent::ResetCombatState()
{
	if (IsDead())
	{
		return;
	}

	SetCombatState(ECombatState::Idle);
}

void UIFCombatComponent::CancelAttack()
{
	if (IsAttacking())
	{
		RestoreIdleStateUnlessDead();
	}
}

void UIFCombatComponent::HandleOwnerDeath()
{
	CancelAttack();
	ClearReactionMontageDelegates();
	SetCombatState(ECombatState::Dead);
}

void UIFCombatComponent::HandleOwnerRevived()
{
	if (IsDead())
	{
		SetCombatState(ECombatState::Idle);
	}
}

void UIFCombatComponent::ReceiveAttack(AActor* Instigator, float Damage, TSubclassOf<UDamageType> DamageTypeClass)
{
	AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	IFCombatTargetingUtils::ApplyDamageTo(Owner, Instigator, Damage, DamageTypeClass);

	if (IsDead())
	{
		return;
	}

	PlayHitReactionMontage();
}

void UIFCombatComponent::SetCombatState(ECombatState NewState)
{
	if (CombatState == NewState)
	{
		return;
	}

	const ECombatState PreviousState = CombatState;
	CombatState = NewState;
	OnCombatStateChanged.Broadcast(PreviousState, NewState);
}

UAnimInstance* UIFCombatComponent::GetAnimInstance() const
{
	return CachedMesh ? CachedMesh->GetAnimInstance() : nullptr;
}

bool UIFCombatComponent::HasUsableStamina(float Amount) const
{
	return !StaminaComponent || StaminaComponent->HasStamina(Amount);
}

void UIFCombatComponent::RestoreIdleStateUnlessDead()
{
	if (!IsDead())
	{
		SetCombatState(ECombatState::Idle);
	}
}

void UIFCombatComponent::PlayHitReactionMontage()
{
	if (!HitReactionMontage || IsDead() || IsAttacking())
	{
		return;
	}

	UAnimInstance* const AnimInstance = GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	ClearReactionMontageDelegates();

	const float Duration = AnimInstance->Montage_Play(HitReactionMontage);
	if (Duration <= 0.f)
	{
		return;
	}

	FOnMontageEnded EndedDelegate;
	EndedDelegate.BindUObject(this, &UIFCombatComponent::HandleHitReactionMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndedDelegate, HitReactionMontage);
}

void UIFCombatComponent::HandleHitReactionMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage == HitReactionMontage)
	{
		ClearReactionMontageDelegates();
		RestoreIdleStateUnlessDead();
	}
}

void UIFCombatComponent::ClearReactionMontageDelegates()
{
	if (!HitReactionMontage)
	{
		return;
	}

	if (UAnimInstance* const AnimInstance = GetAnimInstance())
	{
		IFAnimMontageUtils::ClearMontageEndDelegate(AnimInstance, HitReactionMontage);
	}
}
