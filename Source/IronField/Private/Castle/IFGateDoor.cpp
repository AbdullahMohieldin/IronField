#include "Castle/IFGateDoor.h"

#include "Castle/IFStructureDataAsset.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFPlayerCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerController.h"
#include "Engine/World.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Kismet/GameplayStatics.h"

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
	LeftDoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeftDoorMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	LeftDoorMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	LeftDoorMesh->SetGenerateOverlapEvents(false);
	LeftDoorMesh->SetCanEverAffectNavigation(false);

	RightDoorPivot = CreateDefaultSubobject<USceneComponent>(TEXT("RightDoorPivot"));
	RightDoorPivot->SetupAttachment(VisualRoot);
	RightDoorPivot->SetRelativeLocation(FVector(87.5f, 0.f, 0.f));

	RightDoorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightDoorMesh"));
	RightDoorMesh->SetupAttachment(RightDoorPivot);
	RightDoorMesh->SetRelativeLocation(FVector(-87.5f, 0.f, 0.f));
	RightDoorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightDoorMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	RightDoorMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RightDoorMesh->SetGenerateOverlapEvents(false);
	RightDoorMesh->SetCanEverAffectNavigation(false);

	InteractionZone = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionZone"));
	InteractionZone->SetupAttachment(BoundsCollision);
	InteractionZone->InitSphereRadius(280.f);
	InteractionZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionZone->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	InteractionZone->SetGenerateOverlapEvents(true);
	InteractionZone->SetCanEverAffectNavigation(false);
}

bool AIFGateDoor::ShouldStripPrimitiveCollision(const UPrimitiveComponent* Prim) const
{
	return Prim != InteractionZone;
}

void AIFGateDoor::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(InteractionZone))
	{
		InteractionZone->SetSphereRadius(InteractionRadiusCm);
		InteractionZone->OnComponentBeginOverlap.AddUniqueDynamic(this, &AIFGateDoor::HandleInteractionBeginOverlap);
		InteractionZone->OnComponentEndOverlap.AddUniqueDynamic(this, &AIFGateDoor::HandleInteractionEndOverlap);
		InteractionZone->UpdateOverlaps();
	}

	ApplyDoorLeafYaw(bIsGateOpen ? 1.f : 0.f);
	SetStructureCollisionBlocking(!bIsGateOpen);

	// Register immediately if the local player character is already inside InteractionRadiusCm at spawn.
	if (const UWorld* const World = GetWorld())
	{
		if (AIFPlayerController* const PC = Cast<AIFPlayerController>(UGameplayStatics::GetPlayerController(World, 0)))
		{
			if (AIFPlayerCharacter* const PlayerChar = Cast<AIFPlayerCharacter>(PC->GetPawn()))
			{
				if (CanInteract(PlayerChar)
					&& FVector::DistSquared(PlayerChar->GetActorLocation(), GetActorLocation()) <= FMath::Square(InteractionRadiusCm))
				{
					RegisteredController = PC;
					PC->RegisterNearbyInteractable(this);
				}
			}
		}
	}
}

void AIFGateDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromNearbyControllers();

	if (IsValid(InteractionZone))
	{
		InteractionZone->OnComponentBeginOverlap.RemoveDynamic(this, &AIFGateDoor::HandleInteractionBeginOverlap);
		InteractionZone->OnComponentEndOverlap.RemoveDynamic(this, &AIFGateDoor::HandleInteractionEndOverlap);
	}

	Super::EndPlay(EndPlayReason);
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

	if (AIFPlayerController* const PC = RegisteredController.Get(); IsValid(PC))
	{
		PC->RefreshInteractionPrompt();
	}
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

void AIFGateDoor::OnTeamChanged(FGenericTeamId PreviousTeam, FGenericTeamId NewTeam)
{
	Super::OnTeamChanged(PreviousTeam, NewTeam);

	if (AIFPlayerController* const PC = RegisteredController.Get(); IsValid(PC))
	{
		if (!CanInteract(PC->GetPawn()))
		{
			UnregisterFromNearbyControllers();
		}
		else
		{
			PC->RefreshInteractionPrompt();
		}
	}
}

void AIFGateDoor::OnStructureDeactivated()
{
	Super::OnStructureDeactivated();
	UnregisterFromNearbyControllers();
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
	UnregisterFromNearbyControllers();

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

void AIFGateDoor::HandleInteractionBeginOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32,
	bool,
	const FHitResult&)
{
	if (!IsStructureAlive())
	{
		return;
	}

	AIFPlayerCharacter* const PlayerChar = Cast<AIFPlayerCharacter>(OtherActor);
	if (!IsValid(PlayerChar) || !CanInteract(PlayerChar))
	{
		return;
	}

	if (AIFPlayerController* const PC = Cast<AIFPlayerController>(PlayerChar->GetController()))
	{
		RegisteredController = PC;
		PC->RegisterNearbyInteractable(this);
	}
}

void AIFGateDoor::HandleInteractionEndOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32)
{
	const AIFPlayerCharacter* const PlayerChar = Cast<AIFPlayerCharacter>(OtherActor);
	if (!IsValid(PlayerChar))
	{
		return;
	}

	if (AIFPlayerController* const PC = Cast<AIFPlayerController>(PlayerChar->GetController()))
	{
		PC->UnregisterNearbyInteractable(this);
		if (RegisteredController.Get() == PC)
		{
			RegisteredController.Reset();
		}
	}
}

void AIFGateDoor::UnregisterFromNearbyControllers()
{
	if (AIFPlayerController* const PC = RegisteredController.Get(); IsValid(PC))
	{
		PC->UnregisterNearbyInteractable(this);
	}
	RegisteredController.Reset();
}
