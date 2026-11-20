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
	ActiveFlinchMontage.Reset();
	Super::EndPlay(EndPlayReason);
}

bool UIFCombatComponent::TryBlockIncomingDamage(float, AActor*, AController*)
{
	return false;
}

void UIFCombatComponent::PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator, bool bIsHeavyStagger)
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

	UAnimMontage* const MontageToPlay = ResolveDirectionalHitReactMontage(DamageCauser, EventInstigator);
	if (!IsValid(MontageToPlay))
	{
		return;
	}

	if (!bIsHeavyStagger)
	{
		PlayAdditiveFlinchMontage(MontageToPlay);
		return;
	}

	ActiveFlinchMontage.Reset();
	SetCombatState(EIFCombatState::HitReact);
	const float Duration = CachedAnimInstance->Montage_Play(MontageToPlay, 1.f);
	const float FallbackDuration = (Duration > 0.f) ? Duration : 0.5f;

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HitReactTimerHandle, this, &UIFCombatComponent::OnHitReactFinished, FallbackDuration, false);
	}
}

UAnimMontage* UIFCombatComponent::ResolveDirectionalHitReactMontage(AActor* DamageCauser, AController* EventInstigator)
{
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

	UAnimMontage* MontageToPlay = SelectedSoftMontage.Get();
	if (!IsValid(MontageToPlay) && !SelectedSoftMontage.IsNull())
	{
		MontageToPlay = SelectedSoftMontage.LoadSynchronous();
	}

	if (!IsValid(MontageToPlay))
	{
		MontageToPlay = DirectionalHitReacts.HitReactFront.Get();
		if (!IsValid(MontageToPlay) && !DirectionalHitReacts.HitReactFront.IsNull())
		{
			MontageToPlay = DirectionalHitReacts.HitReactFront.LoadSynchronous();
		}
	}

	return MontageToPlay;
}

bool UIFCombatComponent::PlayAdditiveFlinchMontage(UAnimMontage* MontageToPlay)
{
	if (!IsValid(CachedAnimInstance) || !IsValid(MontageToPlay))
	{
		return false;
	}

	if (UAnimMontage* const PreviousFlinch = ActiveFlinchMontage.Get())
	{
		if (CachedAnimInstance->Montage_IsPlaying(PreviousFlinch))
		{
			CachedAnimInstance->Montage_Stop(0.05f, PreviousFlinch);
		}
	}

	// bStopAllMontages = false keeps active swings/blocks running uninterrupted.
	const float PlayLength = CachedAnimInstance->Montage_Play(
		MontageToPlay,
		1.f,
		EMontagePlayReturnType::MontageLength,
		0.f,
		false);

	if (PlayLength > 0.f)
	{
		ActiveFlinchMontage = MontageToPlay;
		return true;
	}

	ActiveFlinchMontage.Reset();
	return false;
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
	ActiveFlinchMontage.Reset();
	SetCombatState(EIFCombatState::Dead);
}

void UIFCombatComponent::HandleOwnerRevived()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactTimerHandle);
	}
	ActiveFlinchMontage.Reset();
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
	HandleCombatStateTransition(PreviousState, NewState);
	OnCombatStateChanged.Broadcast(PreviousState, NewState);
}