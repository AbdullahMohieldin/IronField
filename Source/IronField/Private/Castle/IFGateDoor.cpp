#include "Castle/IFGateDoor.h"

#include "Castle/IFStructureDataAsset.h"
#include "Character/IFBaseCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IFLog.h"
#include "Engine/CollisionProfile.h"
#include "GeometryCollection/GeometryCollectionObject.h"

AIFGateDoor::AIFGateDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	LeftDoorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LeftDoorPivot"));
	LeftDoorPivot->SetupAttachment(VisualRoot);
	LeftDoorPivot->SetRelativeLocation(FVector(-87.5f, 0.f, 0.f));

	LeftDoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftDoorMesh"));
	LeftDoorMesh->SetupAttachment(LeftDoorPivot);
	LeftDoorMesh->SetRelativeLocation(FVector(87.5f, 0.f, 0.f));
	LeftDoorMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	LeftDoorMesh->SetGenerateOverlapEvents(false);
	LeftDoorMesh->SetCanEverAffectNavigation(false);

	RightDoorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("RightDoorPivot"));
	RightDoorPivot->SetupAttachment(VisualRoot);
	RightDoorPivot->SetRelativeLocation(FVector(87.5f, 0.f, 0.f));

	RightDoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightDoorMesh"));
	RightDoorMesh->SetupAttachment(RightDoorPivot);
	RightDoorMesh->SetRelativeLocation(FVector(-87.5f, 0.f, 0.f));
	RightDoorMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	RightDoorMesh->SetGenerateOverlapEvents(false);
	RightDoorMesh->SetCanEverAffectNavigation(false);
}

void AIFGateDoor::BeginPlay()
{
	Super::BeginPlay();

	TInlineComponentArray<UBoxComponent*> CollisionBoxes;
	GetComponents(CollisionBoxes);
	for (UBoxComponent* const Box : CollisionBoxes)
	{
		if (IsValid(Box))
		{
			Box->SetGenerateOverlapEvents(true);
		}
	}

	ApplyDoorLeafYaw(bIsGateOpen ? 1.f : 0.f);
	SetStructureCollisionBlocking(!bIsGateOpen);
}

void AIFGateDoor::SetHostGate(AIFStructure* InHostGate)
{
	SetHostStructure(InHostGate);
}

void AIFGateDoor::ToggleGateOpen()
{
	SetGateOpen(!bIsGateOpen);
}

void AIFGateDoor::SetGateOpen(bool bNewOpen)
{
	if (!IsStructureAlive())
	{
		return;
	}

	if (bIsGateOpen == bNewOpen)
	{
		return;
	}

	bIsGateOpen = bNewOpen;
	TargetOpenAlpha = bIsGateOpen ? 1.f : 0.f;
	SetStructureCollisionBlocking(!bIsGateOpen);
	SetActorTickEnabled(true);
	OnGateDoorStateChanged.Broadcast(this, bIsGateOpen);
}

bool AIFGateDoor::CanInteract(AActor* Interactor) const
{
	if (!IsStructureAlive() || !IsValid(Interactor))
	{
		return false;
	}

	if (const AIFBaseCharacter* const BaseChar = Cast<AIFBaseCharacter>(Interactor); IsValid(BaseChar) && BaseChar->IsDead())
	{
		return false;
	}

	return IFTeamUtils::AreFriendly(Interactor, this);
}

void AIFGateDoor::Interact(AActor* Interactor)
{
	if (CanInteract(Interactor))
	{
		ToggleGateOpen();
	}
}

FText AIFGateDoor::GetInteractPromptText(AActor*) const
{
	return bIsGateOpen
		? NSLOCTEXT("IronFieldTactical", "PromptCloseGate", "[E] Close Gate")
		: NSLOCTEXT("IronFieldTactical", "PromptOpenGate", "[E] Open Gate");
}

void AIFGateDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Rate = 1.f / FMath::Max(0.05f, SwingDurationSeconds);
	CurrentOpenAlpha = FMath::FInterpConstantTo(CurrentOpenAlpha, TargetOpenAlpha, DeltaSeconds, Rate);

	const float EasedAlpha = FMath::InterpEaseInOut(0.f, 1.f, CurrentOpenAlpha, 2.f);
	ApplyDoorLeafYaw(EasedAlpha);

	if (FMath::IsNearlyEqual(CurrentOpenAlpha, TargetOpenAlpha, 0.001f))
	{
		CurrentOpenAlpha = TargetOpenAlpha;
		ApplyDoorLeafYaw(TargetOpenAlpha);
		SetActorTickEnabled(false);
	}
}

void AIFGateDoor::ApplyDoorLeafYaw(float Alpha)
{
	const float YawAngle = OpenAngleDegrees * Alpha;
	if (IsValid(LeftDoorPivot))
	{
		LeftDoorPivot->SetRelativeRotation(FRotator(0.f, YawAngle, 0.f));
	}
	if (IsValid(RightDoorPivot))
	{
		RightDoorPivot->SetRelativeRotation(FRotator(0.f, -YawAngle, 0.f));
	}
}

void AIFGateDoor::GatherStructureAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const
{
	Super::GatherStructureAssetPaths(OutAssetPaths);

	if (!LeftDoorDestructionGC.IsNull())
	{
		OutAssetPaths.AddUnique(LeftDoorDestructionGC.ToSoftObjectPath());
	}
	if (!RightDoorDestructionGC.IsNull())
	{
		OutAssetPaths.AddUnique(RightDoorDestructionGC.ToSoftObjectPath());
	}
}

void AIFGateDoor::TriggerDestructionFeedback()
{
	SetActorTickEnabled(false);

	auto CaptureLeafWorldTransform = [](const UStaticMeshComponent* LeafMesh, const FTransform& FallbackTransform) -> FTransform
	{
		if (IsValid(LeafMesh))
		{
			FTransform WorldXf = LeafMesh->GetComponentTransform();
			// Scale is baked into GC_Structure_GateDoorLeaf_L / _R so Chaos simulates at unit (1,1,1) scale.
			WorldXf.SetScale3D(FVector::OneVector);
			return WorldXf;
		}
		FTransform SafeFallback = FallbackTransform;
		SafeFallback.SetScale3D(FVector::OneVector);
		return SafeFallback;
	};

	const FTransform LeftLeafWorldXf = CaptureLeafWorldTransform(LeftDoorMesh, GetActorTransform());
	const FTransform RightLeafWorldXf = CaptureLeafWorldTransform(RightDoorMesh, GetActorTransform());

	if (IsValid(LeftDoorMesh))
	{
		LeftDoorMesh->SetVisibility(false, true);
	}
	if (IsValid(RightDoorMesh))
	{
		RightDoorMesh->SetVisibility(false, true);
	}

	auto LoadOrFallbackGC = [this](const TSoftObjectPtr<UGeometryCollection>& LeafRef) -> UGeometryCollection*
	{
		UGeometryCollection* LoadedGC = LeafRef.Get();
		if (!IsValid(LoadedGC) && !LeafRef.IsNull())
		{
			LoadedGC = LeafRef.LoadSynchronous();
		}
		if (!IsValid(LoadedGC) && IsValid(StructureData))
		{
			LoadedGC = StructureData->DestructionGeometryCollection.Get();
			if (!IsValid(LoadedGC) && !StructureData->DestructionGeometryCollection.IsNull())
			{
				LoadedGC = StructureData->DestructionGeometryCollection.LoadSynchronous();
			}
		}
		return LoadedGC;
	};

	UGeometryCollection* const LeftGC = LoadOrFallbackGC(LeftDoorDestructionGC);
	UGeometryCollection* const RightGC = LoadOrFallbackGC(RightDoorDestructionGC);

	const float LeafHeightHalf = IsValid(StructureData) ? StructureData->FootprintExtent.Z * 0.5f : 110.f;
	const float LeafImpulseRadius = IsValid(StructureData) ? StructureData->ChaosRadialFalloffRadius : 300.f;

	if (IsValid(LeftGC))
	{
		const FVector LeftImpulseOrigin = LeftLeafWorldXf.TransformPosition(FVector(-44.f, 0.f, LeafHeightHalf));
		SpawnFracturedGC(LeftGC, LeftLeafWorldXf, /*bIsWorldTransform=*/true, LeftImpulseOrigin, LeafImpulseRadius);
	}
	else
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-GateDoor] %s has no LeftDoorDestructionGC or fallback GC assigned."), *GetName());
	}

	if (IsValid(RightGC))
	{
		const FVector RightImpulseOrigin = RightLeafWorldXf.TransformPosition(FVector(44.f, 0.f, LeafHeightHalf));
		SpawnFracturedGC(RightGC, RightLeafWorldXf, /*bIsWorldTransform=*/true, RightImpulseOrigin, LeafImpulseRadius);
	}
	else
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-GateDoor] %s has no RightDoorDestructionGC or fallback GC assigned."), *GetName());
	}

	SpawnDestructionVFX();
}
