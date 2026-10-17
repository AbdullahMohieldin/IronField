#include "Combat/IFComboCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/IFCombatTargetingUtils.h"
#include "Components/BoxComponent.h"
#include "Core/IFAnimMontageUtils.h"
#include "Core/IFFeedbackUtils.h"
#include "Core/IFLog.h"
#include "GameFramework/Actor.h"
#include "Stats/IFHealthComponent.h"
#include "Stats/IFStaminaComponent.h"

namespace
{
	constexpr float AttackBlendOutTime = 0.15f;
}

UIFComboCombatComponent::UIFComboCombatComponent()
{
}

void UIFComboCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveWeaponCollisionBox();

	if (WeaponCollisionBox)
	{
		if (!WeaponCollisionBox->GetAttachParent())
		{
			UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s WeaponCollisionBox has no attach parent; parent it to the weapon in Blueprint."),
				*GetNameSafe(GetOwner()));
		}

		WeaponCollisionBox->OnComponentBeginOverlap.AddDynamic(this, &UIFComboCombatComponent::HandleWeaponBoxBeginOverlap);
		SetWeaponCollisionEnabled(false);
	}
	else if (RequiresWeaponCollisionBox())
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s requires a weapon collision box but none was found."),
			*GetNameSafe(GetOwner()));
	}
}

void UIFComboCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (WeaponCollisionBox)
	{
		WeaponCollisionBox->OnComponentBeginOverlap.RemoveAll(this);
	}
	SetWeaponCollisionEnabled(false);

	Super::EndPlay(EndPlayReason);
}

void UIFComboCombatComponent::StartAttack()
{
	if (IsDead() || IsBlocking())
	{
		return;
	}

	if (IsAttacking())
	{
		if (CanQueueComboAttack())
		{
			bComboQueued = true;
		}
		return;
	}

	TryPlayAttackMontage(0);
}

void UIFComboCombatComponent::ResetCombatState()
{
	CancelAttack();
	Super::ResetCombatState();
}

void UIFComboCombatComponent::CancelAttack()
{
	SetWeaponCollisionEnabled(false);
	bAttackCollisionActive = false;
	ResetRegisteredAttackHits();

	ClearAttackMontageDelegate();

	if (ActiveAttackMontage)
	{
		if (UAnimInstance* const AnimInstance = GetAnimInstance())
		{
			AnimInstance->Montage_Stop(AttackBlendOutTime, ActiveAttackMontage);
		}
	}

	ActiveAttackMontage = nullptr;
	bComboQueued = false;
	CurrentComboIndex = 0;

	Super::CancelAttack();
}

void UIFComboCombatComponent::BeginAttackCollision()
{
	bAttackCollisionActive = true;
	ActiveAttackDamage = GetCurrentAttackDamage() * GetDamageScale();
	ActiveDamageTypeClass = GetCurrentDamageTypeClass();
	ResetRegisteredAttackHits();
	SetWeaponCollisionEnabled(true);
}

void UIFComboCombatComponent::EndAttackCollision()
{
	bAttackCollisionActive = false;
	ActiveAttackDamage = 0.f;
	ActiveDamageTypeClass = nullptr;
	ResetRegisteredAttackHits();
	SetWeaponCollisionEnabled(false);
}

float UIFComboCombatComponent::GetCurrentAttackDamage() const
{
	return ComboSteps.IsValidIndex(CurrentComboIndex) ? ComboSteps[CurrentComboIndex].Damage : 0.f;
}

TSubclassOf<UDamageType> UIFComboCombatComponent::GetCurrentDamageTypeClass() const
{
	return ComboSteps.IsValidIndex(CurrentComboIndex) ? ComboSteps[CurrentComboIndex].DamageTypeClass : nullptr;
}

bool UIFComboCombatComponent::CanQueueComboAttack() const
{
	return HasNextComboStep();
}

bool UIFComboCombatComponent::CanPlayAttackMontage(int32 ComboIndex) const
{
	if (!ComboSteps.IsValidIndex(ComboIndex) || !ComboSteps[ComboIndex].AttackMontage)
	{
		return false;
	}

	return HasUsableStamina(GetComboStaminaCost(ComboIndex));
}

bool UIFComboCombatComponent::TryPlayAttackMontage(int32 ComboIndex)
{
	if (!CanPlayAttackMontage(ComboIndex))
	{
		return false;
	}

	UAnimInstance* const AnimInstance = GetAnimInstance();
	if (!AnimInstance)
	{
		return false;
	}

	UAnimMontage* const AttackMontage = ComboSteps[ComboIndex].AttackMontage;
	const float Cost = GetComboStaminaCost(ComboIndex);
	if (StaminaComponent && Cost > 0.f)
	{
		StaminaComponent->TryConsumeStamina(Cost);
	}

	ClearAttackMontageDelegate();

	const float Duration = AnimInstance->Montage_Play(AttackMontage);
	if (Duration <= 0.f)
	{
		return false;
	}

	FOnMontageEnded MontageEndedDelegate;
	MontageEndedDelegate.BindUObject(this, &UIFComboCombatComponent::HandleAttackMontageEnded);
	AnimInstance->Montage_SetEndDelegate(MontageEndedDelegate, AttackMontage);

	CurrentComboIndex = ComboIndex;
	ActiveAttackMontage = AttackMontage;
	bComboQueued = false;
	SetCombatState(ECombatState::Attacking);
	return true;
}

void UIFComboCombatComponent::ClearAttackMontageDelegate()
{
	if (!ActiveAttackMontage)
	{
		return;
	}

	if (UAnimInstance* const AnimInstance = GetAnimInstance())
	{
		IFAnimMontageUtils::ClearMontageEndDelegate(AnimInstance, ActiveAttackMontage);
	}
}

void UIFComboCombatComponent::HandleAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (Montage != ActiveAttackMontage)
	{
		return;
	}

	ActiveAttackMontage = nullptr;
	SetWeaponCollisionEnabled(false);
	bAttackCollisionActive = false;
	ResetRegisteredAttackHits();

	if (IsDead())
	{
		return;
	}

	if (bInterrupted)
	{
		bComboQueued = false;
		CurrentComboIndex = 0;
		RestoreIdleStateUnlessDead();
		return;
	}

	const int32 NextComboIndex = CurrentComboIndex + 1;
	if (ComboSteps.IsValidIndex(NextComboIndex))
	{
		const bool bWantsNext = bComboQueued || (FMath::FRand() < GetComboContinueChance(CurrentComboIndex));
		if (bWantsNext && TryPlayAttackMontage(NextComboIndex))
		{
			return;
		}
	}

	bComboQueued = false;
	CurrentComboIndex = 0;
	RestoreIdleStateUnlessDead();
}

float UIFComboCombatComponent::GetComboStaminaCost(int32 ComboIndex) const
{
	return ComboSteps.IsValidIndex(ComboIndex) ? ComboSteps[ComboIndex].StaminaCost : 0.f;
}

void UIFComboCombatComponent::HandleWeaponBoxBeginOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	ResolveAttackHit(OtherActor);
}

void UIFComboCombatComponent::ResolveAttackHit(AActor* TargetActor)
{
	UIFHealthComponent* const TargetHealth = GetValidActiveAttackTargetHealth(TargetActor);
	if (!TargetHealth)
	{
		return;
	}

	if (!TryRegisterAttackHit(TargetActor))
	{
		return;
	}

	IFCombatTargetingUtils::DeliverDamage(TargetActor, GetOwner(), ActiveAttackDamage, ActiveDamageTypeClass);
	PlayHitFeedback(TargetActor);
}

UIFHealthComponent* UIFComboCombatComponent::GetValidActiveAttackTargetHealth(AActor* TargetActor) const
{
	if (!bAttackCollisionActive || !TargetActor)
	{
		return nullptr;
	}

	return IFCombatTargetingUtils::GetValidAttackTargetHealth(GetOwner(), TargetActor);
}

bool UIFComboCombatComponent::TryRegisterAttackHit(AActor* TargetActor)
{
	if (!TargetActor || RegisteredAttackHits.Contains(TargetActor))
	{
		return false;
	}

	RegisteredAttackHits.Add(TargetActor);
	return true;
}

void UIFComboCombatComponent::ResetRegisteredAttackHits()
{
	RegisteredAttackHits.Reset();
}

void UIFComboCombatComponent::SetWeaponCollisionEnabled(bool bEnabled) const
{
	if (!WeaponCollisionBox)
	{
		return;
	}

	WeaponCollisionBox->SetGenerateOverlapEvents(bEnabled);
	WeaponCollisionBox->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void UIFComboCombatComponent::ResolveWeaponCollisionBox()
{
	WeaponCollisionBox = nullptr;

	const AActor* const Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<UBoxComponent*> Boxes;
	Owner->GetComponents<UBoxComponent>(Boxes);

	// Preferred: look for an explicit IFWeapon tag on the collision box.
	for (UBoxComponent* const Box : Boxes)
	{
		if (Box && Box->ComponentHasTag(TEXT("IFWeapon")))
		{
			WeaponCollisionBox = Box;
			break;
		}
	}

	// Fallback heuristic: a parented box wins over an unparented root-level box.
	if (!WeaponCollisionBox)
	{
		for (UBoxComponent* const Box : Boxes)
		{
			if (Box && Box->GetAttachParent())
			{
				WeaponCollisionBox = Box;
				break;
			}
		}
	}

	if (!WeaponCollisionBox && Boxes.Num() > 0)
	{
		WeaponCollisionBox = Boxes[0];
	}
}

void UIFComboCombatComponent::PlayHitFeedback(const AActor* TargetActor) const
{
	const FVector Location = TargetActor ? TargetActor->GetActorLocation() : GetOwner()->GetActorLocation();
	IFFeedbackUtils::PlayAtLocation(GetWorld(), HitSound, HitVFX, Location);
}
