#include "Character/IFSoldierCharacter.h"

#include "AI/IFSoldierAIController.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "BrainComponent.h"
#include "Combat/IFMeleeCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Core/IFLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Stats/IFStaminaComponent.h"
#include "TimerManager.h"
#include "UI/IFVitalBarsWidget.h"

AIFSoldierCharacter::AIFSoldierCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UIFMeleeCombatComponent>(AIFBaseCharacter::CombatComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	AIControllerClass = AIFSoldierAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	StaminaComponent = CreateDefaultSubobject<UIFStaminaComponent>(TEXT("Stamina"));
	StaminaComponent->ConfigureStaminaDefaults(60.f, 12.f, 1.2f);

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// AI minions should never push the player camera around.
	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	if (USkeletalMeshComponent* const MeshComp = GetMesh())
	{
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		MeshComp->SetCustomNavigableGeometry(EHasCustomNavigableGeometry::No);
		MeshComp->SetCanEverAffectNavigation(false);
	}

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = true;
		Movement->bUseControllerDesiredRotation = false;
		Movement->RotationRate = FRotator(0.f, 480.f, 0.f);
		Movement->bRequestedMoveUseAcceleration = true;
		Movement->MaxWalkSpeed = 400.f;
		Movement->MaxAcceleration = 1200.f;
		Movement->BrakingDecelerationWalking = 1200.f;

		// Keep minion packs from occupying the same point on the lane while allowing tight Roman squares.
		Movement->bUseRVOAvoidance = true;
		Movement->AvoidanceConsiderationRadius = 130.f;
		Movement->AvoidanceWeight = DefaultAvoidanceWeight;
	}

	OverheadWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverheadVitals"));
	OverheadWidgetComponent->SetupAttachment(GetRootComponent());
	OverheadWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	OverheadWidgetComponent->SetDrawSize(FVector2D(88.f, 16.f));
	OverheadWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 118.f));
	OverheadWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverheadWidgetComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	OverheadWidgetComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	OverheadWidgetComponent->SetGenerateOverlapEvents(false);
	OverheadWidgetComponent->SetCastShadow(false);
	OverheadWidgetComponent->SetCanEverAffectNavigation(false);

	SelectionRingComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectionRing"));
	SelectionRingComponent->SetupAttachment(GetRootComponent());
	SelectionRingComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SelectionRingComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	SelectionRingComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	SelectionRingComponent->SetGenerateOverlapEvents(false);
	SelectionRingComponent->SetCanEverAffectNavigation(false);
	SelectionRingComponent->SetCastShadow(false);
	SelectionRingComponent->bReceivesDecals = false;
	SelectionRingComponent->SetVisibility(false);
	UpdateSelectionRingTransform();

	SetGenericTeamId(FGenericTeamId(IFTeamIds::Enemy));
}

void AIFSoldierCharacter::BeginPlay()
{
	Super::BeginPlay();

	MeleeCombat = Cast<UIFMeleeCombatComponent>(GetCombatComponent());

	// OnPossess may already have refreshed a decision against the fallback anchor
	// (the current actor location), so only capture the spawn point here if no
	// explicit post was set before BeginPlay.
	if (!bHasAnchorLocation)
	{
		AnchorLocation = GetActorLocation();
		bHasAnchorLocation = true;
	}

	if (IsValid(OverheadWidgetComponent) && OverheadWidgetComponent->GetWidgetClass() != nullptr)
	{
		OverheadWidgetComponent->InitWidget();
		if (UIFVitalBarsWidget* const OverheadWidget = Cast<UIFVitalBarsWidget>(OverheadWidgetComponent->GetUserWidgetObject()))
		{
			OverheadWidget->BindToVitalComponents(GetHealthComponent(), StaminaComponent);
			OverheadWidget->SetHealthBarColor(
				(GetGenericTeamId().GetId() == IFTeamIds::Player) ? AllyHealthBarColor : EnemyHealthBarColor);
		}
	}

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		DefaultAvoidanceWeight = Movement->AvoidanceWeight;
	}

	SetActorTickEnabled(false);

	UpdateSelectionRingTransform();
	RequestSelectionRingAssets();
}

void AIFSoldierCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelAnchorFacingAlignment();

	if (SelectionRingLoadHandle.IsValid())
	{
		SelectionRingLoadHandle->CancelHandle();
		SelectionRingLoadHandle.Reset();
	}

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RagdollTransitionTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void AIFSoldierCharacter::GetActorEyesViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	OutLocation = GetPawnViewLocation();
	OutRotation = GetActorRotation();
}

void AIFSoldierCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bIsAligningToAnchorFacing || !bHasAnchorFacing || IsDead() || IsAttacking()
		|| GetVelocity().SizeSquared2D() > FMath::Square(15.f))
	{
		CancelAnchorFacingAlignment();
		return;
	}

	const FRotator CurrentRot = GetActorRotation();
	const FRotator TargetRot(CurrentRot.Pitch, AnchorFacingYaw, CurrentRot.Roll);
	const FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, DeltaSeconds, AnchorFacingTurnSpeed);
	SetActorRotation(NewRot);

	if (FMath::Abs(FRotator::NormalizeAxis(AnchorFacingYaw - NewRot.Yaw)) <= AnchorFacingToleranceDegrees)
	{
		SetActorRotation(TargetRot);
		CancelAnchorFacingAlignment();
	}
}

void AIFSoldierCharacter::HandleDeath(AActor* DeadActor)
{
	// Base stops movement, disables pawn collision, stops montages and broadcasts.
	Super::HandleDeath(DeadActor);

	CancelAnchorFacingAlignment();
	bStationaryFormationRVO = false;
	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->SetAvoidanceEnabled(false);
	}
	bIsTacticalSelected = false;
	bSelectionRingDisplayEnabled = false;
	TacticalRingMode = EIFTacticalRingMode::None;
	RefreshTacticalRingVisual();

	if (IsValid(OverheadWidgetComponent))
	{
		if (UIFVitalBarsWidget* const OverheadWidget = Cast<UIFVitalBarsWidget>(OverheadWidgetComponent->GetUserWidgetObject()))
		{
			OverheadWidget->UnbindVitalComponents();
		}
		OverheadWidgetComponent->SetVisibility(false);
	}

	if (IsValid(StaminaComponent))
	{
		StaminaComponent->StopContinuousDrain();
		StaminaComponent->SetComponentTickEnabled(false);
	}

	AAIController* const AIController = Cast<AAIController>(GetController());
	if (IsValid(AIController))
	{
		if (UBrainComponent* const Brain = AIController->GetBrainComponent())
		{
			Brain->StopLogic(TEXT("SoldierDied"));
		}
		AIController->StopMovement();
	}

	// Detach only unpossesses; an AAIController is not destroyed by this call.
	DetachFromControllerPendingDestroy();

	if (IsValid(AIController))
	{
		AIController->Destroy();
	}

	if (CorpseLifeSpan > 0.f)
	{
		SetLifeSpan(CorpseLifeSpan);
	}

	if (bEnableRagdollDeath)
	{
		if (RagdollTransitionDelaySeconds > 0.f)
		{
			if (UWorld* const World = GetWorld())
			{
				World->GetTimerManager().SetTimer(
					RagdollTransitionTimerHandle,
					this,
					&AIFSoldierCharacter::ActivateRagdollDeath,
					RagdollTransitionDelaySeconds,
					false);
			}
		}
		else
		{
			ActivateRagdollDeath();
		}
	}
}

void AIFSoldierCharacter::ActivateRagdollDeath()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RagdollTransitionTimerHandle);
	}

	if (!IsDead())
	{
		return;
	}

	USkeletalMeshComponent* const MeshComp = GetMesh();
	UPhysicsAsset* const PhysAsset = IsValid(MeshComp) ? MeshComp->GetPhysicsAsset() : nullptr;
	if (!IsValid(MeshComp) || !IsValid(PhysAsset) || PhysAsset->SkeletalBodySetups.Num() == 0)
	{
		return;
	}

	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Capsule->SetCanEverAffectNavigation(false);
	}

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	if (UAnimInstance* const AnimInstance = MeshComp->GetAnimInstance())
	{
		AnimInstance->Montage_Stop(0.f);
	}

	auto ApplyRagdollCollision = [MeshComp]()
	{
		MeshComp->SetCustomNavigableGeometry(EHasCustomNavigableGeometry::No);
		MeshComp->SetCanEverAffectNavigation(false);
		MeshComp->SetCollisionObjectType(ECC_PhysicsBody);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	};

	ApplyRagdollCollision();

	MeshComp->SetSimulatePhysics(true);
	MeshComp->SetAllBodiesSimulatePhysics(true);
	MeshComp->SetAllBodiesPhysicsBlendWeight(1.f);

	// Re-apply after enabling simulation so per-bone PhysicsAsset body instances inherit Pawn/PhysicsBody ignores.
	ApplyRagdollCollision();
	MeshComp->WakeAllRigidBodies();

	if (RagdollDeathImpulse > 0.f)
	{
		FVector ImpulseDir = GetLastDamageDirection();
		ImpulseDir.Z = 0.f;
		if (!ImpulseDir.Normalize())
		{
			ImpulseDir = -GetActorForwardVector();
			ImpulseDir.Z = 0.f;
			if (!ImpulseDir.Normalize())
			{
				ImpulseDir = FVector::BackwardVector;
			}
		}

		ImpulseDir = (ImpulseDir + FVector(0.f, 0.f, 0.25f)).GetSafeNormal();
		MeshComp->AddImpulse(ImpulseDir * RagdollDeathImpulse, NAME_None, true);
	}
}

AIFSoldierAIController* AIFSoldierCharacter::GetSoldierController() const
{
	return Cast<AIFSoldierAIController>(GetController());
}

void AIFSoldierCharacter::SetAnchorLocation(const FVector& NewAnchorLocation, bool bRefreshControllerDecision)
{
	AnchorLocation = NewAnchorLocation;
	bHasAnchorLocation = true;

	if (bRefreshControllerDecision)
	{
		if (AIFSoldierAIController* const SoldierController = GetSoldierController())
		{
			SoldierController->RefreshDecision();
		}
	}
}

void AIFSoldierCharacter::SetAnchorFacingYaw(float NewFacingYaw)
{
	AnchorFacingYaw = FRotator::NormalizeAxis(NewFacingYaw);
	bHasAnchorFacing = true;
}

void AIFSoldierCharacter::ClearAnchorFacing()
{
	bHasAnchorFacing = false;
	CancelAnchorFacingAlignment();
}

void AIFSoldierCharacter::BeginAnchorFacingAlignment()
{
	if (!bHasAnchorFacing || IsDead() || IsAttacking() || GetVelocity().SizeSquared2D() > FMath::Square(15.f))
	{
		return;
	}

	const FRotator CurrentRot = GetActorRotation();
	const float YawError = FMath::Abs(FRotator::NormalizeAxis(AnchorFacingYaw - CurrentRot.Yaw));
	if (YawError <= AnchorFacingToleranceDegrees)
	{
		SetActorRotation(FRotator(CurrentRot.Pitch, AnchorFacingYaw, CurrentRot.Roll));
		CancelAnchorFacingAlignment();
		return;
	}

	bIsAligningToAnchorFacing = true;
	SetActorTickEnabled(true);
}

void AIFSoldierCharacter::CancelAnchorFacingAlignment()
{
	bIsAligningToAnchorFacing = false;
	SetActorTickEnabled(false);
}

void AIFSoldierCharacter::SetStationaryFormationRVO(bool bStationary)
{
	if (bStationaryFormationRVO == bStationary)
	{
		return;
	}

	bStationaryFormationRVO = bStationary;
	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->AvoidanceWeight = bStationary ? 0.f : DefaultAvoidanceWeight;
		Movement->SetAvoidanceEnabled(!bStationary);
	}
}

float AIFSoldierCharacter::GetEffectiveAttackRange(const AActor* Target) const
{
	const float SelfRadius = GetSimpleCollisionRadius();
	const float TargetRadius = IsValid(Target) ? Target->GetSimpleCollisionRadius() : 0.f;
	return FMath::Max(AttackRange, 0.f) + SelfRadius + TargetRadius;
}

float AIFSoldierCharacter::GetMaxMeleeEngagementRange(const AActor* Target) const
{
	return GetEffectiveAttackRange(Target) + FMath::Max(AttackRangeBuffer, 0.f);
}

bool AIFSoldierCharacter::TryPerformMeleeAttack()
{
	if (!IsValid(MeleeCombat) || IsDead())
	{
		return false;
	}
	return MeleeCombat->TryStartAIAttack();
}

bool AIFSoldierCharacter::TryContinueMeleeCombo(int32 CompletedChainIndex)
{
	if (!IsValid(MeleeCombat) || IsDead())
	{
		return false;
	}
	return MeleeCombat->TryContinueAICombo(CompletedChainIndex);
}

void AIFSoldierCharacter::EnsureMeleeComboPlan()
{
	if (IsValid(MeleeCombat) && !IsDead())
	{
		MeleeCombat->EnsureAIComboPlan();
	}
}

bool AIFSoldierCharacter::HasEnoughStaminaForPlannedCombo() const
{
	if (!IsValid(MeleeCombat) || IsDead())
	{
		return false;
	}
	return MeleeCombat->HasEnoughStaminaForPlannedCombo();
}

bool AIFSoldierCharacter::TryExecuteNextPlannedMeleeStep()
{
	if (!IsValid(MeleeCombat) || IsDead())
	{
		return false;
	}
	return MeleeCombat->TryExecuteNextPlannedAIStep();
}

bool AIFSoldierCharacter::HasRemainingPlannedMeleeSteps() const
{
	return IsValid(MeleeCombat) && !IsDead() && MeleeCombat->HasRemainingPlannedAISteps();
}

void AIFSoldierCharacter::ClearMeleeComboPlan()
{
	if (IsValid(MeleeCombat))
	{
		MeleeCombat->ClearAIComboPlan();
	}
}

bool AIFSoldierCharacter::IsAttacking() const
{
	return IsValid(MeleeCombat) && MeleeCombat->IsAttacking();
}

void AIFSoldierCharacter::SetTacticalSelected(bool bSelected, bool bShowRingNow)
{
	if (bSelected && GetGenericTeamId().GetId() != IFTeamIds::Player)
	{
		bSelected = false;
	}

	bIsTacticalSelected = bSelected && IsAlive();
	bSelectionRingDisplayEnabled = bShowRingNow;

	if (bIsTacticalSelected)
	{
		TacticalRingMode = EIFTacticalRingMode::SelectedAlly;
	}
	else if (TacticalRingMode == EIFTacticalRingMode::SelectedAlly)
	{
		TacticalRingMode = EIFTacticalRingMode::None;
	}

	RefreshTacticalRingVisual();
}

void AIFSoldierCharacter::SetSelectionRingDisplayVisible(bool bVisible)
{
	bSelectionRingDisplayEnabled = bVisible;
	RefreshTacticalRingVisual();
}

void AIFSoldierCharacter::SetHostileTargetHighlighted(bool bHighlighted)
{
	if (bHighlighted && GetGenericTeamId().GetId() == IFTeamIds::Player)
	{
		bHighlighted = false;
	}

	if (bHighlighted && IsAlive())
	{
		TacticalRingMode = EIFTacticalRingMode::HostileTarget;
	}
	else if (TacticalRingMode == EIFTacticalRingMode::HostileTarget)
	{
		TacticalRingMode = EIFTacticalRingMode::None;
	}

	RefreshTacticalRingVisual();
}

void AIFSoldierCharacter::RequestSelectionRingAssets()
{
	TArray<FSoftObjectPath> AssetPaths;
	if (!SelectionRingMesh.IsNull())
	{
		AssetPaths.AddUnique(SelectionRingMesh.ToSoftObjectPath());
	}
	if (!SelectionRingMaterial.IsNull())
	{
		AssetPaths.AddUnique(SelectionRingMaterial.ToSoftObjectPath());
	}
	if (!HostileTargetRingMaterial.IsNull())
	{
		AssetPaths.AddUnique(HostileTargetRingMaterial.ToSoftObjectPath());
	}
	if (AssetPaths.IsEmpty())
	{
		return;
	}

	SelectionRingLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPaths,
		FStreamableDelegate::CreateUObject(this, &AIFSoldierCharacter::HandleSelectionRingAssetsLoaded));
}

void AIFSoldierCharacter::HandleSelectionRingAssetsLoaded()
{
	SelectionRingLoadHandle.Reset();

	if (!IsValid(SelectionRingComponent))
	{
		return;
	}

	if (UStaticMesh* const RingMesh = SelectionRingMesh.Get())
	{
		SelectionRingComponent->SetStaticMesh(RingMesh);
	}
	RefreshTacticalRingVisual();
}

void AIFSoldierCharacter::RefreshTacticalRingVisual()
{
	if (!IsValid(SelectionRingComponent))
	{
		return;
	}

	if (!IsAlive() || TacticalRingMode == EIFTacticalRingMode::None)
	{
		SelectionRingComponent->SetVisibility(false);
		return;
	}

	if (TacticalRingMode == EIFTacticalRingMode::SelectedAlly)
	{
		const bool bAllowed = (GetGenericTeamId().GetId() == IFTeamIds::Player) && bIsTacticalSelected && bSelectionRingDisplayEnabled;
		if (bAllowed)
		{
			if (UMaterialInterface* const AllyMat = SelectionRingMaterial.Get())
			{
				SelectionRingComponent->SetMaterial(0, AllyMat);
			}
		}
		SelectionRingComponent->SetVisibility(bAllowed);
		return;
	}

	if (TacticalRingMode == EIFTacticalRingMode::HostileTarget)
	{
		UMaterialInterface* const HostileMat = HostileTargetRingMaterial.Get();
		const bool bAllowed = (GetGenericTeamId().GetId() != IFTeamIds::Player) && IsValid(HostileMat);
		if (bAllowed)
		{
			SelectionRingComponent->SetMaterial(0, HostileMat);
		}
		SelectionRingComponent->SetVisibility(bAllowed);
	}
}

void AIFSoldierCharacter::UpdateSelectionRingTransform()
{
	if (!IsValid(SelectionRingComponent))
	{
		return;
	}

	float CapsuleHalfHeight = 88.f;
	if (const UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		CapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	}

	SelectionRingComponent->SetRelativeLocation(FVector(0.f, 0.f, -(CapsuleHalfHeight - SelectionRingGroundClearance)));
}
