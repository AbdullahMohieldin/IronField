#include "Combat/IFMeleeCombatComponent.h"

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

void UIFMeleeCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (OwnerCharacter.IsValid())
	{
		Stamina = OwnerCharacter->FindComponentByClass<UIFStaminaComponent>();
	}

	if (!CombatData.IsNull())
	{
		DataLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			CombatData.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &UIFMeleeCombatComponent::HandleCombatDataLoaded));
	}
	else
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s: CombatData is not assigned."), *GetNameSafe(GetOwner()));
	}
}

void UIFMeleeCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bCombatReady = false;
	PlannedAICombo.Reset();

	InvalidateMontageExecution();

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
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	CloseHitWindow();

	Super::EndPlay(EndPlayReason);
}

void UIFMeleeCombatComponent::GatherRequiredAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const
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

	if (!LoadedCombatData->BlockCascadeVFX.IsNull())
	{
		OutAssetPaths.Add(LoadedCombatData->BlockCascadeVFX.ToSoftObjectPath());
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

void UIFMeleeCombatComponent::HandleCombatDataLoaded()
{
	LoadedCombatData = CombatData.Get();
	DataLoadHandle.Reset();

	if (!IsValid(LoadedCombatData))
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s: combat data failed to load."), *GetNameSafe(GetOwner()));
		return;
	}

	DirectionalHitReacts = LoadedCombatData->DirectionalHitReacts;

	TArray<FSoftObjectPath> AssetPaths;
	GatherRequiredAssetPaths(AssetPaths);

	if (AssetPaths.IsEmpty())
	{
		HandleStepAssetsLoaded();
		return;
	}

	StepAssetsLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPaths, FStreamableDelegate::CreateUObject(this, &UIFMeleeCombatComponent::HandleStepAssetsLoaded));
}

void UIFMeleeCombatComponent::HandleStepAssetsLoaded()
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

bool UIFMeleeCombatComponent::AreRequiredAssetsLoaded() const
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

bool UIFMeleeCombatComponent::TryStartAttack()
{
	if (!bCombatReady || !IsValid(LoadedCombatData) || LoadedCombatData->ComboSteps.IsEmpty())
	{
		return false;
	}
	if (!IsIdle())
	{
		return false;
	}

	return PlayStep(GetNextStepIndex());
}

float UIFMeleeCombatComponent::GetAIComboBreatherSeconds() const
{
	return IsValid(LoadedCombatData) ? LoadedCombatData->AIComboBreatherSeconds : 0.f;
}

void UIFMeleeCombatComponent::EnsureAIComboPlan()
{
	if (PlannedAICombo.bHasPlan || !bCombatReady || !IsValid(LoadedCombatData) || LoadedCombatData->ComboSteps.IsEmpty())
	{
		return;
	}

	const int32 StepCount = LoadedCombatData->ComboSteps.Num();
	PlannedAICombo.Reset();
	PlannedAICombo.bHasPlan = true;
	PlannedAICombo.OpenerStepIndex = LoadedCombatData->bRandomizeAIComboOpener
		? FMath::RandRange(0, StepCount - 1)
		: 0;
	PlannedAICombo.PlannedHitCount = 1;
	PlannedAICombo.ExecutedHits = 0;

	const TArray<float>& Chances = LoadedCombatData->AIComboContinuationChances;
	for (int32 ChainIndex = 0; ChainIndex < Chances.Num() && PlannedAICombo.PlannedHitCount < StepCount; ++ChainIndex)
	{
		const float Chance = Chances[ChainIndex];
		if (Chance > 0.f && (Chance >= 1.f || FMath::FRand() < Chance))
		{
			++PlannedAICombo.PlannedHitCount;
		}
		else
		{
			break;
		}
	}

	float TotalStamina = 0.f;
	for (int32 Offset = 0; Offset < PlannedAICombo.PlannedHitCount; ++Offset)
	{
		const int32 StepIndex = (PlannedAICombo.OpenerStepIndex + Offset) % StepCount;
		TotalStamina += FMath::Max(0.f, LoadedCombatData->ComboSteps[StepIndex].StaminaCost);
	}

	if (IsValid(Stamina))
	{
		TotalStamina = FMath::Min(TotalStamina, Stamina->GetMaxStamina());
	}
	PlannedAICombo.RequiredStamina = TotalStamina;
}

bool UIFMeleeCombatComponent::HasEnoughStaminaForPlannedCombo() const
{
	if (!IsValid(Stamina))
	{
		return true;
	}
	if (!PlannedAICombo.bHasPlan)
	{
		return false;
	}
	return Stamina->GetCurrentStamina() + KINDA_SMALL_NUMBER >= PlannedAICombo.RequiredStamina;
}

bool UIFMeleeCombatComponent::TryExecuteNextPlannedAIStep()
{
	if (!bCombatReady || !IsValid(LoadedCombatData) || LoadedCombatData->ComboSteps.IsEmpty()
		|| !PlannedAICombo.bHasPlan || PlannedAICombo.ExecutedHits >= PlannedAICombo.PlannedHitCount || !IsIdle())
	{
		return false;
	}

	const int32 StepIndex = (PlannedAICombo.OpenerStepIndex + PlannedAICombo.ExecutedHits) % LoadedCombatData->ComboSteps.Num();
	if (PlayStep(StepIndex))
	{
		++PlannedAICombo.ExecutedHits;
		return true;
	}

	return false;
}

bool UIFMeleeCombatComponent::HasRemainingPlannedAISteps() const
{
	return PlannedAICombo.bHasPlan && PlannedAICombo.ExecutedHits < PlannedAICombo.PlannedHitCount;
}

void UIFMeleeCombatComponent::ClearAIComboPlan()
{
	PlannedAICombo.Reset();
}

float UIFMeleeCombatComponent::GetAIMaxStaminaWaitSeconds() const
{
	return IsValid(LoadedCombatData) ? LoadedCombatData->MaxStaminaWaitSeconds : 3.5f;
}

int32 UIFMeleeCombatComponent::GetNextStepIndex() const
{
	if (!IsValid(LoadedCombatData) || LoadedCombatData->ComboSteps.IsEmpty())
	{
		return INDEX_NONE;
	}

	const int32 NextIndex = (CurrentStepIndex == INDEX_NONE) ? 0 : CurrentStepIndex + 1;
	return (NextIndex >= LoadedCombatData->ComboSteps.Num()) ? 0 : NextIndex;
}

const FIFMeleeAttackStep* UIFMeleeCombatComponent::GetCurrentStep() const
{
	if (!IsValid(LoadedCombatData))
	{
		return nullptr;
	}

	if (bIsExecutingGuardAttack)
	{
		return &LoadedCombatData->ShieldBlock.GuardAttack;
	}

	if (!LoadedCombatData->ComboSteps.IsValidIndex(CurrentStepIndex))
	{
		return nullptr;
	}

	return &LoadedCombatData->ComboSteps[CurrentStepIndex];
}

bool UIFMeleeCombatComponent::PlayAttackMontage(UAnimMontage* Montage)
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
	InvalidateMontageExecution();

	const float PlayLength = CachedAnimInstance->Montage_Play(Montage, 1.f);
	FAnimMontageInstance* const Instance = PlayLength > 0.f && IsValid(CachedAnimInstance)
		? CachedAnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;

	if (!bCombatReady || !OwnerCharacter.IsValid() || OwnerCharacter->IsDead()
		|| GetCombatState() != StartingState || CurrentMontageInstanceID != INDEX_NONE)
	{
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
		return false;
	}

	CurrentMontage = Montage;
	CurrentMontageInstanceID = Instance->GetInstanceID();
	Instance->OnMontageBlendingOutStarted.BindUObject(
		this, &UIFMeleeCombatComponent::HandleOwnedMontageBlendingOut, CurrentMontageInstanceID);
	return true;
}

void UIFMeleeCombatComponent::InvalidateMontageExecution()
{
	CurrentMontageInstanceID = INDEX_NONE;
	CurrentMontage = nullptr;
}

bool UIFMeleeCombatComponent::PlayStep(int32 StepIndex)
{
	if (!bCombatReady || !IsValid(LoadedCombatData) || !LoadedCombatData->ComboSteps.IsValidIndex(StepIndex))
	{
		return false;
	}

	const FIFMeleeAttackStep& Step = LoadedCombatData->ComboSteps[StepIndex];
	UAnimMontage* const Montage = Step.Montage.Get();
	if (!IsValid(Montage) || !IsValid(CachedAnimInstance) || !OwnerCharacter.IsValid())
	{
		return false;
	}

	if (IsValid(Stamina) && !Stamina->HasStamina(Step.StaminaCost))
	{
		return false;
	}

	const EIFCombatState StartingState = GetCombatState();
	CloseHitWindow();
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	if (!PlayAttackMontage(Montage))
	{
		return false;
	}
	const int32 StartedInstanceID = CurrentMontageInstanceID;

	if (IsValid(Stamina))
	{
		Stamina->TryConsumeStamina(Step.StaminaCost);
	}

	if (CurrentMontageInstanceID != StartedInstanceID || GetCombatState() != StartingState)
	{
		return false;
	}

	bIsExecutingGuardAttack = false;
	CurrentStepIndex = StepIndex;
	SetCombatState(EIFCombatState::Attacking);
	if (CurrentMontageInstanceID != StartedInstanceID || !IsAttacking())
	{
		return false;
	}
	HandleStepStarted();
	return true;
}

bool UIFMeleeCombatComponent::PlayGuardAttack()
{
	if (!bCombatReady || !IsValid(LoadedCombatData))
	{
		return false;
	}

	const FIFMeleeAttackStep& GuardStep = LoadedCombatData->ShieldBlock.GuardAttack;
	UAnimMontage* const Montage = GuardStep.Montage.Get();
	if (!IsValid(Montage) || !IsValid(CachedAnimInstance) || !OwnerCharacter.IsValid())
	{
		return false;
	}

	if (IsValid(Stamina) && !Stamina->HasStamina(GuardStep.StaminaCost))
	{
		return false;
	}

	const EIFCombatState StartingState = GetCombatState();
	CloseHitWindow();
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	if (!PlayAttackMontage(Montage))
	{
		return false;
	}
	const int32 StartedInstanceID = CurrentMontageInstanceID;

	if (IsValid(Stamina))
	{
		Stamina->TryConsumeStamina(GuardStep.StaminaCost);
	}

	if (CurrentMontageInstanceID != StartedInstanceID || GetCombatState() != StartingState)
	{
		return false;
	}

	bIsExecutingGuardAttack = true;
	CurrentStepIndex = INDEX_NONE;
	SetCombatState(EIFCombatState::Attacking);
	if (CurrentMontageInstanceID != StartedInstanceID || !IsAttacking())
	{
		return false;
	}
	HandleStepStarted();
	return true;
}

void UIFMeleeCombatComponent::HandleOwnedMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted, int32 InstanceID)
{
	// Ignore blend-out callbacks from an earlier montage instance.
	if (InstanceID == INDEX_NONE || InstanceID != CurrentMontageInstanceID
		|| Montage != CurrentMontage || !IsAttacking())
	{
		return;
	}

	HandleMontageBlendingOut(Montage, bInterrupted);
}

void UIFMeleeCombatComponent::HandleMontageBlendingOut(UAnimMontage*, bool bInterrupted)
{
	if (bIsExecutingGuardAttack)
	{
		bIsExecutingGuardAttack = false;
		FinishCombo();
		return;
	}

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

void UIFMeleeCombatComponent::FinishCombo()
{
	LastFinishedStepIndex = CurrentStepIndex;
	InvalidateMontageExecution();
	CurrentStepIndex = INDEX_NONE;
	bIsExecutingGuardAttack = false;
	CloseHitWindow();
	SetCombatState(EIFCombatState::Idle);
	StartComboResetTimer();
}

void UIFMeleeCombatComponent::StartComboResetTimer()
{
	UWorld* const World = GetWorld();
	if (!IsValid(World) || !IsValid(LoadedCombatData))
	{
		HandleComboReset();
		return;
	}

	if (LoadedCombatData->ComboResetSeconds > 0.f)
	{
		World->GetTimerManager().SetTimer(ComboResetTimerHandle, this, &UIFMeleeCombatComponent::HandleComboReset, LoadedCombatData->ComboResetSeconds, false);
	}
	else
	{
		HandleComboReset();
	}
}

void UIFMeleeCombatComponent::HandleComboReset()
{
	CurrentStepIndex = INDEX_NONE;
}

void UIFMeleeCombatComponent::HandleOwnerDeath()
{
	PlannedAICombo.Reset();
	InvalidateMontageExecution();
	CurrentStepIndex = INDEX_NONE;
	LastFinishedStepIndex = INDEX_NONE;
	bIsExecutingGuardAttack = false;
	CloseHitWindow();

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	Super::HandleOwnerDeath();
}

void UIFMeleeCombatComponent::HandleOwnerRevived()
{
	PlannedAICombo.Reset();
	InvalidateMontageExecution();
	CurrentStepIndex = INDEX_NONE;
	LastFinishedStepIndex = INDEX_NONE;
	bIsExecutingGuardAttack = false;
	NextHitReactAllowedTime = 0.f;
	NextAdditiveFlinchAllowedTime = 0.f;
	Super::HandleOwnerRevived();
}

void UIFMeleeCombatComponent::HandleCombatStateTransition(EIFCombatState PreviousState, EIFCombatState NewState)
{
	if (!IsValid(Stamina))
	{
		return;
	}

	if (PreviousState == EIFCombatState::Attacking && NewState != EIFCombatState::Attacking)
	{
		CloseHitWindow();
		Stamina->ResetRegenDelay();
	}

	switch (NewState)
	{
	case EIFCombatState::Attacking:
	case EIFCombatState::Dead:
		Stamina->SetRegenRateMultiplier(0.f);
		break;

	case EIFCombatState::Blocking:
		Stamina->SetRegenRateMultiplier(BlockingStaminaRegenMultiplier);
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

void UIFMeleeCombatComponent::OpenHitWindow()
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

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(IFCollision::Damageable);

	for (const FVector& Point : PreviousSamplePoints)
	{
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Point, FQuat::Identity, ObjectParams, Shape, QueryParams);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			ProcessHit(Overlap.GetActor(), Point);
		}

		if (bDrawDebugHitWindow)
		{
			DrawDebugSphere(World, Point, Step->HitRadius, 12, FColor::Red, false, 0.f);
		}
	}
}

void UIFMeleeCombatComponent::UpdateHitWindow(float)
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

	TArray<FVector> CurrentPoints;
	BuildSamplePoints(CurrentPoints);
	if (CurrentPoints.Num() != PreviousSamplePoints.Num())
	{
		PreviousSamplePoints = MoveTemp(CurrentPoints);
		return;
	}

	const FCollisionShape Shape = FCollisionShape::MakeSphere(Step->HitRadius);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFMeleeSweep), false);
	QueryParams.AddIgnoredActor(GetOwner());

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(IFCollision::Damageable);

	for (int32 SampleIndex = 0; SampleIndex < CurrentPoints.Num(); ++SampleIndex)
	{
		TArray<FHitResult> Hits;
		World->SweepMultiByObjectType(Hits, PreviousSamplePoints[SampleIndex], CurrentPoints[SampleIndex],
			FQuat::Identity, ObjectParams, Shape, QueryParams);
		for (const FHitResult& Hit : Hits)
		{
			ProcessHit(Hit.GetActor(), Hit.ImpactPoint);
		}

		if (bDrawDebugHitWindow)
		{
			DrawDebugLine(World, PreviousSamplePoints[SampleIndex], CurrentPoints[SampleIndex], FColor::Orange, false, 0.f);
			DrawDebugSphere(World, CurrentPoints[SampleIndex], Step->HitRadius, 12, FColor::Red, false, 0.f);
		}
	}

	PreviousSamplePoints = MoveTemp(CurrentPoints);
}

void UIFMeleeCombatComponent::CloseHitWindow()
{
	bHitWindowOpen = false;
	HitActorsThisWindow.Reset();
	PreviousSamplePoints.Reset();
}

void UIFMeleeCombatComponent::BuildSamplePoints(TArray<FVector>& OutPoints) const
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

void UIFMeleeCombatComponent::ProcessHit(AActor* Target, const FVector& HitLocation)
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

	// Trace to the hit point rather than actor center so wide structures do not block their own hits.
	const FVector LosEnd = !HitLocation.IsNearlyZero() ? HitLocation : Target->GetActorLocation();
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
		const FRotator HitRotation = (HitLocation - GetOwner()->GetActorLocation()).GetSafeNormal().Rotation();
		if (UNiagaraSystem* const HitVFX = Step->HitVFX.Get())
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, HitVFX, HitLocation, HitRotation);
		}
		else if (UParticleSystem* const HitCascadeVFX = Step->HitCascadeVFX.Get())
		{
			UGameplayStatics::SpawnEmitterAtLocation(World, HitCascadeVFX, HitLocation, HitRotation, true);
		}

		if (USoundBase* const HitSound = Step->HitSound.Get())
		{
			UGameplayStatics::PlaySoundAtLocation(GetOwner(), HitSound, HitLocation);
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

void UIFMeleeCombatComponent::TriggerAdditiveFlinch(AActor* DamageCauser, AController* EventInstigator)
{
	const UWorld* const World = GetWorld();
	const float Now = IsValid(World) ? World->GetTimeSeconds() : 0.f;
	if (IsValid(World) && Now < NextAdditiveFlinchAllowedTime)
	{
		return;
	}

	Super::PlayDirectionalHitReact(DamageCauser, EventInstigator, false);

	const float Cooldown = IsValid(LoadedCombatData) ? LoadedCombatData->AdditiveFlinchCooldownSeconds : 0.25f;
	if (IsValid(World) && Cooldown > 0.f)
	{
		NextAdditiveFlinchAllowedTime = Now + Cooldown;
	}
}

void UIFMeleeCombatComponent::PlayDirectionalHitReact(AActor* DamageCauser, AController* EventInstigator, bool bIsHeavyStagger)
{
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

	const bool bWasAttacking = IsAttacking();
	if (bWasAttacking)
	{
		UAnimMontage* const OutgoingMontage = CurrentMontage;
		InvalidateMontageExecution();
		CloseHitWindow();
		CurrentStepIndex = INDEX_NONE;
		bIsExecutingGuardAttack = false;
		if (UWorld* const MutableWorld = GetWorld())
		{
			MutableWorld->GetTimerManager().ClearTimer(ComboResetTimerHandle);
		}
		if (IsValid(CachedAnimInstance) && IsValid(OutgoingMontage))
		{
			CachedAnimInstance->Montage_Stop(0.1f, OutgoingMontage);
		}
	}

	Super::PlayDirectionalHitReact(DamageCauser, EventInstigator, true);
	if (bWasAttacking && IsAttacking() && CurrentMontageInstanceID == INDEX_NONE)
	{
		FinishCombo();
	}

	if (IsInHitReact() && IsValid(LoadedCombatData) && LoadedCombatData->HitReactCooldownSeconds > 0.f && IsValid(World))
	{
		NextHitReactAllowedTime = World->GetTimeSeconds() + LoadedCombatData->HitReactCooldownSeconds;
	}
}