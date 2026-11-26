#include "Character/IFPlayerCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFPlayerCombatComponent.h"
#include "Components/SphereComponent.h"
#include "Core/IFGameMode.h"
#include "Core/IFInteractable.h"
#include "Core/IFPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Stats/IFHealthComponent.h"
#include "Stats/IFStaminaComponent.h"
#include "Tactical/IFTacticalCommandComponent.h"

AIFPlayerCharacter::AIFPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UIFPlayerCombatComponent>(AIFBaseCharacter::CombatComponentName))
{
	PrimaryActorTick.bCanEverTick = false;

	StaminaComponent = CreateDefaultSubobject<UIFStaminaComponent>(TEXT("Stamina"));
	StaminaComponent->ConfigureStaminaDefaults(100.f, 15.f, 1.45f);

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(RootComponent);
	InteractionSphere->InitSphereRadius(InteractionRadiusCm);
	InteractionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	InteractionSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Overlap);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionSphere->SetGenerateOverlapEvents(true);
	InteractionSphere->SetCanEverAffectNavigation(false);

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

	if (IsValid(InteractionSphere))
	{
		InteractionSphere->SetSphereRadius(InteractionRadiusCm);
		InteractionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &AIFPlayerCharacter::HandleInteractionBeginOverlap);
		InteractionSphere->OnComponentEndOverlap.AddUniqueDynamic(this, &AIFPlayerCharacter::HandleInteractionEndOverlap);

		TArray<AActor*> InitialOverlaps;
		InteractionSphere->GetOverlappingActors(InitialOverlaps);
		for (AActor* const Actor : InitialOverlaps)
		{
			if (IsValid(Actor) && Actor != this && Actor->Implements<UIFInteractable>())
			{
				NearbyInteractables.AddUnique(Actor);
			}
		}
		RefreshInteractionPrompt();
	}

	UpdateMovementSpeed();
}

void AIFPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSprint();
	ClearNearbyInteractablesAndPrompt();

	if (IsValid(InteractionSphere))
	{
		InteractionSphere->OnComponentBeginOverlap.RemoveDynamic(this, &AIFPlayerCharacter::HandleInteractionBeginOverlap);
		InteractionSphere->OnComponentEndOverlap.RemoveDynamic(this, &AIFPlayerCharacter::HandleInteractionEndOverlap);
	}

	if (IsValid(StaminaComponent))
	{
		StaminaComponent->OnStaminaDepleted.RemoveDynamic(this, &AIFPlayerCharacter::HandleStaminaDepleted);
	}
	if (UIFCombatComponent* const Combat = GetCombatComponent())
	{
		Combat->OnCombatStateChanged.RemoveDynamic(this, &AIFPlayerCharacter::HandleCombatStateChanged);
	}

	// UnPossessed does not run on level travel, and the mapping context lives on LocalPlayer.
	if (IsValid(GetController()))
	{
		RemoveDefaultMappingContext();
	}
	bIsInTacticalStandby = false;

	Super::EndPlay(EndPlayReason);
}

void AIFPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	APlayerController* const PlayerController = Cast<APlayerController>(NewController);
	if (IsValid(PlayerController) && IsValid(PlayerController->PlayerCameraManager))
	{
		PlayerController->PlayerCameraManager->ViewPitchMin = CameraMinPitch;
		PlayerController->PlayerCameraManager->ViewPitchMax = CameraMaxPitch;
	}
}

void AIFPlayerCharacter::UnPossessed()
{
	bIsInTacticalStandby = false;
	ClearNearbyInteractablesAndPrompt();
	RemoveDefaultMappingContext();

	Super::UnPossessed();
}

void AIFPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (!bIsInTacticalStandby)
	{
		AddDefaultMappingContext();
	}
	BindInputActions();
}

void AIFPlayerCharacter::AddDefaultMappingContext()
{
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(DefaultMappingContext) || !IsValid(PlayerController))
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* const InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		InputSubsystem->AddMappingContext(DefaultMappingContext, DefaultMappingPriority);
	}
}

void AIFPlayerCharacter::RemoveDefaultMappingContext()
{
	APlayerController* const PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(DefaultMappingContext) || !IsValid(PlayerController))
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* const InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		InputSubsystem->RemoveMappingContext(DefaultMappingContext);
	}
}

void AIFPlayerCharacter::BindInputActions()
{
	UEnhancedInputComponent* const EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	if (!IsValid(EnhancedInput))
	{
		return;
	}

	if (IsValid(MoveAction))
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AIFPlayerCharacter::Move);
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopMoving);
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopMoving);
	}

	if (IsValid(LookAction))
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AIFPlayerCharacter::Look);
	}

	if (IsValid(JumpAction))
	{
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::Jump);
		EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}

	if (IsValid(SprintAction))
	{
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::StartSprint);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopSprint);
		EnhancedInput->BindAction(SprintAction, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopSprint);
	}

	if (IsValid(AttackAction))
	{
		EnhancedInput->BindAction(AttackAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::Attack);
	}

	if (IsValid(BlockAction))
	{
		EnhancedInput->BindAction(BlockAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::StartBlock);
		EnhancedInput->BindAction(BlockAction, ETriggerEvent::Completed, this, &AIFPlayerCharacter::StopBlock);
		EnhancedInput->BindAction(BlockAction, ETriggerEvent::Canceled, this, &AIFPlayerCharacter::StopBlock);
	}

	if (IsValid(LeapSlamAction))
	{
		EnhancedInput->BindAction(LeapSlamAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::RequestLeapSlam);
	}

	if (IsValid(InteractAction))
	{
		EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AIFPlayerCharacter::HandleInteract);
	}
}

void AIFPlayerCharacter::EnterTacticalStandby()
{
	if (bIsInTacticalStandby)
	{
		return;
	}

	bIsInTacticalStandby = true;
	RemoveDefaultMappingContext();

	CachedMovementInput = FVector2D::ZeroVector;
	bSprintRequested = false;
	StopJumping();
	UpdateSprintState();
	UpdateMovementSpeed();

	if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
	{
		if (Movement->IsFalling())
		{
			Movement->Velocity.X = 0.f;
			Movement->Velocity.Y = 0.f;
		}
		else
		{
			Movement->StopMovementImmediately();
		}
	}

	if (UIFPlayerCombatComponent* const PlayerCombat = Cast<UIFPlayerCombatComponent>(GetCombatComponent()))
	{
		PlayerCombat->CancelPendingInputsForTacticalMode();
	}
}

void AIFPlayerCharacter::ExitTacticalStandby()
{
	if (!bIsInTacticalStandby)
	{
		return;
	}

	bIsInTacticalStandby = false;

	if (!IsDead() && IsValid(GetController()))
	{
		AddDefaultMappingContext();
	}

	UpdateRotationMode();
	UpdateSprintState();
	UpdateMovementSpeed();
	RefreshInteractionPrompt();
}

void AIFPlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!IsValid(Controller) || IsDead() || bIsInTacticalStandby)
	{
		return;
	}

	const UIFCombatComponent* const Combat = GetCombatComponent();
	if (IsValid(Combat) && Combat->IsStunned())
	{
		CachedMovementInput = FVector2D::ZeroVector;
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
	// Always clear cached input on release, even while dead.
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
	if (IsDead() || bIsInTacticalStandby)
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
	if (bIsInTacticalStandby)
	{
		return;
	}

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
	if (bIsInTacticalStandby)
	{
		return;
	}

	if (UIFPlayerCombatComponent* const Combat = Cast<UIFPlayerCombatComponent>(GetCombatComponent()))
	{
		Combat->RequestAttack();
	}
}

void AIFPlayerCharacter::StartBlock()
{
	if (bIsInTacticalStandby)
	{
		return;
	}

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

void AIFPlayerCharacter::RequestLeapSlam()
{
	if (bIsInTacticalStandby)
	{
		return;
	}

	if (UIFPlayerCombatComponent* const Combat = Cast<UIFPlayerCombatComponent>(GetCombatComponent()))
	{
		Combat->RequestLeapSlam();
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

	if (!IsDead())
	{
		UpdateRotationMode();
	}
}

void AIFPlayerCharacter::UpdateSprintState()
{
	bool bShouldSprint = false;

	const UIFCombatComponent* const Combat = GetCombatComponent();
	if (bSprintRequested && !IsDead() && HasSprintInput()
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
	bSprintRequested = false;
	CachedMovementInput = FVector2D::ZeroVector;
	UpdateSprintState();

	if (AIFPlayerController* const PC = Cast<AIFPlayerController>(GetController()))
	{
		if (UIFTacticalCommandComponent* const Tactical = PC->GetTacticalCommandComponent())
		{
			Tactical->ExitTacticalMode(true);
		}
	}

	RefreshInteractionPrompt();
	bUseControllerRotationYaw = false;

	if (const UWorld* const World = GetWorld())
	{
		if (AIFGameMode* const GM = World->GetAuthGameMode<AIFGameMode>())
		{
			GM->RequestHeroRespawn(this);
		}
	}
}

void AIFPlayerCharacter::RespawnAtCastle(const FVector& SpawnLocation, const FRotator& SpawnRotation)
{
	SetActorLocationAndRotation(SpawnLocation, SpawnRotation, false, nullptr, ETeleportType::TeleportPhysics);

	if (AController* const PC = GetController())
	{
		PC->SetControlRotation(SpawnRotation);
	}

	RestoreAliveState();

	if (UIFHealthComponent* const Health = GetHealthComponent())
	{
		Health->Revive();
	}

	if (IsValid(StaminaComponent))
	{
		StaminaComponent->ResetStamina();
	}

	if (UIFCombatComponent* const Combat = GetCombatComponent())
	{
		Combat->HandleOwnerRevived();
	}

	UpdateRotationMode();
	UpdateSprintState();
	UpdateMovementSpeed();
	RefreshInteractionPrompt();
}

void AIFPlayerCharacter::HandleInteractionBeginOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32,
	bool,
	const FHitResult&)
{
	if (!IsValid(OtherActor) || OtherActor == this || !OtherActor->Implements<UIFInteractable>())
	{
		return;
	}

	NearbyInteractables.AddUnique(OtherActor);
	RefreshInteractionPrompt();
}

void AIFPlayerCharacter::HandleInteractionEndOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32)
{
	NearbyInteractables.RemoveAll([OtherActor](const TWeakObjectPtr<AActor>& Ptr)
	{
		return !Ptr.IsValid() || Ptr.Get() == OtherActor;
	});
	RefreshInteractionPrompt();
}

AActor* AIFPlayerCharacter::GetNearestInteractable() const
{
	if (IsDead())
	{
		return nullptr;
	}

	const FVector SelfLocation = GetActorLocation();
	float BestDistSq = MAX_flt;
	AActor* BestInteractable = nullptr;

	for (const TWeakObjectPtr<AActor>& Ptr : NearbyInteractables)
	{
		AActor* const Actor = Ptr.Get();
		if (!IsValid(Actor))
		{
			continue;
		}
		const IIFInteractable* const Interactable = Cast<IIFInteractable>(Actor);
		if (Interactable == nullptr || !Interactable->CanInteract(const_cast<AIFPlayerCharacter*>(this)))
		{
			continue;
		}

		const float DistSq = FVector::DistSquared(SelfLocation, Actor->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestInteractable = Actor;
		}
	}

	return BestInteractable;
}

void AIFPlayerCharacter::RefreshInteractionPrompt()
{
	NearbyInteractables.RemoveAll([](const TWeakObjectPtr<AActor>& Ptr)
	{
		return !Ptr.IsValid();
	});

	AActor* const Nearest = GetNearestInteractable();
	const FText PromptText = IsValid(Nearest)
		? Cast<IIFInteractable>(Nearest)->GetInteractPromptText(this)
		: FText::GetEmpty();

	if (AIFPlayerController* const PC = Cast<AIFPlayerController>(GetController()))
	{
		PC->SetInteractionPromptText(PromptText);
	}
}

void AIFPlayerCharacter::ClearNearbyInteractablesAndPrompt()
{
	NearbyInteractables.Reset();
	if (AIFPlayerController* const PC = Cast<AIFPlayerController>(GetController()))
	{
		PC->SetInteractionPromptText(FText::GetEmpty());
	}
}

void AIFPlayerCharacter::HandleInteract()
{
	if (IsDead() || bIsInTacticalStandby)
	{
		return;
	}

	if (AActor* const Nearest = GetNearestInteractable())
	{
		Cast<IIFInteractable>(Nearest)->Interact(this);
		RefreshInteractionPrompt();
	}
}

void AIFPlayerCharacter::DebugDamage(float Amount)
{
#if !UE_BUILD_SHIPPING
	UGameplayStatics::ApplyDamage(this, Amount, nullptr, nullptr, UDamageType::StaticClass());
#endif
}
