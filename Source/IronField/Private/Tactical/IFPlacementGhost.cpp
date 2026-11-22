#include "Tactical/IFPlacementGhost.h"

#include "Castle/IFStructureDataAsset.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Tactical/IFTacticalCommandComponent.h"

AIFPlacementGhost::AIFPlacementGhost()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bTickEvenWhenPaused = false;
	// Run after AIFTacticalCameraRig pans/zooms so the ghost never lags 1 frame behind the camera.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	GhostRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GhostRoot"));
	SetRootComponent(GhostRoot);

	FootprintPreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FootprintPreviewMesh"));
	FootprintPreviewMesh->SetupAttachment(GhostRoot);
	FootprintPreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FootprintPreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	FootprintPreviewMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	FootprintPreviewMesh->SetGenerateOverlapEvents(false);
	FootprintPreviewMesh->SetCanEverAffectNavigation(false);
	FootprintPreviewMesh->SetCastShadow(false);
	FootprintPreviewMesh->bReceivesDecals = false;
	FootprintPreviewMesh->SetVisibility(false);

	SetActorHiddenInGame(true);
}

void AIFPlacementGhost::SetOwningTacticalComponent(UIFTacticalCommandComponent* InOwnerComponent)
{
	OwningTacticalComponent = InOwnerComponent;
}

void AIFPlacementGhost::BeginPreview(
	const UIFStructureDataAsset* InStructureData,
	UStaticMesh* InPreviewMesh,
	UMaterialInterface* InValidMaterial,
	UMaterialInterface* InInvalidMaterial)
{
	ValidMaterial = InValidMaterial;
	InvalidMaterial = InInvalidMaterial;
	bHasAppliedMaterial = false;

	CurrentFootprintExtent = IsValid(InStructureData)
		? InStructureData->FootprintExtent
		: FVector(150.f, 150.f, 150.f);

	if (IsValid(FootprintPreviewMesh))
	{
		if (IsValid(InPreviewMesh))
		{
			FootprintPreviewMesh->SetStaticMesh(InPreviewMesh);
		}
		// Standard 100cm BasicShape Cube scaled to match exact structure FootprintExtent.
		const FVector MeshScale(
			FMath::Max(0.1f, (CurrentFootprintExtent.X * 2.f) / 100.f),
			FMath::Max(0.1f, (CurrentFootprintExtent.Y * 2.f) / 100.f),
			FMath::Max(0.1f, (CurrentFootprintExtent.Z * 2.f) / 100.f));
		FootprintPreviewMesh->SetRelativeScale3D(MeshScale);
		FootprintPreviewMesh->SetRelativeLocation(FVector(0.f, 0.f, CurrentFootprintExtent.Z));
		FootprintPreviewMesh->SetVisibility(true);
	}

	bPreviewActive = true;
	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);
}

void AIFPlacementGhost::EndPreview()
{
	bPreviewActive = false;
	bHasAppliedMaterial = false;
	SetActorTickEnabled(false);
	SetActorHiddenInGame(true);

	if (IsValid(FootprintPreviewMesh))
	{
		FootprintPreviewMesh->SetVisibility(false);
	}
}

void AIFPlacementGhost::UpdatePreviewTransformAndValidity(const FVector& WorldLocation, float YawDegrees, bool bIsValidPlacement)
{
	SetActorLocationAndRotation(WorldLocation, FRotator(0.f, YawDegrees, 0.f), false, nullptr, ETeleportType::TeleportPhysics);

	if (!bHasAppliedMaterial || bLastValidityState != bIsValidPlacement)
	{
		bHasAppliedMaterial = true;
		bLastValidityState = bIsValidPlacement;

		if (IsValid(FootprintPreviewMesh))
		{
			UMaterialInterface* const Mat = bIsValidPlacement ? ValidMaterial.Get() : InvalidMaterial.Get();
			if (IsValid(Mat))
			{
				FootprintPreviewMesh->SetMaterial(0, Mat);
			}
		}
	}
}

void AIFPlacementGhost::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bPreviewActive)
	{
		SetActorTickEnabled(false);
		return;
	}

	if (UIFTacticalCommandComponent* const Tactical = OwningTacticalComponent.Get())
	{
		Tactical->UpdateBuildPlacementPreview();
	}
	else
	{
		EndPreview();
	}
}
