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
#include "Core/IFGameTypes.h"
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
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Stats/IFHealthComponent.h"
#include "TimerManager.h"
#include "UI/IFVitalBarsWidget.h"

const FName AIFStructure::TeamColoredComponentTag(TEXT("TeamColored"));

AIFStructure::AIFStructure()
{
	PrimaryActorTick.bCanEverTick = false;

	BoundsCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsCollision"));
	BoundsCollision->InitBoxExtent(FVector(150.f, 150.f, 150.f));
	BoundsCollision->SetCollisionObjectType(IFCollision::Damageable);
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

	EnforceCameraIgnoreOnVisuals();
	ApplyFootprintConfiguration();

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
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HostCollapseTimerHandle);
	}

	if (AIFStructure* const Host = HostStructure.Get(); IsValid(Host))
	{
		Host->OnStructureDestroyed.RemoveDynamic(this, &AIFStructure::HandleHostStructureDestroyed);
	}
	HostStructure.Reset();

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

	CleanupActiveDestructionGCs();
	UnregisterDestroyedStructure(this);

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
	if (!ExtraCollisionBoxes.IsEmpty())
	{
		auto FindClosestAmong = [this, &FromLocation](bool bGroundReachableOnly, FVector& OutPoint) -> bool
		{
			bool bHasAny = false;
			float BestDistSq = MAX_flt;
			for (const UBoxComponent* const Box : ExtraCollisionBoxes)
			{
				if (!IsValid(Box))
				{
					continue;
				}
				const FVector Extent = Box->GetUnscaledBoxExtent();
				if (bGroundReachableOnly && (Box->GetRelativeLocation().Z - Extent.Z) >= 180.f)
				{
					continue;
				}

				const FTransform BoxTransform = Box->GetComponentTransform();
				const FVector LocalPoint = BoxTransform.InverseTransformPosition(FromLocation);
				const FVector ClampedLocal(
					FMath::Clamp(LocalPoint.X, -Extent.X, Extent.X),
					FMath::Clamp(LocalPoint.Y, -Extent.Y, Extent.Y),
					0.f);
				FVector WorldSurface = BoxTransform.TransformPosition(ClampedLocal);
				WorldSurface.Z = GetActorLocation().Z;
				const float DistSq = FVector::DistSquared2D(FromLocation, WorldSurface);
				if (!bHasAny || DistSq < BestDistSq)
				{
					bHasAny = true;
					BestDistSq = DistSq;
					OutPoint = WorldSurface;
				}
			}
			return bHasAny;
		};

		FVector BestPoint = GetActorLocation();
		if (FindClosestAmong(true, BestPoint) || FindClosestAmong(false, BestPoint))
		{
			return BestPoint;
		}
	}

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

void AIFStructure::ComputeWorldCollisionBoxes(
	const UIFStructureDataAsset* Data,
	const FVector& Location,
	float YawDeg,
	TArray<FIFWorldCollisionBox>& OutBoxes)
{
	OutBoxes.Reset();
	if (!IsValid(Data))
	{
		return;
	}

	const FTransform ActorXf(FRotator(0.f, YawDeg, 0.f), Location);

	if (Data->CollisionBoxes.IsEmpty())
	{
		const FVector Ext = Data->FootprintExtent;
		FIFWorldCollisionBox Box;
		Box.WorldCenter = Location + FVector(0.f, 0.f, Ext.Z);
		Box.HalfExtent = Ext;
		Box.WorldRotation = FRotator(0.f, YawDeg, 0.f);
		Box.WorldMinZ = Location.Z;
		Box.WorldMaxZ = Location.Z + Ext.Z * 2.f;
		Box.Role = EIFCollisionRole::SolidUnwalkable;
		OutBoxes.Add(Box);
		return;
	}

	OutBoxes.Reserve(Data->CollisionBoxes.Num());
	for (const FIFCollisionBox& LocalBox : Data->CollisionBoxes)
	{
		const FTransform LocalXf(LocalBox.LocalRotation, LocalBox.LocalCenter);
		const FTransform WorldXf = LocalXf * ActorXf;

		FIFWorldCollisionBox WorldBox;
		WorldBox.WorldCenter = WorldXf.GetLocation();
		WorldBox.HalfExtent = LocalBox.HalfExtent;
		WorldBox.WorldRotation = WorldXf.GetRotation().Rotator();
		WorldBox.Role = LocalBox.Role;

		// Exact world Z bounds of the rotated 3D box.
		const FMatrix RotMat = WorldXf.GetRotation().ToMatrix();
		const float HalfWorldZ =
			FMath::Abs(RotMat.GetScaledAxis(EAxis::X).Z) * LocalBox.HalfExtent.X +
			FMath::Abs(RotMat.GetScaledAxis(EAxis::Y).Z) * LocalBox.HalfExtent.Y +
			FMath::Abs(RotMat.GetScaledAxis(EAxis::Z).Z) * LocalBox.HalfExtent.Z;
		WorldBox.WorldMinZ = WorldBox.WorldCenter.Z - HalfWorldZ;
		WorldBox.WorldMaxZ = WorldBox.WorldCenter.Z + HalfWorldZ;

		OutBoxes.Add(WorldBox);
	}
}

void AIFStructure::ComputeWorldConnectors(
	const UIFStructureDataAsset* Data,
	const FVector& Location,
	float YawDeg,
	TArray<FIFModularConnector>& OutWorldConnectors)
{
	OutWorldConnectors.Reset();
	if (!IsValid(Data) || Data->Connectors.IsEmpty())
	{
		return;
	}

	const float Rad = FMath::DegreesToRadians(YawDeg);
	const float CosA = FMath::Cos(Rad);
	const float SinA = FMath::Sin(Rad);

	OutWorldConnectors.Reserve(Data->Connectors.Num());
	for (const FIFModularConnector& LocalConn : Data->Connectors)
	{
		FIFModularConnector WorldConn;
		WorldConn.Channel = LocalConn.Channel;
		WorldConn.LocalPosition.X = Location.X + (LocalConn.LocalPosition.X * CosA - LocalConn.LocalPosition.Y * SinA);
		WorldConn.LocalPosition.Y = Location.Y + (LocalConn.LocalPosition.X * SinA + LocalConn.LocalPosition.Y * CosA);

		const FVector2D Dir = LocalConn.LocalOutwardNormal.GetSafeNormal();
		WorldConn.LocalOutwardNormal.X = Dir.X * CosA - Dir.Y * SinA;
		WorldConn.LocalOutwardNormal.Y = Dir.X * SinA + Dir.Y * CosA;
		WorldConn.LocalOutwardNormal = WorldConn.LocalOutwardNormal.GetSafeNormal();

		OutWorldConnectors.Add(WorldConn);
	}
}

void AIFStructure::GetWorldCollisionBoxes(TArray<FIFWorldCollisionBox>& OutBoxes) const
{
	if (IsValid(StructureData))
	{
		ComputeWorldCollisionBoxes(StructureData, GetActorLocation(), GetActorRotation().Yaw, OutBoxes);
		return;
	}

	const FVector Ext = GetFootprintExtent();
	const FVector Loc = GetActorLocation();
	OutBoxes.Reset();
	FIFWorldCollisionBox Box;
	Box.WorldCenter = Loc + FVector(0.f, 0.f, Ext.Z);
	Box.HalfExtent = Ext;
	Box.WorldRotation = FRotator(0.f, GetActorRotation().Yaw, 0.f);
	Box.WorldMinZ = Loc.Z;
	Box.WorldMaxZ = Loc.Z + Ext.Z * 2.f;
	Box.Role = EIFCollisionRole::SolidUnwalkable;
	OutBoxes.Add(Box);
}

void AIFStructure::GetWorldConnectors(TArray<FIFModularConnector>& OutWorldConnectors) const
{
	ComputeWorldConnectors(StructureData, GetActorLocation(), GetActorRotation().Yaw, OutWorldConnectors);
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

void AIFStructure::SetHostStructure(AIFStructure* InHost)
{
	if (AIFStructure* const OldHost = HostStructure.Get(); IsValid(OldHost))
	{
		OldHost->OnStructureDestroyed.RemoveDynamic(this, &AIFStructure::HandleHostStructureDestroyed);
	}

	HostStructure = (InHost != this) ? InHost : nullptr;
	if (AIFStructure* const NewHost = HostStructure.Get(); IsValid(NewHost) && NewHost->IsStructureAlive())
	{
		NewHost->OnStructureDestroyed.AddUniqueDynamic(this, &AIFStructure::HandleHostStructureDestroyed);
	}
}

void AIFStructure::HandleHostStructureDestroyed(AIFStructure*, AActor* KillerActor)
{
	if (!IsStructureAlive())
	{
		return;
	}

	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		CollapseStructure(KillerActor);
		return;
	}

	if (World->GetTimerManager().IsTimerActive(HostCollapseTimerHandle))
	{
		return;
	}

	TWeakObjectPtr<AIFStructure> WeakThis(this);
	TWeakObjectPtr<AActor> WeakKiller(KillerActor);
	World->GetTimerManager().SetTimer(
		HostCollapseTimerHandle,
		FTimerDelegate::CreateLambda([WeakThis, WeakKiller]()
		{
			if (AIFStructure* const Self = WeakThis.Get(); IsValid(Self) && Self->IsStructureAlive())
			{
				Self->CollapseStructure(WeakKiller.Get());
			}
		}),
		0.05f,
		false);
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
	for (UBoxComponent* const ExtraBox : ExtraCollisionBoxes)
	{
		if (IsValid(ExtraBox))
		{
			ExtraBox->SetCanEverAffectNavigation(false);
			FNavigationSystem::UpdateComponentData(*ExtraBox);
		}
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
	if (!ExtraCollisionBoxes.IsEmpty())
	{
		for (UBoxComponent* const ExtraBox : ExtraCollisionBoxes)
		{
			if (IsValid(ExtraBox))
			{
				ExtraBox->SetCanEverAffectNavigation(true);
				FNavigationSystem::UpdateComponentData(*ExtraBox);
			}
		}
	}
	else if (IsValid(BoundsCollision))
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
	if (IsValid(EventInstigator) && IFTeamUtils::AreFriendly(this, EventInstigator))
	{
		return 0.f;
	}

	if (!IsValid(HealthComponent))
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

	Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	return HealthComponent->ApplyDamage(DamageAmount, InstigatorActor);
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

void AIFStructure::SetStructureCollisionBlocking(bool bBlockPawnsAndNav)
{
	const bool bAlive = IsStructureAlive();
	const bool bShouldBlock = bBlockPawnsAndNav && bAlive;

	auto UpdateBox = [bShouldBlock](UBoxComponent* Box)
	{
		if (!IsValid(Box))
		{
			return;
		}
		Box->SetCollisionResponseToChannel(ECC_Pawn, bShouldBlock ? ECR_Block : ECR_Ignore);
		Box->SetCanEverAffectNavigation(bShouldBlock);
		FNavigationSystem::UpdateComponentData(*Box);
	};

	if (ExtraCollisionBoxes.IsEmpty())
	{
		UpdateBox(BoundsCollision);
	}
	else
	{
		for (UBoxComponent* const Box : ExtraCollisionBoxes)
		{
			UpdateBox(Box);
		}
	}
}

void AIFStructure::ApplyFootprintConfiguration()
{
	const FVector Extent = GetFootprintExtent();
	const bool bAlive = IsStructureAlive();

	for (UBoxComponent* const OldBox : ExtraCollisionBoxes)
	{
		if (IsValid(OldBox))
		{
			OldBox->SetCanEverAffectNavigation(false);
			FNavigationSystem::UpdateComponentData(*OldBox);
			OldBox->DestroyComponent();
		}
	}
	ExtraCollisionBoxes.Reset();

	auto ConfigureBoxComponent = [bAlive](UBoxComponent* Box, EIFCollisionRole BoxRole)
	{
		if (!IsValid(Box))
		{
			return;
		}
		Box->SetCollisionObjectType(IFCollision::Damageable);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Box->SetGenerateOverlapEvents(false);
		Box->bDynamicObstacle = false;

		if (BoxRole == EIFCollisionRole::WalkableDeck || BoxRole == EIFCollisionRole::WalkableRamp)
		{
			Box->CanCharacterStepUpOn = ECB_Yes;
			const FWalkableSlopeOverride SlopeOverride(WalkableSlope_Increase, 50.f);
			Box->SetWalkableSlopeOverride(SlopeOverride);
			if (UBodySetup* const BodySetup = Box->GetBodySetup())
			{
				BodySetup->WalkableSlopeOverride = SlopeOverride;
			}
		}
		else
		{
			Box->CanCharacterStepUpOn = ECB_No;
			const FWalkableSlopeOverride SlopeOverride(WalkableSlope_Unwalkable, 0.f);
			Box->SetWalkableSlopeOverride(SlopeOverride);
			if (UBodySetup* const BodySetup = Box->GetBodySetup())
			{
				BodySetup->WalkableSlopeOverride = SlopeOverride;
			}
		}

		Box->SetCanEverAffectNavigation(bAlive);
		FNavigationSystem::UpdateComponentData(*Box);
	};

	if (IsValid(BoundsCollision))
	{
		const bool bHasCustomBoxes = IsValid(StructureData) && !StructureData->CollisionBoxes.IsEmpty();
		if (bHasCustomBoxes)
		{
			// Multi-box / data-driven structures use BoundsCollision as a non-colliding root anchor at Z=0.
			BoundsCollision->SetBoxExtent(FVector(Extent.X, Extent.Y, Extent.Z * 2.f), false);
			BoundsCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			BoundsCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
			BoundsCollision->SetCanEverAffectNavigation(false);
			FNavigationSystem::UpdateComponentData(*BoundsCollision);

			const TArray<FIFCollisionBox>& CustomBoxes = StructureData->CollisionBoxes;
			for (int32 i = 0; i < CustomBoxes.Num(); ++i)
			{
				const FIFCollisionBox& BoxDef = CustomBoxes[i];
				const FName BoxName(*FString::Printf(TEXT("FootprintBox_%d"), i));
				UBoxComponent* const BoxComp = NewObject<UBoxComponent>(this, BoxName);
				if (!IsValid(BoxComp))
				{
					continue;
				}
				BoxComp->SetupAttachment(BoundsCollision);
				BoxComp->SetRelativeLocation(BoxDef.LocalCenter);
				BoxComp->SetRelativeRotation(BoxDef.LocalRotation);
				BoxComp->SetBoxExtent(BoxDef.HalfExtent, true);
				BoxComp->RegisterComponent();
				ConfigureBoxComponent(BoxComp, BoxDef.Role);
				ExtraCollisionBoxes.Add(BoxComp);
			}
		}
		else
		{
			// Fallback single-box footprint (e.g., Keep).
			BoundsCollision->SetBoxExtent(FVector(Extent.X, Extent.Y, Extent.Z * 2.f), true);
			ConfigureBoxComponent(BoundsCollision, EIFCollisionRole::SolidUnwalkable);
		}
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
			const int32 NumMats = Prim->GetNumMaterials();
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

	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HostCollapseTimerHandle);
	}
	if (AIFStructure* const Host = HostStructure.Get(); IsValid(Host))
	{
		Host->OnStructureDestroyed.RemoveDynamic(this, &AIFStructure::HandleHostStructureDestroyed);
	}
	HostStructure.Reset();

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

	for (UBoxComponent* const ExtraBox : ExtraCollisionBoxes)
	{
		if (IsValid(ExtraBox))
		{
			ExtraBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ExtraBox->SetCollisionResponseToAllChannels(ECR_Ignore);
			ExtraBox->SetCanEverAffectNavigation(false);
			FNavigationSystem::UpdateComponentData(*ExtraBox);
		}
	}

	RefreshOverheadWidget();

	if (IsValid(VisualRoot))
	{
		VisualRoot->SetVisibility(false, true);
	}

	TriggerDestructionFeedback();

	if (!ActiveDestructionGCs.IsEmpty())
	{
		RegisterDestroyedStructureAndEnforceCap(this);
	}

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

UStaticMeshComponent* AIFStructure::FindPrimaryVisualMeshComponent() const
{
	if (!IsValid(VisualRoot))
	{
		return nullptr;
	}

	TArray<USceneComponent*> VisualChildren;
	VisualRoot->GetChildrenComponents(true, VisualChildren);
	for (USceneComponent* const Child : VisualChildren)
	{
		if (UStaticMeshComponent* const MeshComp = Cast<UStaticMeshComponent>(Child))
		{
			if (MeshComp != RubbleVisualComponent
				&& MeshComp != SelectionRingComponent
				&& IsValid(MeshComp->GetStaticMesh()))
			{
				return MeshComp;
			}
		}
	}

	return nullptr;
}

UGeometryCollectionComponent* AIFStructure::SpawnFracturedGC(
	UGeometryCollection* GCAsset,
	const FTransform& WorldOrRelTransform,
	bool bIsWorldTransform,
	const FVector& ImpulseOrigin,
	float ImpulseRadiusOverride)
{
	if (!IsValid(GCAsset) || !IsValid(BoundsCollision))
	{
		return nullptr;
	}

	const FName UniqueGCName = MakeUniqueObjectName(
		this,
		UGeometryCollectionComponent::StaticClass(),
		TEXT("DestructionGeometryCollection"));
	UGeometryCollectionComponent* const GCComp = NewObject<UGeometryCollectionComponent>(this, UniqueGCName);
	if (!IsValid(GCComp))
	{
		return nullptr;
	}

	UPhysicalMaterial* StoneRubblePhysMat = nullptr;
	if (IsValid(StructureData) && !StructureData->DestructionPhysicalMaterial.IsNull())
	{
		StoneRubblePhysMat = StructureData->DestructionPhysicalMaterial.Get();
		if (!IsValid(StoneRubblePhysMat))
		{
			StoneRubblePhysMat = StructureData->DestructionPhysicalMaterial.LoadSynchronous();
		}
	}
	if (!IsValid(StoneRubblePhysMat))
	{
		static const FSoftObjectPath DefaultStoneRubblePath(TEXT("/Game/IronField/Structures/Destruction/PM_IF_StoneRubble.PM_IF_StoneRubble"));
		StoneRubblePhysMat = Cast<UPhysicalMaterial>(DefaultStoneRubblePath.TryLoad());
	}

	auto ApplyDebrisCollisionProfile = [StoneRubblePhysMat](UGeometryCollectionComponent* TargetGC)
	{
		TargetGC->SetCanEverAffectNavigation(false);
		TargetGC->SetUseRootProxyForNavigation(false);
		TargetGC->SetGenerateOverlapEvents(false);
		TargetGC->SetEnableDamageFromCollision(true);
		TargetGC->SetCastShadow(false);
		TargetGC->bCastDynamicShadow = false;
		TargetGC->CanCharacterStepUpOn = ECB_No;
		TargetGC->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		TargetGC->SetCollisionObjectType(ECC_PhysicsBody);
		TargetGC->SetCollisionResponseToAllChannels(ECR_Ignore);
		TargetGC->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		TargetGC->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		TargetGC->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		TargetGC->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		TargetGC->SetCollisionResponseToChannel(IFCollision::Damageable, ECR_Ignore);
		if (IsValid(StoneRubblePhysMat))
		{
			TargetGC->SetPhysMaterialOverride(StoneRubblePhysMat);
		}
		TargetGC->SetLinearDamping(0.35f);
		TargetGC->SetAngularDamping(0.55f);
	};

	GCComp->SetupAttachment(BoundsCollision);
	const FTransform RelativeXf = bIsWorldTransform
		? WorldOrRelTransform.GetRelativeTransform(BoundsCollision->GetComponentTransform())
		: WorldOrRelTransform;
	GCComp->SetRelativeTransform(RelativeXf);
	GCComp->ObjectType = EObjectStateTypeEnum::Chaos_Object_Dynamic;
	ApplyDebrisCollisionProfile(GCComp);

	// Register first so ComponentToWorld is valid, then apply RestCollection defaults and re-enforce debris overrides.
	GCComp->RegisterComponent();
	GCComp->SetRestCollection(GCAsset, /*bApplyAssetDefaults=*/true);
	ApplyDebrisCollisionProfile(GCComp);
	GCComp->SetSimulatePhysics(true);

	ActiveDestructionGCs.Add(GCComp);

	const float BreakStrain = (IsValid(StructureData) && StructureData->ChaosBreakStrain > 0.f)
		? StructureData->ChaosBreakStrain
		: 500000.f;
	const float PushMagnitude = IsValid(StructureData) ? StructureData->ChaosRadialImpulse : 60.f;
	const float ConfiguredRadius = (ImpulseRadiusOverride > 0.f)
		? ImpulseRadiusOverride
		: (IsValid(StructureData) ? StructureData->ChaosRadialFalloffRadius : 350.f);

	if (UWorld* const World = GetWorld())
	{
		TWeakObjectPtr<AIFStructure> WeakThis(this);
		TWeakObjectPtr<UGeometryCollectionComponent> WeakGC(GCComp);
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
			[WeakThis, WeakGC, ImpulseOrigin, BreakStrain, PushMagnitude, ConfiguredRadius]()
			{
				UGeometryCollectionComponent* const GC = WeakGC.Get();
				AIFStructure* const Owner = WeakThis.Get();
				if (!IsValid(GC) || !IsValid(Owner))
				{
					return;
				}

				GC->UpdateBounds();
				const FBoxSphereBounds GCBounds = GC->Bounds;
				const float EffectiveRadius = FMath::Max(ConfiguredRadius, GCBounds.SphereRadius * 1.35f);
				const FVector EffectiveOrigin = (GCBounds.SphereRadius > 1.f)
					? (GCBounds.Origin - FVector(0.f, 0.f, GCBounds.BoxExtent.Z * 0.2f))
					: ImpulseOrigin;

				const UGeometryCollection* const RestCollection = GC->GetRestCollection();
				const int32 RootIdx = IsValid(RestCollection) ? RestCollection->GetRootIndex() : INDEX_NONE;
				if (RootIdx != INDEX_NONE)
				{
					// Break only the Level 0 root cluster into Level 1 chunks so ground impact shatters them into Level 2 rubble.
					GC->CrumbleCluster(RootIdx);
				}
				else
				{
					if (URadialFalloff* const StrainField = NewObject<URadialFalloff>(Owner))
					{
						StrainField->SetRadialFalloff(
							BreakStrain,
							BreakStrain,
							1.f,
							0.f,
							EffectiveRadius,
							EffectiveOrigin,
							EFieldFalloffType::Field_FallOff_None);
						GC->ApplyPhysicsField(
							true,
							EGeometryCollectionPhysicsTypeEnum::Chaos_ExternalClusterStrain,
							nullptr,
							StrainField);
					}
					GC->CrumbleActiveClusters();
				}

				if (PushMagnitude > 0.f)
				{
					FVector PushDir = Owner->GetActorForwardVector().GetSafeNormal2D();
					if (Owner->LastDamageInstigator.IsValid())
					{
						const FVector Away = (Owner->GetActorLocation() - Owner->LastDamageInstigator->GetActorLocation()).GetSafeNormal2D();
						if (!Away.IsNearlyZero())
						{
							PushDir = Away;
						}
					}
					if (PushDir.IsNearlyZero())
					{
						PushDir = FVector::ForwardVector;
					}

					const FVector PrimaryVelocity = PushDir * PushMagnitude + FVector(0.f, 0.f, -PushMagnitude * 0.35f);
					if (UUniformVector* const UniformField = NewObject<UUniformVector>(Owner))
					{
						UniformField->SetUniformVector(PrimaryVelocity.Size(), PrimaryVelocity.GetSafeNormal());
						UFieldNodeBase* FinalField = UniformField;

						if (EffectiveRadius > 0.f)
						{
							URadialVector* const ScatterField = NewObject<URadialVector>(Owner);
							URadialFalloff* const FalloffField = NewObject<URadialFalloff>(Owner);
							UOperatorField* const MaskedScatter = NewObject<UOperatorField>(Owner);
							UOperatorField* const CombinedField = NewObject<UOperatorField>(Owner);
							if (IsValid(ScatterField) && IsValid(FalloffField) && IsValid(MaskedScatter) && IsValid(CombinedField))
							{
								ScatterField->SetRadialVector(PushMagnitude * 0.25f, EffectiveOrigin);
								FalloffField->SetRadialFalloff(
									1.f,
									0.f,
									1.f,
									0.f,
									EffectiveRadius,
									EffectiveOrigin,
									EFieldFalloffType::Field_Falloff_Linear);
								MaskedScatter->SetOperatorField(
									1.f,
									ScatterField,
									FalloffField,
									EFieldOperationType::Field_Multiply);
								CombinedField->SetOperatorField(
									1.f,
									UniformField,
									MaskedScatter,
									EFieldOperationType::Field_Add);
								FinalField = CombinedField;
							}
						}

						GC->ApplyPhysicsField(
							true,
							EGeometryCollectionPhysicsTypeEnum::Chaos_LinearVelocity,
							nullptr,
							FinalField);
					}
				}
			}));
	}

	const int32 TransformCount = GCComp->GetDynamicCollection()
		? GCComp->GetDynamicCollection()->GetNumTransforms()
		: 0;
	UE_LOG(LogIronField, Log, TEXT("[IF-Structure] %s spawned Chaos GC '%s' at %s (Transforms=%d, PhysicsProxy=%s)."),
		*GetName(),
		*GCAsset->GetName(),
		*GCComp->GetComponentLocation().ToCompactString(),
		TransformCount,
		GCComp->GetPhysicsProxy() ? TEXT("Valid") : TEXT("Null"));

	return GCComp;
}

void AIFStructure::SpawnDestructionVFX() const
{
	if (IsValid(StructureData))
	{
		if (UParticleSystem* const DestroyVFX = StructureData->DestructionCascadeVFX.Get())
		{
			UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), DestroyVFX, GetActorLocation(), FRotator::ZeroRotator, true);
		}
	}
}

void AIFStructure::TriggerDestructionFeedback()
{
	bool bSpawnedChaosDestruction = false;

	if (!IsValid(StructureData) || StructureData->DestructionGeometryCollection.IsNull())
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Structure] %s has no DestructionGeometryCollection assigned."), *GetName());
	}
	else
	{
		UGeometryCollection* DestructionGC = StructureData->DestructionGeometryCollection.Get();
		if (!IsValid(DestructionGC))
		{
			DestructionGC = StructureData->DestructionGeometryCollection.LoadSynchronous();
		}

		if (IsValid(DestructionGC))
		{
			const UStaticMeshComponent* const PrimaryVisual = FindPrimaryVisualMeshComponent();
			const bool bUseLiveVisualTransform = IsValid(PrimaryVisual);
			const FTransform SpawnXf = bUseLiveVisualTransform
				? PrimaryVisual->GetComponentTransform()
				: StructureData->DestructionGCRelativeTransform;
			const FVector Extent = GetFootprintExtent();
			const FVector BreakOrigin = GetActorLocation() + FVector(0.f, 0.f, Extent.Z * 0.5f);

			bSpawnedChaosDestruction = IsValid(SpawnFracturedGC(
				DestructionGC,
				SpawnXf,
				bUseLiveVisualTransform,
				BreakOrigin));
		}
	}

	if (IsValid(RubbleVisualComponent))
	{
		const UStaticMesh* const RubbleMesh = RubbleVisualComponent->GetStaticMesh();
		const bool bIsPrototypeCube = IsValid(RubbleMesh) && RubbleMesh->GetPathName().Contains(TEXT("/Engine/BasicShapes/Cube"));
		if (bSpawnedChaosDestruction || bIsPrototypeCube || !IsValid(RubbleMesh))
		{
			RubbleVisualComponent->SetVisibility(false, true);
		}
		else
		{
			RubbleVisualComponent->SetVisibility(true, true);
		}
	}

	SpawnDestructionVFX();
}

void AIFStructure::GatherStructureAssetPaths(TArray<FSoftObjectPath>& AssetPaths) const
{
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
		AddIfSet(StructureData->PreviewMesh);
		AddIfSet(StructureData->DestructionGeometryCollection);
		AddIfSet(StructureData->DestructionPhysicalMaterial);
		AddIfSet(StructureData->DestructionCascadeVFX);
		AddIfSet(StructureData->BuildCompleteCascadeVFX);
	}
}

void AIFStructure::RequestStructureAssets()
{
	TArray<FSoftObjectPath> AssetPaths;
	GatherStructureAssetPaths(AssetPaths);

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

bool AIFStructure::ShouldStripPrimitiveCollision(const UPrimitiveComponent*) const
{
	return true;
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

		// Keep BoundsCollision / ExtraCollisionBoxes (and subclass opt-outs like GateDoor InteractionZone) intact; cosmetic meshes stay NoCollision.
		const UBoxComponent* const AsBox = Cast<UBoxComponent>(Prim);
		if (Prim != BoundsCollision
			&& !(IsValid(AsBox) && ExtraCollisionBoxes.Contains(AsBox))
			&& ShouldStripPrimitiveCollision(Prim))
		{
			Prim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Prim->SetCollisionResponseToAllChannels(ECR_Ignore);
			Prim->SetCanEverAffectNavigation(false);
			Prim->SetGenerateOverlapEvents(false);
		}
	}
}

namespace
{
	constexpr int32 MaxActiveDestroyedStructures = 8;
	TArray<TWeakObjectPtr<AIFStructure>> GActiveDestroyedStructures;

	void PruneDestroyedStructuresForWorld(const UWorld* TargetWorld)
	{
		GActiveDestroyedStructures.RemoveAll([TargetWorld](const TWeakObjectPtr<AIFStructure>& Ptr)
		{
			const AIFStructure* const Struct = Ptr.Get();
			return !IsValid(Struct) || Struct->IsActorBeingDestroyed() || Struct->GetWorld() != TargetWorld;
		});
	}
}

void AIFStructure::CleanupActiveDestructionGCs()
{
	for (UGeometryCollectionComponent* const GCComp : ActiveDestructionGCs)
	{
		if (IsValid(GCComp))
		{
			GCComp->DestroyComponent();
		}
	}
	ActiveDestructionGCs.Reset();
}

void AIFStructure::RegisterDestroyedStructureAndEnforceCap(AIFStructure* NewlyDestroyed)
{
	if (!IsValid(NewlyDestroyed))
	{
		return;
	}

	const UWorld* const CurrentWorld = NewlyDestroyed->GetWorld();
	PruneDestroyedStructuresForWorld(CurrentWorld);

	const TWeakObjectPtr<AIFStructure> NewWeak(NewlyDestroyed);
	if (!GActiveDestroyedStructures.Contains(NewWeak))
	{
		GActiveDestroyedStructures.Add(NewWeak);
	}

	while (GActiveDestroyedStructures.Num() > MaxActiveDestroyedStructures)
	{
		AIFStructure* const Oldest = GActiveDestroyedStructures[0].Get();
		GActiveDestroyedStructures.RemoveAt(0);
		if (IsValid(Oldest) && Oldest != NewlyDestroyed && !Oldest->IsActorBeingDestroyed())
		{
			Oldest->CleanupActiveDestructionGCs();
			Oldest->SetLifeSpan(0.05f);
		}
	}
}

void AIFStructure::UnregisterDestroyedStructure(const AIFStructure* Structure)
{
	if (!Structure)
	{
		return;
	}

	const UWorld* const CurrentWorld = Structure->GetWorld();
	GActiveDestroyedStructures.RemoveAll([Structure, CurrentWorld](const TWeakObjectPtr<AIFStructure>& Ptr)
	{
		const AIFStructure* const Existing = Ptr.Get();
		return !IsValid(Existing) || Existing == Structure || Existing->GetWorld() != CurrentWorld;
	});
}
