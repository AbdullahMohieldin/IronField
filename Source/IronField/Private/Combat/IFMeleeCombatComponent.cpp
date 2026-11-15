#include "Combat/IFMeleeCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/IFBaseCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Components/SkeletalMeshComponent.h"
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
#include "Sound/SoundBase.h"
#include "Stats/IFStaminaComponent.h"
#include "TimerManager.h"

void UIFMeleeCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AIFBaseCharacter>(GetOwner());
	if (OwnerCharacter.IsValid())
	{
		if (USkeletalMeshComponent* const Mesh = OwnerCharacter->GetMesh())
		{
			CachedAnimInstance = Mesh->GetAnimInstance();
		}
		Stamina = GetOwner()->FindComponentByClass<UIFStaminaComponent>();
	}

	if (IsValid(CachedAnimInstance))
	{
		CachedAnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &UIFMeleeCombatComponent::HandleMontageBlendingOut);
	}

	if (!CombatData.IsNull())
	{
		DataLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
			CombatData.ToSoftObjectPath(), FStreamableDelegate::CreateUObject(this, &UIFMeleeCombatComponent::HandleCombatDataLoaded));
	}
}

void UIFMeleeCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(CachedAnimInstance))
	{
		CachedAnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &UIFMeleeCombatComponent::HandleMontageBlendingOut);
	}

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

void UIFMeleeCombatComponent::HandleCombatDataLoaded()
{
	DataLoadHandle.Reset();

	LoadedCombatData = CombatData.Get();
	if (!IsValid(LoadedCombatData))
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Combat] %s: combat data failed to load."), *GetNameSafe(GetOwner()));
		return;
	}

	TArray<FSoftObjectPath> AssetPaths;
	for (const FIFMeleeAttackStep& Step : LoadedCombatData->ComboSteps)
	{
		if (!Step.Montage.IsNull())
		{
			AssetPaths.Add(Step.Montage.ToSoftObjectPath());
		}
		if (!Step.HitVFX.IsNull())
		{
			AssetPaths.Add(Step.HitVFX.ToSoftObjectPath());
		}
		if (!Step.HitSound.IsNull())
		{
			AssetPaths.Add(Step.HitSound.ToSoftObjectPath());
		}
	}

	if (AssetPaths.IsEmpty())
	{
		bCombatReady = true;
		return;
	}

	StepAssetsLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPaths, FStreamableDelegate::CreateUObject(this, &UIFMeleeCombatComponent::HandleStepAssetsLoaded));
}

void UIFMeleeCombatComponent::HandleStepAssetsLoaded()
{
	StepAssetsLoadHandle.Reset();
	bCombatReady = true;
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
	if (!IsValid(LoadedCombatData) || !LoadedCombatData->ComboSteps.IsValidIndex(CurrentStepIndex))
	{
		return nullptr;
	}

	return &LoadedCombatData->ComboSteps[CurrentStepIndex];
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

	CloseHitWindow();
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	// Point at the incoming montage before playing so the outgoing step's BlendingOut
	// (fired by the intentional interruption) fails the identity guard below.
	UAnimMontage* const PreviousMontage = CurrentMontage;
	CurrentMontage = Montage;

	const float PlayLength = CachedAnimInstance->Montage_Play(Montage, 1.f);
	if (PlayLength <= 0.f)
	{
		CurrentMontage = PreviousMontage;
		return false;
	}

	if (IsValid(Stamina))
	{
		Stamina->TryConsumeStamina(Step.StaminaCost);
	}

	CurrentStepIndex = StepIndex;
	SetCombatState(EIFCombatState::Attacking);
	HandleStepStarted();
	return true;
}

void UIFMeleeCombatComponent::HandleMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	if (!IsValid(Montage) || Montage != CurrentMontage)
	{
		return;
	}
	if (GetCombatState() == EIFCombatState::Dead)
	{
		return;
	}

	if (!bInterrupted && TryBufferedAdvance())
	{
		return;
	}

	FinishCombo();
}

void UIFMeleeCombatComponent::FinishCombo()
{
	CurrentMontage = nullptr;
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
	// The base character stops montages right after this call; invalidating first keeps
	// that BlendingOut from touching combat state.
	CurrentMontage = nullptr;
	CurrentStepIndex = INDEX_NONE;
	CloseHitWindow();

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboResetTimerHandle);
	}

	Super::HandleOwnerDeath();
}

void UIFMeleeCombatComponent::HandleOwnerRevived()
{
	CurrentStepIndex = INDEX_NONE;
	Super::HandleOwnerRevived();
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

	// Initial overlap catches targets already inside the arc at window start (legacy B2).
	const FCollisionShape Shape = FCollisionShape::MakeSphere(Step->HitRadius);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFMeleeOverlap), false);
	QueryParams.AddIgnoredActor(GetOwner());

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_GameTraceChannel1); // Damageable object channel

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
	if (!bHitWindowOpen)
	{
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
	ObjectParams.AddObjectTypesToQuery(ECC_GameTraceChannel1); // Damageable object channel

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

	// Walls block swings: one visibility trace per new target, not per frame.
	FHitResult LineOfSight;
	FCollisionQueryParams LosParams(SCENE_QUERY_STAT(IFMeleeLOS), false);
	LosParams.AddIgnoredActor(GetOwner());
	if (World->LineTraceSingleByChannel(LineOfSight, GetOwner()->GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, LosParams)
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
	UGameplayStatics::ApplyDamage(Target, Step->Damage, InstigatorController, GetOwner(), UDamageType::StaticClass());

	if (UNiagaraSystem* const HitVFX = Step->HitVFX.Get())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, HitVFX, HitLocation);
	}
	if (USoundBase* const HitSound = Step->HitSound.Get())
	{
		UGameplayStatics::PlaySoundAtLocation(GetOwner(), HitSound, HitLocation);
	}
}