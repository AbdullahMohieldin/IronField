#include "Combat/IFCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/IFBaseCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "TimerManager.h"

UIFCombatComponent::UIFCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIFCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AIFBaseCharacter>(GetOwner());
	if (OwnerCharacter.IsValid())
	{
		if (USkeletalMeshComponent* const MeshComp = OwnerCharacter->GetMesh())
		{
			CachedAnimInstance = MeshComp->GetAnimInstance();
		}
	}
}

void UIFCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool UIFCombatComponent::TryBlockIncomingDamage(float, AActor*, AController*)
{
	return false;
}

void UIFCombatComponent::PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator)
{
	if (CombatState == EIFCombatState::Dead || IsStunned())
	{
		return;
	}

	if (!OwnerCharacter.IsValid())
	{
		OwnerCharacter = Cast<AIFBaseCharacter>(GetOwner());
	}
	if (!OwnerCharacter.IsValid() || !OwnerCharacter->IsAlive())
	{
		return;
	}

	if (!IsValid(CachedAnimInstance))
	{
		if (USkeletalMeshComponent* const MeshComp = OwnerCharacter->GetMesh())
		{
			CachedAnimInstance = MeshComp->GetAnimInstance();
		}
	}

	if (!IsValid(CachedAnimInstance))
	{
		return;
	}

	const AActor* Attacker = DamageCauser;
	if (!IsValid(Attacker) && IsValid(EventInstigator))
	{
		Attacker = EventInstigator->GetPawn();
	}

	FVector OwnerForward2D = OwnerCharacter->GetActorForwardVector();
	OwnerForward2D.Z = 0.f;
	OwnerForward2D.Normalize();

	FVector OwnerRight2D = OwnerCharacter->GetActorRightVector();
	OwnerRight2D.Z = 0.f;
	OwnerRight2D.Normalize();

	FVector ToAttacker2D = OwnerForward2D;
	if (IsValid(Attacker) && Attacker != OwnerCharacter.Get())
	{
		ToAttacker2D = Attacker->GetActorLocation() - OwnerCharacter->GetActorLocation();
		ToAttacker2D.Z = 0.f;
		if (!ToAttacker2D.Normalize())
		{
			ToAttacker2D = OwnerForward2D;
		}
	}

	const float ForwardDot = FVector::DotProduct(OwnerForward2D, ToAttacker2D);
	const float RightDot = FVector::DotProduct(OwnerRight2D, ToAttacker2D);

	TSoftObjectPtr<UAnimMontage> SelectedSoftMontage;
	if (FMath::Abs(ForwardDot) >= FMath::Abs(RightDot))
	{
		SelectedSoftMontage = (ForwardDot >= 0.f) ? DirectionalHitReacts.HitReactFront : DirectionalHitReacts.HitReactBack;
	}
	else
	{
		SelectedSoftMontage = (RightDot >= 0.f) ? DirectionalHitReacts.HitReactRight : DirectionalHitReacts.HitReactLeft;
	}

	UAnimMontage* MontageToPlay = SelectedSoftMontage.LoadSynchronous();
	if (!IsValid(MontageToPlay))
	{
		MontageToPlay = DirectionalHitReacts.HitReactFront.LoadSynchronous();
	}

	if (IsValid(MontageToPlay))
	{
		SetCombatState(EIFCombatState::HitReact);
		const float Duration = CachedAnimInstance->Montage_Play(MontageToPlay, 1.f);
		const float FallbackDuration = (Duration > 0.f) ? Duration : 0.5f;

		if (UWorld* const World = GetWorld())
		{
			World->GetTimerManager().SetTimer(HitReactTimerHandle, this, &UIFCombatComponent::OnHitReactFinished, FallbackDuration, false);
		}
	}
}

void UIFCombatComponent::OnHitReactFinished()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactTimerHandle);
	}

	if (CombatState == EIFCombatState::HitReact)
	{
		SetCombatState(EIFCombatState::Idle);
	}
}

void UIFCombatComponent::HandleOwnerDeath()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactTimerHandle);
	}
	SetCombatState(EIFCombatState::Dead);
}

void UIFCombatComponent::HandleOwnerRevived()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactTimerHandle);
	}
	SetCombatState(EIFCombatState::Idle);
}

void UIFCombatComponent::SetCombatState(EIFCombatState NewState)
{
	if (CombatState == NewState)
	{
		return;
	}

	const EIFCombatState PreviousState = CombatState;
	CombatState = NewState;
	OnCombatStateChanged.Broadcast(PreviousState, NewState);
}