#include "Character/IFSoldierCharacter.h"

#include "AI/IFCrowdSubsystem.h"
#include "AI/IFSoldierAIController.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "BrainComponent.h"
#include "Castle/IFStructure.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Core/IFGameTypes.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Stats/IFHealthComponent.h"
#include "Stats/IFStaminaComponent.h"
#include "TimerManager.h"
#include "UI/IFVitalBarsWidget.h"

AIFSoldierCharacter::AIFSoldierCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	AIControllerClass = AIFSoldierAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	StaminaComponent = CreateDefaultSubobject<UIFStaminaComponent>(TEXT("Stamina"));
	StaminaComponent->ConfigureStaminaDefaults(60.f, 12.f, 1.2f);

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	if (UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Capsule->SetCanEverAffectNavigation(false);
		Capsule->bDynamicObstacle = false;
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

		Movement->bUseRVOAvoidance = false;
	}

	OverheadWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverheadVitals"));
	OverheadWidgetComponent->SetupAttachment(GetRootComponent());
	OverheadWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	OverheadWidgetComponent->SetTickMode(ETickMode::Automatic);
	OverheadWidgetComponent->SetDrawSize(FVector2D(88.f, 16.f));
	OverheadWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 118.f));
	OverheadWidgetComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	OverheadWidgetComponent->SetGenerateOverlapEvents(false);
	OverheadWidgetComponent->SetCastShadow(false);
	OverheadWidgetComponent->SetCanEverAffectNavigation(false);

	SelectionRingComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectionRing"));
	SelectionRingComponent->SetupAttachment(GetRootComponent());
	SelectionRingComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SelectionRingComponent->SetGenerateOverlapEvents(false);
	SelectionRingComponent->SetCanEverAffectNavigation(false);
	SelectionRingComponent->SetCastShadow(false);
	SelectionRingComponent->bReceivesDecals = false;
	SelectionRingComponent->SetVisibility(false);
	UpdateSelectionRingTransform();

	SetGenericTeamId(FGenericTeamId(IFTeamIds::Dusk));
}

void AIFSoldierCharacter::BeginPlay()
{
	Super::BeginPlay();

	// OnPossess can run before BeginPlay, so do not overwrite an anchor already set there.
	if (!bHasAnchorLocation)
	{
		AnchorLocation = GetActorLocation();
		bHasAnchorLocation = true;
	}

	if (UIFHealthComponent* const Health = GetHealthComponent())
	{
		Health->OnHealthChanged.AddUniqueDynamic(this, &AIFSoldierCharacter::HandleHealthChanged);
	}

	if (IsValid(OverheadWidgetComponent) && OverheadWidgetComponent->GetWidgetClass() != nullptr)
	{
		OverheadWidgetComponent->InitWidget();
		if (UIFVitalBarsWidget* const OverheadWidget = Cast<UIFVitalBarsWidget>(OverheadWidgetComponent->GetUserWidgetObject()))
		{
			OverheadWidget->BindToVitalComponents(GetHealthComponent(), StaminaComponent);
			OverheadWidget->SetHealthBarColor(
				(GetGenericTeamId().GetId() == IFTeamIds::Dawn) ? AllyHealthBarColor : EnemyHealthBarColor);
		}
	}
	UpdateOverheadWidgetVisibility();

	UpdateSelectionRingTransform();
	if (IsValid(SelectionRingComponent) && IsValid(SelectionRingMesh))
	{
		SelectionRingComponent->SetStaticMesh(SelectionRingMesh);
	}
	RefreshTacticalRingVisual();
}

void AIFSoldierCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UIFHealthComponent* const Health = GetHealthComponent())
	{
		Health->OnHealthChanged.RemoveDynamic(this, &AIFSoldierCharacter::HandleHealthChanged);
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

void AIFSoldierCharacter::OnDeathStarted()
{
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
		OverheadWidgetComponent->SetComponentTickEnabled(false);
	}

	if (IsValid(StaminaComponent))
	{
		StaminaComponent->StopContinuousDrain();
		StaminaComponent->SetComponentTickEnabled(false);
	}

	// Stop, detach, and destroy AIController immediately so dead units do not tick Perception/BT during CorpseLifeSpan.
	AAIController* const AIController = Cast<AAIController>(GetController());
	if (IsValid(AIController))
	{
		if (UBrainComponent* const Brain = AIController->GetBrainComponent())
		{
			Brain->StopLogic(TEXT("SoldierDied"));
		}
		AIController->StopMovement();
	}

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
		MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(IFCollision::PawnDawn, ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(IFCollision::PawnDusk, ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	};

	ApplyRagdollCollision();

	MeshComp->SetSimulatePhysics(true);
	MeshComp->SetAllBodiesSimulatePhysics(true);
	MeshComp->SetAllBodiesPhysicsBlendWeight(1.f);

	// Re-apply after enabling physics so per-bone body instances inherit the collision channels.
	ApplyRagdollCollision();
	MeshComp->WakeAllRigidBodies();

	if (RagdollDeathImpulse > 0.f)
	{
		FVector ImpulseDir = GetLastDamageDirection().GetSafeNormal2D();
		if (ImpulseDir.IsNearlyZero())
		{
			ImpulseDir = (-GetActorForwardVector()).GetSafeNormal2D();
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
}

float AIFSoldierCharacter::GetEffectiveAttackRange(const AActor* Target) const
{
	const float SelfRadius = GetSimpleCollisionRadius();
	if (Cast<AIFStructure>(Target))
	{
		// Structure distance is already measured to the box surface, so skip target radius.
		return FMath::Max(AttackRange, 0.f) + SelfRadius;
	}

	const float TargetRadius = IsValid(Target) ? Target->GetSimpleCollisionRadius() : 0.f;
	return FMath::Max(AttackRange, 0.f) + SelfRadius + TargetRadius;
}

float AIFSoldierCharacter::GetMaxMeleeEngagementRange(const AActor* Target) const
{
	return GetEffectiveAttackRange(Target) + FMath::Max(AttackRangeBuffer, 0.f);
}

float AIFSoldierCharacter::GetHorizontalDistanceToTarget(const AActor* Target) const
{
	if (!IsValid(Target))
	{
		return TNumericLimits<float>::Max();
	}

	if (const AIFStructure* const Structure = Cast<AIFStructure>(Target))
	{
		return Structure->GetHorizontalSurfaceDistance(GetActorLocation());
	}

	return FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
}

bool AIFSoldierCharacter::IsTargetInMeleeReach(const AActor* Target, bool bAllowHoldBuffer) const
{
	if (!IsValid(Target))
	{
		return false;
	}

	const float MaxHorizRange = bAllowHoldBuffer ? GetMaxMeleeEngagementRange(Target) : GetEffectiveAttackRange(Target);
	if (GetHorizontalDistanceToTarget(Target) > MaxHorizRange)
	{
		return false;
	}

	const float SelfZ = GetActorLocation().Z;
	if (const AIFStructure* const Structure = Cast<AIFStructure>(Target))
	{
		const FVector SurfacePoint3D = Structure->GetClosestSurfacePoint(GetActorLocation());
		return FMath::Abs(SurfacePoint3D.Z - SelfZ) <= MaxMeleeVerticalDistance;
	}

	return FMath::Abs(Target->GetActorLocation().Z - SelfZ) <= MaxMeleeVerticalDistance;
}

FVector AIFSoldierCharacter::GetCombatApproachPoint(const AActor* Target) const
{
	if (!IsValid(Target))
	{
		return GetActorLocation();
	}

	if (const UWorld* const World = GetWorld())
	{
		if (const UIFCrowdSubsystem* const CrowdSubsystem = World->GetSubsystem<UIFCrowdSubsystem>())
		{
			return CrowdSubsystem->ComputeCombatApproachPoint(*this, Target);
		}
	}

	if (const AIFStructure* const Structure = Cast<AIFStructure>(Target))
	{
		const float CapsuleRadius = IsValid(GetCapsuleComponent()) ? GetCapsuleComponent()->GetScaledCapsuleRadius() : 42.f;
		return Structure->GetSpreadCombatApproachPoint(GetActorLocation(), CapsuleRadius, 0.f);
	}

	return Target->GetActorLocation();
}

void AIFSoldierCharacter::SetTacticalSelected(bool bSelected, bool bShowRingNow)
{
	if (bSelected && GetGenericTeamId().GetId() != IFTeamIds::Dawn)
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
	UpdateOverheadWidgetVisibility();
}

void AIFSoldierCharacter::SetSelectionRingDisplayVisible(bool bVisible)
{
	bSelectionRingDisplayEnabled = bVisible;
	RefreshTacticalRingVisual();
	UpdateOverheadWidgetVisibility();
}

void AIFSoldierCharacter::SetHostileTargetHighlighted(bool bHighlighted)
{
	if (bHighlighted && GetGenericTeamId().GetId() == IFTeamIds::Dawn)
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
	UpdateOverheadWidgetVisibility();
}

void AIFSoldierCharacter::HandleHealthChanged(float)
{
	UpdateOverheadWidgetVisibility();
}

void AIFSoldierCharacter::UpdateOverheadWidgetVisibility()
{
	if (!IsValid(OverheadWidgetComponent))
	{
		return;
	}

	if (!IsAlive())
	{
		OverheadWidgetComponent->SetVisibility(false);
		OverheadWidgetComponent->SetComponentTickEnabled(false);
		return;
	}

	const UIFHealthComponent* const Health = GetHealthComponent();
	const bool bDamaged = IsValid(Health)
		&& (Health->GetCurrentHealth() < Health->GetMaxHealth() - KINDA_SMALL_NUMBER);
	const bool bSelectedInTactical = bIsTacticalSelected && bSelectionRingDisplayEnabled;
	const bool bHostileTargeted = (TacticalRingMode == EIFTacticalRingMode::HostileTarget);
	const bool bShouldShow = bDamaged || bSelectedInTactical || bHostileTargeted;

	// Toggle the inner widget instead of the Screen WidgetComponent to avoid triggering NativeDestruct.
	OverheadWidgetComponent->SetVisibility(true);
	if (UUserWidget* const OverheadWidget = OverheadWidgetComponent->GetUserWidgetObject())
	{
		OverheadWidget->SetVisibility(bShouldShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	OverheadWidgetComponent->SetComponentTickEnabled(bShouldShow);
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
		const bool bAllowed = (GetGenericTeamId().GetId() == IFTeamIds::Dawn) && bIsTacticalSelected && bSelectionRingDisplayEnabled;
		if (bAllowed && IsValid(SelectionRingMaterial))
		{
			SelectionRingComponent->SetMaterial(0, SelectionRingMaterial);
		}
		SelectionRingComponent->SetVisibility(bAllowed);
		return;
	}

	if (TacticalRingMode == EIFTacticalRingMode::HostileTarget)
	{
		const bool bAllowed = (GetGenericTeamId().GetId() != IFTeamIds::Dawn) && IsValid(HostileTargetRingMaterial);
		if (bAllowed)
		{
			SelectionRingComponent->SetMaterial(0, HostileTargetRingMaterial);
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

	float CapsuleHalfHeight = 90.f;
	if (const UCapsuleComponent* const Capsule = GetCapsuleComponent())
	{
		CapsuleHalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	}

	SelectionRingComponent->SetRelativeLocation(FVector(0.f, 0.f, -(CapsuleHalfHeight - SelectionRingGroundClearance)));
}
