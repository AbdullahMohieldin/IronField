#include "Character/IFPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFPlayerCombatComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Stats/IFHealthComponent.h"
#include "Stats/IFStaminaComponent.h"
#include "TimerManager.h"

AIFPlayerCharacter::AIFPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UIFPlayerCombatComponent>(AIFBaseCharacter::CombatComponentName))
{
	PrimaryActorTick.bCanEverTick = false;

	StaminaComponent = CreateDefaultSubobject<UIFStaminaComponent>(TEXT("Stamina"));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 350.f;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 65.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraLagSpeed = 12.f;
	CameraBoom->CameraRotationLagSpeed = 15.f;
	CameraBoom->bDoCollisionTest = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = false;
		Movement->bUseControllerDesiredRotation = true;
		Movement->RotationRate = FRotator(0.f, 720.f, 0.f);
	}

	SetGenericTeamId(FGenericTeamId(IFTeamIds::Player));
}

void AIFPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (IsValid(StaminaComponent))
	{
		StaminaComponent->OnStaminaDepleted.AddUniqueDynamic(this, &AIFPlayerCharacter::HandleStaminaDepleted);
	}
	if (UIFCombatComponent* const Combat = GetCombatComponent())
	{
		Combat->OnCombatStateChanged.AddUniqueDynamic(this, &AIFPlayerCharacter::HandleCombatStateChanged);
	}

	UpdateMovementSpeed();
}

void AIFPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearReviveTimers();
	StopSprint();

	if (IsValid(StaminaComponent))
	{
		StaminaComponent->OnStaminaDepleted.RemoveDynamic(this, &AIFPlayerCharacter::HandleStaminaDepleted);
	}
	if (UIFCombatComponent* const Combat = GetCombatComponent())
	{
		Combat->OnCombatStateChanged.RemoveDynamic(this, &AIFPlayerCharacter::HandleCombatStateChanged);
	}

	if (InputAssetsLoadHandle.IsValid())
	{
		InputAssetsLoadHandle->CancelHandle();
		InputAssetsLoadHandle.Reset();
	}

	// Destroyed while still possessed (level travel): UnPossessed never ran, and the
	// mapping context lives on the LocalPlayer, which survives the travel.
	if (IsValid(GetController()))
	{
		RemoveDefaultMappingContext();
	}

	Super::EndPlay(EndPlayReason);
}

void AIFPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Pitch limits live on the camera manager (spawned in PC PostInitializeComponents)
	// so control rotation is clamped before the boom inherits it.
	APlayerController* const PlayerController = Cast<APlayerController>(NewController);
	if (IsValid(PlayerController) && IsValid(PlayerController->PlayerCameraManager))
	{
		PlayerController->PlayerCameraManager->ViewPitchMin = CameraMinPitch;
		PlayerController->PlayerCameraManager->ViewPitchMax = CameraMaxPitch;
	}
}

void AIFPlayerCharacter::UnPossessed()
{
	// The controller is still valid here; the base clears it and destroys the input component.
	RemoveDefaultMappingContext();

	if (InputAssetsLoadHandle.IsValid())
	{
		InputAssetsLoadHandle->CancelHandle();
		InputAssetsLoadHandle.Reset();
	}

	Super::UnPossessed();
}

void AIFPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	RequestInputAssets();
}

void AIFPlayerCharacter::RequestInputAssets()
{
	TArray<FSoftObjectPath> AssetPaths;
	if (!DefaultMappingContext.IsNull())
	{
		AssetPaths.Add(DefaultMappingContext.ToSoftObjectPath());
	}
	if (!MoveAction.IsNull())
	{
		AssetPaths.Add(MoveAction.ToSoftObjectPath());
	}
	if (!LookAction.IsNull())
	{
		AssetPaths.Add(LookAction.ToSoftObjectPath());
	}
	if (!JumpAction.IsNull())
	{
		AssetPaths.Add(JumpAction.ToSoftObjectPath());
	}
	if (!SprintAction.IsNull())
	{
		AssetPaths.Add(SprintAction.ToSoftObjectPath());
	}
	if (!AttackAction.IsNull())
	{
		AssetPaths.Add(AttackAction.ToSoftObjectPath());
	}
	if (AssetPaths.IsEmpty())
	{
		return;
	}

	InputAssetsLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPaths, FStreamableDelegate::CreateUObject(this, &AIFPlayerCharacter::HandleInputAssetsLoaded));
}

void AIFPlayerCharacter::HandleInputAssetsLoaded()
{
	InputAssetsLoadHandle.Reset();

	// The input component only exists while possessed; a load finishing after
	// unpossess must not re-add the context.
	if (!IsValid(GetController()) || !IsValid(InputComponent))
	{
		return;
	}

	AddDefaultMappingContext();
	BindInputActions();
}

void AIFPlayerCharacter::AddDefaultMappingContext()
{
	UInputMappingContext* const MappingContext = DefaultMappingContext.Get();
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(MappingContext) || !IsValid(PlayerController))
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* const InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		InputSubsystem->AddMappingContext(MappingContext, DefaultMappingPriority);
	}
}

void AIFPlayerCharacter::RemoveDefaultMappingContext()
{
	UInputMappingContext* const MappingContext = DefaultMappingContext.Get();
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(MappingContext) || !IsValid(PlayerController))
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* const InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		InputSubsystem->RemoveMappingContext(MappingContext);
	}
}

void AIFPlayerCharacter::BindInputActions()
{
	UEnhancedInputComponent* const EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!IsValid(EnhancedInput))
	{
		return;
	}

	UInputAction* const Move = MoveAction.Get();
	if (IsValid(Move))
	{
		EnhancedInput->BindAction(Move, ETriggerEvent::Triggered, this, &AIFPlayerCharacter::Move);
		EnhancedInput->BindAction(Move, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopMoving);
		EnhancedInput->BindAction(Move, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopMoving);
	}

	UInputAction* const Look = LookAction.Get();
	if (IsValid(Look))
	{
		EnhancedInput->BindAction(Look, ETriggerEvent::Triggered, this, &AIFPlayerCharacter::Look);
	}

	UInputAction* const Jump = JumpAction.Get();
	if (IsValid(Jump))
	{
		EnhancedInput->BindAction(Jump, ETriggerEvent::Started, this, &AIFPlayerCharacter::Jump);
		EnhancedInput->BindAction(Jump, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}

	UInputAction* const Sprint = SprintAction.Get();
	if (IsValid(Sprint))
	{
		EnhancedInput->BindAction(Sprint, ETriggerEvent::Started, this, &AIFPlayerCharacter::StartSprint);
		EnhancedInput->BindAction(Sprint, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopSprint);
		EnhancedInput->BindAction(Sprint, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopSprint);
	}

	UInputAction* const Attack = AttackAction.Get();
	if (IsValid(Attack))
	{
		EnhancedInput->BindAction(Attack, ETriggerEvent::Started, this, &AIFPlayerCharacter::Attack);
	}

	if (IsValid(BlockAction))
	{
		EnhancedInput->BindAction(BlockAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::StartBlock);
		EnhancedInput->BindAction(BlockAction, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopBlock);
		EnhancedInput->BindAction(BlockAction, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopBlock);
	}

	if (IsValid(SpinAction))
	{
		EnhancedInput->BindAction(SpinAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::RequestSpinAttack);
	}
}

void AIFPlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!IsValid(Controller) || IsDead() || bIsGettingUp)
	{
		return;
	}

	CachedMovementInput = Value.Get<FVector2D>();

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FRotationMatrix YawMatrix(YawRotation);

	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), CachedMovementInput.Y);
	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), CachedMovementInput.X);

	UpdateMovementSpeed();
	UpdateSprintState();
}

void AIFPlayerCharacter::StopMoving()
{
	// Deliberately ungated: release events must zero the cache even while dead.
	CachedMovementInput = FVector2D::ZeroVector;
	UpdateMovementSpeed();
	UpdateSprintState();
}

void AIFPlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(bInvertLookPitch ? -LookInput.Y : LookInput.Y);
}

void AIFPlayerCharacter::Jump()
{
	if (IsDead() || bIsGettingUp)
	{
		return;
	}

	const UIFCombatComponent* const Combat = GetCombatComponent();
	if (IsValid(Combat) && (Combat->IsStunned() || Combat->IsBlocking()))
	{
		return;
	}

	Super::Jump();
}

void AIFPlayerCharacter::StartSprint()
{
	bSprintRequested = true;
	UpdateSprintState();
}

void AIFPlayerCharacter::StopSprint()
{
	bSprintRequested = false;
	UpdateSprintState();
}

void AIFPlayerCharacter::Attack()
{
	if (UIFPlayerCombatComponent* const Combat = Cast<UIFPlayerCombatComponent>(GetCombatComponent()))
	{
		Combat->RequestAttack();
	}
}

void AIFPlayerCharacter::StartBlock()
{
	if (UIFPlayerCombatComponent* const Combat = Cast<UIFPlayerCombatComponent>(GetCombatComponent()))
	{
		Combat->StartBlock();
	}
}

void AIFPlayerCharacter::StopBlock()
{
	if (UIFPlayerCombatComponent* const Combat = Cast<UIFPlayerCombatComponent>(GetCombatComponent()))
	{
		Combat->StopBlock();
	}
}

void AIFPlayerCharacter::RequestSpinAttack()
{
	if (UIFPlayerCombatComponent* const Combat = Cast<UIFPlayerCombatComponent>(GetCombatComponent()))
	{
		Combat->RequestSpinAttack();
	}
}

void AIFPlayerCharacter::HandleStaminaDepleted()
{
	UpdateSprintState();
}

void AIFPlayerCharacter::HandleCombatStateChanged(EIFCombatState, EIFCombatState)
{
	UpdateMovementSpeed();
	UpdateSprintState();

	if (!IsDead() && !bIsGettingUp)
	{
		UpdateRotationMode();
	}
}

void AIFPlayerCharacter::UpdateSprintState()
{
	bool bShouldSprint = false;

	const UIFCombatComponent* const Combat = GetCombatComponent();
	if (bSprintRequested && !IsDead() && !bIsGettingUp && HasSprintInput()
		&& IsValid(Combat) && Combat->IsIdle() && IsValid(StaminaComponent))
	{
		bShouldSprint = bIsSprinting
			? StaminaComponent->GetCurrentStamina() > 0.f
			: StaminaComponent->HasStamina(MinimumStaminaToStartSprint);
	}

	if (bShouldSprint == bIsSprinting)
	{
		return;
	}

	if (bShouldSprint)
	{
		bIsSprinting = StaminaComponent->StartContinuousDrain(SprintStaminaDrainRate);
	}
	else
	{
		bIsSprinting = false;
		StaminaComponent->StopContinuousDrain();
	}

	UpdateMovementSpeed();
}

bool AIFPlayerCharacter::HasSprintInput() const
{
	return CachedMovementInput.SizeSquared() >= FMath::Square(SprintInputThreshold);
}

void AIFPlayerCharacter::UpdateMovementSpeed()
{
	UCharacterMovementComponent* const Movement = GetCharacterMovement();
	if (!IsValid(Movement))
	{
		return;
	}

	const float DesiredSpeed = CalculateDesiredMovementSpeed();
	if (!FMath::IsNearlyEqual(Movement->MaxWalkSpeed, DesiredSpeed))
	{
		Movement->MaxWalkSpeed = DesiredSpeed;
	}
}

void AIFPlayerCharacter::UpdateRotationMode()
{
	UCharacterMovementComponent* const Movement = GetCharacterMovement();
	if (!IsValid(Movement))
	{
		return;
	}

	// Always face controller/camera direction smoothly via RotationRate (720 deg/s)
	// for directional combat strafing (W=Fwd, A/D=Strafe, S=Bwd).
	bUseControllerRotationYaw = false;
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = true;
}

float AIFPlayerCharacter::CalculateDesiredMovementSpeed() const
{
	const UIFCombatComponent* const Combat = GetCombatComponent();
	if (IsValid(Combat))
	{
		if (Combat->IsStunned())
		{
			return 0.f;
		}
		if (Combat->IsAttacking())
		{
			return AttackMoveSpeed;
		}
		if (Combat->IsBlocking())
		{
			return BlockingSpeed;
		}
	}

	if (bIsSprinting && HasSprintInput())
	{
		return SprintSpeed;
	}

	return WalkSpeed;
}

void AIFPlayerCharacter::OnDeathStarted()
{
	// Zero cached input so revive never inherits pre-death movement or sprint.
	bSprintRequested = false;
	CachedMovementInput = FVector2D::ZeroVector;
	UpdateSprintState();

	bUseControllerRotationYaw = false;
	StartReviveTimer();
}

void AIFPlayerCharacter::ClearReviveTimers()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReviveTimerHandle);
		World->GetTimerManager().ClearTimer(GetUpFallbackTimerHandle);
	}
}

void AIFPlayerCharacter::StartReviveTimer()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ReviveTimerHandle, this, &AIFPlayerCharacter::AttemptRevive, ReviveDelaySeconds, false);
	}
}

void AIFPlayerCharacter::AttemptRevive()
{
	UIFHealthComponent* const Health = GetHealthComponent();
	UIFCombatComponent* const Combat = GetCombatComponent();
	if (!IsValid(Health) || !IsValid(Combat))
	{
		return;
	}

	Health->Revive();
	Health->SetInvincible(true);
	if (IsValid(StaminaComponent))
	{
		StaminaComponent->ResetStamina();
	}
	Combat->HandleOwnerRevived();
	RestoreAliveState();

	bIsGettingUp = true;

	// The AnimBP calls NotifyGetUpFinished; this timer prevents a permanent lockout if that call is missing.
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(GetUpFallbackTimerHandle, this, &AIFPlayerCharacter::NotifyGetUpFinished, GetUpDuration, false);
	}
}

void AIFPlayerCharacter::NotifyGetUpFinished()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GetUpFallbackTimerHandle);
	}

	if (bIsGettingUp)
	{
		CompleteRevive();
	}
}

void AIFPlayerCharacter::CompleteRevive()
{
	bIsGettingUp = false;

	if (UIFHealthComponent* const Health = GetHealthComponent())
	{
		Health->SetInvincible(false);
	}

	UpdateRotationMode();
	UpdateSprintState();
	UpdateMovementSpeed();
}

void AIFPlayerCharacter::DebugDamage(float Amount)
{
	if (UIFHealthComponent* const Health = GetHealthComponent())
	{
		Health->ApplyDamage(Amount);
	}
}