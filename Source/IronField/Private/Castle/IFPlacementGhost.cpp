#include "Castle/IFPlacementGhost.h"

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

	auto ConfigurePreviewMeshComponent = [](UStaticMeshComponent* MeshComp)
	{
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComp->SetGenerateOverlapEvents(false);
		MeshComp->SetCanEverAffectNavigation(false);
		MeshComp->SetCastShadow(false);
		// Translucent ghost materials bypass Nanite rasterization, so force fallback meshes on Nanite castle assets.
		MeshComp->bDisallowNanite = true;
		MeshComp->bReceivesDecals = false;
		MeshComp->SetVisibility(false);
	};

	PrimaryPreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrimaryPreviewMesh"));
	PrimaryPreviewMesh->SetupAttachment(GhostRoot);
	ConfigurePreviewMeshComponent(PrimaryPreviewMesh);

	MirroredPreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MirroredPreviewMesh"));
	MirroredPreviewMesh->SetupAttachment(GhostRoot);
	ConfigurePreviewMeshComponent(MirroredPreviewMesh);

	SetActorHiddenInGame(true);
}

void AIFPlacementGhost::SetOwningBuildingComponent(UIFBuildingComponent* InOwnerComponent)
{
	OwningBuildingComponent = InOwnerComponent;
}

void AIFPlacementGhost::BeginPreview(
	const UIFStructureDataAsset* InStructureData,
	UStaticMesh* InFallbackCubeMesh,
	UMaterialInterface* InValidMaterial,
	UMaterialInterface* InInvalidMaterial)
{
	ValidMaterial = InValidMaterial;
	InvalidMaterial = InInvalidMaterial;
	bHasAppliedMaterial = false;
	bHasAppliedTransform = false;
	bUseMirroredMesh = false;

	CurrentFootprintExtent = IsValid(InStructureData)
		? InStructureData->FootprintExtent
		: FVector(150.f, 150.f, 150.f);

	if (IsValid(PrimaryPreviewMesh))
	{
		if (!IsValid(DefaultCubeMesh))
		{
			if (IsValid(InFallbackCubeMesh))
			{
				DefaultCubeMesh = InFallbackCubeMesh;
			}
			else if (IsValid(PrimaryPreviewMesh->GetStaticMesh()))
			{
				DefaultCubeMesh = PrimaryPreviewMesh->GetStaticMesh();
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
			PrimaryPreviewMesh->SetStaticMesh(CustomPreviewMesh);
			PrimaryPreviewMesh->SetRelativeTransform(InStructureData->PreviewMeshRelativeTransform);
		}
		else
		{
			if (IsValid(DefaultCubeMesh))
			{
				PrimaryPreviewMesh->SetStaticMesh(DefaultCubeMesh);
			}

			// Assumes a 100 cm unit fallback cube mesh scaled to match 2 * FootprintExtent.
			const FVector MeshScale(
				FMath::Max(0.1f, (CurrentFootprintExtent.X * 2.f) / 100.f),
				FMath::Max(0.1f, (CurrentFootprintExtent.Y * 2.f) / 100.f),
				FMath::Max(0.1f, (CurrentFootprintExtent.Z * 2.f) / 100.f));
			PrimaryPreviewMesh->SetRelativeTransform(
				FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, CurrentFootprintExtent.Z), MeshScale));
		}

		PrimaryPreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PrimaryPreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		PrimaryPreviewMesh->SetCanEverAffectNavigation(false);
		PrimaryPreviewMesh->SetVisibility(true);

		if (IsValid(MirroredPreviewMesh))
		{
			if (IsValid(InStructureData) && InStructureData->bMirrorPreviewMeshX && IsValid(CustomPreviewMesh))
			{
				FTransform MirroredTransform = InStructureData->PreviewMeshRelativeTransform;
				FVector MirroredLoc = MirroredTransform.GetLocation();
				MirroredLoc.X = -MirroredLoc.X;
				FVector MirroredScale = MirroredTransform.GetScale3D();
				MirroredScale.X = -MirroredScale.X;
				MirroredTransform.SetLocation(MirroredLoc);
				MirroredTransform.SetScale3D(MirroredScale);

				MirroredPreviewMesh->SetStaticMesh(CustomPreviewMesh);
				MirroredPreviewMesh->SetRelativeTransform(MirroredTransform);
				MirroredPreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				MirroredPreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
				MirroredPreviewMesh->SetCanEverAffectNavigation(false);
				MirroredPreviewMesh->SetVisibility(true);
				bUseMirroredMesh = true;
			}
			else
			{
				MirroredPreviewMesh->SetStaticMesh(nullptr);
				MirroredPreviewMesh->SetVisibility(false);
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
	bUseMirroredMesh = false;
	bHasAppliedTransform = false;
	bHasAppliedMaterial = false;
	SetActorTickEnabled(false);
	SetActorHiddenInGame(true);

	if (IsValid(PrimaryPreviewMesh))
	{
		PrimaryPreviewMesh->SetVisibility(false);
	}
	if (IsValid(MirroredPreviewMesh))
	{
		MirroredPreviewMesh->SetStaticMesh(nullptr);
		MirroredPreviewMesh->SetVisibility(false);
	}
}

void AIFPlacementGhost::SetPreviewMeshVisible(bool bVisible)
{
	if (IsValid(PrimaryPreviewMesh))
	{
		PrimaryPreviewMesh->SetVisibility(bVisible);
	}
	if (IsValid(MirroredPreviewMesh))
	{
		MirroredPreviewMesh->SetVisibility(bVisible && bUseMirroredMesh);
	}
}

void AIFPlacementGhost::UpdatePreviewTransformAndValidity(const FVector& WorldLocation, float YawDegrees, bool bIsValidPlacement)
{
	SetPreviewMeshVisible(true);

	if (!bHasAppliedTransform
		|| !LastAppliedLocation.Equals(WorldLocation, 0.01f)
		|| !FMath::IsNearlyEqual(LastAppliedYaw, YawDegrees, 0.01f))
	{
		bHasAppliedTransform = true;
		LastAppliedLocation = WorldLocation;
		LastAppliedYaw = YawDegrees;
		SetActorLocationAndRotation(WorldLocation, FRotator(0.f, YawDegrees, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (!bHasAppliedMaterial || bLastValidityState != bIsValidPlacement)
	{
		bHasAppliedMaterial = true;
		bLastValidityState = bIsValidPlacement;

		UMaterialInterface* const Mat = bIsValidPlacement ? ValidMaterial.Get() : InvalidMaterial.Get();
		if (IsValid(Mat) && IsValid(PrimaryPreviewMesh))
		{
			const int32 NumMats = FMath::Max(1, PrimaryPreviewMesh->GetNumMaterials());
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				PrimaryPreviewMesh->SetMaterial(MatIdx, Mat);
			}
		}
		if (IsValid(Mat) && IsValid(MirroredPreviewMesh) && bUseMirroredMesh)
		{
			const int32 NumMats = FMath::Max(1, MirroredPreviewMesh->GetNumMaterials());
			for (int32 MatIdx = 0; MatIdx < NumMats; ++MatIdx)
			{
				MirroredPreviewMesh->SetMaterial(MatIdx, Mat);
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
