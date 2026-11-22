#include "Castle/IFStructure.h"

#include "AI/NavigationSystemBase.h"
#include "Castle/IFStructureDataAsset.h"
#include "Castle/IFTerritory.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Core/IFGameState.h"
#include "Core/IFLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Field/FieldSystemObjects.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "GeometryCollectionProxyData.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AISense_Sight.h"
#include "PhysicsEngine/BodySetup.h"
#include "Stats/IFHealthComponent.h"
#include "TimerManager.h"
#include "UI/IFVitalBarsWidget.h"

const FName AIFStructure::TeamColoredComponentTag(TEXT("TeamColored"));

AIFStructure::AIFStructure()
{
	PrimaryActorTick.bCanEverTick = false;

	BoundsCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsCollision"));
	BoundsCollision->InitBoxExtent(FVector(150.f, 150.f, 150.f));
	BoundsCollision->SetCollisionObjectType(ECC_GameTraceChannel1); // Damageable
	BoundsCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	BoundsCollision->SetCollisionResponseToAllChannels(ECR_Block);
	BoundsCollision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// Dynamic Recast voxelizes BoundsCollision directly so walls carve exact rotated footprints.
	BoundsCollision->SetCanEverAffectNavigation(true);
	BoundsCollision->bDynamicObstacle = false;
	BoundsCollision->CanCharacterStepUpOn = ECB_No;
	BoundsCollision->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.f));
	BoundsCollision->SetGenerateOverlapEvents(false);
	SetRootComponent(BoundsCollision);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(BoundsCollision);

	RubbleVisualComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RubbleVisual"));
	RubbleVisualComponent->SetupAttachment(BoundsCollision);
	RubbleVisualComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RubbleVisualComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	RubbleVisualComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RubbleVisualComponent->SetCanEverAffectNavigation(false);
	RubbleVisualComponent->SetGenerateOverlapEvents(false);
	RubbleVisualComponent->SetVisibility(false, true);

	HealthComponent = CreateDefaultSubobject<UIFHealthComponent>(TEXT("Health"));

	OverheadWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("OverheadVitals"));
	OverheadWidgetComponent->SetupAttachment(BoundsCollision);
	OverheadWidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	OverheadWidgetComponent->SetDrawSize(FVector2D(110.f, 16.f));
	OverheadWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, 340.f));
	OverheadWidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverheadWidgetComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	OverheadWidgetComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	OverheadWidgetComponent->SetGenerateOverlapEvents(false);
	OverheadWidgetComponent->SetCastShadow(false);
	OverheadWidgetComponent->SetCanEverAffectNavigation(false);

	PerceptionStimuliSource = CreateDefaultSubobject<UAIPerceptionStimuliSourceComponent>(TEXT("PerceptionStimuliSource"));
	PerceptionStimuliSource->bAutoRegister = true;
	PerceptionStimuliSource->RegisterForSense(UAISense_Sight::StaticClass());

	SelectionRingComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectionRing"));
	SelectionRingComponent->SetupAttachment(BoundsCollision);
	SelectionRingComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SelectionRingComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	SelectionRingComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	SelectionRingComponent->SetGenerateOverlapEvents(false);
	SelectionRingComponent->SetCanEverAffectNavigation(false);
	SelectionRingComponent->SetCastShadow(false);
	SelectionRingComponent->bReceivesDecals = false;
	SelectionRingComponent->SetVisibility(false);
}

void AIFStructure::InitializeStructure(
	UIFStructureDataAsset* InStructureData,
	FGenericTeamId InTeamID,
	AIFTerritory* InOwningTerritory,
	bool bStartActive)
{
	if (IsValid(InStructureData))
	{
		StructureData = InStructureData;
	}

	TeamID = InTeamID;
	OwningTerritory = InOwningTerritory;
	bInitializedFromSpawn = true;

	if (bStartActive || !IsValid(StructureData) || StructureData->bInstantBuild)
	{
		StructureState = EIFStructureState::Active;
		ConstructionProgress = 1.f;
	}
	else
	{
		StructureState = EIFStructureState::UnderConstruction;
		ConstructionProgress = 0.f;
	}

	ApplyFootprintConfiguration();

	if (IsValid(HealthComponent) && IsValid(StructureData))
	{
		HealthComponent->InitializeHealth(StructureData->MaxHealth);
	}
}

void AIFStructure::BeginPlay()
{
	Super::BeginPlay();

	ApplyFootprintConfiguration();
	EnforceCameraIgnoreOnVisuals();

	if (IsValid(HealthComponent))
	{
		if (!bInitializedFromSpawn && IsValid(StructureData))
		{
			HealthComponent->InitializeHealth(StructureData->MaxHealth);
		}
		HealthComponent->OnHealthChanged.AddUniqueDynamic(this, &AIFStructure::HandleHealthChanged);
		HealthComponent->OnDeath.AddUniqueDynamic(this, &AIFStructure::HandleHealthDepleted);
	}

	// Order-independent registration for level-placed structures.
	if (!OwningTerritory.IsValid())
	{
		if (const UWorld* const World = GetWorld())
		{
			if (const AIFGameState* const GS = World->GetGameState<AIFGameState>())
			{
				if (AIFTerritory* const EnclosingTerritory = GS->FindTerritoryAtLocation(GetActorLocation()))
				{
					OwningTerritory = EnclosingTerritory;
				}
			}
		}
	}

	if (AIFTerritory* const Territory = OwningTerritory.Get())
	{
		Territory->RegisterStructure(this);
	}

	RequestStructureAssets();
	ApplyTeamVisuals();
	RefreshOverheadWidget();

	if (IsStructureActive())
	{
		OnStructureActivated();
		if (IsValid(StructureData) && bInitializedFromSpawn)
		{
			if (UParticleSystem* const BuildVFX = StructureData->BuildCompleteCascadeVFX.Get())
			{
				UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), BuildVFX, GetActorLocation(), FRotator::ZeroRotator, true);
			}
		}
	}
}

void AIFStructure::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (StructureAssetsLoadHandle.IsValid())
	{
		StructureAssetsLoadHandle->CancelHandle();
		StructureAssetsLoadHandle.Reset();
	}

	if (IsValid(HealthComponent))
	{
		HealthComponent->OnHealthChanged.RemoveDynamic(this, &AIFStructure::HandleHealthChanged);
		HealthComponent->OnDeath.RemoveDynamic(this, &AIFStructure::HandleHealthDepleted);
	}

	if (IsValid(ActiveDestructionGC))
	{
		ActiveDestructionGC->DestroyComponent();
		ActiveDestructionGC = nullptr;
	}

	if (IsValid(OverheadWidgetComponent))
	{
		if (UIFVitalBarsWidget* const OverheadWidget = Cast<UIFVitalBarsWidget>(OverheadWidgetComponent->GetUserWidgetObject()))
		{
			OverheadWidget->UnbindVitalComponents();
		}
	}

	if (AIFTerritory* const Territory = OwningTerritory.Get())
	{
		Territory->UnregisterStructure(this);
	}

	Super::EndPlay(EndPlayReason);
}

EIFStructureType AIFStructure::GetStructureType() const
{
	return IsValid(StructureData) ? StructureData->StructureType : EIFStructureType::Farm;
}

void AIFStructure::SetOwningTerritory(AIFTerritory* InTerritory)
{
	OwningTerritory = InTerritory;
}

FVector AIFStructure::GetFootprintExtent() const
{
	if (IsValid(StructureData))
	{
		return StructureData->FootprintExtent;
	}
	return IsValid(BoundsCollision) ? BoundsCollision->GetUnscaledBoxExtent() : FVector(150.f, 150.f, 150.f);
}

FVector AIFStructure::GetClosestSurfacePoint2D(const FVector& FromLocation) const
{
	if (!IsValid(BoundsCollision))
	{
		return GetActorLocation();
	}

	const FTransform BoxTransform = BoundsCollision->GetComponentTransform();
	const FVector LocalPoint = BoxTransform.InverseTransformPosition(FromLocation);
	const FVector Extent = BoundsCollision->GetUnscaledBoxExtent();

	const FVector ClampedLocal(
		FMath::Clamp(LocalPoint.X, -Extent.X, Extent.X),
		FMath::Clamp(LocalPoint.Y, -Extent.Y, Extent.Y),
		0.f);

	FVector WorldSurface = BoxTransform.TransformPosition(ClampedLocal);
	WorldSurface.Z = GetActorLocation().Z;
	return WorldSurface;
}

float AIFStructure::GetHorizontalSurfaceDistance(const FVector& FromLocation) const
{
	const FVector ClosestPoint = GetClosestSurfacePoint2D(FromLocation);
	return FVector::Dist2D(FromLocation, ClosestPoint);
}

void AIFStructure::GetWallSnapEndpoints(FVector& OutNegXEnd, FVector& OutPosXEnd) const
{
	const FVector Extent = GetFootprintExtent();
	const FTransform ActorXf = GetActorTransform();
	OutNegXEnd = ActorXf.TransformPosition(FVector(-Extent.X, 0.f, 0.f));
	OutPosXEnd = ActorXf.TransformPosition(FVector(Extent.X, 0.f, 0.f));
}

void AIFStructure::AddConstructionProgress(float DeltaProgress)
{
	if (StructureState != EIFStructureState::UnderConstruction || DeltaProgress <= 0.f)
	{
		return;
	}

	ConstructionProgress = FMath::Clamp(ConstructionProgress + DeltaProgress, 0.f, 1.f);
	if (ConstructionProgress >= 1.f - KINDA_SMALL_NUMBER)
	{
		ConstructionProgress = 1.f;
		SetStructureState(EIFStructureState::Active);
		OnStructureActivated();
	}
}

void AIFStructure::RepairStructure(float HealAmount)
{
	if (!IsStructureAlive() || HealAmount <= 0.f || !IsValid(HealthComponent))
	{
		return;
	}

	HealthComponent->ApplyHealing(HealAmount);
}

void AIFStructure::CollapseStructure(AActor* KillerActor)
{
	if (!IsStructureAlive())
	{
		return;
	}

	LastDamageInstigator = KillerActor;
	HandleStructureDestroyedInternal(KillerActor);
}

void AIFStructure::SetHostileTargetHighlighted(bool bHighlighted)
{
	if (!IsStructureAlive())
	{
		bHighlighted = false;
	}

	if (bHostileRingHighlighted == bHighlighted)
	{
		return;
	}

	bHostileRingHighlighted = bHighlighted;
	RefreshSelectionRingVisual();
	UpdateOverheadWidgetVisibility();
}

void AIFStructure::SetFriendlySelected(bool bSelected)
{
	if (!IsStructureAlive())
	{
		bSelected = false;
	}

	if (bFriendlySelected == bSelected)
	{
		return;
	}

	bFriendlySelected = bSelected;
	RefreshSelectionRingVisual();
	UpdateOverheadWidgetVisibility();
}

void AIFStructure::RelocateStructure(const FVector& NewLocation, float NewYaw, AIFTerritory* NewTerritory)
{
	if (!IsStructureAlive())
	{
		return;
	}

	// Uncarve NavMesh at old location before moving.
	if (IsValid(BoundsCollision))
	{
		BoundsCollision->SetCanEverAffectNavigation(false);
		FNavigationSystem::UpdateComponentData(*BoundsCollision);
	}

	SetActorLocationAndRotation(
		NewLocation,
		FRotator(0.f, NewYaw, 0.f),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	if (IsValid(NewTerritory) && NewTerritory != OwningTerritory.Get())
	{
		if (AIFTerritory* const OldTerritory = OwningTerritory.Get())
		{
			OldTerritory->UnregisterStructure(this);
		}
		NewTerritory->RegisterStructure(this);
	}

	// Re-carve NavMesh at new location (current HP is preserved untouched).
	if (IsValid(BoundsCollision))
	{
		BoundsCollision->SetCanEverAffectNavigation(true);
		FNavigationSystem::UpdateComponentData(*BoundsCollision);
	}
}

void AIFStructure::DemolishStructure()
{
	if (!IsStructureAlive() || IsKeep())
	{
		return;
	}

	HandleStructureDestroyedInternal(nullptr);
}

float AIFStructure::TakeDamage(
	float DamageAmount,
	struct FDamageEvent const& DamageEvent,
	class AController* EventInstigator,
	AActor* DamageCauser)
{
	if (DamageAmount <= 0.f || !IsStructureAlive())
	{
		return 0.f;
	}

	if (IsValid(DamageCauser) && IFTeamUtils::AreFriendly(this, DamageCauser))
	{
		return 0.f;
	}
	if (IsValid(EventInstigator) && IFTeamUtils::AreFriendly(this, EventInstigator->GetPawn()))
	{
		return 0.f;
	}

	if (!IsValid(HealthComponent) || HealthComponent->IsInvincible())
	{
		return 0.f;
	}

	AActor* InstigatorActor = IsValid(EventInstigator) ? static_cast<AActor*>(EventInstigator->GetPawn()) : nullptr;
	if (!IsValid(InstigatorActor))
	{
		InstigatorActor = DamageCauser;
	}
	if (IsValid(InstigatorActor))
	{
		LastDamageInstigator = InstigatorActor;
	}

	const float PrevHealth = HealthComponent->GetCurrentHealth();
	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	return PrevHealth - HealthComponent->GetCurrentHealth();
}

void AIFStructure::SetGenericTeamId(const FGenericTeamId& InTeamID)
{
	if (TeamID == InTeamID)
	{
		return;
	}

	const FGenericTeamId PreviousTeam = TeamID;
	TeamID = InTeamID;

	ApplyTeamVisuals();
	RefreshOverheadWidget();
	OnTeamChanged(PreviousTeam, TeamID);
}

ETeamAttitude::Type AIFStructure::GetTeamAttitudeTowards(const AActor& Other) const
{
	return IFTeamUtils::SolveAttitude(TeamID, IFTeamUtils::GetActorTeamId(&Other));
}

void AIFStructure::SetStructureState(EIFStructureState NewState)
{
	if (StructureState == NewState)
	{
		return;
	}

	const EIFStructureState PreviousState = StructureState;
	StructureState = NewState;
	UpdateOverheadWidgetVisibility();
	OnStructureStateChanged.Broadcast(this, PreviousState, NewState);
}

void AIFStructure::ApplyFootprintConfiguration()
{
	const FVector Extent = GetFootprintExtent();

	if (IsValid(BoundsCollision))
	{
		// Double Z extent around Z=0 root so overhead clicks and traces cover the full structure height.
		BoundsCollision->SetBoxExtent(FVector(Extent.X, Extent.Y, Extent.Z * 2.f), true);
		BoundsCollision->CanCharacterStepUpOn = ECB_No;
		BoundsCollision->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.f));
		if (UBodySetup* const BodySetup = BoundsCollision->GetBodySetup())
		{
			BodySetup->WalkableSlopeOverride = FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.f);
		}
		BoundsCollision->SetCanEverAffectNavigation(IsStructureAlive());
		FNavigationSystem::UpdateComponentData(*BoundsCollision);
	}

	if (IsValid(OverheadWidgetComponent))
	{
		const float ZOffset = IsValid(StructureData) ? StructureData->OverheadBarZOffset : (Extent.Z * 2.f + 40.f);
		OverheadWidgetComponent->SetRelativeLocation(FVector(0.f, 0.f, ZOffset));
	}

	if (IsValid(SelectionRingComponent))
	{
		const float OutlineWidth = (Extent.X + SelectionOutlineMarginCm) * 2.f;
		const float OutlineHeight = (Extent.Y + SelectionOutlineMarginCm) * 2.f;
		SelectionRingComponent->SetRelativeLocation(FVector(0.f, 0.f, 5.f));
		SelectionRingComponent->SetRelativeScale3D(FVector(FMath::Max(0.2f, OutlineWidth / 100.f), FMath::Max(0.2f, OutlineHeight / 100.f), 0.02f));

		if (IsValid(SelectionOutlineMID))
		{
			SelectionOutlineMID->SetScalarParameterValue(TEXT("OutlineWidth"), OutlineWidth);
			SelectionOutlineMID->SetScalarParameterValue(TEXT("OutlineHeight"), OutlineHeight);
		}
	}
}

void AIFStructure::ApplyTeamVisuals()
{
	UMaterialInterface* TeamMat = nullptr;
	const uint8 RawTeamId = TeamID.GetId();
	if (RawTeamId == IFTeamIds::Player)
	{
		TeamMat = DawnTeamMaterial.Get();
	}
	else if (RawTeamId == IFTeamIds::Enemy)
	{
		TeamMat = DuskTeamMaterial.Get();
	}
	else
	{
		TeamMat = NeutralTeamMaterial.Get();
	}

	if (!IsValid(TeamMat))
	{
		return;
	}

	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* const Prim : Primitives)
	{
		if (IsValid(Prim) && Prim->ComponentHasTag(TeamColoredComponentTag))
		{
			const int32 NumMats = FMath::Max(1, Prim->GetNumMaterials());
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				Prim->SetMaterial(MatIdx, TeamMat);
			}
		}
	}
}

void AIFStructure::RefreshOverheadWidget()
{
	if (!IsValid(OverheadWidgetComponent))
	{
		return;
	}

	if (!IsStructureAlive())
	{
		if (UIFVitalBarsWidget* const OverheadWidget = Cast<UIFVitalBarsWidget>(OverheadWidgetComponent->GetUserWidgetObject()))
		{
			OverheadWidget->UnbindVitalComponents();
		}
		OverheadWidgetComponent->SetVisibility(false);
		return;
	}

	if (OverheadWidgetComponent->GetWidgetClass() == nullptr)
	{
		return;
	}

	OverheadWidgetComponent->InitWidget();
	if (UIFVitalBarsWidget* const OverheadWidget = Cast<UIFVitalBarsWidget>(OverheadWidgetComponent->GetUserWidgetObject()))
	{
		OverheadWidget->BindToVitalComponents(HealthComponent, nullptr);
		const uint8 RawTeamId = TeamID.GetId();
		FLinearColor BarColor = NeutralHealthBarColor;
		if (RawTeamId == IFTeamIds::Player)
		{
			BarColor = AllyHealthBarColor;
		}
		else if (RawTeamId == IFTeamIds::Enemy)
		{
			BarColor = EnemyHealthBarColor;
		}
		OverheadWidget->SetHealthBarColor(BarColor);
	}
	UpdateOverheadWidgetVisibility();
}

void AIFStructure::UpdateOverheadWidgetVisibility()
{
	if (!IsValid(OverheadWidgetComponent))
	{
		return;
	}

	if (!IsStructureAlive())
	{
		OverheadWidgetComponent->SetVisibility(false);
		return;
	}

	const bool bDamaged = IsValid(HealthComponent)
		&& (HealthComponent->GetCurrentHealth() < HealthComponent->GetMaxHealth() - KINDA_SMALL_NUMBER);
	const bool bUnderConstruction = (StructureState == EIFStructureState::UnderConstruction);
	const bool bShouldShow = bDamaged || bFriendlySelected || bHostileRingHighlighted || bUnderConstruction;

	// Toggle inner Slate visibility while keeping the Screen UWidgetComponent active so UE 5.8
	// never triggers RemoveWidgetFromScreen -> NativeDestruct on full-HP structures.
	OverheadWidgetComponent->SetVisibility(true);
	if (UUserWidget* const OverheadWidget = OverheadWidgetComponent->GetUserWidgetObject())
	{
		OverheadWidget->SetVisibility(bShouldShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void AIFStructure::HandleHealthChanged(float)
{
	UpdateOverheadWidgetVisibility();
}

void AIFStructure::RefreshSelectionRingVisual()
{
	if (!IsValid(SelectionRingComponent))
	{
		return;
	}

	if (!IsStructureAlive() || (!bHostileRingHighlighted && !bFriendlySelected))
	{
		SelectionRingComponent->SetVisibility(false);
		return;
	}

	if (UStaticMesh* const RingMesh = SelectionRingMesh.Get())
	{
		SelectionRingComponent->SetStaticMesh(RingMesh);
	}

	UMaterialInterface* SourceMat = nullptr;
	if (bHostileRingHighlighted)
	{
		SourceMat = HostileRingMaterial.Get();
	}
	else if (bFriendlySelected)
	{
		SourceMat = FriendlyRingMaterial.Get();
		if (!IsValid(SourceMat))
		{
			SourceMat = DawnTeamMaterial.Get();
		}
	}

	if (IsValid(SourceMat))
	{
		if (!IsValid(SelectionOutlineMID) || LastSelectionOutlineSourceMat.Get() != SourceMat)
		{
			SelectionOutlineMID = UMaterialInstanceDynamic::Create(SourceMat, this);
			LastSelectionOutlineSourceMat = SourceMat;
			if (IsValid(SelectionOutlineMID))
			{
				SelectionRingComponent->SetMaterial(0, SelectionOutlineMID);
			}
		}

		if (IsValid(SelectionOutlineMID))
		{
			const FVector Extent = GetFootprintExtent();
			const float OutlineWidth = (Extent.X + SelectionOutlineMarginCm) * 2.f;
			const float OutlineHeight = (Extent.Y + SelectionOutlineMarginCm) * 2.f;
			SelectionOutlineMID->SetScalarParameterValue(TEXT("OutlineWidth"), OutlineWidth);
			SelectionOutlineMID->SetScalarParameterValue(TEXT("OutlineHeight"), OutlineHeight);
		}
		else
		{
			SelectionRingComponent->SetMaterial(0, SourceMat);
		}
	}
	SelectionRingComponent->SetVisibility(true);
}

void AIFStructure::HandleHealthDepleted(AActor*)
{
	HandleStructureDestroyedInternal(LastDamageInstigator.Get());
}

void AIFStructure::HandleStructureDestroyedInternal(AActor* KillerActor)
{
	if (!IsStructureAlive())
	{
		return;
	}

	SetStructureState(EIFStructureState::Destroyed);
	OnStructureDeactivated();

	bHostileRingHighlighted = false;
	bFriendlySelected = false;
	RefreshSelectionRingVisual();

	if (IsValid(PerceptionStimuliSource))
	{
		PerceptionStimuliSource->UnregisterFromPerceptionSystem();
	}

	if (IsValid(BoundsCollision))
	{
		BoundsCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoundsCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
		BoundsCollision->SetCanEverAffectNavigation(false);
		FNavigationSystem::UpdateComponentData(*BoundsCollision);
	}

	RefreshOverheadWidget();

	if (IsValid(VisualRoot))
	{
		VisualRoot->SetVisibility(false, true);
	}

	TriggerDestructionFeedback();

	OnStructureDestroyed.Broadcast(this, KillerActor);

	const float LifeSpan = IsValid(StructureData) ? StructureData->DebrisLifeSpan : 4.f;
	if (LifeSpan > 0.f)
	{
		SetLifeSpan(LifeSpan);
	}
	else
	{
		Destroy();
	}
}

void AIFStructure::TriggerDestructionFeedback()
{
	bool bSpawnedChaosDestruction = false;

	if (!IsValid(StructureData) || StructureData->DestructionGeometryCollection.IsNull())
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Structure] %s has no DestructionGeometryCollection assigned; falling back to static rubble."), *GetName());
	}
	else
	{
		UGeometryCollection* DestructionGC = StructureData->DestructionGeometryCollection.Get();
		if (!IsValid(DestructionGC))
		{
			DestructionGC = StructureData->DestructionGeometryCollection.LoadSynchronous();
		}

		if (IsValid(DestructionGC) && !IsValid(ActiveDestructionGC))
		{
			ActiveDestructionGC = NewObject<UGeometryCollectionComponent>(this, TEXT("DestructionGeometryCollection"));
			if (IsValid(ActiveDestructionGC))
			{
				auto ApplyDebrisCollisionProfile = [](UGeometryCollectionComponent* GCComp)
				{
					GCComp->SetCanEverAffectNavigation(false);
					GCComp->SetGenerateOverlapEvents(false);
					GCComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
					GCComp->SetCollisionObjectType(ECC_PhysicsBody);
					GCComp->SetCollisionResponseToAllChannels(ECR_Ignore);
					GCComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
					GCComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
					GCComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
					GCComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
					GCComp->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
				};

				ActiveDestructionGC->SetupAttachment(BoundsCollision);
				ActiveDestructionGC->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
				ActiveDestructionGC->ObjectType = EObjectStateTypeEnum::Chaos_Object_Dynamic;
				ActiveDestructionGC->SetEnableDamageFromCollision(false);
				ApplyDebrisCollisionProfile(ActiveDestructionGC);

				// Register first so ComponentToWorld is valid and SetRestCollection runs full OnCreatePhysicsState().
				ActiveDestructionGC->RegisterComponent();
				ActiveDestructionGC->SetRestCollection(DestructionGC, true);
				ApplyDebrisCollisionProfile(ActiveDestructionGC);
				ActiveDestructionGC->SetSimulatePhysics(true);
				ActiveDestructionGC->CrumbleActiveClusters();

				const FVector Extent = GetFootprintExtent();
				const FVector BreakOrigin = GetActorLocation() + FVector(0.f, 0.f, Extent.Z * 0.5f);
				const float RadialImpulse = StructureData->ChaosRadialImpulse;

				if (UWorld* const World = GetWorld())
				{
					TWeakObjectPtr<AIFStructure> WeakThis(this);
					TWeakObjectPtr<UGeometryCollectionComponent> WeakGC(ActiveDestructionGC);
					World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
						[WeakThis, WeakGC, BreakOrigin, RadialImpulse]()
						{
							UGeometryCollectionComponent* const GC = WeakGC.Get();
							AIFStructure* const Owner = WeakThis.Get();
							if (!IsValid(GC) || !IsValid(Owner))
							{
								return;
							}

							GC->CrumbleActiveClusters();

							if (RadialImpulse > 0.f)
							{
								if (URadialVector* const RadialField = NewObject<URadialVector>(Owner))
								{
									RadialField->SetRadialVector(RadialImpulse, BreakOrigin);
									GC->ApplyPhysicsField(
										true,
										EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
										nullptr,
										RadialField);
								}
							}
						}));
				}

				const int32 TransformCount = ActiveDestructionGC->GetDynamicCollection()
					? ActiveDestructionGC->GetDynamicCollection()->GetNumTransforms()
					: 0;
				UE_LOG(LogIronField, Log, TEXT("[IF-Structure] %s spawned Chaos GC '%s' at %s (Transforms=%d, PhysicsProxy=%s)."),
					*GetName(),
					*DestructionGC->GetName(),
					*ActiveDestructionGC->GetComponentLocation().ToCompactString(),
					TransformCount,
					ActiveDestructionGC->GetPhysicsProxy() ? TEXT("Valid") : TEXT("Null"));

				bSpawnedChaosDestruction = true;
			}
		}
	}

	if (!bSpawnedChaosDestruction && IsValid(RubbleVisualComponent) && IsValid(RubbleVisualComponent->GetStaticMesh()))
	{
		RubbleVisualComponent->SetVisibility(true, true);
	}

	if (IsValid(StructureData))
	{
		if (UParticleSystem* const DestroyVFX = StructureData->DestructionCascadeVFX.Get())
		{
			UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), DestroyVFX, GetActorLocation(), FRotator::ZeroRotator, true);
		}
	}
}

void AIFStructure::RequestStructureAssets()
{
	TArray<FSoftObjectPath> AssetPaths;
	auto AddIfSet = [&AssetPaths](const auto& SoftRef)
	{
		if (!SoftRef.IsNull())
		{
			AssetPaths.AddUnique(SoftRef.ToSoftObjectPath());
		}
	};

	AddIfSet(DawnTeamMaterial);
	AddIfSet(DuskTeamMaterial);
	AddIfSet(NeutralTeamMaterial);
	AddIfSet(SelectionRingMesh);
	AddIfSet(FriendlyRingMaterial);
	AddIfSet(HostileRingMaterial);

	if (IsValid(StructureData))
	{
		AddIfSet(StructureData->DestructionGeometryCollection);
		AddIfSet(StructureData->DestructionCascadeVFX);
		AddIfSet(StructureData->BuildCompleteCascadeVFX);
	}

	if (AssetPaths.IsEmpty())
	{
		return;
	}

	StructureAssetsLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPaths,
		FStreamableDelegate::CreateUObject(this, &AIFStructure::HandleStructureAssetsLoaded));
}

void AIFStructure::HandleStructureAssetsLoaded()
{
	StructureAssetsLoadHandle.Reset();
	ApplyTeamVisuals();
	RefreshSelectionRingVisual();
}

void AIFStructure::EnforceCameraIgnoreOnVisuals()
{
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* const Prim : Primitives)
	{
		if (!IsValid(Prim))
		{
			continue;
		}

		Prim->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

		// Keep BoundsCollision as the sole collision/damage/NavMesh volume; all cosmetic meshes stay NoCollision.
		if (Prim != BoundsCollision)
		{
			Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Prim->SetCollisionResponseToAllChannels(ECR_Ignore);
			Prim->SetCanEverAffectNavigation(false);
			Prim->SetGenerateOverlapEvents(false);
		}
	}
}
