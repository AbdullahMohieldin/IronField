#include "Tactical/IFPlacementGhost.h"

#include "Castle/IFBuildingComponent.h"
#include "Castle/IFStructureDataAsset.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

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
	FootprintPreviewMesh->bDisallowNanite = true;
	FootprintPreviewMesh->bReceivesDecals = false;
	FootprintPreviewMesh->SetVisibility(false);

	SecondaryPreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SecondaryPreviewMesh"));
	SecondaryPreviewMesh->SetupAttachment(GhostRoot);
	SecondaryPreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SecondaryPreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	SecondaryPreviewMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	SecondaryPreviewMesh->SetGenerateOverlapEvents(false);
	SecondaryPreviewMesh->SetCanEverAffectNavigation(false);
	SecondaryPreviewMesh->SetCastShadow(false);
	SecondaryPreviewMesh->bDisallowNanite = true;
	SecondaryPreviewMesh->bReceivesDecals = false;
	SecondaryPreviewMesh->SetVisibility(false);

	SetActorHiddenInGame(true);
}

void AIFPlacementGhost::SetOwningBuildingComponent(UIFBuildingComponent* InOwnerComponent)
{
	OwningBuildingComponent = InOwnerComponent;
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
		if (!IsValid(DefaultCubeMesh))
		{
			if (IsValid(InPreviewMesh))
			{
				DefaultCubeMesh = InPreviewMesh;
			}
			else if (IsValid(FootprintPreviewMesh->GetStaticMesh()))
			{
				DefaultCubeMesh = FootprintPreviewMesh->GetStaticMesh();
			}
		}

		UStaticMesh* CustomPreviewMesh = nullptr;
		if (IsValid(InStructureData) && !InStructureData->PreviewMesh.IsNull())
		{
			CustomPreviewMesh = InStructureData->PreviewMesh.Get();
			if (!IsValid(CustomPreviewMesh))
			{
				CustomPreviewMesh = InStructureData->PreviewMesh.LoadSynchronous();
			}
		}

		if (IsValid(CustomPreviewMesh))
		{
			FootprintPreviewMesh->SetStaticMesh(CustomPreviewMesh);
			FootprintPreviewMesh->SetRelativeTransform(InStructureData->PreviewMeshRelativeTransform);
		}
		else
		{
			if (IsValid(DefaultCubeMesh))
			{
				FootprintPreviewMesh->SetStaticMesh(DefaultCubeMesh);
			}

			// Standard 100cm BasicShape Cube scaled to match exact structure FootprintExtent.
			const FVector MeshScale(
				FMath::Max(0.1f, (CurrentFootprintExtent.X * 2.f) / 100.f),
				FMath::Max(0.1f, (CurrentFootprintExtent.Y * 2.f) / 100.f),
				FMath::Max(0.1f, (CurrentFootprintExtent.Z * 2.f) / 100.f));
			FootprintPreviewMesh->SetRelativeTransform(
				FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, CurrentFootprintExtent.Z), MeshScale));
		}

		FootprintPreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FootprintPreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		FootprintPreviewMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		FootprintPreviewMesh->SetCanEverAffectNavigation(false);
		FootprintPreviewMesh->SetVisibility(true);

		if (IsValid(SecondaryPreviewMesh))
		{
			if (IsValid(InStructureData)
				&& InStructureData->PlacementRule == EIFPlacementRule::GateOpeningOnly
				&& IsValid(CustomPreviewMesh))
			{
				FTransform MirroredTransform = InStructureData->PreviewMeshRelativeTransform;
				FVector MirroredLoc = MirroredTransform.GetLocation();
				MirroredLoc.X = -MirroredLoc.X;
				FVector MirroredScale = MirroredTransform.GetScale3D();
				MirroredScale.X = -MirroredScale.X;
				MirroredTransform.SetLocation(MirroredLoc);
				MirroredTransform.SetScale3D(MirroredScale);

				SecondaryPreviewMesh->SetStaticMesh(CustomPreviewMesh);
				SecondaryPreviewMesh->SetRelativeTransform(MirroredTransform);
				SecondaryPreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				SecondaryPreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
				SecondaryPreviewMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				SecondaryPreviewMesh->SetCanEverAffectNavigation(false);
				SecondaryPreviewMesh->SetVisibility(true);
			}
			else
			{
				SecondaryPreviewMesh->SetVisibility(false);
			}
		}
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
	if (IsValid(SecondaryPreviewMesh))
	{
		SecondaryPreviewMesh->SetVisibility(false);
	}
}

void AIFPlacementGhost::UpdatePreviewTransformAndValidity(const FVector& WorldLocation, float YawDegrees, bool bIsValidPlacement)
{
	SetActorLocationAndRotation(WorldLocation, FRotator(0.f, YawDegrees, 0.f), false, nullptr, ETeleportType::TeleportPhysics);

	if (!bHasAppliedMaterial || bLastValidityState != bIsValidPlacement)
	{
		bHasAppliedMaterial = true;
		bLastValidityState = bIsValidPlacement;

		UMaterialInterface* const Mat = bIsValidPlacement ? ValidMaterial.Get() : InvalidMaterial.Get();
		if (IsValid(Mat) && IsValid(FootprintPreviewMesh))
		{
			const int32 NumMats = FMath::Max(1, FootprintPreviewMesh->GetNumMaterials());
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				FootprintPreviewMesh->SetMaterial(MatIdx, Mat);
			}
		}
		if (IsValid(Mat) && IsValid(SecondaryPreviewMesh) && SecondaryPreviewMesh->IsVisible())
		{
			const int32 NumMats = FMath::Max(1, SecondaryPreviewMesh->GetNumMaterials());
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				SecondaryPreviewMesh->SetMaterial(MatIdx, Mat);
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

	if (UIFBuildingComponent* const Building = OwningBuildingComponent.Get())
	{
		Building->UpdatePlacementPreview();
	}
	else
	{
		EndPreview();
	}
}
