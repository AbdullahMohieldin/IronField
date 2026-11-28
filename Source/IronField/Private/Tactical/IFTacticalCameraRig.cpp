#include "Tactical/IFTacticalCameraRig.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

AIFTacticalCameraRig::AIFTacticalCameraRig()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(SceneRoot);
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bEnableCameraRotationLag = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = false;
	CameraBoom->TargetArmLength = FMath::Lerp(MinArmLength, MaxArmLength, DefaultZoomAlpha);
	CameraBoom->SetRelativeRotation(FRotator(FMath::Lerp(NearPitch, FarPitch, DefaultZoomAlpha), 0.f, 0.f));

	TacticalCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("TacticalCamera"));
	TacticalCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	TacticalCamera->bUsePawnControlRotation = false;
}

void AIFTacticalCameraRig::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ForceResetTimeDilation();
	Super::EndPlay(EndPlayReason);
}

void AIFTacticalCameraRig::ActivateTacticalCamera(const APawn* CommanderPawn, float InitialYaw, float InTargetTimeDilation, float InRampDurationSeconds)
{
	bIsTacticalActive = true;
	TrackedCommander = CommanderPawn;
	ClearControlInputs();
	CurrentPanVelocity = FVector::ZeroVector;
	CurrentYawVelocity = 0.f;

	CurrentZoomAlpha = FMath::Clamp(DefaultZoomAlpha, 0.f, 1.f);
	TargetZoomAlpha = CurrentZoomAlpha;
	ApplyCameraZoomAndPitch();

	FVector InitialLocation = GetActorLocation();
	if (IsValid(CommanderPawn))
	{
		InitialLocation = CommanderPawn->GetActorLocation();
	}

	ActivationOrigin = InitialLocation;
	CurrentGroundZ = InitialLocation.Z;
	TargetGroundZ = InitialLocation.Z;
	bNeedsInitialGroundTrace = true;

	SetActorLocationAndRotation(
		InitialLocation,
		FRotator(0.f, InitialYaw, 0.f),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	TargetTimeDilation = FMath::Clamp(InTargetTimeDilation, 0.1f, 1.f);
	CurrentTimeDilation = UGameplayStatics::GetGlobalTimeDilation(this);

	if (InRampDurationSeconds > 0.f)
	{
		TimeDilationRampRate = FMath::Abs(TargetTimeDilation - CurrentTimeDilation) / InRampDurationSeconds;
	}
	else
	{
		TimeDilationRampRate = 0.f;
		CurrentTimeDilation = TargetTimeDilation;
		UGameplayStatics::SetGlobalTimeDilation(this, CurrentTimeDilation);
	}

	SetActorTickEnabled(true);
}

void AIFTacticalCameraRig::DeactivateTacticalCamera(float InRampDurationSeconds)
{
	bIsTacticalActive = false;
	ClearControlInputs();
	CurrentPanVelocity = FVector::ZeroVector;
	CurrentYawVelocity = 0.f;

	if (InRampDurationSeconds <= 0.f)
	{
		ForceResetTimeDilation();
		return;
	}

	TargetTimeDilation = 1.f;
	CurrentTimeDilation = UGameplayStatics::GetGlobalTimeDilation(this);
	TimeDilationRampRate = FMath::Abs(TargetTimeDilation - CurrentTimeDilation) / InRampDurationSeconds;

	if (FMath::IsNearlyEqual(CurrentTimeDilation, 1.f, 0.001f))
	{
		ForceResetTimeDilation();
		return;
	}

	SetActorTickEnabled(true);
}

void AIFTacticalCameraRig::ForceResetTimeDilation()
{
	bIsTacticalActive = false;
	ClearControlInputs();
	CurrentPanVelocity = FVector::ZeroVector;
	CurrentYawVelocity = 0.f;
	CurrentTimeDilation = 1.f;
	TargetTimeDilation = 1.f;
	TimeDilationRampRate = 0.f;

	if (IsValid(GetWorld()))
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}

	SetActorTickEnabled(false);
}

void AIFTacticalCameraRig::SetPanInput(const FVector2D& InPanInput)
{
	ActivePanInput = InPanInput.GetClampedToMaxSize(1.f);
}

void AIFTacticalCameraRig::AddZoomInput(float WheelDelta)
{
	if (FMath::IsNearlyZero(WheelDelta))
	{
		return;
	}

	const float SignedDelta = bInvertZoom ? WheelDelta : -WheelDelta;
	TargetZoomAlpha = FMath::Clamp(TargetZoomAlpha + SignedDelta * ZoomStep, 0.f, 1.f);
}

void AIFTacticalCameraRig::SetYawInput(float InYawInput)
{
	ActiveYawInput = FMath::Clamp(InYawInput, -1.f, 1.f);
}

void AIFTacticalCameraRig::ClearControlInputs()
{
	ActivePanInput = FVector2D::ZeroVector;
	ActiveYawInput = 0.f;
}

void AIFTacticalCameraRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Use true engine delta so GlobalTimeDilation (0.35x) and ramp transitions never skew RTS motion.
	const float RealDeltaSeconds = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.f, 0.05f);

	UpdateTimeDilationRamp(RealDeltaSeconds);

	if (!bIsTacticalActive)
	{
		if (FMath::IsNearlyEqual(CurrentTimeDilation, 1.f, 0.001f))
		{
			ForceResetTimeDilation();
		}
		return;
	}

	if (!FMath::IsNearlyEqual(CurrentZoomAlpha, TargetZoomAlpha, 0.0005f))
	{
		CurrentZoomAlpha = FMath::FInterpTo(CurrentZoomAlpha, TargetZoomAlpha, RealDeltaSeconds, ZoomInterpSpeed);
		ApplyCameraZoomAndPitch();
	}

	const float TargetYawVel = ActiveYawInput * YawRotationSpeed;
	const float YawInterpSpeed = FMath::IsNearlyZero(ActiveYawInput) ? YawDecelerationInterpSpeed : YawAccelerationInterpSpeed;
	CurrentYawVelocity = FMath::FInterpTo(CurrentYawVelocity, TargetYawVel, RealDeltaSeconds, YawInterpSpeed);
	if (!FMath::IsNearlyZero(CurrentYawVelocity, 0.05f))
	{
		FRotator NewRot = GetActorRotation();
		NewRot.Yaw = FRotator::NormalizeAxis(NewRot.Yaw + CurrentYawVelocity * RealDeltaSeconds);
		SetActorRotation(NewRot);
	}
	else if (FMath::IsNearlyZero(ActiveYawInput))
	{
		CurrentYawVelocity = 0.f;
	}

	FVector TargetPanVelocity = FVector::ZeroVector;
	if (!ActivePanInput.IsNearlyZero())
	{
		const FRotator YawOnly(0.f, GetActorRotation().Yaw, 0.f);
		const FRotationMatrix YawMatrix(YawOnly);
		const FVector Forward2D = YawMatrix.GetUnitAxis(EAxis::X);
		const FVector Right2D = YawMatrix.GetUnitAxis(EAxis::Y);

		const float PanSpeed = FMath::Lerp(MinPanSpeed, MaxPanSpeed, CurrentZoomAlpha);
		TargetPanVelocity = (Forward2D * ActivePanInput.Y + Right2D * ActivePanInput.X) * PanSpeed;
	}

	const float PanInterpSpeed = ActivePanInput.IsNearlyZero() ? PanDecelerationInterpSpeed : PanAccelerationInterpSpeed;
	CurrentPanVelocity = FMath::VInterpTo(CurrentPanVelocity, TargetPanVelocity, RealDeltaSeconds, PanInterpSpeed);
	if (ActivePanInput.IsNearlyZero() && CurrentPanVelocity.SizeSquared2D() < 1.f)
	{
		CurrentPanVelocity = FVector::ZeroVector;
	}

	FVector NewLocation = GetActorLocation() + CurrentPanVelocity * RealDeltaSeconds;
	NewLocation = ClampLocationToBounds(NewLocation);
	UpdateTerrainFollowZ(NewLocation.X, NewLocation.Y, RealDeltaSeconds);
	NewLocation.Z = CurrentGroundZ;

	SetActorLocation(NewLocation);
}

void AIFTacticalCameraRig::UpdateTimeDilationRamp(float RealDeltaSeconds)
{
	if (FMath::IsNearlyEqual(CurrentTimeDilation, TargetTimeDilation, 0.001f))
	{
		if (CurrentTimeDilation != TargetTimeDilation)
		{
			CurrentTimeDilation = TargetTimeDilation;
			UGameplayStatics::SetGlobalTimeDilation(this, CurrentTimeDilation);
		}
		return;
	}

	if (TimeDilationRampRate > 0.f)
	{
		CurrentTimeDilation = FMath::FInterpConstantTo(CurrentTimeDilation, TargetTimeDilation, RealDeltaSeconds, TimeDilationRampRate);
	}
	else
	{
		CurrentTimeDilation = TargetTimeDilation;
	}

	UGameplayStatics::SetGlobalTimeDilation(this, CurrentTimeDilation);
}

void AIFTacticalCameraRig::ApplyCameraZoomAndPitch()
{
	if (!IsValid(CameraBoom))
	{
		return;
	}

	CameraBoom->TargetArmLength = FMath::Lerp(MinArmLength, MaxArmLength, CurrentZoomAlpha);
	const float DesiredPitch = FMath::Lerp(NearPitch, FarPitch, CurrentZoomAlpha);
	CameraBoom->SetRelativeRotation(FRotator(DesiredPitch, 0.f, 0.f));
}

void AIFTacticalCameraRig::UpdateTerrainFollowZ(float XYLocationX, float XYLocationY, float RealDeltaSeconds)
{
	if (!bNeedsInitialGroundTrace
		&& CurrentPanVelocity.IsNearlyZero()
		&& FMath::IsNearlyEqual(CurrentGroundZ, TargetGroundZ, 0.5f))
	{
		return;
	}
	bNeedsInitialGroundTrace = false;

	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	const FVector TraceStart(XYLocationX, XYLocationY, CurrentGroundZ + GroundTraceUpOffset);
	const FVector TraceEnd(XYLocationX, XYLocationY, CurrentGroundZ - GroundTraceDownOffset);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFTacticalCamGround), false);
	QueryParams.AddIgnoredActor(this);
	if (const APawn* const Commander = TrackedCommander.Get())
	{
		QueryParams.AddIgnoredActor(Commander);
	}

	FCollisionObjectQueryParams GroundObjectParams;
	GroundObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

	FHitResult Hit;
	if (World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, GroundObjectParams, QueryParams))
	{
		if (!Hit.bStartPenetrating
			&& Hit.bBlockingHit
			&& Hit.ImpactNormal.Z >= MinWalkableNormalZ
			&& FMath::Abs(Hit.ImpactPoint.Z - CurrentGroundZ) <= MaxGroundStepHeightDelta)
		{
			TargetGroundZ = Hit.ImpactPoint.Z;
		}
	}

	CurrentGroundZ = FMath::FInterpTo(CurrentGroundZ, TargetGroundZ, RealDeltaSeconds, GroundZInterpSpeed);
}

FVector AIFTacticalCameraRig::ClampLocationToBounds(const FVector& DesiredLocation) const
{
	FVector Clamped = DesiredLocation;

	if (MaxPanRadiusFromCommander > 0.f)
	{
		const APawn* const Commander = TrackedCommander.Get();
		const FVector Anchor = IsValid(Commander) ? Commander->GetActorLocation() : ActivationOrigin;

		FVector2D Offset2D(Clamped.X - Anchor.X, Clamped.Y - Anchor.Y);
		const float MaxRadiusSq = FMath::Square(MaxPanRadiusFromCommander);
		if (Offset2D.SizeSquared() > MaxRadiusSq)
		{
			Offset2D = Offset2D.GetSafeNormal() * MaxPanRadiusFromCommander;
			Clamped.X = Anchor.X + Offset2D.X;
			Clamped.Y = Anchor.Y + Offset2D.Y;
		}
	}

	if (bUseWorldBoundsClamp)
	{
		Clamped.X = FMath::Clamp(Clamped.X, FMath::Min(WorldBoundsMin.X, WorldBoundsMax.X), FMath::Max(WorldBoundsMin.X, WorldBoundsMax.X));
		Clamped.Y = FMath::Clamp(Clamped.Y, FMath::Min(WorldBoundsMin.Y, WorldBoundsMax.Y), FMath::Max(WorldBoundsMin.Y, WorldBoundsMax.Y));
	}

	return Clamped;
}
