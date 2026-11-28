#include "Castle/IFBuildingComponent.h"

#include "Castle/IFBuildingSnapSolver.h"
#include "Castle/IFPlacementGhost.h"
#include "Castle/IFStructure.h"
#include "Castle/IFStructureDataAsset.h"
#include "Castle/IFTerritory.h"
#include "Combat/IFTeamTypes.h"
#include "Core/IFGameTypes.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerController.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Tactical/IFTacticalCommandComponent.h"
#include "Tactical/IFTacticalCursorQuery.h"

namespace
{
	constexpr double StationaryValidationIntervalSeconds = 0.08;

	bool UsesStructureMountedPlacement(EIFPlacementRule Rule)
	{
		return Rule == EIFPlacementRule::WallWalkwayOnly
			|| Rule == EIFPlacementRule::CornerWalkwayOnly
			|| Rule == EIFPlacementRule::GateOpeningOnly
			|| Rule == EIFPlacementRule::StairMountOrFree;
	}
}

UIFBuildingComponent::UIFBuildingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIFBuildingComponent::BeginPlay()
{
	Super::BeginPlay();
	RequestBuildAssets();
}

void UIFBuildingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BuildAssetsLoadHandle.IsValid())
	{
		BuildAssetsLoadHandle->CancelHandle();
		BuildAssetsLoadHandle.Reset();
	}
	if (BuildClassLoadHandle.IsValid())
	{
		BuildClassLoadHandle->CancelHandle();
		BuildClassLoadHandle.Reset();
	}

	CancelBuildPlacement();
	ClearSelectedStructure();

	CachedValidGhostMaterial = nullptr;
	CachedInvalidGhostMaterial = nullptr;

	if (IsValid(PlacementGhost))
	{
		PlacementGhost->Destroy();
		PlacementGhost = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void UIFBuildingComponent::RequestBuildAssets()
{
	TArray<FSoftObjectPath> AssetPaths;
	auto AddIfValid = [&AssetPaths](const auto& SoftRef)
	{
		if (!SoftRef.IsNull())
		{
			AssetPaths.AddUnique(SoftRef.ToSoftObjectPath());
		}
	};

	AddIfValid(FallbackGhostCubeMesh);
	AddIfValid(PlacementGhostValidMaterial);
	AddIfValid(PlacementGhostInvalidMaterial);

	for (const TSoftObjectPtr<UIFStructureDataAsset>& BuildSoftRef : BuildableStructures)
	{
		AddIfValid(BuildSoftRef);
	}

	if (AssetPaths.IsEmpty())
	{
		return;
	}

	BuildAssetsLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPaths,
		FStreamableDelegate::CreateUObject(this, &UIFBuildingComponent::HandleBuildAssetsLoaded));
}

void UIFBuildingComponent::HandleBuildAssetsLoaded()
{
	// Retain BuildAssetsLoadHandle until EndPlay so soft-referenced ghost materials and cube mesh stay resident.
	ResolveGhostMaterials();
	EnsureBuildableStructuresLoaded();
}

void UIFBuildingComponent::ResolveGhostMaterials()
{
	if (!IsValid(CachedValidGhostMaterial) && !PlacementGhostValidMaterial.IsNull())
	{
		CachedValidGhostMaterial = PlacementGhostValidMaterial.Get();
		if (!IsValid(CachedValidGhostMaterial))
		{
			CachedValidGhostMaterial = PlacementGhostValidMaterial.LoadSynchronous();
		}
	}

	if (!IsValid(CachedInvalidGhostMaterial) && !PlacementGhostInvalidMaterial.IsNull())
	{
		CachedInvalidGhostMaterial = PlacementGhostInvalidMaterial.Get();
		if (!IsValid(CachedInvalidGhostMaterial))
		{
			CachedInvalidGhostMaterial = PlacementGhostInvalidMaterial.LoadSynchronous();
		}
	}
}

void UIFBuildingComponent::EnsureBuildableStructuresLoaded()
{
	if (bBuildableStructuresInitialized)
	{
		return;
	}

	bBuildableStructuresInitialized = true;
	LoadedBuildableStructures.Reset();
	TArray<FSoftObjectPath> StructureClassPaths;

	for (const TSoftObjectPtr<UIFStructureDataAsset>& BuildSoftRef : BuildableStructures)
	{
		UIFStructureDataAsset* LoadedData = BuildSoftRef.Get();
		if (!IsValid(LoadedData) && !BuildSoftRef.IsNull())
		{
			LoadedData = BuildSoftRef.LoadSynchronous();
		}

		if (IsValid(LoadedData))
		{
			LoadedBuildableStructures.Add(LoadedData);
			if (!LoadedData->StructureClass.IsNull() && !IsValid(LoadedData->StructureClass.Get()))
			{
				StructureClassPaths.AddUnique(LoadedData->StructureClass.ToSoftObjectPath());
			}
			if (!LoadedData->PreviewMesh.IsNull() && !IsValid(LoadedData->PreviewMesh.Get()))
			{
				StructureClassPaths.AddUnique(LoadedData->PreviewMesh.ToSoftObjectPath());
			}
		}
	}

	if (BuildClassLoadHandle.IsValid())
	{
		BuildClassLoadHandle->CancelHandle();
		BuildClassLoadHandle.Reset();
	}

	if (!StructureClassPaths.IsEmpty())
	{
		BuildClassLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(StructureClassPaths);
	}
}

UIFStructureDataAsset* UIFBuildingComponent::GetBuildableStructureByType(EIFStructureType StructureType) const
{
	for (UIFStructureDataAsset* const LoadedData : LoadedBuildableStructures)
	{
		if (IsValid(LoadedData) && LoadedData->StructureType == StructureType)
		{
			return LoadedData;
		}
	}

	for (const TSoftObjectPtr<UIFStructureDataAsset>& SoftRef : BuildableStructures)
	{
		if (UIFStructureDataAsset* const Data = SoftRef.Get(); IsValid(Data) && Data->StructureType == StructureType)
		{
			return Data;
		}
	}

	return nullptr;
}

bool UIFBuildingComponent::BeginBuildPlacement(UIFStructureDataAsset* StructureData)
{
	const AIFPlayerController* const PC = GetOwningPlayerController();
	const UIFTacticalCommandComponent* const Tactical = IsValid(PC) ? PC->GetTacticalCommandComponent() : nullptr;
	if (!IsValid(Tactical) || !Tactical->IsTacticalModeActive() || !IsValid(StructureData))
	{
		return false;
	}

	ClearSelectedStructure();
	FreeYawIndex = 0;
	return BeginBuildPlacementInternal(StructureData, nullptr);
}

bool UIFBuildingComponent::BeginBuildPlacementByType(EIFStructureType StructureType)
{
	UIFStructureDataAsset* const Data = GetBuildableStructureByType(StructureType);
	if (!IsValid(Data))
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-Build] No UIFStructureDataAsset configured for StructureType=%d."), static_cast<int32>(StructureType));
		return false;
	}

	if (IsInBuildPlacementMode() && !IsRelocatingStructure() && ActiveBuildData.Get() == Data)
	{
		CancelBuildPlacement();
		return false;
	}

	return BeginBuildPlacement(Data);
}

bool UIFBuildingComponent::HasLivingMountedDependents(const AIFStructure* HostStructure) const
{
	const UWorld* const World = GetWorld();
	if (!IsValid(World) || !IsValid(HostStructure))
	{
		return false;
	}

	for (TActorIterator<AIFStructure> It(World); It; ++It)
	{
		const AIFStructure* const Struct = *It;
		if (IsValid(Struct)
			&& Struct != HostStructure
			&& Struct->IsStructureAlive()
			&& Struct->GetHostStructure() == HostStructure)
		{
			return true;
		}
	}

	return false;
}

bool UIFBuildingComponent::TraceFriendlyStructureUnderCursor(
	const FVector& RayOrigin,
	const FVector& RayDir,
	FGenericTeamId BuilderTeamId,
	const AActor* CameraRig,
	float MaxDistance,
	const AIFStructure*& OutStructure,
	FVector& OutImpactPoint,
	FVector& OutImpactNormal)
{
	OutStructure = nullptr;
	OutImpactPoint = FVector::ZeroVector;
	OutImpactNormal = FVector::UpVector;

	const UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	FCollisionObjectQueryParams StructObjectParams;
	StructObjectParams.AddObjectTypesToQuery(IFCollision::Damageable);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFBuildStructureTrace), false);
	if (IsValid(CameraRig))
	{
		QueryParams.AddIgnoredActor(CameraRig);
	}
	if (IsValid(PlacementGhost))
	{
		QueryParams.AddIgnoredActor(PlacementGhost);
	}
	if (IsValid(RelocatingStructure.Get()))
	{
		QueryParams.AddIgnoredActor(RelocatingStructure.Get());
	}

	StructureTraceScratchHits.Reset();
	if (!World->LineTraceMultiByObjectType(
		StructureTraceScratchHits,
		RayOrigin,
		RayOrigin + RayDir * MaxDistance,
		StructObjectParams,
		QueryParams))
	{
		return false;
	}

	const uint8 BuilderTeamRaw = BuilderTeamId.GetId();
	for (const FHitResult& Hit : StructureTraceScratchHits)
	{
		const AIFStructure* const Candidate = Cast<AIFStructure>(Hit.GetActor());
		if (!IsValid(Candidate) || !Candidate->IsStructureAlive())
		{
			continue;
		}
		if (Candidate->GetGenericTeamId().GetId() != BuilderTeamRaw)
		{
			continue;
		}

		OutStructure = Candidate;
		OutImpactPoint = Hit.ImpactPoint;
		OutImpactNormal = Hit.ImpactNormal;
		return true;
	}

	return false;
}

bool UIFBuildingComponent::BeginRelocateSelectedStructure()
{
	AIFStructure* const TargetStructure = SelectedStructure.Get();
	const AIFPlayerController* const PC = GetOwningPlayerController();
	const UIFTacticalCommandComponent* const Tactical = IsValid(PC) ? PC->GetTacticalCommandComponent() : nullptr;
	const APawn* const PlayerPawn = IsValid(PC) ? PC->GetPawn() : nullptr;

	if (!IsValid(Tactical) || !Tactical->IsTacticalModeActive() || !IsValid(TargetStructure) || !TargetStructure->IsStructureAlive())
	{
		return false;
	}
	if (!IsValid(PlayerPawn) || !IFTeamUtils::AreFriendly(PlayerPawn, TargetStructure))
	{
		return false;
	}

	if (HasLivingMountedDependents(TargetStructure))
	{
		LastInvalidReason = EIFPlacementInvalidReason::HostHasMountedChildren;
		OnBuildPlacementRejected.Broadcast(EIFPlacementInvalidReason::HostHasMountedChildren);
		UE_LOG(LogIronField, Log, TEXT("[IF-Build] Blocked relocate of %s because mounted pieces depend on it."), *TargetStructure->GetName());
		return false;
	}

	UIFStructureDataAsset* const Data = TargetStructure->GetStructureData();
	if (!IsValid(Data))
	{
		return false;
	}

	FreeYawIndex = FMath::RoundToInt(FRotator::NormalizeAxis(TargetStructure->GetActorRotation().Yaw) / 90.f);
	FreeYawIndex = ((FreeYawIndex % 4) + 4) % 4;
	ClearSelectedStructure();
	return BeginBuildPlacementInternal(Data, TargetStructure);
}

bool UIFBuildingComponent::DemolishSelectedStructure()
{
	AIFStructure* const TargetStructure = SelectedStructure.Get();
	const AIFPlayerController* const PC = GetOwningPlayerController();
	const UIFTacticalCommandComponent* const Tactical = IsValid(PC) ? PC->GetTacticalCommandComponent() : nullptr;
	const APawn* const PlayerPawn = IsValid(PC) ? PC->GetPawn() : nullptr;

	if (!IsValid(Tactical) || !Tactical->IsTacticalModeActive()
		|| !IsValid(TargetStructure) || !TargetStructure->IsStructureAlive() || TargetStructure->IsKeep())
	{
		return false;
	}
	if (!IsValid(PlayerPawn) || !IFTeamUtils::AreFriendly(PlayerPawn, TargetStructure))
	{
		return false;
	}

	const FVector DemolishLocation = TargetStructure->GetActorLocation();
	ClearSelectedStructure();
	TargetStructure->DemolishStructure();
	OnStructureActionCompleted.Broadcast(EIFTacticalOrderType::DemolishStructure, DemolishLocation);
	return true;
}

void UIFBuildingComponent::UnbindRelocatingStructureDelegate()
{
	if (AIFStructure* const PrevRelocating = RelocatingStructure.Get(); IsValid(PrevRelocating))
	{
		PrevRelocating->OnStructureDestroyed.RemoveDynamic(this, &UIFBuildingComponent::HandleRelocatingStructureDestroyed);
	}
}

bool UIFBuildingComponent::BeginBuildPlacementInternal(UIFStructureDataAsset* StructureData, AIFStructure* InRelocatingStructure)
{
	if (!IsValid(StructureData))
	{
		return false;
	}

	UnbindRelocatingStructureDelegate();

	ActiveBuildData = StructureData;
	RelocatingStructure = InRelocatingStructure;
	bIsRelocatingMode = IsValid(InRelocatingStructure);
	bIsInBuildPlacementMode = true;
	bIsCurrentPlacementValid = false;
	bHasBroadcastValidity = false;
	bHasPreviewCache = false;
	bLastCursorTraceHit = false;
	bIsCurrentlySnapped = false;
	ConnectorFlipIndex = 0;
	StairMountMode = 0;
	LastSnapSideSign = 0;
	LastKnownStructureCount = INDEX_NONE;
	LastValidationRealTime = 0.0;
	LastInvalidReason = EIFPlacementInvalidReason::None;
	CurrentSnapState = EIFPlacementSnapState::Free;
	LastSnapHostStructure.Reset();
	LastValidBuildTerritory.Reset();
	LastGroundActor.Reset();

	if (bIsRelocatingMode && IsValid(InRelocatingStructure))
	{
		InRelocatingStructure->OnStructureDestroyed.AddUniqueDynamic(this, &UIFBuildingComponent::HandleRelocatingStructureDestroyed);
	}

	if (!StructureData->StructureClass.IsNull() && !IsValid(StructureData->StructureClass.Get()))
	{
		StructureData->StructureClass.LoadSynchronous();
	}
	ResolveGhostMaterials();

	if (AIFPlacementGhost* const Ghost = EnsurePlacementGhostSpawned())
	{
		if (const AIFPlayerController* const PC = GetOwningPlayerController())
		{
			if (const UIFTacticalCommandComponent* const Tactical = PC->GetTacticalCommandComponent())
			{
				if (AActor* const CameraRig = Tactical->GetTacticalCameraRigActor())
				{
					// Pair CameraRig tick prerequisite with TG_PostUpdateWork so mouse deprojection uses the current frame's camera transform.
					Ghost->AddTickPrerequisiteActor(CameraRig);
				}
			}
		}
		Ghost->BeginPreview(
			StructureData,
			FallbackGhostCubeMesh.Get(),
			CachedValidGhostMaterial,
			CachedInvalidGhostMaterial);
	}

	OnBuildPlacementModeChanged.Broadcast(true, StructureData);
	UpdatePlacementPreview(/*bForceFullRefresh=*/true);
	return true;
}

void UIFBuildingComponent::CancelBuildPlacement()
{
	const bool bWasActive = bIsInBuildPlacementMode || bIsRelocatingMode || ActiveBuildData.IsValid() || RelocatingStructure.IsValid();

	UnbindRelocatingStructureDelegate();

	bIsInBuildPlacementMode = false;
	bIsRelocatingMode = false;
	bIsCurrentPlacementValid = false;
	bHasBroadcastValidity = false;
	bHasPreviewCache = false;
	bLastCursorTraceHit = false;
	bIsCurrentlySnapped = false;
	ConnectorFlipIndex = 0;
	StairMountMode = 0;
	LastSnapSideSign = 0;
	LastKnownStructureCount = INDEX_NONE;
	LastValidationRealTime = 0.0;
	LastInvalidReason = EIFPlacementInvalidReason::None;
	CurrentSnapState = EIFPlacementSnapState::Free;
	ActiveBuildData.Reset();
	RelocatingStructure.Reset();
	LastSnapHostStructure.Reset();
	LastValidBuildTerritory.Reset();
	LastGroundActor.Reset();
	LastPreviewBuildData.Reset();
	LastPreviewRelocatingStructure.Reset();

	if (IsValid(PlacementGhost))
	{
		PlacementGhost->EndPreview();
	}

	if (bWasActive)
	{
		OnBuildPlacementModeChanged.Broadcast(false, nullptr);
	}
}

void UIFBuildingComponent::RotateBuildPreview(bool bReverse)
{
	if (!IsInBuildPlacementMode())
	{
		return;
	}

	const UIFStructureDataAsset* const Data = ActiveBuildData.Get();
	if (!IsValid(Data))
	{
		return;
	}

	if (bIsCurrentlySnapped)
	{
		if (Data->PlacementRule == EIFPlacementRule::WallWalkwayOnly || Data->PlacementRule == EIFPlacementRule::CornerWalkwayOnly)
		{
			// Cursor side selects the walkway face, so rotate input is ignored while mounted.
			return;
		}
		if (Data->PlacementRule == EIFPlacementRule::StairMountOrFree)
		{
			StairMountMode = (StairMountMode + (bReverse ? 2 : 1)) % 3;
			UpdatePlacementPreview(/*bForceFullRefresh=*/true);
			return;
		}
		if (Data->Connectors.Num() >= 2)
		{
			ConnectorFlipIndex = (ConnectorFlipIndex + (bReverse ? Data->Connectors.Num() - 1 : 1)) % Data->Connectors.Num();
			UpdatePlacementPreview(/*bForceFullRefresh=*/true);
			return;
		}
	}

	FreeYawIndex = (FreeYawIndex + (bReverse ? 3 : 1)) % 4;
	UpdatePlacementPreview(/*bForceFullRefresh=*/true);
}

void UIFBuildingComponent::UpdatePlacementPreview(bool bForceFullRefresh)
{
	const AIFPlayerController* const PC = GetOwningPlayerController();
	const UIFTacticalCommandComponent* const Tactical = IsValid(PC) ? PC->GetTacticalCommandComponent() : nullptr;
	if (!IsValid(Tactical) || !Tactical->IsTacticalModeActive() || !IsInBuildPlacementMode())
	{
		CancelBuildPlacement();
		return;
	}

	if (bIsRelocatingMode)
	{
		const AIFStructure* const MovingStruct = RelocatingStructure.Get();
		if (!IsValid(MovingStruct) || !MovingStruct->IsStructureAlive())
		{
			CancelBuildPlacement();
			return;
		}
	}

	const UIFStructureDataAsset* const Data = ActiveBuildData.Get();
	UWorld* const World = GetWorld();
	if (!IsValid(Data) || !IsValid(PC) || !IsValid(World))
	{
		return;
	}

	float MouseX = 0.f;
	float MouseY = 0.f;
	const FVector2D CurrentMousePos = PC->GetMousePosition(MouseX, MouseY)
		? FVector2D(MouseX, MouseY)
		: LastPreviewMousePos;

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDir = FVector::ForwardVector;
	if (!PC->DeprojectMousePositionToWorld(RayOrigin, RayDir))
	{
		return;
	}

	const bool bInputsUnchanged = !bForceFullRefresh
		&& bHasPreviewCache
		&& LastPreviewFreeYawIndex == FreeYawIndex
		&& LastPreviewConnectorFlipIndex == ConnectorFlipIndex
		&& LastPreviewStairMountMode == StairMountMode
		&& LastPreviewBuildData.Get() == Data
		&& LastPreviewRelocatingStructure.Get() == RelocatingStructure.Get()
		&& FVector2D::DistSquared(CurrentMousePos, LastPreviewMousePos) <= FMath::Square(0.5f)
		&& FVector::DistSquared(RayOrigin, LastPreviewRayOrigin) <= FMath::Square(0.1f)
		&& FVector::DotProduct(RayDir, LastPreviewRayDir) >= 0.9999996f;

	const double NowRealSeconds = FPlatformTime::Seconds();
	if (bInputsUnchanged && (NowRealSeconds - LastValidationRealTime) < StationaryValidationIntervalSeconds)
	{
		return;
	}

	const FGenericTeamId BuilderTeamId = ResolveBuilderTeamId();
	FIFBuildingSnapSolver::GatherAliveStructures(
		World,
		BuilderTeamId,
		RelocatingStructure.Get(),
		SolverScratch.AllAliveStructures,
		SolverScratch.FriendlyStructures);

	const bool bStructureSetChanged = (SolverScratch.AllAliveStructures.Num() != LastKnownStructureCount);
	LastKnownStructureCount = SolverScratch.AllAliveStructures.Num();
	LastValidationRealTime = NowRealSeconds;

	const bool bNeedFullSolve = !bInputsUnchanged || bStructureSetChanged || !bLastCursorTraceHit;
	if (bNeedFullSolve)
	{
		LastPreviewMousePos = CurrentMousePos;
		LastPreviewRayOrigin = RayOrigin;
		LastPreviewRayDir = RayDir;
		LastPreviewFreeYawIndex = FreeYawIndex;
		LastPreviewConnectorFlipIndex = ConnectorFlipIndex;
		LastPreviewStairMountMode = StairMountMode;
		LastPreviewBuildData = const_cast<UIFStructureDataAsset*>(Data);
		LastPreviewRelocatingStructure = RelocatingStructure;
		bHasPreviewCache = true;

		const AActor* const CameraRig = Tactical->GetTacticalCameraRigActor();
		FHitResult GroundHit;
		const bool bHitGround = FIFTacticalCursorQuery::TraceGroundUnderCursor(
			World,
			RayOrigin,
			RayDir,
			GroundTraceDistance,
			CameraRig,
			GroundHit,
			&GroundTraceScratchHits);

		const AIFStructure* HoveredStructure = nullptr;
		FVector HoveredImpactPoint = FVector::ZeroVector;
		FVector HoveredImpactNormal = FVector::UpVector;
		bool bHitStructure = false;
		if (UsesStructureMountedPlacement(Data->PlacementRule))
		{
			const float StructureTraceMaxDist = bHitGround ? (GroundHit.Distance + 10.f) : GroundTraceDistance;
			bHitStructure = TraceFriendlyStructureUnderCursor(
				RayOrigin,
				RayDir,
				BuilderTeamId,
				CameraRig,
				StructureTraceMaxDist,
				HoveredStructure,
				HoveredImpactPoint,
				HoveredImpactNormal);
		}

		if (!bHitGround && !bHitStructure)
		{
			bLastCursorTraceHit = false;
			bIsCurrentPlacementValid = false;
			LastValidBuildTerritory.Reset();
			LastGroundActor.Reset();

			if (IsValid(PlacementGhost))
			{
				PlacementGhost->SetPreviewMeshVisible(false);
			}

			if (!bHasBroadcastValidity || LastInvalidReason != EIFPlacementInvalidReason::InvalidLocation)
			{
				bHasBroadcastValidity = true;
				LastInvalidReason = EIFPlacementInvalidReason::InvalidLocation;
				OnBuildPlacementValidityChanged.Broadcast(false, LastInvalidReason);
			}
			return;
		}

		bLastCursorTraceHit = true;
		LastGroundNormal = bHitGround ? GroundHit.ImpactNormal : FVector::UpVector;
		LastGroundActor = bHitGround ? GroundHit.GetActor() : nullptr;

		FIFSnapQuery SnapQuery;
		SnapQuery.World = World;
		SnapQuery.Data = Data;
		SnapQuery.CursorGroundLocation = bHitGround ? GroundHit.ImpactPoint : HoveredImpactPoint;
		SnapQuery.FreeYawIndex = FreeYawIndex;
		SnapQuery.ConnectorFlipIndex = ConnectorFlipIndex;
		SnapQuery.StairMountMode = StairMountMode;
		SnapQuery.SnapRadiusCm = SnapRadiusCm;
		SnapQuery.MountSnapRadiusCm = MountSnapRadiusCm;
		SnapQuery.PreviousSnapHost = LastSnapHostStructure.Get();
		SnapQuery.PreviousSideSign = LastSnapSideSign;
		SnapQuery.HoveredStructure = HoveredStructure;
		SnapQuery.HoveredImpactPoint = HoveredImpactPoint;
		SnapQuery.HoveredImpactNormal = HoveredImpactNormal;
		SnapQuery.ViewRayOrigin = RayOrigin;

		CachedSnapResult = FIFBuildingSnapSolver::SolvePlacementTransform(
			SnapQuery,
			SolverScratch.FriendlyStructures,
			SolverScratch);
	}

	const EIFPlacementSnapState PrevSnapState = CurrentSnapState;
	const bool PrevValidState = bIsCurrentPlacementValid;
	bIsCurrentlySnapped = CachedSnapResult.bSnapped;
	CurrentSnapState = CachedSnapResult.bSnapped ? CachedSnapResult.SnapState : EIFPlacementSnapState::Free;
	CurrentPreviewLocation = CachedSnapResult.Location;
	CurrentPreviewYaw = CachedSnapResult.YawDegrees;
	LastSnapSideSign = CachedSnapResult.bSnapped ? CachedSnapResult.SideSign : 0;

	FIFPlacementValidationContext ValidationCtx;
	ValidationCtx.World = World;
	ValidationCtx.Data = Data;
	ValidationCtx.Candidate = &CachedSnapResult;
	ValidationCtx.PlayerPawn = PC->GetPawn();
	ValidationCtx.GroundActor = LastGroundActor.Get();
	ValidationCtx.GroundNormal = LastGroundNormal;
	ValidationCtx.MaxPlacementSlopeDegrees = MaxPlacementSlopeDegrees;
	ValidationCtx.BuilderTeamId = BuilderTeamId;
	ValidationCtx.bRequireHeroInTerritory = bRequireHeroInTerritory;
	ValidationCtx.RelocatingStructure = RelocatingStructure.Get();
	ValidationCtx.PlacementGhost = PlacementGhost;

	AIFTerritory* ValidTerritory = nullptr;
	EIFPlacementInvalidReason InvalidReason = EIFPlacementInvalidReason::None;
	const bool bIsValidPlacement = FIFBuildingSnapSolver::ValidatePlacement(
		ValidationCtx,
		SolverScratch.AllAliveStructures,
		SolverScratch,
		ValidTerritory,
		InvalidReason);

	bIsCurrentPlacementValid = bIsValidPlacement;
	LastSnapHostStructure = const_cast<AIFStructure*>(CachedSnapResult.HostStructure.Get());
	LastValidBuildTerritory = ValidTerritory;

	if (IsValid(PlacementGhost))
	{
		PlacementGhost->UpdatePreviewTransformAndValidity(CurrentPreviewLocation, CurrentPreviewYaw, bIsValidPlacement);
	}

	if (!bHasBroadcastValidity
		|| LastInvalidReason != InvalidReason
		|| PrevValidState != bIsValidPlacement
		|| PrevSnapState != CurrentSnapState)
	{
		bHasBroadcastValidity = true;
		LastInvalidReason = InvalidReason;
		OnBuildPlacementValidityChanged.Broadcast(bIsValidPlacement, InvalidReason);
	}
}

FText UIFBuildingComponent::GetPlacementStatusText() const
{
	if (bIsCurrentPlacementValid)
	{
		switch (CurrentSnapState)
		{
		case EIFPlacementSnapState::SnappedToCorner:
			return NSLOCTEXT("IronFieldTactical", "BuildSnapCorner", "SNAPPED TO CORNER");
		case EIFPlacementSnapState::SnappedToWall:
			return NSLOCTEXT("IronFieldTactical", "BuildSnapWall", "SNAPPED TO WALL");
		case EIFPlacementSnapState::SnappedToWalkway:
			return NSLOCTEXT("IronFieldTactical", "BuildSnapWalkway", "SNAPPED TO WALKWAY");
		case EIFPlacementSnapState::SnappedToGate:
			return NSLOCTEXT("IronFieldTactical", "BuildSnapGate", "SNAPPED TO GATE ARCHWAY");
		case EIFPlacementSnapState::Free:
		default:
			return NSLOCTEXT("IronFieldTactical", "BuildReady", "READY TO PLACE");
		}
	}

	switch (LastInvalidReason)
	{
	case EIFPlacementInvalidReason::RequiresWallAttachment:
		return NSLOCTEXT("IronFieldTactical", "BuildReqWall", "ATTACH TO AN EXISTING WALL");
	case EIFPlacementInvalidReason::RequiresCornerAttachment:
		return NSLOCTEXT("IronFieldTactical", "BuildReqCorner", "ATTACH TO A WALL CORNER");
	case EIFPlacementInvalidReason::RequiresGateArchway:
		return NSLOCTEXT("IronFieldTactical", "BuildReqGate", "ATTACH INSIDE A GATE ARCHWAY");
	case EIFPlacementInvalidReason::GateAlreadyHasDoor:
		return NSLOCTEXT("IronFieldTactical", "BuildGateHasDoor", "GATE ALREADY HAS A DOOR");
	case EIFPlacementInvalidReason::HostHasMountedChildren:
		return NSLOCTEXT("IronFieldTactical", "BuildHostHasChildren", "REMOVE MOUNTED PIECES FIRST");
	case EIFPlacementInvalidReason::OutsideOwnedTerritory:
		return NSLOCTEXT("IronFieldTactical", "BuildOutsideTerr", "OUTSIDE TERRITORY");
	case EIFPlacementInvalidReason::HeroNotInTerritory:
		return NSLOCTEXT("IronFieldTactical", "BuildHeroOut", "MOVE COMMANDER INTO TERRITORY");
	case EIFPlacementInvalidReason::KeepAlreadyExists:
		return NSLOCTEXT("IronFieldTactical", "BuildKeepExists", "TERRITORY ALREADY HAS A KEEP");
	case EIFPlacementInvalidReason::TooCloseToFlag:
		return NSLOCTEXT("IronFieldTactical", "BuildFlag", "TOO CLOSE TO FLAG");
	case EIFPlacementInvalidReason::SlopeTooSteep:
		return NSLOCTEXT("IronFieldTactical", "BuildSlopeSteep", "TERRAIN TOO STEEP");
	case EIFPlacementInvalidReason::BlockedByObstacle:
		return NSLOCTEXT("IronFieldTactical", "BuildBlockedStruct", "BLOCKED BY STRUCTURE");
	case EIFPlacementInvalidReason::BlockedByUnit:
		return NSLOCTEXT("IronFieldTactical", "BuildBlockedUnit", "UNIT IN THE WAY");
	case EIFPlacementInvalidReason::InvalidLocation:
	default:
		return NSLOCTEXT("IronFieldTactical", "BuildInvalidLoc", "INVALID LOCATION");
	}
}

bool UIFBuildingComponent::TryConfirmPlacement()
{
	UpdatePlacementPreview(/*bForceFullRefresh=*/true);

	if (bIsRelocatingMode)
	{
		AIFStructure* const MovingStruct = RelocatingStructure.Get();
		if (!IsValid(MovingStruct) || !MovingStruct->IsStructureAlive())
		{
			CancelBuildPlacement();
			return false;
		}
	}

	if (!bIsCurrentPlacementValid || !ActiveBuildData.IsValid())
	{
		OnBuildPlacementRejected.Broadcast(LastInvalidReason);
		return false;
	}

	UWorld* const World = GetWorld();
	UIFStructureDataAsset* const Data = ActiveBuildData.Get();
	AIFTerritory* const Territory = LastValidBuildTerritory.Get();
	if (!IsValid(World) || !IsValid(Data) || !IsValid(Territory))
	{
		return false;
	}

	const bool bIsMountedSnap = (Data->PlacementRule == EIFPlacementRule::WallWalkwayOnly)
		|| (Data->PlacementRule == EIFPlacementRule::CornerWalkwayOnly)
		|| (Data->PlacementRule == EIFPlacementRule::GateOpeningOnly)
		|| (Data->PlacementRule == EIFPlacementRule::StairMountOrFree && bIsCurrentlySnapped);

	if (bIsRelocatingMode)
	{
		AIFStructure* const MovingStruct = RelocatingStructure.Get();
		if (!IsValid(MovingStruct) || !MovingStruct->IsStructureAlive())
		{
			CancelBuildPlacement();
			return false;
		}

		AIFStructure* const HostToBind = bIsMountedSnap ? LastSnapHostStructure.Get() : nullptr;
		MovingStruct->RelocateStructure(CurrentPreviewLocation, CurrentPreviewYaw, Territory);
		MovingStruct->SetHostStructure(HostToBind);
		CancelBuildPlacement();
		SelectFriendlyStructure(MovingStruct);
		OnStructureActionCompleted.Broadcast(EIFTacticalOrderType::RelocateStructure, CurrentPreviewLocation);
		return true;
	}

	UClass* StructureClass = Data->StructureClass.Get();
	if (!IsValid(StructureClass) && !Data->StructureClass.IsNull())
	{
		StructureClass = Data->StructureClass.LoadSynchronous();
	}
	if (!IsValid(StructureClass))
	{
		StructureClass = AIFStructure::StaticClass();
	}

	const FTransform SpawnTransform(FRotator(0.f, CurrentPreviewYaw, 0.f), CurrentPreviewLocation);
	AIFStructure* const NewStructure = World->SpawnActorDeferred<AIFStructure>(
		StructureClass,
		SpawnTransform,
		Territory,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(NewStructure))
	{
		return false;
	}

	// InitializeStructure and SetHostStructure must run before FinishSpawning because AIFStructure::BeginPlay reads data, team, and territory.
	NewStructure->InitializeStructure(Data, ResolveBuilderTeamId(), Territory, Data->bInstantBuild);
	if (bIsMountedSnap)
	{
		NewStructure->SetHostStructure(LastSnapHostStructure.Get());
	}
	NewStructure->FinishSpawning(SpawnTransform);

	OnStructureActionCompleted.Broadcast(EIFTacticalOrderType::BuildStructure, CurrentPreviewLocation);

	if (Data->bContinuousPlacement)
	{
		UpdatePlacementPreview(/*bForceFullRefresh=*/true);
	}
	else
	{
		CancelBuildPlacement();
	}

	return true;
}

void UIFBuildingComponent::SelectFriendlyStructure(AIFStructure* Structure)
{
	if (!IsValid(Structure) || !Structure->IsStructureAlive())
	{
		ClearSelectedStructure();
		return;
	}

	if (SelectedStructure.Get() == Structure)
	{
		Structure->SetFriendlySelected(true);
		OnSelectedStructureChanged.Broadcast(Structure);
		return;
	}

	ClearSelectedStructure();

	SelectedStructure = Structure;
	Structure->OnStructureDestroyed.AddUniqueDynamic(this, &UIFBuildingComponent::HandleSelectedStructureDestroyed);
	Structure->SetFriendlySelected(true);
	OnSelectedStructureChanged.Broadcast(Structure);
}

void UIFBuildingComponent::ClearSelectedStructure()
{
	AIFStructure* const PrevSelected = SelectedStructure.Get();
	if (IsValid(PrevSelected))
	{
		PrevSelected->OnStructureDestroyed.RemoveDynamic(this, &UIFBuildingComponent::HandleSelectedStructureDestroyed);
		PrevSelected->SetFriendlySelected(false);
	}

	const bool bHadSelected = SelectedStructure.IsValid() || (PrevSelected != nullptr);
	SelectedStructure.Reset();

	if (bHadSelected)
	{
		OnSelectedStructureChanged.Broadcast(nullptr);
	}
}

void UIFBuildingComponent::HandleSelectedStructureDestroyed(AIFStructure* DestroyedStructure, AActor*)
{
	if (SelectedStructure.Get() == DestroyedStructure)
	{
		ClearSelectedStructure();
	}
}

void UIFBuildingComponent::HandleRelocatingStructureDestroyed(AIFStructure* DestroyedStructure, AActor*)
{
	if (RelocatingStructure.Get() == DestroyedStructure)
	{
		CancelBuildPlacement();
	}
}

AIFPlacementGhost* UIFBuildingComponent::EnsurePlacementGhostSpawned()
{
	if (IsValid(PlacementGhost))
	{
		return PlacementGhost;
	}

	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	PlacementGhost = World->SpawnActor<AIFPlacementGhost>(AIFPlacementGhost::StaticClass(), FTransform::Identity, SpawnParams);
	if (IsValid(PlacementGhost))
	{
		PlacementGhost->SetOwningBuildingComponent(this);
	}
	return PlacementGhost;
}

FGenericTeamId UIFBuildingComponent::ResolveBuilderTeamId() const
{
	if (const AIFPlayerController* const PC = GetOwningPlayerController())
	{
		if (const APawn* const PlayerPawn = PC->GetPawn())
		{
			const FGenericTeamId PawnTeam = IFTeamUtils::GetActorTeamId(PlayerPawn);
			if (PawnTeam != FGenericTeamId::NoTeam)
			{
				return PawnTeam;
			}
		}
	}
	return FGenericTeamId(IFTeamIds::Dawn);
}

AIFPlayerController* UIFBuildingComponent::GetOwningPlayerController() const
{
	return Cast<AIFPlayerController>(GetOwner());
}
