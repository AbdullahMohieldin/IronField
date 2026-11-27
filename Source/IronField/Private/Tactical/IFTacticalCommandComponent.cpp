#include "Tactical/IFTacticalCommandComponent.h"

#include "AI/IFSoldierAIController.h"
#include "Castle/IFBuildingComponent.h"
#include "Castle/IFGateDoor.h"
#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFPlayerCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/IFGameTypes.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerController.h"
#include "Engine/AssetManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NavigationSystem.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Tactical/IFFormationSolver.h"
#include "Tactical/IFTacticalCameraRig.h"
#include "TimerManager.h"
#include "UI/IFTacticalHUD.h"
#include "UI/IFTacticalHUDWidget.h"

UIFTacticalCommandComponent::UIFTacticalCommandComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	TacticalCameraRigClass = AIFTacticalCameraRig::StaticClass();
}

void UIFTacticalCommandComponent::BeginPlay()
{
	Super::BeginPlay();

	if (FSlateApplication::IsInitialized())
	{
		AppActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
			this, &UIFTacticalCommandComponent::HandleApplicationActivationChanged);
	}

	RequestTacticalAssets();
}

void UIFTacticalCommandComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AppActivationHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(AppActivationHandle);
		AppActivationHandle.Reset();
	}

	if (TacticalAssetsLoadHandle.IsValid())
	{
		TacticalAssetsLoadHandle->CancelHandle();
		TacticalAssetsLoadHandle.Reset();
	}

	CleanupOrderMarkerVFX();
	if (UIFBuildingComponent* const Building = GetBuildingComponent())
	{
		Building->CancelBuildPlacement();
		Building->ClearSelectedStructure();
	}
	CancelMarqueeSelection();
	CancelFormationDragPreview();
	ClearHighlightedHostileTarget();
	ClearHighlightedHostileStructure();
	ClearSelection();
	RemoveTacticalMappingContext();

	FormationPreviewISM = nullptr;

	if (IsValid(TacticalCameraRig))
	{
		TacticalCameraRig->ForceResetTimeDilation();
		TacticalCameraRig->Destroy();
		TacticalCameraRig = nullptr;
	}
	else if (IsValid(GetWorld()))
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}

	if (IsValid(TacticalHUDWidget))
	{
		TacticalHUDWidget->UnbindFromTacticalComponent();
		TacticalHUDWidget->RemoveFromParent();
		TacticalHUDWidget = nullptr;
	}

	BoundInputActions.Reset();
	bIsTacticalModeActive = false;

	Super::EndPlay(EndPlayReason);
}

void UIFTacticalCommandComponent::SetupControllerInput(UInputComponent* PlayerInputComponent)
{
	BoundInputActions.Reset();

	if (!IsValid(PlayerInputComponent))
	{
		return;
	}

	// Raw Tab acts only as fallback before IA_ToggleTacticalMode is assigned; never double-bind.
	if (ToggleTacticalModeAction.IsNull())
	{
		PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &UIFTacticalCommandComponent::ToggleTacticalMode);
	}
	if (RotateBuildAction.IsNull())
	{
		PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &UIFTacticalCommandComponent::HandleRotateBuildInput);
	}
	// Secondary cancel fallback for standalone builds (RMB is primary cancel in PIE).
	PlayerInputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &UIFTacticalCommandComponent::HandleEscapeCancelInput);

	BindTacticalInputActions();
}

void UIFTacticalCommandComponent::RequestTacticalAssets()
{
	TArray<FSoftObjectPath> AssetPaths;
	auto AddIfValid = [&AssetPaths](const auto& SoftRef)
	{
		if (!SoftRef.IsNull())
		{
			AssetPaths.AddUnique(SoftRef.ToSoftObjectPath());
		}
	};

	AddIfValid(TacticalMappingContext);
	AddIfValid(ToggleTacticalModeAction);
	AddIfValid(PanAction);
	AddIfValid(ZoomAction);
	AddIfValid(RotateAction);
	AddIfValid(SelectAction);
	AddIfValid(SelectModifierAction);
	AddIfValid(CommandAction);
	AddIfValid(FormationLineAction);
	AddIfValid(FormationSquareAction);
	AddIfValid(FormationCircleAction);
	AddIfValid(FormationPreviewMesh);
	AddIfValid(FormationPreviewMaterial);
	AddIfValid(MoveOrderCascadeVFX);
	AddIfValid(AttackOrderCascadeVFX);
	AddIfValid(FollowOrderCascadeVFX);
	AddIfValid(TacticalHUDWidgetClass);
	AddIfValid(RotateBuildAction);

	if (AssetPaths.IsEmpty())
	{
		return;
	}

	TacticalAssetsLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		AssetPaths,
		FStreamableDelegate::CreateUObject(this, &UIFTacticalCommandComponent::HandleTacticalAssetsLoaded));
}

void UIFTacticalCommandComponent::HandleTacticalAssetsLoaded()
{
	if (UIFBuildingComponent* const Building = GetBuildingComponent())
	{
		Building->EnsureBuildableStructuresLoaded();
	}

	BindTacticalInputActions();
	EnsureTacticalHUDWidgetCreated();
	if (IsValid(TacticalHUDWidget))
	{
		TacticalHUDWidget->RefreshBuildBar();
		TacticalHUDWidget->RefreshTacticalAndPromptVisibility(bIsTacticalModeActive);
	}
	if (const AIFPlayerController* const PC = GetOwningPlayerController())
	{
		if (AIFPlayerCharacter* const PlayerChar = Cast<AIFPlayerCharacter>(PC->GetPawn()))
		{
			PlayerChar->RefreshInteractionPrompt();
		}
	}
	ApplyFormationPreviewAssets();
}

void UIFTacticalCommandComponent::BindTacticalInputActions()
{
	const AIFPlayerController* const PC = GetOwningPlayerController();
	UEnhancedInputComponent* const EnhancedInput = IsValid(PC) ? Cast<UEnhancedInputComponent>(PC->InputComponent) : nullptr;
	if (!IsValid(EnhancedInput))
	{
		return;
	}

	auto ClaimAction = [this](UInputAction* Action) -> bool
	{
		if (!IsValid(Action))
		{
			return false;
		}
		const TObjectKey<UInputAction> Key(Action);
		if (BoundInputActions.Contains(Key))
		{
			return false;
		}
		BoundInputActions.Add(Key);
		return true;
	};

	if (UInputAction* const ToggleAction = ToggleTacticalModeAction.Get(); ClaimAction(ToggleAction))
	{
		EnhancedInput->BindAction(ToggleAction, ETriggerEvent::Started, this, &UIFTacticalCommandComponent::ToggleTacticalMode);
	}

	if (UInputAction* const Pan = PanAction.Get(); ClaimAction(Pan))
	{
		EnhancedInput->BindAction(Pan, ETriggerEvent::Triggered, this, &UIFTacticalCommandComponent::HandlePanInput);
		EnhancedInput->BindAction(Pan, ETriggerEvent::Completed, this, &UIFTacticalCommandComponent::HandlePanStopped);
		EnhancedInput->BindAction(Pan, ETriggerEvent::Canceled, this, &UIFTacticalCommandComponent::HandlePanStopped);
	}

	if (UInputAction* const Zoom = ZoomAction.Get(); ClaimAction(Zoom))
	{
		EnhancedInput->BindAction(Zoom, ETriggerEvent::Triggered, this, &UIFTacticalCommandComponent::HandleZoomInput);
	}

	if (UInputAction* const Rotate = RotateAction.Get(); ClaimAction(Rotate))
	{
		EnhancedInput->BindAction(Rotate, ETriggerEvent::Triggered, this, &UIFTacticalCommandComponent::HandleRotateInput);
		EnhancedInput->BindAction(Rotate, ETriggerEvent::Completed, this, &UIFTacticalCommandComponent::HandleRotateStopped);
		EnhancedInput->BindAction(Rotate, ETriggerEvent::Canceled, this, &UIFTacticalCommandComponent::HandleRotateStopped);
	}

	if (UInputAction* const Select = SelectAction.Get(); ClaimAction(Select))
	{
		EnhancedInput->BindAction(Select, ETriggerEvent::Started, this, &UIFTacticalCommandComponent::HandleSelectStarted);
		EnhancedInput->BindAction(Select, ETriggerEvent::Triggered, this, &UIFTacticalCommandComponent::HandleSelectTriggered);
		EnhancedInput->BindAction(Select, ETriggerEvent::Completed, this, &UIFTacticalCommandComponent::HandleSelectCompleted);
		EnhancedInput->BindAction(Select, ETriggerEvent::Canceled, this, &UIFTacticalCommandComponent::CancelMarqueeSelection);
	}

	if (UInputAction* const SelectMod = SelectModifierAction.Get(); ClaimAction(SelectMod))
	{
		EnhancedInput->BindAction(SelectMod, ETriggerEvent::Started, this, &UIFTacticalCommandComponent::HandleSelectModifierStarted);
		EnhancedInput->BindAction(SelectMod, ETriggerEvent::Completed, this, &UIFTacticalCommandComponent::HandleSelectModifierCompleted);
		EnhancedInput->BindAction(SelectMod, ETriggerEvent::Canceled, this, &UIFTacticalCommandComponent::HandleSelectModifierCompleted);
	}

	if (UInputAction* const Command = CommandAction.Get(); ClaimAction(Command))
	{
		EnhancedInput->BindAction(Command, ETriggerEvent::Started, this, &UIFTacticalCommandComponent::HandleCommandStarted);
		EnhancedInput->BindAction(Command, ETriggerEvent::Triggered, this, &UIFTacticalCommandComponent::HandleCommandTriggered);
		EnhancedInput->BindAction(Command, ETriggerEvent::Completed, this, &UIFTacticalCommandComponent::HandleCommandCompleted);
		EnhancedInput->BindAction(Command, ETriggerEvent::Canceled, this, &UIFTacticalCommandComponent::CancelFormationDragPreview);
	}

	if (UInputAction* const LineAct = FormationLineAction.Get(); ClaimAction(LineAct))
	{
		EnhancedInput->BindAction(LineAct, ETriggerEvent::Started, this, &UIFTacticalCommandComponent::HandleFormationLineInput);
	}

	if (UInputAction* const SquareAct = FormationSquareAction.Get(); ClaimAction(SquareAct))
	{
		EnhancedInput->BindAction(SquareAct, ETriggerEvent::Started, this, &UIFTacticalCommandComponent::HandleFormationSquareInput);
	}

	if (UInputAction* const CircleAct = FormationCircleAction.Get(); ClaimAction(CircleAct))
	{
		EnhancedInput->BindAction(CircleAct, ETriggerEvent::Started, this, &UIFTacticalCommandComponent::HandleFormationCircleInput);
	}

	if (UInputAction* const RotateBuildAct = RotateBuildAction.Get(); ClaimAction(RotateBuildAct))
	{
		EnhancedInput->BindAction(RotateBuildAct, ETriggerEvent::Started, this, &UIFTacticalCommandComponent::HandleRotateBuildInput);
	}
}

void UIFTacticalCommandComponent::ToggleTacticalMode()
{
	if (bIsTacticalModeActive)
	{
		ExitTacticalMode(false);
	}
	else
	{
		EnterTacticalMode();
	}
}

void UIFTacticalCommandComponent::EnterTacticalMode()
{
	if (bIsTacticalModeActive)
	{
		return;
	}

	AIFPlayerController* const PC = GetOwningPlayerController();
	if (!IsValid(PC) || PC->IsGameOverActive() || UGameplayStatics::IsGamePaused(this))
	{
		return;
	}

	AIFPlayerCharacter* const PlayerChar = Cast<AIFPlayerCharacter>(PC->GetPawn());
	if (!IsValid(PlayerChar) || PlayerChar->IsDead())
	{
		return;
	}

	AIFTacticalCameraRig* const Rig = EnsureCameraRigSpawned();
	if (!IsValid(Rig))
	{
		return;
	}

	bIsTacticalModeActive = true;
	bIsSelectModifierHeld = false;

	PlayerChar->EnterTacticalStandby();
	AddTacticalMappingContext();
	PC->ApplyTacticalInputMode();

	const float StartDilation = UGameplayStatics::GetGlobalTimeDilation(this);
	const float TargetDilation = FMath::Clamp(TacticalTimeDilation, 0.1f, 1.f);
	Rig->ActivateTacticalCamera(PlayerChar, PC->GetControlRotation().Yaw, TargetDilation, TimeDilationRampSeconds);

	const float CompensatedBlendIn = ComputeDilatedBlendTime(CameraBlendInSeconds, StartDilation, TargetDilation);
	PC->SetViewTargetWithBlend(Rig, CompensatedBlendIn, EViewTargetBlendFunction::VTBlend_Cubic);

	PruneInvalidSoldiers();
	SetSelectedSoldierRingsVisible(true);

	if (UIFBuildingComponent* const Building = GetBuildingComponent())
	{
		Building->EnsureBuildableStructuresLoaded();
	}
	ShowTacticalHUDWidget();
	OnTacticalModeChanged.Broadcast(true);

	UE_LOG(LogIronField, Log, TEXT("[IF-Tactical] Entered Tactical Command Mode (dilation=%.2f)."), TargetDilation);
}

void UIFTacticalCommandComponent::ExitTacticalMode(bool bImmediateSnap)
{
	if (!bIsTacticalModeActive)
	{
		if (bImmediateSnap && IsValid(TacticalCameraRig))
		{
			TacticalCameraRig->ForceResetTimeDilation();
		}
		return;
	}

	bIsTacticalModeActive = false;
	bIsSelectModifierHeld = false;
	if (UIFBuildingComponent* const Building = GetBuildingComponent())
	{
		Building->CancelBuildPlacement();
		Building->ClearSelectedStructure();
	}
	CancelMarqueeSelection();
	CancelFormationDragPreview();
	ClearHighlightedHostileTarget();
	ClearHighlightedHostileStructure();
	SetSelectedSoldierRingsVisible(false);

	AIFPlayerController* const PC = GetOwningPlayerController();
	AIFPlayerCharacter* const PlayerChar = IsValid(PC) ? Cast<AIFPlayerCharacter>(PC->GetPawn()) : nullptr;

	RemoveTacticalMappingContext();

	if (IsValid(TacticalCameraRig))
	{
		if (bImmediateSnap)
		{
			TacticalCameraRig->ForceResetTimeDilation();
		}
		else
		{
			TacticalCameraRig->DeactivateTacticalCamera(TimeDilationRampSeconds);
		}
	}
	else if (IsValid(GetWorld()))
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}

	if (IsValid(PlayerChar))
	{
		PlayerChar->ExitTacticalStandby();
		if (IsValid(PC))
		{
			const float StartDilation = UGameplayStatics::GetGlobalTimeDilation(this);
			const float BlendOut = bImmediateSnap ? 0.f : ComputeDilatedBlendTime(CameraBlendOutSeconds, StartDilation, 1.f);
			PC->SetViewTargetWithBlend(PlayerChar, BlendOut, EViewTargetBlendFunction::VTBlend_Cubic);
		}
	}

	if (IsValid(PC) && !PC->IsGameOverActive() && !UGameplayStatics::IsGamePaused(this))
	{
		// Do not flush keys on Tab exit so holding Tab cannot re-trigger Started.
		PC->ApplyGameplayInputMode(false);
	}

	HideTacticalHUDWidget();
	OnTacticalModeChanged.Broadcast(false);

	UE_LOG(LogIronField, Log, TEXT("[IF-Tactical] Exited Tactical Command Mode."));
}

void UIFTacticalCommandComponent::HandleMatchPausedChanged(bool bIsPaused)
{
	bIsSelectModifierHeld = false;
	if (bIsPaused)
	{
		if (UIFBuildingComponent* const Building = GetBuildingComponent())
		{
			Building->CancelBuildPlacement();
		}
		CancelMarqueeSelection();
		CancelFormationDragPreview();
		if (IsValid(TacticalCameraRig))
		{
			TacticalCameraRig->ClearControlInputs();
		}
	}
}

void UIFTacticalCommandComponent::CancelMarqueeSelection()
{
	bIsSelectPressed = false;
	bIsMarqueeDragging = false;
	MarqueeStartScreenPos = FVector2D::ZeroVector;
	MarqueeCurrentScreenPos = FVector2D::ZeroVector;
	UpdateHUDMarqueeRect(false);
}

void UIFTacticalCommandComponent::CancelFormationDragPreview()
{
	bIsCommandPressed = false;
	bCommandHandledOnPress = false;
	bIsFormationDragging = false;
	bHasPreviewCache = false;
	LastPreviewUnitCount = 0;
	PreviewFormationSlots.Reset();

	if (IsValid(FormationPreviewISM))
	{
		FormationPreviewISM->ClearInstances();
		FormationPreviewISM->SetVisibility(false);
	}

	UpdateHUDFormationPreview(false);
}

void UIFTacticalCommandComponent::ClearSelection()
{
	const bool bHadSelection = !SelectedSoldiers.IsEmpty();

	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		if (AIFSoldierCharacter* const Soldier = SoldierPtr.Get())
		{
			UnbindSoldierDeath(Soldier);
			Soldier->SetTacticalSelected(false, false);
		}
	}

	SelectedSoldiers.Reset();

	if (bHadSelection)
	{
		BroadcastSelectionCount();
	}
}

void UIFTacticalCommandComponent::SetActiveFormationType(EIFTacticalFormationType NewFormationType)
{
	if (ActiveFormationType == NewFormationType)
	{
		return;
	}

	ActiveFormationType = NewFormationType;
	OnFormationTypeChanged.Broadcast(ActiveFormationType);

	if (bIsTacticalModeActive && bIsFormationDragging)
	{
		UpdateFormationDragPreview(FormationDragCurrentPoint);
	}
}

int32 UIFTacticalCommandComponent::GetSelectedSoldierCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		if (const AIFSoldierCharacter* const Soldier = SoldierPtr.Get())
		{
			if (Soldier->IsAlive())
			{
				++Count;
			}
		}
	}
	return Count;
}

AIFTacticalCameraRig* UIFTacticalCommandComponent::EnsureCameraRigSpawned()
{
	if (IsValid(TacticalCameraRig))
	{
		return TacticalCameraRig;
	}

	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	UClass* RigClass = TacticalCameraRigClass.Get();
	if (!IsValid(RigClass))
	{
		RigClass = AIFTacticalCameraRig::StaticClass();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TacticalCameraRig = World->SpawnActor<AIFTacticalCameraRig>(RigClass, FTransform::Identity, SpawnParams);
	return TacticalCameraRig;
}

UInstancedStaticMeshComponent* UIFTacticalCommandComponent::EnsureFormationPreviewComponent()
{
	if (IsValid(FormationPreviewISM))
	{
		return FormationPreviewISM;
	}

	AIFTacticalCameraRig* const Rig = EnsureCameraRigSpawned();
	if (!IsValid(Rig))
	{
		return nullptr;
	}

	FormationPreviewISM = NewObject<UInstancedStaticMeshComponent>(Rig, TEXT("FormationPreviewISM"));
	if (!IsValid(FormationPreviewISM))
	{
		return nullptr;
	}

	FormationPreviewISM->PrimaryComponentTick.bCanEverTick = false;
	FormationPreviewISM->SetUsingAbsoluteLocation(true);
	FormationPreviewISM->SetUsingAbsoluteRotation(true);
	FormationPreviewISM->SetUsingAbsoluteScale(true);
	FormationPreviewISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FormationPreviewISM->SetCollisionResponseToAllChannels(ECR_Ignore);
	FormationPreviewISM->SetGenerateOverlapEvents(false);
	FormationPreviewISM->SetCanEverAffectNavigation(false);
	FormationPreviewISM->SetCastShadow(false);
	FormationPreviewISM->bReceivesDecals = false;
	FormationPreviewISM->SetVisibility(false);
	FormationPreviewISM->SetupAttachment(Rig->GetRootComponent());
	FormationPreviewISM->RegisterComponent();

	ApplyFormationPreviewAssets();
	return FormationPreviewISM;
}

void UIFTacticalCommandComponent::ApplyFormationPreviewAssets()
{
	if (!IsValid(FormationPreviewISM))
	{
		return;
	}

	if (UStaticMesh* const PreviewMesh = FormationPreviewMesh.Get())
	{
		FormationPreviewISM->SetStaticMesh(PreviewMesh);
	}
	if (UMaterialInterface* const PreviewMat = FormationPreviewMaterial.Get())
	{
		FormationPreviewISM->SetMaterial(0, PreviewMat);
	}
}

void UIFTacticalCommandComponent::EnsureTacticalHUDWidgetCreated()
{
	if (IsValid(TacticalHUDWidget))
	{
		return;
	}

	AIFPlayerController* const PC = GetOwningPlayerController();
	if (!IsValid(PC) || !PC->IsLocalController())
	{
		return;
	}

	UClass* const WidgetClass = TacticalHUDWidgetClass.Get();
	if (!IsValid(WidgetClass))
	{
		return;
	}

	TacticalHUDWidget = CreateWidget<UIFTacticalHUDWidget>(PC, WidgetClass);
	if (IsValid(TacticalHUDWidget))
	{
		TacticalHUDWidget->BindToTacticalComponent(this);
		TacticalHUDWidget->BindToBuildingComponent(GetBuildingComponent());
		TacticalHUDWidget->AddToViewport();
		TacticalHUDWidget->RefreshTacticalAndPromptVisibility(bIsTacticalModeActive);
	}
}

void UIFTacticalCommandComponent::ShowTacticalHUDWidget()
{
	EnsureTacticalHUDWidgetCreated();
	if (IsValid(TacticalHUDWidget))
	{
		TacticalHUDWidget->RefreshTacticalAndPromptVisibility(true);
	}
}

void UIFTacticalCommandComponent::HideTacticalHUDWidget()
{
	if (IsValid(TacticalHUDWidget))
	{
		TacticalHUDWidget->RefreshTacticalAndPromptVisibility(false);
	}
}

void UIFTacticalCommandComponent::AddTacticalMappingContext()
{
	UInputMappingContext* const MappingContext = TacticalMappingContext.Get();
	const AIFPlayerController* const PC = GetOwningPlayerController();
	if (!IsValid(MappingContext) || !IsValid(PC))
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* const InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		InputSubsystem->AddMappingContext(MappingContext, TacticalMappingPriority);
	}
}

void UIFTacticalCommandComponent::RemoveTacticalMappingContext()
{
	UInputMappingContext* const MappingContext = TacticalMappingContext.Get();
	const AIFPlayerController* const PC = GetOwningPlayerController();
	if (!IsValid(MappingContext) || !IsValid(PC))
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* const InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		InputSubsystem->RemoveMappingContext(MappingContext);
	}
}

void UIFTacticalCommandComponent::HandlePanInput(const FInputActionValue& Value)
{
	if (bIsTacticalModeActive && IsValid(TacticalCameraRig))
	{
		TacticalCameraRig->SetPanInput(Value.Get<FVector2D>());
	}
}

void UIFTacticalCommandComponent::HandlePanStopped()
{
	if (IsValid(TacticalCameraRig))
	{
		TacticalCameraRig->SetPanInput(FVector2D::ZeroVector);
	}
}

void UIFTacticalCommandComponent::HandleZoomInput(const FInputActionValue& Value)
{
	if (bIsTacticalModeActive && IsValid(TacticalCameraRig))
	{
		TacticalCameraRig->AddZoomInput(Value.Get<float>());
	}
}

void UIFTacticalCommandComponent::HandleRotateInput(const FInputActionValue& Value)
{
	if (bIsTacticalModeActive && IsValid(TacticalCameraRig))
	{
		TacticalCameraRig->SetYawInput(Value.Get<float>());
	}
}

void UIFTacticalCommandComponent::HandleRotateStopped()
{
	if (IsValid(TacticalCameraRig))
	{
		TacticalCameraRig->SetYawInput(0.f);
	}
}

void UIFTacticalCommandComponent::HandleSelectStarted()
{
	if (!bIsTacticalModeActive)
	{
		return;
	}

	if (UIFBuildingComponent* const Building = GetBuildingComponent(); IsValid(Building) && Building->IsInBuildPlacementMode())
	{
		Building->TryConfirmPlacement();
		return;
	}

	const AIFPlayerController* const PC = GetOwningPlayerController();
	if (!IsValid(PC))
	{
		return;
	}

	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PC->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	bIsSelectPressed = true;
	bIsMarqueeDragging = false;
	MarqueeStartScreenPos = FVector2D(MouseX, MouseY);
	MarqueeCurrentScreenPos = MarqueeStartScreenPos;
	UpdateHUDMarqueeRect(false);
}

void UIFTacticalCommandComponent::HandleSelectTriggered()
{
	if (!bIsTacticalModeActive || !bIsSelectPressed)
	{
		return;
	}

	const AIFPlayerController* const PC = GetOwningPlayerController();
	if (!IsValid(PC))
	{
		return;
	}

	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PC->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	MarqueeCurrentScreenPos = FVector2D(MouseX, MouseY);
	if (!bIsMarqueeDragging && FVector2D::Distance(MarqueeStartScreenPos, MarqueeCurrentScreenPos) >= MarqueeDragThresholdPixels)
	{
		bIsMarqueeDragging = true;
	}

	if (bIsMarqueeDragging)
	{
		UpdateHUDMarqueeRect(true);
	}
}

void UIFTacticalCommandComponent::HandleSelectCompleted()
{
	if (!bIsTacticalModeActive || !bIsSelectPressed)
	{
		CancelMarqueeSelection();
		return;
	}

	const AIFPlayerController* const PC = GetOwningPlayerController();
	float MouseX = MarqueeCurrentScreenPos.X;
	float MouseY = MarqueeCurrentScreenPos.Y;
	if (IsValid(PC))
	{
		PC->GetMousePosition(MouseX, MouseY);
	}

	const bool bWasMarquee = bIsMarqueeDragging;
	const FVector2D StartPos = MarqueeStartScreenPos;
	const FVector2D EndPos(MouseX, MouseY);

	CancelMarqueeSelection();

	if (bWasMarquee)
	{
		PerformMarqueeSelection(StartPos, EndPos);
	}
	else
	{
		PerformSingleClickSelection();
	}
}

void UIFTacticalCommandComponent::HandleSelectModifierStarted()
{
	bIsSelectModifierHeld = true;
}

void UIFTacticalCommandComponent::HandleSelectModifierCompleted()
{
	bIsSelectModifierHeld = false;
}

void UIFTacticalCommandComponent::HandleFormationLineInput()
{
	const UIFBuildingComponent* const Building = GetBuildingComponent();
	if (bIsTacticalModeActive && (!IsValid(Building) || !Building->IsInBuildPlacementMode()))
	{
		SetActiveFormationType(EIFTacticalFormationType::Line);
	}
}

void UIFTacticalCommandComponent::HandleFormationSquareInput()
{
	const UIFBuildingComponent* const Building = GetBuildingComponent();
	if (bIsTacticalModeActive && (!IsValid(Building) || !Building->IsInBuildPlacementMode()))
	{
		SetActiveFormationType(EIFTacticalFormationType::Square);
	}
}

void UIFTacticalCommandComponent::HandleFormationCircleInput()
{
	const UIFBuildingComponent* const Building = GetBuildingComponent();
	if (bIsTacticalModeActive && (!IsValid(Building) || !Building->IsInBuildPlacementMode()))
	{
		SetActiveFormationType(EIFTacticalFormationType::Circle);
	}
}

void UIFTacticalCommandComponent::HandleRotateBuildInput()
{
	if (UIFBuildingComponent* const Building = GetBuildingComponent(); bIsTacticalModeActive && IsValid(Building) && Building->IsInBuildPlacementMode())
	{
		Building->RotateBuildPreview(IsSelectModifierDown());
	}
}

void UIFTacticalCommandComponent::HandleEscapeCancelInput()
{
	if (UIFBuildingComponent* const Building = GetBuildingComponent(); bIsTacticalModeActive && IsValid(Building) && Building->IsInBuildPlacementMode())
	{
		Building->CancelBuildPlacement();
	}
}

void UIFTacticalCommandComponent::HandleApplicationActivationChanged(bool bIsAppActive)
{
	if (!bIsAppActive)
	{
		if (UIFBuildingComponent* const Building = GetBuildingComponent())
		{
			Building->CancelBuildPlacement();
		}
		CancelMarqueeSelection();
		CancelFormationDragPreview();
		bIsSelectModifierHeld = false;
		if (IsValid(TacticalCameraRig))
		{
			TacticalCameraRig->ClearControlInputs();
		}
	}
}

void UIFTacticalCommandComponent::PerformSingleClickSelection()
{
	const AIFPlayerController* const PC = GetOwningPlayerController();
	const APawn* const PlayerPawn = IsValid(PC) ? PC->GetPawn() : nullptr;
	if (!IsValid(PC) || !IsValid(PlayerPawn))
	{
		return;
	}

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDir = FVector::ForwardVector;
	if (!PC->DeprojectMousePositionToWorld(RayOrigin, RayDir))
	{
		return;
	}

	AActor* const HitActor = FindBestPawnUnderCursor(
		RayOrigin,
		RayDir,
		CommandTraceDistance,
		[PlayerPawn](const AActor* Candidate)
		{
			const AIFSoldierCharacter* const Soldier = Cast<AIFSoldierCharacter>(Candidate);
			return IsValid(Soldier) && Soldier->IsAlive() && IFTeamUtils::AreFriendly(PlayerPawn, Soldier);
		});

	AIFSoldierCharacter* const HitSoldier = Cast<AIFSoldierCharacter>(HitActor);
	const bool bShiftDown = IsSelectModifierDown();

	if (IsValid(HitSoldier))
	{
		if (UIFBuildingComponent* const Building = GetBuildingComponent())
		{
			Building->ClearSelectedStructure();
		}
		if (bShiftDown)
		{
			ToggleSoldierInSelection(HitSoldier);
		}
		else
		{
			ReplaceSelection({ HitSoldier });
		}
	}
	else if (!bShiftDown)
	{
		ClearSelection();
		if (UIFBuildingComponent* const Building = GetBuildingComponent())
		{
			if (AIFStructure* const FriendlyStruct = FindBestStructureUnderCursor(RayOrigin, RayDir, CommandTraceDistance, PlayerPawn, /*bHostileOnly*/ false))
			{
				Building->SelectFriendlyStructure(FriendlyStruct);
			}
			else
			{
				Building->ClearSelectedStructure();
			}
		}
	}
}

void UIFTacticalCommandComponent::PerformMarqueeSelection(const FVector2D& StartPos, const FVector2D& EndPos)
{
	const AIFPlayerController* const PC = GetOwningPlayerController();
	const APawn* const PlayerPawn = IsValid(PC) ? PC->GetPawn() : nullptr;
	UWorld* const World = GetWorld();
	if (!IsValid(PC) || !IsValid(PlayerPawn) || !IsValid(World))
	{
		return;
	}

	const FVector2D BoxMin(FMath::Min(StartPos.X, EndPos.X), FMath::Min(StartPos.Y, EndPos.Y));
	const FVector2D BoxMax(FMath::Max(StartPos.X, EndPos.X), FMath::Max(StartPos.Y, EndPos.Y));
	const FBox2D SelectionBox(BoxMin, BoxMax);

	TArray<AIFSoldierCharacter*> BoxedSoldiers;
	for (TActorIterator<AIFSoldierCharacter> It(World); It; ++It)
	{
		AIFSoldierCharacter* const Soldier = *It;
		if (!IsValid(Soldier) || !Soldier->IsAlive() || !IFTeamUtils::AreFriendly(PlayerPawn, Soldier))
		{
			continue;
		}

		FVector2D CenterScreen = FVector2D::ZeroVector;
		FVector2D BaseScreen = FVector2D::ZeroVector;
		const bool bCenterInside = PC->ProjectWorldLocationToScreen(Soldier->GetActorLocation(), CenterScreen, true)
			&& SelectionBox.IsInside(CenterScreen);
		const bool bBaseInside = !bCenterInside
			&& PC->ProjectWorldLocationToScreen(Soldier->GetActorLocation() - FVector(0.f, 0.f, 50.f), BaseScreen, true)
			&& SelectionBox.IsInside(BaseScreen);

		if (bCenterInside || bBaseInside)
		{
			BoxedSoldiers.Add(Soldier);
		}
	}

	if (!BoxedSoldiers.IsEmpty() || !IsSelectModifierDown())
	{
		if (UIFBuildingComponent* const Building = GetBuildingComponent())
		{
			Building->ClearSelectedStructure();
		}
	}

	if (IsSelectModifierDown())
	{
		AddToSelection(BoxedSoldiers);
	}
	else
	{
		ReplaceSelection(BoxedSoldiers);
	}
}

void UIFTacticalCommandComponent::ReplaceSelection(const TArray<AIFSoldierCharacter*>& NewSoldiers)
{
	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		if (AIFSoldierCharacter* const Soldier = SoldierPtr.Get())
		{
			if (!NewSoldiers.Contains(Soldier))
			{
				UnbindSoldierDeath(Soldier);
				Soldier->SetTacticalSelected(false, false);
			}
		}
	}

	SelectedSoldiers.Reset();
	for (AIFSoldierCharacter* const Soldier : NewSoldiers)
	{
		if (IsValid(Soldier) && Soldier->IsAlive())
		{
			const bool bAlreadyAdded = SelectedSoldiers.ContainsByPredicate(
				[Soldier](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr) { return Ptr.Get() == Soldier; });
			if (!bAlreadyAdded)
			{
				SelectedSoldiers.Add(Soldier);
			}
			BindSoldierDeath(Soldier);
			Soldier->SetTacticalSelected(true, bIsTacticalModeActive);
		}
	}

	BroadcastSelectionCount();
}

void UIFTacticalCommandComponent::AddToSelection(const TArray<AIFSoldierCharacter*>& SoldiersToAdd)
{
	bool bChanged = false;
	for (AIFSoldierCharacter* const Soldier : SoldiersToAdd)
	{
		if (!IsValid(Soldier) || !Soldier->IsAlive())
		{
			continue;
		}

		const bool bAlreadySelected = SelectedSoldiers.ContainsByPredicate(
			[Soldier](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr) { return Ptr.Get() == Soldier; });
		if (!bAlreadySelected)
		{
			SelectedSoldiers.Add(Soldier);
			BindSoldierDeath(Soldier);
			Soldier->SetTacticalSelected(true, bIsTacticalModeActive);
			bChanged = true;
		}
	}

	if (bChanged)
	{
		BroadcastSelectionCount();
	}
}

void UIFTacticalCommandComponent::ToggleSoldierInSelection(AIFSoldierCharacter* Soldier)
{
	if (!IsValid(Soldier) || !Soldier->IsAlive())
	{
		return;
	}

	const int32 ExistingIndex = SelectedSoldiers.IndexOfByPredicate(
		[Soldier](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr) { return Ptr.Get() == Soldier; });
	if (ExistingIndex != INDEX_NONE)
	{
		UnbindSoldierDeath(Soldier);
		Soldier->SetTacticalSelected(false, false);
		SelectedSoldiers.RemoveAt(ExistingIndex);
	}
	else
	{
		SelectedSoldiers.Add(Soldier);
		BindSoldierDeath(Soldier);
		Soldier->SetTacticalSelected(true, bIsTacticalModeActive);
	}

	BroadcastSelectionCount();
}

void UIFTacticalCommandComponent::SetSelectedSoldierRingsVisible(bool bVisible)
{
	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		if (AIFSoldierCharacter* const Soldier = SoldierPtr.Get())
		{
			Soldier->SetSelectionRingDisplayVisible(bVisible);
		}
	}
}

void UIFTacticalCommandComponent::SetHighlightedHostileTarget(AIFSoldierCharacter* HostileSoldier)
{
	if (HighlightedHostileTarget.Get() == HostileSoldier)
	{
		if (IsValid(HostileSoldier) && HostileSoldier->IsAlive())
		{
			HostileSoldier->SetHostileTargetHighlighted(true);
		}
		return;
	}

	ClearHighlightedHostileTarget();

	if (!IsValid(HostileSoldier) || !HostileSoldier->IsAlive())
	{
		return;
	}

	HighlightedHostileTarget = HostileSoldier;
	HostileSoldier->OnCharacterDied.AddUniqueDynamic(this, &UIFTacticalCommandComponent::HandleHighlightedHostileDied);
	HostileSoldier->SetHostileTargetHighlighted(true);
}

void UIFTacticalCommandComponent::ClearHighlightedHostileTarget()
{
	if (AIFSoldierCharacter* const Hostile = HighlightedHostileTarget.Get())
	{
		Hostile->OnCharacterDied.RemoveDynamic(this, &UIFTacticalCommandComponent::HandleHighlightedHostileDied);
		Hostile->SetHostileTargetHighlighted(false);
	}
	HighlightedHostileTarget.Reset();
}

void UIFTacticalCommandComponent::BindSoldierDeath(AIFSoldierCharacter* Soldier)
{
	if (IsValid(Soldier))
	{
		Soldier->OnCharacterDied.AddUniqueDynamic(this, &UIFTacticalCommandComponent::HandleSelectedSoldierDied);
	}
}

void UIFTacticalCommandComponent::UnbindSoldierDeath(AIFSoldierCharacter* Soldier)
{
	if (IsValid(Soldier))
	{
		Soldier->OnCharacterDied.RemoveDynamic(this, &UIFTacticalCommandComponent::HandleSelectedSoldierDied);
	}
}

void UIFTacticalCommandComponent::HandleSelectedSoldierDied(AIFBaseCharacter* DeadCharacter)
{
	AIFSoldierCharacter* const DeadSoldier = Cast<AIFSoldierCharacter>(DeadCharacter);
	if (!IsValid(DeadSoldier))
	{
		return;
	}

	UnbindSoldierDeath(DeadSoldier);
	DeadSoldier->SetTacticalSelected(false, false);

	const int32 Removed = SelectedSoldiers.RemoveAll(
		[DeadSoldier](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr)
		{
			return !Ptr.IsValid() || Ptr.Get() == DeadSoldier;
		});

	if (Removed > 0)
	{
		BroadcastSelectionCount();
	}
}

void UIFTacticalCommandComponent::HandleHighlightedHostileDied(AIFBaseCharacter* DeadCharacter)
{
	if (HighlightedHostileTarget.Get() == DeadCharacter)
	{
		ClearHighlightedHostileTarget();
	}
}

void UIFTacticalCommandComponent::PruneInvalidSoldiers()
{
	const int32 PrevCount = SelectedSoldiers.Num();
	SelectedSoldiers.RemoveAll(
		[this](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr)
		{
			AIFSoldierCharacter* const Soldier = Ptr.Get();
			if (!IsValid(Soldier) || !Soldier->IsAlive())
			{
				if (IsValid(Soldier))
				{
					UnbindSoldierDeath(Soldier);
					Soldier->SetTacticalSelected(false, false);
				}
				return true;
			}
			return false;
		});

	if (SelectedSoldiers.Num() != PrevCount)
	{
		BroadcastSelectionCount();
	}
}

void UIFTacticalCommandComponent::UpdateHUDMarqueeRect(bool bActive)
{
	if (AIFTacticalHUD* const HUD = GetTacticalHUD())
	{
		HUD->SetMarqueeSelectionRect(bActive, MarqueeStartScreenPos, MarqueeCurrentScreenPos);
	}
}

void UIFTacticalCommandComponent::UpdateHUDFormationPreview(bool bActive)
{
	if (AIFTacticalHUD* const HUD = GetTacticalHUD())
	{
		HUD->SetFormationPreview(bActive, PreviewFormationSlots);
	}
}

void UIFTacticalCommandComponent::BroadcastSelectionCount()
{
	OnSelectionChanged.Broadcast(GetSelectedSoldierCount());
}

void UIFTacticalCommandComponent::HandleCommandStarted()
{
	if (!bIsTacticalModeActive)
	{
		return;
	}

	if (UIFBuildingComponent* const Building = GetBuildingComponent(); IsValid(Building) && Building->IsInBuildPlacementMode())
	{
		Building->CancelBuildPlacement();
		bCommandHandledOnPress = true;
		return;
	}

	PruneInvalidSoldiers();
	if (SelectedSoldiers.IsEmpty())
	{
		return;
	}

	const AIFPlayerController* const PC = GetOwningPlayerController();
	APawn* const PlayerPawn = IsValid(PC) ? PC->GetPawn() : nullptr;
	if (!IsValid(PC) || !IsValid(PlayerPawn))
	{
		return;
	}

	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDir = FVector::ForwardVector;
	if (!PC->DeprojectMousePositionToWorld(RayOrigin, RayDir))
	{
		return;
	}

	AActor* const TargetActor = FindBestPawnUnderCursor(
		RayOrigin,
		RayDir,
		CommandTraceDistance,
		[PlayerPawn](const AActor* Candidate)
		{
			const AIFBaseCharacter* const BaseChar = Cast<AIFBaseCharacter>(Candidate);
			if (!IsValid(BaseChar) || !BaseChar->IsAlive())
			{
				return false;
			}
			return (BaseChar == PlayerPawn) || IFTeamUtils::AreHostile(PlayerPawn, BaseChar);
		});

	if (IsValid(TargetActor))
	{
		CancelFormationDragPreview();
		ClearHighlightedHostileStructure();
		bCommandHandledOnPress = true;

		if (TargetActor == PlayerPawn)
		{
			ClearHighlightedHostileTarget();
			for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
			{
				if (AIFSoldierCharacter* const Soldier = SoldierPtr.Get())
				{
					if (AIFSoldierAIController* const AI = Soldier->GetSoldierController())
					{
						AI->IssueFollowActor(PlayerPawn, FollowOrderRadius);
					}
				}
			}

			const FVector MarkerLoc = PlayerPawn->GetActorLocation();
			SpawnOrderMarkerVFX(EIFTacticalOrderType::FollowCommander, MarkerLoc);
			OnOrderIssued.Broadcast(EIFTacticalOrderType::FollowCommander, MarkerLoc);
			return;
		}

		// Group Attack: distribute the squad across nearby hostiles around the clicked target so soldiers don't dogpile a single actor.
		TArray<AIFBaseCharacter*> ClusterTargets;
		if (AIFBaseCharacter* const PrimaryBase = Cast<AIFBaseCharacter>(TargetActor))
		{
			ClusterTargets.Add(PrimaryBase);
		}

		const FVector PrimaryLoc = TargetActor->GetActorLocation();
		const float ClusterRadius = FMath::Max(AttackOrderClusterRadius, 100.f);
		if (UWorld* const World = GetWorld())
		{
			TArray<FOverlapResult> Overlaps;
			FCollisionObjectQueryParams PawnParams;
			PawnParams.AddObjectTypesToQuery(ECC_Pawn);
			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFTacticalClusterAttack), false);
			if (World->OverlapMultiByObjectType(
				Overlaps,
				PrimaryLoc,
				FQuat::Identity,
				PawnParams,
				FCollisionShape::MakeSphere(ClusterRadius),
				QueryParams))
			{
				for (const FOverlapResult& Overlap : Overlaps)
				{
					AIFBaseCharacter* const Candidate = Cast<AIFBaseCharacter>(Overlap.GetActor());
					if (IsValid(Candidate) && Candidate != TargetActor && Candidate->IsAlive()
						&& IFTeamUtils::AreHostile(PlayerPawn, Candidate))
					{
						ClusterTargets.AddUnique(Candidate);
					}
				}
			}
		}

		TArray<AIFSoldierCharacter*> AliveSquad;
		TArray<FVector> AliveLocations;
		FVector UnusedCentroid = FVector::ZeroVector;
		GatherAliveSelectedSquad(AliveSquad, AliveLocations, UnusedCentroid);

		if (ClusterTargets.Num() <= 1 || AliveSquad.Num() <= 1)
		{
			for (AIFSoldierCharacter* const Soldier : AliveSquad)
			{
				if (AIFSoldierAIController* const AI = Soldier->GetSoldierController())
				{
					AI->IssueAttackTarget(TargetActor);
				}
			}
		}
		else
		{
			TArray<int32> AssignedCounts;
			AssignedCounts.Init(0, ClusterTargets.Num());

			// Guarantee the closest soldier immediately commits to the clicked primary target.
			int32 PrimarySoldierIdx = 0;
			float BestPrimaryDistSq = TNumericLimits<float>::Max();
			for (int32 i = 0; i < AliveSquad.Num(); ++i)
			{
				const float DistSq = FVector::DistSquared2D(AliveLocations[i], PrimaryLoc);
				if (DistSq < BestPrimaryDistSq)
				{
					BestPrimaryDistSq = DistSq;
					PrimarySoldierIdx = i;
				}
			}

			if (AIFSoldierAIController* const PrimaryAI = AliveSquad[PrimarySoldierIdx]->GetSoldierController())
			{
				PrimaryAI->IssueAttackTarget(ClusterTargets[0]);
				AssignedCounts[0] = 1;
			}

			for (int32 i = 0; i < AliveSquad.Num(); ++i)
			{
				if (i == PrimarySoldierIdx)
				{
					continue;
				}

				AIFSoldierAIController* const AI = AliveSquad[i]->GetSoldierController();
				if (!IsValid(AI))
				{
					continue;
				}

				int32 BestTargetIdx = 0;
				float BestScore = TNumericLimits<float>::Max();
				for (int32 TargetIdx = 0; TargetIdx < ClusterTargets.Num(); ++TargetIdx)
				{
					const float Score =
						FVector::Dist2D(AliveLocations[i], ClusterTargets[TargetIdx]->GetActorLocation())
						+ (static_cast<float>(AssignedCounts[TargetIdx]) * AttackOrderTargetSpreadPenalty);
					if (Score < BestScore)
					{
						BestScore = Score;
						BestTargetIdx = TargetIdx;
					}
				}

				AI->IssueAttackTarget(ClusterTargets[BestTargetIdx]);
				++AssignedCounts[BestTargetIdx];
			}
		}

		SetHighlightedHostileTarget(Cast<AIFSoldierCharacter>(TargetActor));
		const FVector MarkerLoc = TargetActor->GetActorLocation();
		SpawnOrderMarkerVFX(EIFTacticalOrderType::AttackTarget, MarkerLoc);
		OnOrderIssued.Broadcast(EIFTacticalOrderType::AttackTarget, MarkerLoc);
		return;
	}

	// Hostile structure RMB attack order.
	if (AIFStructure* const HostileStructure = FindBestStructureUnderCursor(RayOrigin, RayDir, CommandTraceDistance, PlayerPawn, /*bHostileOnly*/ true))
	{
		CancelFormationDragPreview();
		ClearHighlightedHostileTarget();
		bCommandHandledOnPress = true;

		TArray<AIFSoldierCharacter*> AliveSquad;
		TArray<FVector> AliveLocations;
		FVector UnusedCentroid = FVector::ZeroVector;
		GatherAliveSelectedSquad(AliveSquad, AliveLocations, UnusedCentroid);

		for (AIFSoldierCharacter* const Soldier : AliveSquad)
		{
			if (AIFSoldierAIController* const AI = Soldier->GetSoldierController())
			{
				AI->IssueAttackTarget(HostileStructure);
			}
		}

		SetHighlightedHostileStructure(HostileStructure);
		const FVector MarkerLoc = HostileStructure->GetActorLocation();
		SpawnOrderMarkerVFX(EIFTacticalOrderType::AttackTarget, MarkerLoc);
		OnOrderIssued.Broadcast(EIFTacticalOrderType::AttackTarget, MarkerLoc);
		return;
	}

	FHitResult GroundHit;
	if (TraceGroundUnderCursor(RayOrigin, RayDir, GroundHit))
	{
		bIsCommandPressed = true;
		bCommandHandledOnPress = false;
		bIsFormationDragging = false;
		FormationDragStartPoint = GroundHit.ImpactPoint;
		FormationDragCurrentPoint = GroundHit.ImpactPoint;
	}
}

void UIFTacticalCommandComponent::HandleCommandTriggered()
{
	if (!bIsTacticalModeActive || !bIsCommandPressed || bCommandHandledOnPress)
	{
		return;
	}

	const AIFPlayerController* const PC = GetOwningPlayerController();
	if (!IsValid(PC))
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
		return;
	}

	FormationDragCurrentPoint = GroundHit.ImpactPoint;
	if (!bIsFormationDragging && FVector::Dist2D(FormationDragStartPoint, FormationDragCurrentPoint) >= FormationDragThresholdCm)
	{
		bIsFormationDragging = true;
	}

	if (bIsFormationDragging)
	{
		UpdateFormationDragPreview(FormationDragCurrentPoint);
	}
}

void UIFTacticalCommandComponent::HandleCommandCompleted()
{
	if (bCommandHandledOnPress)
	{
		bCommandHandledOnPress = false;
		bIsCommandPressed = false;
		return;
	}

	if (!bIsTacticalModeActive || !bIsCommandPressed)
	{
		CancelFormationDragPreview();
		return;
	}

	const bool bWasDragging = bIsFormationDragging;
	const FVector AnchorPoint = FormationDragStartPoint;
	const FVector DragEndPoint = FormationDragCurrentPoint;

	CancelFormationDragPreview();
	IssueFormationMoveOrder(AnchorPoint, DragEndPoint, bWasDragging);
}

template <typename TPredicate>
AActor* UIFTacticalCommandComponent::FindBestPawnUnderCursor(const FVector& RayOrigin, const FVector& RayDir, float MaxDistance, TPredicate&& IsEligible) const
{
	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	FCollisionObjectQueryParams EnvObjectParams;
	EnvObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams EnvQueryParams(SCENE_QUERY_STAT(IFTacticalEnvTrace), false);
	if (IsValid(TacticalCameraRig))
	{
		EnvQueryParams.AddIgnoredActor(TacticalCameraRig);
	}

	FHitResult EnvHit;
	float EnvDistance = MaxDistance;
	if (World->LineTraceSingleByObjectType(EnvHit, RayOrigin, RayOrigin + RayDir * MaxDistance, EnvObjectParams, EnvQueryParams))
	{
		EnvDistance = EnvHit.Distance;
	}

	const float SweepDistance = FMath::Min(MaxDistance, EnvDistance + PawnClickToleranceRadius);
	const FVector SweepEnd = RayOrigin + RayDir * SweepDistance;

	FCollisionObjectQueryParams PawnObjectParams;
	PawnObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams PawnQueryParams(SCENE_QUERY_STAT(IFTacticalPawnTrace), false);

	// 1. Direct line trace gives priority to exact cursor hits even if another unit stands nearby.
	TArray<FHitResult> DirectHits;
	if (World->LineTraceMultiByObjectType(DirectHits, RayOrigin, SweepEnd, PawnObjectParams, PawnQueryParams))
	{
		for (const FHitResult& Hit : DirectHits)
		{
			AActor* const Candidate = Hit.GetActor();
			if (Hit.Distance <= EnvDistance + 25.f && IsEligible(Candidate))
			{
				return Candidate;
			}
		}
	}

	// 2. Forgiving sphere sweep catches moving targets or units clicked near capsule edges from overhead.
	TArray<FHitResult> SweepHits;
	if (!World->SweepMultiByObjectType(
		SweepHits,
		RayOrigin,
		SweepEnd,
		FQuat::Identity,
		PawnObjectParams,
		FCollisionShape::MakeSphere(PawnClickToleranceRadius),
		PawnQueryParams))
	{
		return nullptr;
	}

	AActor* BestCandidate = nullptr;
	float BestPerpDistSq = TNumericLimits<float>::Max();

	for (const FHitResult& Hit : SweepHits)
	{
		AActor* const Candidate = Hit.GetActor();
		if (!IsEligible(Candidate))
		{
			continue;
		}

		const float PerpDistSq = FMath::PointDistToSegmentSquared(Candidate->GetActorLocation(), RayOrigin, SweepEnd);
		if (PerpDistSq < BestPerpDistSq)
		{
			BestPerpDistSq = PerpDistSq;
			BestCandidate = Candidate;
		}
	}

	return BestCandidate;
}

bool UIFTacticalCommandComponent::TraceGroundUnderCursor(const FVector& RayOrigin, const FVector& RayDir, FHitResult& OutHit) const
{
	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return false;
	}

	FCollisionObjectQueryParams GroundObjectParams;
	GroundObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFTacticalGroundTrace), false);
	if (IsValid(TacticalCameraRig))
	{
		QueryParams.AddIgnoredActor(TacticalCameraRig);
	}

	TArray<FHitResult> Hits;
	if (!World->LineTraceMultiByObjectType(Hits, RayOrigin, RayOrigin + RayDir * CommandTraceDistance, GroundObjectParams, QueryParams))
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

void UIFTacticalCommandComponent::GatherAliveSelectedSquad(
	TArray<AIFSoldierCharacter*>& OutSquad,
	TArray<FVector>& OutLocations,
	FVector& OutCentroid) const
{
	OutSquad.Reset();
	OutLocations.Reset();
	OutCentroid = FVector::ZeroVector;

	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		AIFSoldierCharacter* const Soldier = SoldierPtr.Get();
		if (IsValid(Soldier) && Soldier->IsAlive() && IsValid(Soldier->GetSoldierController()))
		{
			const FVector Loc = Soldier->GetActorLocation();
			OutSquad.Add(Soldier);
			OutLocations.Add(Loc);
			OutCentroid += Loc;
		}
	}

	if (!OutSquad.IsEmpty())
	{
		OutCentroid /= static_cast<float>(OutSquad.Num());
	}
}

FVector UIFTacticalCommandComponent::ResolvePreviewCameraForward() const
{
	if (IsValid(TacticalCameraRig))
	{
		return TacticalCameraRig->GetActorForwardVector();
	}

	if (const AIFPlayerController* const PC = GetOwningPlayerController())
	{
		if (const APawn* const PlayerPawn = PC->GetPawn())
		{
			return PlayerPawn->GetActorForwardVector();
		}
	}

	return FVector::ForwardVector;
}

void UIFTacticalCommandComponent::UpdateFormationDragPreview(const FVector& CurrentGroundPoint)
{
	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	TArray<AIFSoldierCharacter*> ActiveSquad;
	TArray<FVector> SoldierLocations;
	FVector SquadCentroid = FVector::ZeroVector;
	GatherAliveSelectedSquad(ActiveSquad, SoldierLocations, SquadCentroid);

	if (ActiveSquad.IsEmpty())
	{
		CancelFormationDragPreview();
		return;
	}

	if (bHasPreviewCache
		&& LastPreviewFormationType == ActiveFormationType
		&& LastPreviewUnitCount == ActiveSquad.Num()
		&& FVector::DistSquared2D(CurrentGroundPoint, LastPreviewGroundPoint) < FMath::Square(8.f))
	{
		return;
	}

	LastPreviewGroundPoint = CurrentGroundPoint;
	LastPreviewFormationType = ActiveFormationType;
	LastPreviewUnitCount = ActiveSquad.Num();
	bHasPreviewCache = true;

	FIFFormationSolveParams Params;
	Params.FormationType = ActiveFormationType;
	Params.UnitCount = ActiveSquad.Num();
	Params.AnchorPoint = FormationDragStartPoint;
	Params.DragEndPoint = CurrentGroundPoint;
	Params.bIsDrag = true;
	Params.SquadCentroid = SquadCentroid;
	Params.CameraForward = ResolvePreviewCameraForward();
	Params.LateralSpacing = FormationLateralSpacing;
	Params.RankSpacing = FormationRankSpacing;
	Params.LineAspectRatio = FormationLineAspectRatio;
	Params.MinLineColumns = FormationMinLineColumns;
	Params.MaxLineColumns = FormationMaxLineColumns;
	Params.MinShapeRadius = FormationMinShapeRadius;
	Params.MaxShapeRadius = FormationMaxShapeRadius;

	FVector FormationCenter = FormationDragStartPoint;
	const UNavigationSystemV1* const NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!FIFFormationSolver::SolveFormation(Params, NavSys, PreviewFormationSlots, FormationCenter))
	{
		return;
	}

	if (UInstancedStaticMeshComponent* const PreviewISM = EnsureFormationPreviewComponent())
	{
		ApplyFormationPreviewAssets();
		PreviewISM->ClearInstances();

		TArray<FTransform> InstanceTransforms;
		InstanceTransforms.Reserve(PreviewFormationSlots.Num());
		for (const FIFFormationSlot& Slot : PreviewFormationSlots)
		{
			const FVector RingPos = Slot.Location + FVector(0.f, 0.f, FormationPreviewGroundOffset);
			const FRotator RingRot(0.f, Slot.FacingYaw, 0.f);
			InstanceTransforms.Emplace(RingRot, RingPos, FormationPreviewScale);
		}

		PreviewISM->AddInstances(InstanceTransforms, false, true);
		PreviewISM->SetVisibility(true);
	}

	UpdateHUDFormationPreview(true);
}

void UIFTacticalCommandComponent::IssueFormationMoveOrder(const FVector& AnchorPoint, const FVector& DragEndPoint, bool bIsDrag)
{
	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	ClearHighlightedHostileTarget();

	TArray<AIFSoldierCharacter*> ActiveSquad;
	TArray<FVector> SoldierLocations;
	FVector SquadCentroid = FVector::ZeroVector;
	GatherAliveSelectedSquad(ActiveSquad, SoldierLocations, SquadCentroid);

	if (ActiveSquad.IsEmpty())
	{
		return;
	}

	FIFFormationSolveParams Params;
	Params.FormationType = ActiveFormationType;
	Params.UnitCount = ActiveSquad.Num();
	Params.AnchorPoint = AnchorPoint;
	Params.DragEndPoint = DragEndPoint;
	Params.bIsDrag = bIsDrag;
	Params.SquadCentroid = SquadCentroid;
	Params.CameraForward = ResolvePreviewCameraForward();
	Params.LateralSpacing = FormationLateralSpacing;
	Params.RankSpacing = FormationRankSpacing;
	Params.LineAspectRatio = FormationLineAspectRatio;
	Params.MinLineColumns = FormationMinLineColumns;
	Params.MaxLineColumns = FormationMaxLineColumns;
	Params.MinShapeRadius = FormationMinShapeRadius;
	Params.MaxShapeRadius = FormationMaxShapeRadius;

	TArray<FIFFormationSlot> SolvedSlots;
	FVector FormationCenter = AnchorPoint;
	const UNavigationSystemV1* const NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!FIFFormationSolver::SolveFormation(Params, NavSys, SolvedSlots, FormationCenter))
	{
		return;
	}

	TArray<int32> SoldierToSlotIndices;
	FIFFormationSolver::AssignSoldiersToSlots(SoldierLocations, SolvedSlots, SoldierToSlotIndices);

	const int32 AssignCount = FMath::Min(ActiveSquad.Num(), SoldierToSlotIndices.Num());
	for (int32 i = 0; i < AssignCount; ++i)
	{
		const int32 SlotIndex = SoldierToSlotIndices[i];
		if (!SolvedSlots.IsValidIndex(SlotIndex))
		{
			continue;
		}

		AIFSoldierCharacter* const Soldier = ActiveSquad[i];
		if (AIFSoldierAIController* const AI = IsValid(Soldier) ? Soldier->GetSoldierController() : nullptr)
		{
			const FIFFormationSlot& AssignedSlot = SolvedSlots[SlotIndex];
			AI->IssueMoveToLocation(AssignedSlot.Location, MoveOrderAcceptanceRadius, AssignedSlot.FacingYaw, true);
		}
	}

	SpawnOrderMarkerVFX(EIFTacticalOrderType::MoveTo, FormationCenter + FVector(0.f, 0.f, 5.f));
	OnOrderIssued.Broadcast(EIFTacticalOrderType::MoveTo, FormationCenter);
}

void UIFTacticalCommandComponent::SpawnOrderMarkerVFX(EIFTacticalOrderType OrderType, const FVector& Location)
{
	CleanupOrderMarkerVFX();

	UWorld* const World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	auto ResolveVFX = [](const TSoftObjectPtr<UParticleSystem>& SoftVFX) -> UParticleSystem*
	{
		if (SoftVFX.IsNull())
		{
			return nullptr;
		}
		UParticleSystem* Asset = SoftVFX.Get();
		return IsValid(Asset) ? Asset : SoftVFX.LoadSynchronous();
	};

	UParticleSystem* ParticleAsset = nullptr;
	switch (OrderType)
	{
	case EIFTacticalOrderType::AttackTarget:
		ParticleAsset = ResolveVFX(AttackOrderCascadeVFX);
		break;
	case EIFTacticalOrderType::FollowCommander:
		ParticleAsset = ResolveVFX(FollowOrderCascadeVFX);
		break;
	case EIFTacticalOrderType::MoveTo:
	default:
		ParticleAsset = ResolveVFX(MoveOrderCascadeVFX);
		break;
	}

	if (!IsValid(ParticleAsset))
	{
		ParticleAsset = ResolveVFX(MoveOrderCascadeVFX);
	}
	if (!IsValid(ParticleAsset))
	{
		return;
	}

	FVector SpawnLocation = Location;
	FHitResult GroundHit;
	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(IFTacticalOrderVFXGround), false);
	if (IsValid(TacticalCameraRig))
	{
		GroundParams.AddIgnoredActor(TacticalCameraRig);
	}
	if (World->LineTraceSingleByChannel(
		GroundHit,
		Location + FVector(0.f, 0.f, 150.f),
		Location - FVector(0.f, 0.f, 300.f),
		ECC_WorldStatic,
		GroundParams))
	{
		SpawnLocation = GroundHit.ImpactPoint + FVector(0.f, 0.f, 5.f);
	}

	UParticleSystemComponent* const SpawnedPSC = UGameplayStatics::SpawnEmitterAtLocation(
		World,
		ParticleAsset,
		SpawnLocation,
		FRotator::ZeroRotator,
		FVector(OrderMarkerScale),
		true,
		EPSCPoolMethod::None,
		true);

	if (IsValid(SpawnedPSC))
	{
		ActiveOrderMarkerPSC = SpawnedPSC;
		const float EffectiveDilation = FMath::Max(UGameplayStatics::GetGlobalTimeDilation(this), 0.1f);
		SpawnedPSC->CustomTimeDilation = 1.f / EffectiveDilation;
		const float TimerDelay = FMath::Max(0.05f, OrderMarkerDurationSeconds * EffectiveDilation);
		World->GetTimerManager().SetTimer(
			OrderMarkerTimerHandle,
			this,
			&UIFTacticalCommandComponent::CleanupOrderMarkerVFX,
			TimerDelay,
			false);
	}
}

void UIFTacticalCommandComponent::CleanupOrderMarkerVFX()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OrderMarkerTimerHandle);
	}

	if (UParticleSystemComponent* const PSC = ActiveOrderMarkerPSC.Get())
	{
		PSC->DeactivateSystem();
		PSC->DestroyComponent();
	}
	ActiveOrderMarkerPSC.Reset();
}

bool UIFTacticalCommandComponent::IsSelectModifierDown() const
{
	if (bIsSelectModifierHeld)
	{
		return true;
	}

	const AIFPlayerController* const PC = GetOwningPlayerController();
	return IsValid(PC) && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
}

float UIFTacticalCommandComponent::ComputeDilatedBlendTime(float DesiredRealSeconds, float StartDilation, float EndDilation) const
{
	if (DesiredRealSeconds <= 0.f)
	{
		return 0.f;
	}

	const float RampPortion = FMath::Min(DesiredRealSeconds, TimeDilationRampSeconds);
	const float PostRampPortion = FMath::Max(0.f, DesiredRealSeconds - TimeDilationRampSeconds);
	const float AvgRampDilation = 0.5f * (StartDilation + EndDilation);
	return FMath::Max(0.01f, (RampPortion * AvgRampDilation) + (PostRampPortion * EndDilation));
}

AIFPlayerController* UIFTacticalCommandComponent::GetOwningPlayerController() const
{
	return Cast<AIFPlayerController>(GetOwner());
}

AIFTacticalHUD* UIFTacticalCommandComponent::GetTacticalHUD() const
{
	const AIFPlayerController* const PC = GetOwningPlayerController();
	return IsValid(PC) ? Cast<AIFTacticalHUD>(PC->GetHUD()) : nullptr;
}

AActor* UIFTacticalCommandComponent::GetTacticalCameraRigActor() const
{
	return TacticalCameraRig;
}

UIFBuildingComponent* UIFTacticalCommandComponent::GetBuildingComponent() const
{
	const AIFPlayerController* const PC = GetOwningPlayerController();
	return IsValid(PC) ? PC->GetBuildingComponent() : nullptr;
}

AIFStructure* UIFTacticalCommandComponent::FindBestStructureUnderCursor(
	const FVector& RayOrigin,
	const FVector& RayDir,
	float MaxDistance,
	const APawn* PlayerPawn,
	bool bHostileOnly) const
{
	const UWorld* const World = GetWorld();
	if (!IsValid(World) || !IsValid(PlayerPawn))
	{
		return nullptr;
	}

	FCollisionObjectQueryParams StructObjectParams;
	StructObjectParams.AddObjectTypesToQuery(IFCollision::Damageable);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFTacticalStructureTrace), false);
	if (IsValid(TacticalCameraRig))
	{
		QueryParams.AddIgnoredActor(TacticalCameraRig);
	}

	auto PickBestMatch = [PlayerPawn, bHostileOnly](const TArray<FHitResult>& Hits) -> AIFStructure*
	{
		AIFStructure* FirstMatch = nullptr;
		for (const FHitResult& Hit : Hits)
		{
			AIFStructure* const Candidate = Cast<AIFStructure>(Hit.GetActor());
			if (!IsValid(Candidate) || !Candidate->IsStructureAlive())
			{
				continue;
			}

			const bool bMatchesAttitude = bHostileOnly
				? IFTeamUtils::AreHostile(PlayerPawn, Candidate)
				: IFTeamUtils::AreFriendly(PlayerPawn, Candidate);
			if (bMatchesAttitude)
			{
				if (Cast<AIFGateDoor>(Candidate))
				{
					return Candidate;
				}
				if (!IsValid(FirstMatch))
				{
					FirstMatch = Candidate;
				}
			}
		}
		return FirstMatch;
	};

	const FVector TraceEnd = RayOrigin + RayDir * MaxDistance;
	TArray<FHitResult> DirectHits;
	if (World->LineTraceMultiByObjectType(DirectHits, RayOrigin, TraceEnd, StructObjectParams, QueryParams))
	{
		if (AIFStructure* const DirectMatch = PickBestMatch(DirectHits))
		{
			return DirectMatch;
		}
	}

	// Forgiving 35 cm sphere sweep catches narrow towers viewed obliquely from overhead RTS angles.
	TArray<FHitResult> SweepHits;
	if (World->SweepMultiByObjectType(SweepHits, RayOrigin, TraceEnd, FQuat::Identity, StructObjectParams, FCollisionShape::MakeSphere(35.f), QueryParams))
	{
		return PickBestMatch(SweepHits);
	}

	return nullptr;
}

void UIFTacticalCommandComponent::SetHighlightedHostileStructure(AIFStructure* HostileStructure)
{
	if (HighlightedHostileStructure.Get() == HostileStructure)
	{
		if (IsValid(HostileStructure) && HostileStructure->IsStructureAlive())
		{
			HostileStructure->SetHostileTargetHighlighted(true);
		}
		return;
	}

	ClearHighlightedHostileStructure();

	if (!IsValid(HostileStructure) || !HostileStructure->IsStructureAlive())
	{
		return;
	}

	HighlightedHostileStructure = HostileStructure;
	HostileStructure->OnStructureDestroyed.AddUniqueDynamic(this, &UIFTacticalCommandComponent::HandleHighlightedStructureDestroyed);
	HostileStructure->SetHostileTargetHighlighted(true);
}

void UIFTacticalCommandComponent::ClearHighlightedHostileStructure()
{
	if (AIFStructure* const Structure = HighlightedHostileStructure.Get())
	{
		Structure->OnStructureDestroyed.RemoveDynamic(this, &UIFTacticalCommandComponent::HandleHighlightedStructureDestroyed);
		Structure->SetHostileTargetHighlighted(false);
	}
	HighlightedHostileStructure.Reset();
}

void UIFTacticalCommandComponent::HandleHighlightedStructureDestroyed(AIFStructure* DestroyedStructure, AActor*)
{
	if (HighlightedHostileStructure.Get() == DestroyedStructure)
	{
		ClearHighlightedHostileStructure();
	}
}
