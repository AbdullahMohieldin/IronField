#include "Castle/IFBuildingComponent.h"

#include "Castle/IFBuildingSnapSolver.h"
#include "Castle/IFGateDoor.h"
#include "Castle/IFStructure.h"
#include "Castle/IFStructureDataAsset.h"
#include "Castle/IFTerritory.h"
#include "Combat/IFTeamTypes.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerController.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Tactical/IFPlacementGhost.h"
#include "Tactical/IFTacticalCommandComponent.h"

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

	AddIfValid(PlacementGhostMesh);
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
	BuildAssetsLoadHandle.Reset();
	EnsureBuildableStructuresLoaded();
}

void UIFBuildingComponent::EnsureBuildableStructuresLoaded()
{
	if (LoadedBuildableStructures.Num() == BuildableStructures.Num() && !LoadedBuildableStructures.IsEmpty())
	{
		return;
	}

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

bool UIFBuildingComponent::BeginBuildPlacementByIndex(int32 BuildIndex)
{
	if (!LoadedBuildableStructures.IsValidIndex(BuildIndex))
	{
		return false;
	}

	UIFStructureDataAsset* const Data = LoadedBuildableStructures[BuildIndex].Get();
	if (!IsValid(Data))
	{
		return false;
	}

	if (IsInBuildPlacementMode() && !IsRelocatingStructure() && ActiveBuildData.Get() == Data)
	{
		CancelBuildPlacement();
		return false;
	}

	return BeginBuildPlacement(Data);
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
	if (IsValid(PlayerPawn) && !IFTeamUtils::AreFriendly(PlayerPawn, TargetStructure))
	{
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
	if (IsValid(PlayerPawn) && !IFTeamUtils::AreFriendly(PlayerPawn, TargetStructure))
	{
		return false;
	}

	const FVector DemolishLocation = TargetStructure->GetActorLocation();
	ClearSelectedStructure();
	TargetStructure->DemolishStructure();
	OnStructureActionCompleted.Broadcast(EIFTacticalOrderType::DemolishStructure, DemolishLocation);
	return true;
}

bool UIFBuildingComponent::BeginBuildPlacementInternal(UIFStructureDataAsset* StructureData, AIFStructure* InRelocatingStructure)
{
	if (!IsValid(StructureData))
	{
		return false;
	}

	if (const AIFPlayerController* const PC = GetOwningPlayerController())
	{
		if (UIFTacticalCommandComponent* const Tactical = PC->GetTacticalCommandComponent())
		{
			Tactical->CancelMarqueeSelection();
			Tactical->CancelFormationDragPreview();
		}
	}

	ActiveBuildData = StructureData;
	RelocatingStructure = InRelocatingStructure;
	bIsInBuildPlacementMode = true;
	bIsCurrentPlacementValid = false;
	bHasBroadcastValidity = false;
	bIsCurrentlySnapped = false;
	ConnectorFlipIndex = 0;
	StairMountMode = 0;
	LastInvalidReason = EIFPlacementInvalidReason::None;
	CurrentSnapState = EIFPlacementSnapState::Free;
	LastSnapHostStructure.Reset();
	LastValidBuildTerritory.Reset();

	if (!StructureData->StructureClass.IsNull() && !IsValid(StructureData->StructureClass.Get()))
	{
		StructureData->StructureClass.LoadSynchronous();
	}
	if (!PlacementGhostValidMaterial.IsNull() && !IsValid(PlacementGhostValidMaterial.Get()))
	{
		PlacementGhostValidMaterial.LoadSynchronous();
	}
	if (!PlacementGhostInvalidMaterial.IsNull() && !IsValid(PlacementGhostInvalidMaterial.Get()))
	{
		PlacementGhostInvalidMaterial.LoadSynchronous();
	}

	if (AIFPlacementGhost* const Ghost = EnsurePlacementGhostSpawned())
	{
		if (const AIFPlayerController* const PC = GetOwningPlayerController())
		{
			if (const UIFTacticalCommandComponent* const Tactical = PC->GetTacticalCommandComponent())
			{
				if (AActor* const CameraRig = Tactical->GetTacticalCameraRigActor())
				{
					Ghost->AddTickPrerequisiteActor(CameraRig);
				}
			}
		}
		Ghost->BeginPreview(
			StructureData,
			PlacementGhostMesh.Get(),
			PlacementGhostValidMaterial.Get(),
			PlacementGhostInvalidMaterial.Get());
	}

	OnBuildPlacementModeChanged.Broadcast(true, StructureData);
	UpdatePlacementPreview();
	return true;
}

void UIFBuildingComponent::CancelBuildPlacement()
{
	const bool bWasActive = bIsInBuildPlacementMode || ActiveBuildData.IsValid() || RelocatingStructure.IsValid();

	bIsInBuildPlacementMode = false;
	bIsCurrentPlacementValid = false;
	bHasBroadcastValidity = false;
	bIsCurrentlySnapped = false;
	ConnectorFlipIndex = 0;
	StairMountMode = 0;
	LastInvalidReason = EIFPlacementInvalidReason::None;
	CurrentSnapState = EIFPlacementSnapState::Free;
	ActiveBuildData.Reset();
	RelocatingStructure.Reset();
	LastSnapHostStructure.Reset();
	LastValidBuildTerritory.Reset();

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
			// Side/corner lock is strictly driven by the mouse cursor; R never jumps across.
			return;
		}
		if (Data->PlacementRule == EIFPlacementRule::StairMountOrFree)
		{
			StairMountMode = (StairMountMode + (bReverse ? 2 : 1)) % 3;
			UpdatePlacementPreview();
			return;
		}
		if (Data->Connectors.Num() >= 2)
		{
			ConnectorFlipIndex = (ConnectorFlipIndex + (bReverse ? Data->Connectors.Num() - 1 : 1)) % Data->Connectors.Num();
			UpdatePlacementPreview();
			return;
		}
	}

	FreeYawIndex = (FreeYawIndex + (bReverse ? 3 : 1)) % 4;
	UpdatePlacementPreview();
}

void UIFBuildingComponent::UpdatePlacementPreview()
{
	const AIFPlayerController* const PC = GetOwningPlayerController();
	const UIFTacticalCommandComponent* const Tactical = IsValid(PC) ? PC->GetTacticalCommandComponent() : nullptr;
	if (!IsValid(Tactical) || !Tactical->IsTacticalModeActive() || !IsInBuildPlacementMode())
	{
		CancelBuildPlacement();
		return;
	}

	const UIFStructureDataAsset* const Data = ActiveBuildData.Get();
	if (!IsValid(Data) || !IsValid(PC))
	{
		return;
	}

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDir = FVector::ForwardVector;
	if (!PC->DeprojectMousePositionToWorld(RayOrigin, RayDir))
	{
		return;
	}

	FHitResult GroundHit;
	if (!TraceGroundUnderCursor(RayOrigin, RayDir, GroundHit))
	{
		bIsCurrentPlacementValid = false;
		LastValidBuildTerritory.Reset();
		if (!bHasBroadcastValidity || LastInvalidReason != EIFPlacementInvalidReason::InvalidLocation)
		{
			bHasBroadcastValidity = true;
			LastInvalidReason = EIFPlacementInvalidReason::InvalidLocation;
			OnBuildPlacementValidityChanged.Broadcast(false, LastInvalidReason);
		}
		return;
	}

	const FIFSnapResult SnapResult = FIFBuildingSnapSolver::SolvePlacementTransform(
		GetWorld(),
		Data,
		GroundHit.ImpactPoint,
		FreeYawIndex,
		ConnectorFlipIndex,
		StairMountMode,
		SnapRadiusCm,
		MountSnapRadiusCm,
		RelocatingStructure.Get());

	const EIFPlacementSnapState PrevSnapState = CurrentSnapState;
	const bool PrevValidState = bIsCurrentPlacementValid;
	bIsCurrentlySnapped = SnapResult.bSnapped;
	CurrentSnapState = SnapResult.bSnapped ? SnapResult.SnapState : EIFPlacementSnapState::Free;
	CurrentPreviewLocation = SnapResult.Location;
	CurrentPreviewYaw = SnapResult.YawDegrees;

	AIFTerritory* ValidTerritory = nullptr;
	EIFPlacementInvalidReason InvalidReason = EIFPlacementInvalidReason::None;
	const bool bIsValidPlacement = FIFBuildingSnapSolver::ValidatePlacement(
		GetWorld(),
		Data,
		SnapResult,
		PC->GetPawn(),
		bRequireHeroInTerritory,
		RelocatingStructure.Get(),
		PlacementGhost,
		ValidTerritory,
		InvalidReason);

	bIsCurrentPlacementValid = bIsValidPlacement;
	LastSnapHostStructure = const_cast<AIFStructure*>(SnapResult.HostStructure.Get());
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
	case EIFPlacementInvalidReason::OutsideOwnedTerritory:
		return NSLOCTEXT("IronFieldTactical", "BuildOutsideTerr", "OUTSIDE TERRITORY");
	case EIFPlacementInvalidReason::HeroNotInTerritory:
		return NSLOCTEXT("IronFieldTactical", "BuildHeroOut", "MOVE COMMANDER INTO TERRITORY");
	case EIFPlacementInvalidReason::KeepAlreadyExists:
		return NSLOCTEXT("IronFieldTactical", "BuildKeepExists", "TERRITORY ALREADY HAS A KEEP");
	case EIFPlacementInvalidReason::TooCloseToFlag:
		return NSLOCTEXT("IronFieldTactical", "BuildFlag", "TOO CLOSE TO FLAG");
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
	UpdatePlacementPreview();

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

	if (AIFStructure* const MovingStruct = RelocatingStructure.Get(); IsValid(MovingStruct) && MovingStruct->IsStructureAlive())
	{
		const bool bIsMountedSnap = (Data->PlacementRule == EIFPlacementRule::WallWalkwayOnly)
			|| (Data->PlacementRule == EIFPlacementRule::CornerWalkwayOnly)
			|| (Data->PlacementRule == EIFPlacementRule::GateOpeningOnly)
			|| (Data->PlacementRule == EIFPlacementRule::StairMountOrFree && bIsCurrentlySnapped);
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

	const bool bIsMountedSnap = (Data->PlacementRule == EIFPlacementRule::WallWalkwayOnly)
		|| (Data->PlacementRule == EIFPlacementRule::CornerWalkwayOnly)
		|| (Data->PlacementRule == EIFPlacementRule::GateOpeningOnly)
		|| (Data->PlacementRule == EIFPlacementRule::StairMountOrFree && bIsCurrentlySnapped);

	NewStructure->InitializeStructure(Data, FGenericTeamId(IFTeamIds::Player), Territory, Data->bInstantBuild);
	if (bIsMountedSnap)
	{
		NewStructure->SetHostStructure(LastSnapHostStructure.Get());
	}
	NewStructure->FinishSpawning(SpawnTransform);
	Territory->RegisterStructure(NewStructure);

	OnStructureActionCompleted.Broadcast(EIFTacticalOrderType::BuildStructure, CurrentPreviewLocation);

	if (Data->bContinuousPlacement)
	{
		UpdatePlacementPreview();
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

bool UIFBuildingComponent::TraceGroundUnderCursor(const FVector& RayOrigin, const FVector& RayDir, FHitResult& OutHit) const
{
	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	FCollisionObjectQueryParams GroundObjectParams;
	GroundObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFBuildGroundTrace), false);
	if (const AIFPlayerController* const PC = GetOwningPlayerController())
	{
		if (const UIFTacticalCommandComponent* const Tactical = PC->GetTacticalCommandComponent())
		{
			if (AActor* const CameraRig = Tactical->GetTacticalCameraRigActor())
			{
				QueryParams.AddIgnoredActor(CameraRig);
			}
		}
	}

	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByObjectType(Hits, RayOrigin, RayOrigin + RayDir * GroundTraceDistance, GroundObjectParams, QueryParams))
	{
		return false;
	}

	for (const FHitResult& Hit : Hits)
	{
		if (Hit.bBlockingHit && !Cast<APawn>(Hit.GetActor()))
		{
			OutHit = Hit;
			return true;
		}
	}

	return false;
}

AIFPlayerController* UIFBuildingComponent::GetOwningPlayerController() const
{
	return Cast<AIFPlayerController>(GetOwner());
}
