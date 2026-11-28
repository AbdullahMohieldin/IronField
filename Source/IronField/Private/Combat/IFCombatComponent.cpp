#include "Combat/IFCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/IFBaseCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/IFGameTypes.h"
#include "Core/IFLog.h"
#include "DrawDebugHelpers.h"
#include "Engine/AssetManager.h"
#include "Engine/OverlapResult.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"
#include "Stats/IFStaminaComponent.h"
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
		Stamina = OwnerCharacter->FindComponentByClass<UIFStaminaComponent>();
	}

	if (!CombatData.IsNull())
	{
		DataLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			CombatData.ToSoftObjectPath(),
			FStreamableDelegate::CreateUObject(this, &UIFCombatComponent::HandleCombatDataLoaded));
	}
	else
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s: CombatData is not assigned."), *GetNameSafe(GetOwner()));
	}
}

void UIFCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bCombatReady = false;
	InvalidateMontageExecution();
	ActiveFlinchMontage.Reset();

	if (DataLoadHandle.IsValid())
	{
		DataLoadHandle->CancelHandle();
		DataLoadHandle.Reset();
	}
	if (StepAssetsLoadHandle.IsValid())
	{
		StepAssetsLoadHandle->CancelHandle();
		StepAssetsLoadHandle.Reset();
	}

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactTimerHandle);
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	CloseHitWindow();
	RestoreAnimTickOption();

	Super::EndPlay(EndPlayReason);
}

void UIFCombatComponent::GatherRequiredAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const
{
	if (!IsValid(LoadedCombatData))
	{
		return;
	}

	for (const FIFMeleeAttackStep& Step : LoadedCombatData->ComboSteps)
	{
		if (!Step.Montage.IsNull())
		{
			OutAssetPaths.Add(Step.Montage.ToSoftObjectPath());
		}
		if (!Step.HitVFX.IsNull())
		{
			OutAssetPaths.Add(Step.HitVFX.ToSoftObjectPath());
		}
		if (!Step.HitCascadeVFX.IsNull())
		{
			OutAssetPaths.Add(Step.HitCascadeVFX.ToSoftObjectPath());
		}
		if (!Step.HitSound.IsNull())
		{
			OutAssetPaths.Add(Step.HitSound.ToSoftObjectPath());
		}
	}

	if (!LoadedCombatData->DirectionalHitReacts.HitReactFront.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->DirectionalHitReacts.HitReactFront.ToSoftObjectPath());
	}
	if (!LoadedCombatData->DirectionalHitReacts.HitReactBack.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->DirectionalHitReacts.HitReactBack.ToSoftObjectPath());
	}
	if (!LoadedCombatData->DirectionalHitReacts.HitReactLeft.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->DirectionalHitReacts.HitReactLeft.ToSoftObjectPath());
	}
	if (!LoadedCombatData->DirectionalHitReacts.HitReactRight.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->DirectionalHitReacts.HitReactRight.ToSoftObjectPath());
	}
}

void UIFCombatComponent::HandleCombatDataLoaded()
{
	LoadedCombatData = CombatData.Get();
	DataLoadHandle.Reset();

	if (!IsValid(LoadedCombatData))
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s: combat data failed to load."), *GetNameSafe(GetOwner()));
		return;
	}

	TArray<FSoftObjectPath> AssetPaths;
	GatherRequiredAssetPaths(AssetPaths);

	if (AssetPaths.IsEmpty())
	{
		HandleStepAssetsLoaded();
		return;
	}

	StepAssetsLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPaths,
		FStreamableDelegate::CreateUObject(this, &UIFCombatComponent::HandleStepAssetsLoaded));
}

void UIFCombatComponent::HandleStepAssetsLoaded()
{
	bCombatReady = AreRequiredAssetsLoaded();
	if (!bCombatReady)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s: combat unavailable; combo is empty or a required montage is missing."), *GetNameSafe(GetOwner()));
	}

	if (StepAssetsLoadHandle.IsValid())
	{
		TArray<FSoftObjectPath> RequestedAssets;
		StepAssetsLoadHandle->GetRequestedAssets(RequestedAssets);
		for (const FSoftObjectPath& AssetPath : RequestedAssets)
		{
			if (!IsValid(AssetPath.ResolveObject()))
			{
				UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s: combat asset failed to load: %s."), *GetNameSafe(GetOwner()), *AssetPath.ToString());
			}
		}
	}
}

bool UIFCombatComponent::AreRequiredAssetsLoaded() const
{
	if (!IsValid(LoadedCombatData) || LoadedCombatData->ComboSteps.IsEmpty())
	{
		return false;
	}

	for (const FIFMeleeAttackStep& Step : LoadedCombatData->ComboSteps)
	{
		if (!IsValid(Step.Montage.Get()))
		{
			return false;
		}
	}

	return true;
}

bool UIFCombatComponent::TryStartAttack()
{
	if (!bCombatReady || !IsValid(LoadedCombatData) || LoadedCombatData->ComboSteps.IsEmpty())
	{
		return false;
	}
	if (!IsIdle() && !IsBlocking())
	{
		return false;
	}

	return PlayStep(GetNextStepIndex());
}

bool UIFCombatComponent::TryStartAttackStep(int32 StepIndex)
{
	if (!bCombatReady || !IsIdle())
	{
		return false;
	}

	return PlayStep(StepIndex);
}

int32 UIFCombatComponent::GetNextStepIndex() const
{
	if (!IsValid(LoadedCombatData) || LoadedCombatData->ComboSteps.IsEmpty())
	{
		return INDEX_NONE;
	}

	const int32 BaseIndex = (CurrentStepIndex != INDEX_NONE) ? CurrentStepIndex : LastFinishedStepIndex;
	const int32 NextIndex = (BaseIndex == INDEX_NONE) ? 0 : BaseIndex + 1;
	return (NextIndex >= LoadedCombatData->ComboSteps.Num()) ? 0 : NextIndex;
}

const FIFMeleeAttackStep* UIFCombatComponent::GetCurrentStep() const
{
	if (!IsValid(LoadedCombatData) || !LoadedCombatData->ComboSteps.IsValidIndex(CurrentStepIndex))
	{
		return nullptr;
	}

	return &LoadedCombatData->ComboSteps[CurrentStepIndex];
}

bool UIFCombatComponent::PlayAttackMontage(UAnimMontage* Montage)
{
	if (!bCombatReady || !IsValid(CachedAnimInstance) || !IsValid(Montage)
		|| !OwnerCharacter.IsValid() || OwnerCharacter->IsDead()
		|| (!IsIdle() && !IsAttacking() && !IsBlocking()))
	{
		return false;
	}

	UAnimMontage* const OutgoingMontage = CurrentMontage;
	const int32 OutgoingInstanceID = CurrentMontageInstanceID;
	const EIFCombatState StartingState = GetCombatState();
	// Clear active ID first because Montage_Play synchronously fires blend-out on the outgoing montage.
	InvalidateMontageExecution();
	EnableAttackAnimTickOption();

	const float PlayLength = CachedAnimInstance->Montage_Play(Montage, 1.f);
	FAnimMontageInstance* const Instance = PlayLength > 0.f && IsValid(CachedAnimInstance)
		? CachedAnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;

	// Montage_Play can synchronously fire blend-out delegates or hit-react state changes; bail if re-entered.
	if (!bCombatReady || !OwnerCharacter.IsValid() || OwnerCharacter->IsDead()
		|| GetCombatState() != StartingState || CurrentMontageInstanceID != INDEX_NONE)
	{
		if (!IsAttacking())
		{
			RestoreAnimTickOption();
		}
		return false;
	}

	if (!Instance)
	{
		FAnimMontageInstance* const OutgoingInstance = IsValid(CachedAnimInstance) && IsValid(OutgoingMontage)
			? CachedAnimInstance->GetActiveInstanceForMontage(OutgoingMontage) : nullptr;
		if (OutgoingInstance && OutgoingInstance->GetInstanceID() == OutgoingInstanceID)
		{
			CachedAnimInstance->Montage_Stop(0.1f, OutgoingMontage);
		}
		if (IsAttacking() && CurrentMontageInstanceID == INDEX_NONE)
		{
			FinishCombo();
		}
		else if (!IsAttacking())
		{
			RestoreAnimTickOption();
		}
		return false;
	}

	CurrentMontage = Montage;
	CurrentMontageInstanceID = Instance->GetInstanceID();
	Instance->OnMontageBlendingOutStarted.BindUObject(
		this, &UIFCombatComponent::HandleOwnedMontageBlendingOut, CurrentMontageInstanceID);
	return true;
}

void UIFCombatComponent::InvalidateMontageExecution()
{
	CurrentMontageInstanceID = INDEX_NONE;
	CurrentMontage = nullptr;
}

bool UIFCombatComponent::ExecuteAttackStep(const FIFMeleeAttackStep& Step, int32 InStepIndex)
{
	UAnimMontage* const Montage = Step.Montage.Get();
	if (!bCombatReady || !IsValid(Montage) || !IsValid(CachedAnimInstance) || !OwnerCharacter.IsValid())
	{
		return false;
	}

	if (IsValid(Stamina) && !Stamina->HasStamina(Step.StaminaCost))
	{
		return false;
	}

	CloseHitWindow();
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	if (!PlayAttackMontage(Montage))
	{
		return false;
	}

	if (IsValid(Stamina))
	{
		Stamina->TryConsumeStamina(Step.StaminaCost);
	}

	CurrentStepIndex = InStepIndex;
	SetCombatState(EIFCombatState::Attacking);
	HandleStepStarted();
	return true;
}

bool UIFCombatComponent::PlayStep(int32 StepIndex)
{
	if (!IsValid(LoadedCombatData) || !LoadedCombatData->ComboSteps.IsValidIndex(StepIndex))
	{
		return false;
	}

	return ExecuteAttackStep(LoadedCombatData->ComboSteps[StepIndex], StepIndex);
}

void UIFCombatComponent::HandleOwnedMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted, int32 InstanceID)
{
	// Ignore blend-out callbacks from an earlier montage instance.
	if (InstanceID == INDEX_NONE || InstanceID != CurrentMontageInstanceID
		|| Montage != CurrentMontage || !IsAttacking())
	{
		return;
	}

	HandleMontageBlendingOut(Montage, bInterrupted);
}

void UIFCombatComponent::HandleMontageBlendingOut(UAnimMontage*, bool bInterrupted)
{
	const int32 FinishingInstanceID = CurrentMontageInstanceID;
	if (!bInterrupted && TryBufferedAdvance())
	{
		return;
	}

	if (CurrentMontageInstanceID == FinishingInstanceID)
	{
		FinishCombo();
	}
}

void UIFCombatComponent::FinishCombo()
{
	LastFinishedStepIndex = CurrentStepIndex;
	InvalidateMontageExecution();
	CurrentStepIndex = INDEX_NONE;
	CloseHitWindow();
	RestoreAnimTickOption();
	SetCombatState(EIFCombatState::Idle);
	StartComboResetTimer();
}

void UIFCombatComponent::StartComboResetTimer()
{
	UWorld* const World = GetWorld();
	if (!IsValid(World) || !IsValid(LoadedCombatData))
	{
		HandleComboReset();
		return;
	}

	if (LoadedCombatData->ComboResetSeconds > 0.f)
	{
		World->GetTimerManager().SetTimer(
			ComboResetTimerHandle,
			this,
			&UIFCombatComponent::HandleComboReset,
			LoadedCombatData->ComboResetSeconds,
			false);
	}
	else
	{
		HandleComboReset();
	}
}

void UIFCombatComponent::HandleComboReset()
{
	CurrentStepIndex = INDEX_NONE;
	LastFinishedStepIndex = INDEX_NONE;
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

	if (!bIsHeavyStagger)
	{
		TriggerAdditiveFlinch(DamageCauser, EventInstigator);
		return;
	}

	if (IsAttacking() && IsValid(LoadedCombatData) && !LoadedCombatData->bAllowHitReactDuringAttack)
	{
		TriggerAdditiveFlinch(DamageCauser, EventInstigator);
		return;
	}

	const UWorld* const World = GetWorld();
	if (IsValid(LoadedCombatData) && IsValid(World) && World->GetTimeSeconds() < NextHitReactAllowedTime)
	{
		TriggerAdditiveFlinch(DamageCauser, EventInstigator);
		return;
	}

	UAnimMontage* const MontageToPlay = ResolveDirectionalHitReactMontage(DamageCauser, EventInstigator);
	if (!IsValid(MontageToPlay))
	{
		return;
	}

	// Heavy staggers intentionally cancel in-progress swings; close hit window before stopping the outgoing montage.
	if (IsAttacking())
	{
		UAnimMontage* const OutgoingMontage = CurrentMontage;
		InvalidateMontageExecution();
		CloseHitWindow();
		if (IsValid(OutgoingMontage))
		{
			CachedAnimInstance->Montage_Stop(0.1f, OutgoingMontage);
		}
	}

	CurrentStepIndex = INDEX_NONE;
	LastFinishedStepIndex = INDEX_NONE;
	if (UWorld* const MutableWorld = GetWorld())
	{
		MutableWorld->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	ActiveFlinchMontage.Reset();
	SetCombatState(EIFCombatState::HitReact);
	const float Duration = CachedAnimInstance->Montage_Play(
		MontageToPlay,
		1.f,
		EMontagePlayReturnType::Duration);
	const float FallbackDuration = (Duration > 0.f) ? Duration : 0.5f;

	if (UWorld* const MutableWorld = GetWorld())
	{
		MutableWorld->GetTimerManager().SetTimer(
			HitReactTimerHandle,
			this,
			&UIFCombatComponent::OnHitReactFinished,
			FallbackDuration,
			false);
	}

	if (IsValid(LoadedCombatData) && LoadedCombatData->HitReactCooldownSeconds > 0.f && IsValid(World))
	{
		NextHitReactAllowedTime = World->GetTimeSeconds() + LoadedCombatData->HitReactCooldownSeconds;
	}
}

void UIFCombatComponent::TriggerAdditiveFlinch(AActor* DamageCauser, AController* EventInstigator)
{
	const UWorld* const World = GetWorld();
	const float Now = IsValid(World) ? World->GetTimeSeconds() : 0.f;
	if (IsValid(World) && Now < NextAdditiveFlinchAllowedTime)
	{
		return;
	}

	UAnimMontage* const MontageToPlay = ResolveDirectionalHitReactMontage(DamageCauser, EventInstigator);
	if (PlayAdditiveFlinchMontage(MontageToPlay))
	{
		const float Cooldown = IsValid(LoadedCombatData) ? LoadedCombatData->AdditiveFlinchCooldownSeconds : 0.25f;
		if (IsValid(World) && Cooldown > 0.f)
		{
			NextAdditiveFlinchAllowedTime = Now + Cooldown;
		}
	}
}

UAnimMontage* UIFCombatComponent::ResolveDirectionalHitReactMontage(AActor* DamageCauser, AController* EventInstigator)
{
	if (!IsValid(LoadedCombatData))
	{
		return nullptr;
	}
	const FIFDirectionalHitReactConfig& HitReactConfig = LoadedCombatData->DirectionalHitReacts;

	const AActor* Attacker = DamageCauser;
	if (!IsValid(Attacker) && IsValid(EventInstigator))
	{
		Attacker = EventInstigator->GetPawn();
	}

	const FVector OwnerForward2D = OwnerCharacter->GetActorForwardVector().GetSafeNormal2D();
	const FVector OwnerRight2D = OwnerCharacter->GetActorRightVector().GetSafeNormal2D();

	FVector ToAttacker2D = OwnerForward2D;
	if (IsValid(Attacker) && Attacker != OwnerCharacter.Get())
	{
		const FVector Candidate = (Attacker->GetActorLocation() - OwnerCharacter->GetActorLocation()).GetSafeNormal2D();
		if (!Candidate.IsNearlyZero())
		{
			ToAttacker2D = Candidate;
		}
	}

	const float ForwardDot = FVector::DotProduct(OwnerForward2D, ToAttacker2D);
	const float RightDot = FVector::DotProduct(OwnerRight2D, ToAttacker2D);

	TSoftObjectPtr<UAnimMontage> SelectedSoftMontage;
	if (FMath::Abs(ForwardDot) >= FMath::Abs(RightDot))
	{
		SelectedSoftMontage = (ForwardDot >= 0.f) ? HitReactConfig.HitReactFront : HitReactConfig.HitReactBack;
	}
	else
	{
		SelectedSoftMontage = (RightDot >= 0.f) ? HitReactConfig.HitReactRight : HitReactConfig.HitReactLeft;
	}

	UAnimMontage* MontageToPlay = SelectedSoftMontage.Get();
	if (!IsValid(MontageToPlay))
	{
		MontageToPlay = HitReactConfig.HitReactFront.Get();
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

	// bStopAllMontages = false skips group stopping so additive HitReactSlot flinches overlap active DefaultSlot attacks.
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
	InvalidateMontageExecution();
	CurrentStepIndex = INDEX_NONE;
	LastFinishedStepIndex = INDEX_NONE;
	ActiveFlinchMontage.Reset();
	CloseHitWindow();
	RestoreAnimTickOption();

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactTimerHandle);
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	SetCombatState(EIFCombatState::Dead);
}

void UIFCombatComponent::HandleOwnerRevived()
{
	InvalidateMontageExecution();
	CurrentStepIndex = INDEX_NONE;
	LastFinishedStepIndex = INDEX_NONE;
	NextHitReactAllowedTime = 0.f;
	NextAdditiveFlinchAllowedTime = 0.f;
	ActiveFlinchMontage.Reset();

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitReactTimerHandle);
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
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
	HandleCombatStateTransition(PreviousState, NewState);
	OnCombatStateChanged.Broadcast(PreviousState, NewState);
}

void UIFCombatComponent::HandleCombatStateTransition(EIFCombatState PreviousState, EIFCombatState NewState)
{
	if (PreviousState == EIFCombatState::Attacking && NewState != EIFCombatState::Attacking)
	{
		CloseHitWindow();
		RestoreAnimTickOption();
		if (IsValid(Stamina))
		{
			Stamina->ResetRegenDelay();
		}
	}

	if (!IsValid(Stamina))
	{
		return;
	}

	switch (NewState)
	{
	case EIFCombatState::Attacking:
	case EIFCombatState::Dead:
		Stamina->SetRegenRateMultiplier(0.f);
		break;

	case EIFCombatState::Stunned:
		Stamina->ResetRegenDelay();
		Stamina->SetRegenRateMultiplier(1.f);
		break;

	default:
		Stamina->SetRegenRateMultiplier(1.f);
		break;
	}
}

void UIFCombatComponent::OpenHitWindow()
{
	if (!IsAttacking())
	{
		return;
	}
	const FIFMeleeAttackStep* const Step = GetCurrentStep();
	UWorld* const World = GetWorld();
	if (!Step || !IsValid(World))
	{
		return;
	}

	HitActorsThisWindow.Reset();
	BuildSamplePoints(PreviousSamplePoints);
	bHitWindowOpen = true;

	const FCollisionShape Shape = FCollisionShape::MakeSphere(Step->HitRadius);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFMeleeOverlap), false);
	QueryParams.AddIgnoredActor(GetOwner());

	FCollisionObjectQueryParams ObjectParams = IFCollision::MakePawnObjectQueryParams();
	ObjectParams.AddObjectTypesToQuery(IFCollision::Damageable);

	for (const FVector& Point : PreviousSamplePoints)
	{
		ScratchOverlaps.Reset();
		World->OverlapMultiByObjectType(ScratchOverlaps, Point, FQuat::Identity, ObjectParams, Shape, QueryParams);
		for (const FOverlapResult& Overlap : ScratchOverlaps)
		{
			ProcessHit(Overlap.GetActor(), Point);
		}

		if (bDrawDebugHitWindow)
		{
			DrawDebugSphere(World, Point, Step->HitRadius, 12, FColor::Red, false, 0.f);
		}
	}
}

void UIFCombatComponent::UpdateHitWindow(float)
{
	if (!bHitWindowOpen || !IsAttacking())
	{
		CloseHitWindow();
		return;
	}
	const FIFMeleeAttackStep* const Step = GetCurrentStep();
	UWorld* const World = GetWorld();
	if (!Step || !IsValid(World))
	{
		return;
	}

	BuildSamplePoints(CurrentSamplePoints);
	if (CurrentSamplePoints.Num() != PreviousSamplePoints.Num())
	{
		PreviousSamplePoints = CurrentSamplePoints;
		return;
	}

	const FCollisionShape Shape = FCollisionShape::MakeSphere(Step->HitRadius);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFMeleeSweep), false);
	QueryParams.AddIgnoredActor(GetOwner());

	FCollisionObjectQueryParams ObjectParams = IFCollision::MakePawnObjectQueryParams();
	ObjectParams.AddObjectTypesToQuery(IFCollision::Damageable);

	for (int32 SampleIndex = 0; SampleIndex < CurrentSamplePoints.Num(); ++SampleIndex)
	{
		ScratchHits.Reset();
		World->SweepMultiByObjectType(
			ScratchHits,
			PreviousSamplePoints[SampleIndex],
			CurrentSamplePoints[SampleIndex],
			FQuat::Identity,
			ObjectParams,
			Shape,
			QueryParams);
		for (const FHitResult& Hit : ScratchHits)
		{
			ProcessHit(Hit.GetActor(), Hit.ImpactPoint);
		}

		if (bDrawDebugHitWindow)
		{
			DrawDebugLine(World, PreviousSamplePoints[SampleIndex], CurrentSamplePoints[SampleIndex], FColor::Orange, false, 0.f);
			DrawDebugSphere(World, CurrentSamplePoints[SampleIndex], Step->HitRadius, 12, FColor::Red, false, 0.f);
		}
	}

	Swap(PreviousSamplePoints, CurrentSamplePoints);
}

void UIFCombatComponent::CloseHitWindow()
{
	bHitWindowOpen = false;
	HitActorsThisWindow.Reset();
	PreviousSamplePoints.Reset();
	CurrentSamplePoints.Reset();
}

void UIFCombatComponent::EnableAttackAnimTickOption()
{
	if (USkeletalMeshComponent* const MeshComp = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr)
	{
		if (!bHasSavedAnimTickOption)
		{
			SavedAnimTickOption = MeshComp->VisibilityBasedAnimTickOption;
			bHasSavedAnimTickOption = true;
		}
		// Refresh bones off-screen during attacks so melee socket sweeps stay accurate without calling RefreshBoneTransforms inside AnimNotifies.
		MeshComp->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

void UIFCombatComponent::RestoreAnimTickOption()
{
	if (!bHasSavedAnimTickOption)
	{
		return;
	}

	bHasSavedAnimTickOption = false;
	if (USkeletalMeshComponent* const MeshComp = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr)
	{
		MeshComp->VisibilityBasedAnimTickOption = SavedAnimTickOption;
	}
}

void UIFCombatComponent::BuildSamplePoints(TArray<FVector, TInlineAllocator<16>>& OutPoints) const
{
	OutPoints.Reset();

	const FIFMeleeAttackStep* const Step = GetCurrentStep();
	USkeletalMeshComponent* const Mesh = OwnerCharacter.IsValid() ? OwnerCharacter->GetMesh() : nullptr;
	if (!Step || !IsValid(Mesh))
	{
		return;
	}

	const FVector StartLocation = Mesh->GetSocketTransform(Step->HitStartSocket, RTS_World).GetLocation();

	const bool bBladeSweep = Step->HitEndSocket != NAME_None
		&& Step->HitEndSocket != Step->HitStartSocket
		&& Step->TraceSampleCount >= 2;
	if (!bBladeSweep)
	{
		OutPoints.Add(StartLocation);
		return;
	}

	const FVector EndLocation = Mesh->GetSocketTransform(Step->HitEndSocket, RTS_World).GetLocation();
	const int32 SampleCount = FMath::Clamp(Step->TraceSampleCount, 2, 16);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const float Alpha = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount - 1);
		OutPoints.Add(FMath::Lerp(StartLocation, EndLocation, Alpha));
	}
}

void UIFCombatComponent::ProcessHit(AActor* Target, const FVector& HitLocation)
{
	const FIFMeleeAttackStep* const Step = GetCurrentStep();
	UWorld* const World = GetWorld();
	if (!Step || !IsValid(World) || !IsValid(Target) || Target == GetOwner() || HitActorsThisWindow.Contains(Target))
	{
		return;
	}
	if (!IFTeamUtils::AreHostile(GetOwner(), Target))
	{
		return;
	}

	// Trace to Pawn center so low sword tips near stairs/ramps do not false-occlude against walkable floors.
	const FVector LosEnd = (Target->IsA<APawn>() || HitLocation.IsNearlyZero())
		? Target->GetActorLocation()
		: HitLocation;
	FHitResult LineOfSight;
	FCollisionQueryParams LosParams(SCENE_QUERY_STAT(IFMeleeLOS), false);
	LosParams.AddIgnoredActor(GetOwner());
	if (World->LineTraceSingleByChannel(LineOfSight, GetOwner()->GetActorLocation(), LosEnd, ECC_Visibility, LosParams)
		&& LineOfSight.GetActor() != Target)
	{
		return;
	}

	HitActorsThisWindow.Add(Target);

	AController* InstigatorController = nullptr;
	if (const APawn* const OwnerPawn = Cast<APawn>(GetOwner()))
	{
		InstigatorController = OwnerPawn->GetController();
	}
	const TSubclassOf<UDamageType> DamageTypeClass = Step->bCausesHeavyStagger
		? TSubclassOf<UDamageType>(UIFHeavyMeleeDamageType::StaticClass())
		: TSubclassOf<UDamageType>(UDamageType::StaticClass());
	const float AppliedDamage = UGameplayStatics::ApplyDamage(
		Target, Step->Damage, InstigatorController, GetOwner(), DamageTypeClass);

	if (AppliedDamage > 0.f)
	{
		const FVector EffectLocation = !HitLocation.IsNearlyZero() ? HitLocation : Target->GetActorLocation();
		const FRotator HitRotation = (EffectLocation - GetOwner()->GetActorLocation()).GetSafeNormal().Rotation();
		if (UNiagaraSystem* const HitVFX = Step->HitVFX.Get())
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, HitVFX, EffectLocation, HitRotation);
		}
		else if (UParticleSystem* const HitCascadeVFX = Step->HitCascadeVFX.Get())
		{
			UGameplayStatics::SpawnEmitterAtLocation(World, HitCascadeVFX, EffectLocation, HitRotation, true);
		}

		if (USoundBase* const HitSound = Step->HitSound.Get())
		{
			UGameplayStatics::PlaySoundAtLocation(GetOwner(), HitSound, EffectLocation);
		}

		if (Step->HitstopSeconds > 0.f)
		{
			if (OwnerCharacter.IsValid() && OwnerCharacter->IsAlive())
			{
				OwnerCharacter->ApplyLocalHitstop(Step->HitstopSeconds);
			}
			if (AIFBaseCharacter* const TargetCharacter = Cast<AIFBaseCharacter>(Target);
				IsValid(TargetCharacter) && TargetCharacter->IsAlive())
			{
				TargetCharacter->ApplyLocalHitstop(Step->HitstopSeconds);
			}
		}
	}
}
