#include "Character/IFPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFPlayerCombatComponent.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerController.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Stats/IFHealthComponent.h"
#include "Stats/IFStaminaComponent.h"
#include "TimerManager.h"

namespace
{
	constexpr float PlayerDefaultMaxHealth = 150.f;
	constexpr float PlayerDefaultReviveHealth = 60.f;
}

AIFPlayerCharacter::AIFPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UIFPlayerCombatComponent>(TEXT("Combat")))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	StaminaComponent = CreateDefaultSubobject<UIFStaminaComponent>(TEXT("Stamina"));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->bDoCollisionTest = false;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	bUseControllerRotationYaw = true;
	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = false;
	}

	// Tuned HP pool: survives ~9 melee hits (16 dmg). BP can still override.
	if (UIFHealthComponent* const Health = GetHealthComponent())
	{
		Health->SetMaxHealth(PlayerDefaultMaxHealth);
		Health->SetReviveHealth(PlayerDefaultReviveHealth);
	}
}

float AIFPlayerCharacter::GetHealthPercent() const
{
	const UIFHealthComponent* const Health = GetHealthComponent();
	return Health ? Health->GetHealthPercent() : 0.f;
}

float AIFPlayerCharacter::GetStaminaPercent() const
{
	const UIFStaminaComponent* const Stamina = GetStaminaComponent();
	return Stamina ? Stamina->GetStaminaPercent() : 0.f;
}

void AIFPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* const World = GetWorld())
	{
		if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			Registry->RegisterActor(IFRegistryRoles::Player, this);
		}
	}

	if (UIFCombatComponent* const Combat = GetCombatComponent())
	{
		Combat->OnCombatStateChanged.AddDynamic(this, &AIFPlayerCharacter::HandleCombatStateChanged);
	}

	if (StaminaComponent)
	{
		StaminaComponent->OnStaminaDepleted.AddDynamic(this, &AIFPlayerCharacter::HandleStaminaDepleted);
	}

	UpdateMovementSpeed();
}

void AIFPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateSprintState();
}

void AIFPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearReviveTimers();
	StopSprint();

	if (DefaultInputMappingContext)
	{
		if (APlayerController* const PlayerController = Cast<APlayerController>(GetController()))
		{
			if (UEnhancedInputLocalPlayerSubsystem* const InputSubsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
			{
				InputSubsystem->RemoveMappingContext(DefaultInputMappingContext);
			}
		}
	}

	if (UIFCombatComponent* const Combat = GetCombatComponent())
	{
		Combat->OnCombatStateChanged.RemoveDynamic(this, &AIFPlayerCharacter::HandleCombatStateChanged);
	}

	if (StaminaComponent)
	{
		StaminaComponent->OnStaminaDepleted.RemoveDynamic(this, &AIFPlayerCharacter::HandleStaminaDepleted);
	}

	if (UWorld* const World = GetWorld())
	{
		if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			Registry->UnregisterActor(IFRegistryRoles::Player, this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AIFPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (DefaultInputMappingContext)
	{
		if (APlayerController* const PlayerController = Cast<APlayerController>(GetController()))
		{
			if (UEnhancedInputLocalPlayerSubsystem* const InputSubsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
			{
				InputSubsystem->AddMappingContext(DefaultInputMappingContext, DefaultInputMappingPriority);
			}
		}
	}

	UEnhancedInputComponent* const EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	if (MoveInputAction)
	{
		EnhancedInput->BindAction(MoveInputAction, ETriggerEvent::Triggered, this, &AIFPlayerCharacter::Move);
		EnhancedInput->BindAction(MoveInputAction, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopMoving);
		EnhancedInput->BindAction(MoveInputAction, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopMoving);
	}

	if (LookInputAction)
	{
		EnhancedInput->BindAction(LookInputAction, ETriggerEvent::Triggered, this, &AIFPlayerCharacter::Look);
	}

	if (JumpInputAction)
	{
		EnhancedInput->BindAction(JumpInputAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::Jump);
		EnhancedInput->BindAction(JumpInputAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}

	if (SprintInputAction)
	{
		EnhancedInput->BindAction(SprintInputAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::StartSprint);
		EnhancedInput->BindAction(SprintInputAction, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopSprint);
		EnhancedInput->BindAction(SprintInputAction, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopSprint);
	}

	if (BlockInputAction)
	{
		EnhancedInput->BindAction(BlockInputAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::StartBlock);
		EnhancedInput->BindAction(BlockInputAction, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopBlock);
		EnhancedInput->BindAction(BlockInputAction, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopBlock);
	}

	if (AttackInputAction)
	{
		EnhancedInput->BindAction(AttackInputAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::Attack);
	}

	if (SpinAttackInputAction)
	{
		EnhancedInput->BindAction(SpinAttackInputAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::StartSpinAttack);
		EnhancedInput->BindAction(SpinAttackInputAction, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopSpinAttack);
		EnhancedInput->BindAction(SpinAttackInputAction, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopSpinAttack);
	}

	if (PauseInputAction)
	{
		EnhancedInput->BindAction(PauseInputAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::RequestPauseToggle);
		// Pause must also fire while paused so the same key unpauses.
		PlayerInputComponent->SetTickableWhenPaused(true);
	}
}

void AIFPlayerCharacter::Jump()
{
	if (IsDead() || IsGettingUp())
	{
		return;
	}

	Super::Jump();
}

void AIFPlayerCharacter::OnDeathStarted()
{
	bSprintRequested = false;
	UpdateSprintState();
	bUseControllerRotationYaw = false;
	UpdateTickEnabled();
	StartReviveTimer();
}

void AIFPlayerCharacter::HandleStaminaDepleted()
{
	UpdateSprintState();
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
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().SetTimer(ReviveTimerHandle, this, &AIFPlayerCharacter::AttemptRevive, ReviveDelaySeconds, false);
}

void AIFPlayerCharacter::AttemptRevive()
{
	UIFHealthComponent* const Health = GetHealthComponent();
	UIFCombatComponent* const Combat = GetCombatComponent();
	if (!Health || !Combat)
	{
		return;
	}

	Health->Revive();
	Health->SetInvincible(true);
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

	bUseControllerRotationYaw = true;
	UpdateTickEnabled();
	UpdateMovementSpeed();
}

void AIFPlayerCharacter::UpdateTickEnabled()
{
	SetActorTickEnabled(bIsSprinting);
}

void AIFPlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!Controller || IsDead() || IsGettingUp())
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
	CachedMovementInput = FVector2D::ZeroVector;
	UpdateMovementSpeed();
	UpdateSprintState();
}

void AIFPlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	AddControllerYawInput(LookInput.X);

	// The boom ignores controller pitch by design, so tilt it directly within the authored range.
	if (CameraBoom)
	{
		const float PitchInput = bInvertLookPitch ? -LookInput.Y : LookInput.Y;
		FRotator BoomRotation = CameraBoom->GetRelativeRotation();
		BoomRotation.Pitch = FMath::Clamp(BoomRotation.Pitch + PitchInput, CameraMinPitch, CameraMaxPitch);
		BoomRotation.Yaw = 0.f;
		BoomRotation.Roll = 0.f;
		CameraBoom->SetRelativeRotation(BoomRotation);
	}
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

void AIFPlayerCharacter::UpdateSprintState()
{
	bool bShouldSprint = false;

	if (bSprintRequested && !IsDead() && !IsGettingUp() && HasSprintInput())
	{
		const UIFCombatComponent* const Combat = GetCombatComponent();
		if (Combat && Combat->IsIdle())
		{
			const UIFStaminaComponent* const Stamina = GetStaminaComponent();
			if (Stamina)
			{
				if (bIsSprinting)
				{
					bShouldSprint = Stamina->GetStaminaPercent() > 0.f;
				}
				else
				{
					bShouldSprint = Stamina->HasStamina(MinimumStaminaToStartSprint);
				}
			}
		}
	}

	if (bShouldSprint != bIsSprinting)
	{
		bIsSprinting = bShouldSprint;

		if (UIFStaminaComponent* const Stamina = GetStaminaComponent())
		{
			if (bIsSprinting)
			{
				Stamina->StartContinuousDrain(SprintStaminaDrainRate);
			}
			else
			{
				Stamina->StopContinuousDrain();
			}
		}

		UpdateTickEnabled();
		UpdateMovementSpeed();
	}
}

bool AIFPlayerCharacter::TryPrepareCombatAction()
{
	if (IsDead() || IsGettingUp())
	{
		return false;
	}

	bSprintRequested = false;
	UpdateSprintState();
	return true;
}

void AIFPlayerCharacter::Attack()
{
	if (!TryPrepareCombatAction())
	{
		return;
	}

	if (UIFCombatComponent* const Combat = GetCombatComponent())
	{
		Combat->StartAttack();
	}
}

void AIFPlayerCharacter::StartBlock()
{
	if (!TryPrepareCombatAction())
	{
		return;
	}

	if (UIFCombatComponent* const Combat = GetCombatComponent())
	{
		Combat->StartBlock();
	}
}

void AIFPlayerCharacter::StopBlock()
{
	if (UIFCombatComponent* const Combat = GetCombatComponent())
	{
		Combat->StopBlock();
	}
}

void AIFPlayerCharacter::StartSpinAttack()
{
	if (!TryPrepareCombatAction())
	{
		return;
	}

	if (UIFPlayerCombatComponent* const Combat = GetPlayerCombatComponent())
	{
		Combat->StartSpinAttack();
	}
}

void AIFPlayerCharacter::StopSpinAttack()
{
	if (UIFPlayerCombatComponent* const Combat = GetPlayerCombatComponent())
	{
		Combat->StopSpinAttack();
	}
}

void AIFPlayerCharacter::RequestPauseToggle()
{
	// No dead/get-up gate: pause must work in every pawn state.
	AIFPlayerController* const PlayerController = Cast<AIFPlayerController>(GetController());
	if (!PlayerController)
	{
		return;
	}

	PlayerController->TogglePauseGame();
}

void AIFPlayerCharacter::HandleCombatStateChanged(ECombatState, ECombatState)
{
	UpdateMovementSpeed();
}

void AIFPlayerCharacter::UpdateMovementSpeed()
{
	const float DesiredSpeed = CalculateDesiredMovementSpeed();

	UCharacterMovementComponent* const Movement = GetCharacterMovement();
	if (Movement && !FMath::IsNearlyEqual(Movement->MaxWalkSpeed, DesiredSpeed))
	{
		Movement->MaxWalkSpeed = DesiredSpeed;
	}
}

float AIFPlayerCharacter::CalculateDesiredMovementSpeed() const
{
	if (const UIFCombatComponent* const Combat = GetCombatComponent())
	{
		if (Combat->IsAttacking())
		{
			return AttackMoveSpeed;
		}

		if (Combat->IsBlocking())
		{
			return BlockingSpeed;
		}
	}

	if (CachedMovementInput.Y < BackpedalInputThreshold)
	{
		return BackpedalSpeed;
	}

	if (bIsSprinting && HasSprintInput())
	{
		return SprintSpeed;
	}

	return WalkSpeed;
}

bool AIFPlayerCharacter::HasSprintInput() const
{
	return CachedMovementInput.Y >= SprintInputThreshold;
}

UIFPlayerCombatComponent* AIFPlayerCharacter::GetPlayerCombatComponent() const
{
	return Cast<UIFPlayerCombatComponent>(GetCombatComponent());
}
