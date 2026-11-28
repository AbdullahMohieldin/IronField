#include "Castle/IFTerritory.h"

#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IFGameState.h"
#include "Core/IFGameTypes.h"
#include "Core/IFLog.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

namespace
{
	void ConfigureCosmeticMesh(UStaticMeshComponent* MeshComp)
	{
		if (!IsValid(MeshComp))
		{
			return;
		}
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		MeshComp->SetGenerateOverlapEvents(false);
		MeshComp->SetCanEverAffectNavigation(false);
		MeshComp->SetCastShadow(false);
		MeshComp->bReceivesDecals = false;
	}
}

AIFTerritory::AIFTerritory()
{
	PrimaryActorTick.bCanEverTick = false;

	TerritoryRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TerritoryRoot"));
	SetRootComponent(TerritoryRoot);

	BorderNorth = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BorderNorth"));
	BorderNorth->SetupAttachment(TerritoryRoot);
	ConfigureCosmeticMesh(BorderNorth);

	BorderSouth = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BorderSouth"));
	BorderSouth->SetupAttachment(TerritoryRoot);
	ConfigureCosmeticMesh(BorderSouth);

	BorderEast = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BorderEast"));
	BorderEast->SetupAttachment(TerritoryRoot);
	ConfigureCosmeticMesh(BorderEast);

	BorderWest = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BorderWest"));
	BorderWest->SetupAttachment(TerritoryRoot);
	ConfigureCosmeticMesh(BorderWest);

	FlagCaptureZone = CreateDefaultSubobject<USphereComponent>(TEXT("FlagCaptureZone"));
	FlagCaptureZone->SetupAttachment(TerritoryRoot);
	FlagCaptureZone->InitSphereRadius(FlagCaptureRadius);
	FlagCaptureZone->SetCollisionObjectType(ECC_WorldDynamic);
	FlagCaptureZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FlagCaptureZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	FlagCaptureZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	FlagCaptureZone->SetCollisionResponseToChannel(IFCollision::PawnDawn, ECR_Overlap);
	FlagCaptureZone->SetCollisionResponseToChannel(IFCollision::PawnDusk, ECR_Overlap);
	FlagCaptureZone->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	FlagCaptureZone->SetGenerateOverlapEvents(true);
	FlagCaptureZone->SetCanEverAffectNavigation(false);

	FlagPoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlagPole"));
	FlagPoleMesh->SetupAttachment(FlagCaptureZone);
	ConfigureCosmeticMesh(FlagPoleMesh);

	FlagBannerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlagBanner"));
	FlagBannerMesh->SetupAttachment(FlagPoleMesh);
	FlagBannerMesh->ComponentTags.Add(AIFStructure::TeamColoredComponentTag);
	ConfigureCosmeticMesh(FlagBannerMesh);

	FlagZoneRingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlagZoneRing"));
	FlagZoneRingMesh->SetupAttachment(FlagCaptureZone);
	ConfigureCosmeticMesh(FlagZoneRingMesh);

	UpdatePerimeterGeometry();
}

void AIFTerritory::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdatePerimeterGeometry();
}

void AIFTerritory::BeginPlay()
{
	Super::BeginPlay();

	UpdatePerimeterGeometry();

	if (UWorld* const World = GetWorld())
	{
		if (AIFGameState* const GS = World->GetGameState<AIFGameState>())
		{
			GS->RegisterTerritory(this);
		}
	}

	if (IsValid(FlagCaptureZone))
	{
		FlagCaptureZone->OnComponentBeginOverlap.AddUniqueDynamic(this, &AIFTerritory::HandleFlagZoneBeginOverlap);
		FlagCaptureZone->OnComponentEndOverlap.AddUniqueDynamic(this, &AIFTerritory::HandleFlagZoneEndOverlap);

		// Seed any characters already standing inside the flag zone at BeginPlay.
		TArray<AActor*> InitialOverlaps;
		FlagCaptureZone->GetOverlappingActors(InitialOverlaps, AIFBaseCharacter::StaticClass());
		for (AActor* const OverlapActor : InitialOverlaps)
		{
			if (AIFBaseCharacter* const Char = Cast<AIFBaseCharacter>(OverlapActor); IsValid(Char) && Char->IsAlive())
			{
				OverlappingCharacters.Add(Char);
			}
		}
		if (!OverlappingCharacters.IsEmpty())
		{
			StartCaptureTimer();
		}
	}

	DiscoverPrePlacedStructures();
	RequestTerritoryAssets();
	ApplyFactionVisuals();
}

void AIFTerritory::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopCaptureTimer();

	if (TerritoryAssetsLoadHandle.IsValid())
	{
		TerritoryAssetsLoadHandle->CancelHandle();
		TerritoryAssetsLoadHandle.Reset();
	}

	for (const TWeakObjectPtr<AIFStructure>& StructPtr : OwnedStructures)
	{
		if (AIFStructure* const Structure = StructPtr.Get())
		{
			Structure->OnStructureDestroyed.RemoveDynamic(this, &AIFTerritory::HandleRegisteredStructureDestroyed);
		}
	}
	OwnedStructures.Reset();
	ActiveKeep.Reset();
	OverlappingCharacters.Reset();

	if (UWorld* const World = GetWorld())
	{
		if (AIFGameState* const GS = World->GetGameState<AIFGameState>())
		{
			GS->UnregisterTerritory(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool AIFTerritory::ContainsPoint2D(const FVector& WorldLocation) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	return FMath::Abs(Local.X) <= TerritoryHalfExtent.X && FMath::Abs(Local.Y) <= TerritoryHalfExtent.Y;
}

bool AIFTerritory::ContainsOrientedBox2D(const FVector& WorldCenter, const FVector2D& HalfExtents, float YawDegrees) const
{
	const float Rad = FMath::DegreesToRadians(YawDegrees);
	const float CosY = FMath::Cos(Rad);
	const float SinY = FMath::Sin(Rad);

	const FVector AxisX(CosY, SinY, 0.f);
	const FVector AxisY(-SinY, CosY, 0.f);

	const FVector Corners[4] = {
		WorldCenter + AxisX * HalfExtents.X + AxisY * HalfExtents.Y,
		WorldCenter + AxisX * HalfExtents.X - AxisY * HalfExtents.Y,
		WorldCenter - AxisX * HalfExtents.X + AxisY * HalfExtents.Y,
		WorldCenter - AxisX * HalfExtents.X - AxisY * HalfExtents.Y
	};

	for (const FVector& Corner : Corners)
	{
		if (!ContainsPoint2D(Corner))
		{
			return false;
		}
	}
	return true;
}

FVector AIFTerritory::GetFlagWorldLocation() const
{
	return IsValid(FlagCaptureZone)
		? FlagCaptureZone->GetComponentLocation()
		: GetActorTransform().TransformPosition(FlagLocalOffset);
}

FTransform AIFTerritory::GetHeroRespawnTransform() const
{
	const FVector WorldLoc = GetActorTransform().TransformPosition(HeroRespawnLocalOffset);
	return FTransform(GetActorRotation(), WorldLoc);
}

bool AIFTerritory::HasActiveKeep() const
{
	const AIFStructure* const Keep = ActiveKeep.Get();
	return IsValid(Keep) && Keep->IsStructureAlive();
}

void AIFTerritory::RegisterStructure(AIFStructure* Structure)
{
	if (!IsValid(Structure) || !Structure->IsStructureAlive())
	{
		return;
	}

	const bool bAlreadyRegistered = OwnedStructures.ContainsByPredicate(
		[Structure](const TWeakObjectPtr<AIFStructure>& Ptr) { return Ptr.Get() == Structure; });
	if (!bAlreadyRegistered)
	{
		OwnedStructures.Add(Structure);
	}

	Structure->SetOwningTerritory(this);
	Structure->OnStructureDestroyed.AddUniqueDynamic(this, &AIFTerritory::HandleRegisteredStructureDestroyed);

	if (Structure->IsKeep())
	{
		ActiveKeep = Structure;
	}
}

void AIFTerritory::UnregisterStructure(AIFStructure* Structure)
{
	if (!IsValid(Structure))
	{
		return;
	}

	Structure->OnStructureDestroyed.RemoveDynamic(this, &AIFTerritory::HandleRegisteredStructureDestroyed);
	OwnedStructures.RemoveAll([Structure](const TWeakObjectPtr<AIFStructure>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == Structure;
	});

	if (ActiveKeep.Get() == Structure)
	{
		ActiveKeep.Reset();
	}
}

void AIFTerritory::GetOwnedStructures(TArray<AIFStructure*>& OutStructures) const
{
	OutStructures.Reset();
	for (const TWeakObjectPtr<AIFStructure>& Ptr : OwnedStructures)
	{
		if (AIFStructure* const Structure = Ptr.Get(); IsValid(Structure) && Structure->IsStructureAlive())
		{
			OutStructures.Add(Structure);
		}
	}
}

void AIFTerritory::SetTerritoryOwner(FGenericTeamId NewTeamID, bool bPreserveStructures)
{
	const FGenericTeamId PreviousTeam = TeamID;
	if (PreviousTeam == NewTeamID && bPreserveStructures)
	{
		return;
	}

	TeamID = NewTeamID;

	if (bPreserveStructures)
	{
		for (const TWeakObjectPtr<AIFStructure>& StructPtr : OwnedStructures)
		{
			if (AIFStructure* const Structure = StructPtr.Get(); IsValid(Structure) && Structure->IsStructureAlive())
			{
				Structure->SetGenericTeamId(NewTeamID);
			}
		}
	}

	ApplyFactionVisuals();

	UE_LOG(LogIronField, Log, TEXT("[IF-Territory] %s ownership changed: %u -> %u (preserve=%d)."),
		*GetName(), PreviousTeam.GetId(), NewTeamID.GetId(), bPreserveStructures ? 1 : 0);

	OnTerritoryOwnershipChanged.Broadcast(this, PreviousTeam.GetId(), NewTeamID.GetId());
}

void AIFTerritory::SetGenericTeamId(const FGenericTeamId& InTeamID)
{
	SetTerritoryOwner(InTeamID, true);
}

ETeamAttitude::Type AIFTerritory::GetTeamAttitudeTowards(const AActor& Other) const
{
	return IFTeamUtils::SolveAttitude(TeamID, IFTeamUtils::GetActorTeamId(&Other));
}

void AIFTerritory::HandleFlagZoneBeginOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32,
	bool,
	const FHitResult&)
{
	AIFBaseCharacter* const Char = Cast<AIFBaseCharacter>(OtherActor);
	if (!IsValid(Char) || Char->IsDead())
	{
		return;
	}

	OverlappingCharacters.Add(Char);
	StartCaptureTimer();
}

void AIFTerritory::HandleFlagZoneEndOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32)
{
	if (AIFBaseCharacter* const Char = Cast<AIFBaseCharacter>(OtherActor))
	{
		OverlappingCharacters.Remove(Char);
	}
}

void AIFTerritory::HandleRegisteredStructureDestroyed(AIFStructure* DestroyedStructure, AActor* KillerActor)
{
	// Re-entrancy guard during Keep scorched-earth collapse.
	if (bIsCollapsingTerritory || !IsValid(DestroyedStructure))
	{
		return;
	}

	const bool bWasKeep = DestroyedStructure->IsKeep() || (ActiveKeep.Get() == DestroyedStructure);
	UnregisterStructure(DestroyedStructure);

	if (!bWasKeep)
	{
		return;
	}

	// Method B — Violent Keep Destruction: collapse all remaining structures in this territory.
	bIsCollapsingTerritory = true;

	const TArray<TWeakObjectPtr<AIFStructure>> Snapshot = OwnedStructures;
	OwnedStructures.Reset();
	ActiveKeep.Reset();

	for (const TWeakObjectPtr<AIFStructure>& StructPtr : Snapshot)
	{
		if (AIFStructure* const Remaining = StructPtr.Get(); IsValid(Remaining) && Remaining->IsStructureAlive())
		{
			Remaining->OnStructureDestroyed.RemoveDynamic(this, &AIFTerritory::HandleRegisteredStructureDestroyed);
			Remaining->CollapseStructure(KillerActor);
		}
	}

	bIsCollapsingTerritory = false;

	FGenericTeamId KillerTeam = IFTeamUtils::GetActorTeamId(KillerActor);
	if (KillerTeam == FGenericTeamId::NoTeam || KillerTeam == TeamID)
	{
		KillerTeam = FGenericTeamId(IFTeamIds::Neutral);
	}

	CaptureProgress = 0.f;
	CapturingTeamID = FGenericTeamId(IFTeamIds::Neutral);
	SetCaptureState(EIFTerritoryCaptureState::Idle);
	SetTerritoryOwner(KillerTeam, /*bPreserveStructures*/ false);
}

void AIFTerritory::StartCaptureTimer()
{
	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	if (!World->GetTimerManager().IsTimerActive(CaptureTimerHandle))
	{
		World->GetTimerManager().SetTimer(
			CaptureTimerHandle,
			this,
			&AIFTerritory::TickFlagCapture,
			FMath::Max(0.02f, CaptureTickIntervalSeconds),
			true);
	}
}

void AIFTerritory::StopCaptureTimer()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CaptureTimerHandle);
	}
}

void AIFTerritory::TickFlagCapture()
{
	// Safeguard 1: explicitly prune invalid or dead characters so corpses never capture or contest.
	int32 PlayerCount = 0;
	int32 EnemyCount = 0;
	for (auto It = OverlappingCharacters.CreateIterator(); It; ++It)
	{
		const AIFBaseCharacter* const Char = It->Get();
		if (!IsValid(Char) || Char->IsDead())
		{
			It.RemoveCurrent();
			continue;
		}

		const uint8 CharTeam = Char->GetGenericTeamId().GetId();
		if (CharTeam == IFTeamIds::Dawn)
		{
			++PlayerCount;
		}
		else if (CharTeam == IFTeamIds::Dusk)
		{
			++EnemyCount;
		}
	}

	const float Dt = FMath::Max(0.02f, CaptureTickIntervalSeconds);
	const float PrevProgress = CaptureProgress;
	const EIFTerritoryCaptureState PrevState = CaptureState;
	const FGenericTeamId PrevCapturingTeam = CapturingTeamID;

	// Dominant-Side Majority Rule:
	// - Equal non-zero counts -> Contested (progress frozen, dominant color kept visible).
	// - Strictly greater count -> Dominant team drains rival progress to 0 first, then fills.
	// - Empty -> Decays back to 0 and shuts down timer (Zero Idle Tick).
	if (PlayerCount > 0 && PlayerCount == EnemyCount)
	{
		SetCaptureState(EIFTerritoryCaptureState::Contested);
		if (CaptureState != PrevState)
		{
			UpdateFlagRingMaterial();
			OnCaptureProgressChanged.Broadcast(this, CaptureState, CapturingTeamID.GetId(), CaptureProgress);
		}
		return;
	}

	FGenericTeamId DominantTeam = FGenericTeamId(IFTeamIds::Neutral);
	if (PlayerCount > EnemyCount)
	{
		DominantTeam = FGenericTeamId(IFTeamIds::Dawn);
	}
	else if (EnemyCount > PlayerCount)
	{
		DominantTeam = FGenericTeamId(IFTeamIds::Dusk);
	}

	const bool bIsHostileDominant = (DominantTeam != FGenericTeamId::NoTeam) && (DominantTeam != TeamID);
	if (bIsHostileDominant)
	{
		const float FillRate = 1.f / FMath::Max(1.f, FlagCaptureDurationSeconds);

		// Drain rival team's partial progress to 0 before filling for the new dominant team.
		if (CaptureProgress > 0.f && CapturingTeamID != DominantTeam)
		{
			CaptureProgress = FMath::Max(0.f, CaptureProgress - (FillRate * Dt));
			SetCaptureState(EIFTerritoryCaptureState::Decaying);
			if (CaptureProgress <= KINDA_SMALL_NUMBER)
			{
				CaptureProgress = 0.f;
				CapturingTeamID = DominantTeam;
			}
		}
		else
		{
			CapturingTeamID = DominantTeam;
			CaptureProgress = FMath::Clamp(CaptureProgress + (FillRate * Dt), 0.f, 1.f);
			SetCaptureState(EIFTerritoryCaptureState::Capturing);

			if (CaptureProgress >= 1.f - KINDA_SMALL_NUMBER)
			{
				CaptureProgress = 0.f;
				CapturingTeamID = FGenericTeamId(IFTeamIds::Neutral);
				SetCaptureState(EIFTerritoryCaptureState::Idle);
				// Method A — Peaceful Flag Capture: preserves all surviving structures intact.
				SetTerritoryOwner(DominantTeam, /*bPreserveStructures*/ true);
				UpdateFlagRingMaterial();
				OnCaptureProgressChanged.Broadcast(this, CaptureState, CapturingTeamID.GetId(), CaptureProgress);
				if (OverlappingCharacters.IsEmpty())
				{
					StopCaptureTimer();
				}
				return;
			}
		}
	}
	else if (CaptureProgress > 0.f)
	{
		CaptureProgress = FMath::Max(0.f, CaptureProgress - (FlagDecayRatePerSecond * Dt));
		if (CaptureProgress <= KINDA_SMALL_NUMBER)
		{
			CaptureProgress = 0.f;
			CapturingTeamID = FGenericTeamId(IFTeamIds::Neutral);
			SetCaptureState(EIFTerritoryCaptureState::Idle);
		}
		else
		{
			SetCaptureState(EIFTerritoryCaptureState::Decaying);
		}
	}
	else
	{
		CapturingTeamID = FGenericTeamId(IFTeamIds::Neutral);
		SetCaptureState(EIFTerritoryCaptureState::Idle);
	}

	if (!FMath::IsNearlyEqual(PrevProgress, CaptureProgress)
		|| PrevState != CaptureState
		|| PrevCapturingTeam != CapturingTeamID)
	{
		UpdateFlagRingMaterial();
		OnCaptureProgressChanged.Broadcast(this, CaptureState, CapturingTeamID.GetId(), CaptureProgress);
	}

	// Zero Idle Tick: shut down timer when zone is empty and no progress remains.
	if (OverlappingCharacters.IsEmpty() && CaptureProgress <= 0.f)
	{
		StopCaptureTimer();
	}
}

void AIFTerritory::SetCaptureState(EIFTerritoryCaptureState NewState)
{
	CaptureState = NewState;
}

void AIFTerritory::UpdatePerimeterGeometry()
{
	const float HX = FMath::Max(100.f, TerritoryHalfExtent.X);
	const float HY = FMath::Max(100.f, TerritoryHalfExtent.Y);
	const float ThicknessScale = FMath::Max(0.05f, BorderThickness / 100.f);
	const float BorderHeightScale = 0.06f;
	const float BorderZ = 3.f;

	if (IsValid(BorderNorth))
	{
		BorderNorth->SetRelativeLocation(FVector(HX, 0.f, BorderZ));
		BorderNorth->SetRelativeScale3D(FVector(ThicknessScale, (HY * 2.f) / 100.f, BorderHeightScale));
	}
	if (IsValid(BorderSouth))
	{
		BorderSouth->SetRelativeLocation(FVector(-HX, 0.f, BorderZ));
		BorderSouth->SetRelativeScale3D(FVector(ThicknessScale, (HY * 2.f) / 100.f, BorderHeightScale));
	}
	if (IsValid(BorderEast))
	{
		BorderEast->SetRelativeLocation(FVector(0.f, HY, BorderZ));
		BorderEast->SetRelativeScale3D(FVector((HX * 2.f) / 100.f, ThicknessScale, BorderHeightScale));
	}
	if (IsValid(BorderWest))
	{
		BorderWest->SetRelativeLocation(FVector(0.f, -HY, BorderZ));
		BorderWest->SetRelativeScale3D(FVector((HX * 2.f) / 100.f, ThicknessScale, BorderHeightScale));
	}

	if (IsValid(FlagCaptureZone))
	{
		FlagCaptureZone->SetRelativeLocation(FlagLocalOffset);
		FlagCaptureZone->SetSphereRadius(FlagCaptureRadius);
	}

	if (IsValid(FlagZoneRingMesh))
	{
		const float RingScale = FMath::Max(0.5f, (FlagCaptureRadius * 2.f) / 100.f);
		FlagZoneRingMesh->SetRelativeLocation(FVector(0.f, 0.f, 4.f));
		FlagZoneRingMesh->SetRelativeScale3D(FVector(RingScale, RingScale, 0.04f));
	}
}

void AIFTerritory::ApplyFactionVisuals()
{
	auto ResolveMaterial = [](const TSoftObjectPtr<UMaterialInterface>& SoftMat) -> UMaterialInterface*
	{
		if (SoftMat.IsNull())
		{
			return nullptr;
		}
		UMaterialInterface* Mat = SoftMat.Get();
		return IsValid(Mat) ? Mat : SoftMat.LoadSynchronous();
	};

	UMaterialInterface* BorderMat = ResolveMaterial(NeutralBorderMaterial);
	UMaterialInterface* FlagMat = ResolveMaterial(NeutralFlagMaterial);
	const uint8 RawTeamId = TeamID.GetId();
	if (RawTeamId == IFTeamIds::Dawn)
	{
		BorderMat = ResolveMaterial(DawnBorderMaterial);
		FlagMat = ResolveMaterial(DawnFlagMaterial);
	}
	else if (RawTeamId == IFTeamIds::Dusk)
	{
		BorderMat = ResolveMaterial(DuskBorderMaterial);
		FlagMat = ResolveMaterial(DuskFlagMaterial);
	}

	if (IsValid(BorderMat))
	{
		UStaticMeshComponent* const BorderMeshes[] = { BorderNorth, BorderSouth, BorderEast, BorderWest };
		for (UStaticMeshComponent* const MeshComp : BorderMeshes)
		{
			if (IsValid(MeshComp))
			{
				MeshComp->SetMaterial(0, BorderMat);
			}
		}
	}

	UMaterialInterface* const ResolvedFlagMat = IsValid(FlagMat) ? FlagMat : BorderMat;
	if (IsValid(FlagBannerMesh) && IsValid(ResolvedFlagMat))
	{
		FlagBannerMesh->SetMaterial(0, ResolvedFlagMat);
	}

	EnsureFlagRingMID();
	if (IsValid(FlagRingMID))
	{
		UpdateFlagRingMaterial();
	}
	else if (IsValid(FlagZoneRingMesh) && IsValid(BorderMat))
	{
		FlagZoneRingMesh->SetMaterial(0, BorderMat);
	}
}

void AIFTerritory::EnsureFlagRingMID()
{
	if (IsValid(FlagRingMID) || !IsValid(FlagZoneRingMesh))
	{
		return;
	}

	UMaterialInterface* RingMat = FlagCaptureRingMaterial.Get();
	if (!IsValid(RingMat))
	{
		RingMat = FlagZoneRingMesh->GetMaterial(0);
	}

	if (UMaterialInstanceDynamic* const ExistingMID = Cast<UMaterialInstanceDynamic>(RingMat))
	{
		FlagRingMID = ExistingMID;
		return;
	}

	if (IsValid(RingMat))
	{
		FlagRingMID = UMaterialInstanceDynamic::Create(RingMat, this);
		if (IsValid(FlagRingMID))
		{
			FlagZoneRingMesh->SetMaterial(0, FlagRingMID);
		}
	}
}

void AIFTerritory::UpdateFlagRingMaterial()
{
	EnsureFlagRingMID();
	if (!IsValid(FlagRingMID))
	{
		return;
	}

	const FLinearColor OwnerColor = GetTeamRingColor(TeamID);
	const FLinearColor FillColor = (CapturingTeamID != FGenericTeamId::NoTeam)
		? GetTeamRingColor(CapturingTeamID)
		: OwnerColor;

	FlagRingMID->SetScalarParameterValue(TEXT("CaptureProgress"), FMath::Clamp(CaptureProgress, 0.f, 1.f));
	FlagRingMID->SetVectorParameterValue(TEXT("BaseRingColor"), OwnerColor);
	FlagRingMID->SetVectorParameterValue(TEXT("FillRingColor"), FillColor);
	FlagRingMID->SetScalarParameterValue(TEXT("IsContested"), (CaptureState == EIFTerritoryCaptureState::Contested) ? 1.f : 0.f);
}

FLinearColor AIFTerritory::GetTeamRingColor(FGenericTeamId InTeamID) const
{
	const uint8 RawId = InTeamID.GetId();
	if (RawId == IFTeamIds::Dawn)
	{
		return DawnRingColor;
	}
	if (RawId == IFTeamIds::Dusk)
	{
		return DuskRingColor;
	}
	return NeutralRingColor;
}

void AIFTerritory::RequestTerritoryAssets()
{
	TArray<FSoftObjectPath> AssetPaths;
	if (!DawnBorderMaterial.IsNull())
	{
		AssetPaths.AddUnique(DawnBorderMaterial.ToSoftObjectPath());
	}
	if (!DuskBorderMaterial.IsNull())
	{
		AssetPaths.AddUnique(DuskBorderMaterial.ToSoftObjectPath());
	}
	if (!NeutralBorderMaterial.IsNull())
	{
		AssetPaths.AddUnique(NeutralBorderMaterial.ToSoftObjectPath());
	}
	if (!DawnFlagMaterial.IsNull())
	{
		AssetPaths.AddUnique(DawnFlagMaterial.ToSoftObjectPath());
	}
	if (!DuskFlagMaterial.IsNull())
	{
		AssetPaths.AddUnique(DuskFlagMaterial.ToSoftObjectPath());
	}
	if (!NeutralFlagMaterial.IsNull())
	{
		AssetPaths.AddUnique(NeutralFlagMaterial.ToSoftObjectPath());
	}
	if (!FlagCaptureRingMaterial.IsNull())
	{
		AssetPaths.AddUnique(FlagCaptureRingMaterial.ToSoftObjectPath());
	}

	if (AssetPaths.IsEmpty())
	{
		return;
	}

	TerritoryAssetsLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPaths,
		FStreamableDelegate::CreateUObject(this, &AIFTerritory::HandleTerritoryAssetsLoaded));
}

void AIFTerritory::HandleTerritoryAssetsLoaded()
{
	ApplyFactionVisuals();
}

void AIFTerritory::DiscoverPrePlacedStructures()
{
	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	for (TActorIterator<AIFStructure> It(World); It; ++It)
	{
		AIFStructure* const Structure = *It;
		if (IsValid(Structure) && Structure->IsStructureAlive() && ContainsPoint2D(Structure->GetActorLocation()))
		{
			RegisterStructure(Structure);
		}
	}
}
