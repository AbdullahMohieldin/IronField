#include "Tactical/IFTacticalCommandComponent.h"

#include "Castle/IFBuildingComponent.h"
#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFPlayerCharacter.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerController.h"
#include "Engine/AssetManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Tactical/IFFormationSolver.h"
#include "Tactical/IFTacticalCameraRig.h"
#include "Tactical/IFTacticalCursorQuery.h"
#include "Tactical/IFTacticalOrderDispatcher.h"
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
	bCommandHandledOnPress = false;
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

	// Raw Tab acts only as fallback before IA_ToggleTacticalMode is assigned so it never double-binds.
	if (ToggleTacticalModeAction.IsNull())
	{
		PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &UIFTacticalCommandComponent::ToggleTacticalMode);
	}
	if (RotateBuildAction.IsNull())
	{
		PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &UIFTacticalCommandComponent::HandleRotateBuildInput);
	}
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
	// Retain TacticalAssetsLoadHandle until EndPlay so soft-referenced tactical inputs, meshes, and VFX stay resident.
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
	bCommandHandledOnPress = false;

	PlayerChar->EnterTacticalStandby();
	AddTacticalMappingContext();
	PC->ApplyTacticalInputMode();

	const float StartDilation = UGameplayStatics::GetGlobalTimeDilation(this);
	const float TargetDilation = FMath::Clamp(TacticalTimeDilation, 0.1f, 1.f);
	Rig->ActivateTacticalCamera(PlayerChar, PC->GetControlRotation().Yaw, TargetDilation, TimeDilationRampSeconds);
	SyncOrderMarkerTimeDilation(TargetDilation);

	const float CompensatedBlendIn = ComputeDilatedBlendTime(CameraBlendInSeconds, StartDilation, TargetDilation);
	PC->SetViewTargetWithBlend(Rig, CompensatedBlendIn, EViewTargetBlendFunction::VTBlend_Cubic);

	PruneInvalidSoldiers();
	SelectionState.SetSelectedSoldierRingsVisible(true);

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
	bCommandHandledOnPress = false;
	if (UIFBuildingComponent* const Building = GetBuildingComponent())
	{
		Building->CancelBuildPlacement();
		Building->ClearSelectedStructure();
	}
	CancelMarqueeSelection();
	CancelFormationDragPreview();
	ClearHighlightedHostileTarget();
	ClearHighlightedHostileStructure();
	SelectionState.SetSelectedSoldierRingsVisible(false);

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
	SyncOrderMarkerTimeDilation(1.f);

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
	SelectionState.CancelMarquee();
	UpdateHUDMarqueeRect(false);
}

void UIFTacticalCommandComponent::CancelFormationDragPreview()
{
	bIsCommandPressed = false;
	bIsFormationDragging = false;
	bHasPreviewCache = false;
	LastPreviewUnitCount = 0;
	LastPreviewUpdateRealTime = -1.0;
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
	if (SelectionState.ClearSelection([this](AIFSoldierCharacter* Soldier) { UnbindSoldierDeath(Soldier); }))
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
	return SelectionState.GetSelectedSoldierCount();
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

	SelectionState.BeginMarqueePress(FVector2D(MouseX, MouseY));
	UpdateHUDMarqueeRect(false);
}

void UIFTacticalCommandComponent::HandleSelectTriggered()
{
	if (!bIsTacticalModeActive || !SelectionState.IsSelectPressed())
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

	if (SelectionState.UpdateMarqueeDrag(FVector2D(MouseX, MouseY), MarqueeDragThresholdPixels))
	{
		UpdateHUDMarqueeRect(true);
	}
}

void UIFTacticalCommandComponent::HandleSelectCompleted()
{
	if (!bIsTacticalModeActive || !SelectionState.IsSelectPressed())
	{
		CancelMarqueeSelection();
		return;
	}

	const AIFPlayerController* const PC = GetOwningPlayerController();
	float MouseX = SelectionState.GetMarqueeCurrentScreenPos().X;
	float MouseY = SelectionState.GetMarqueeCurrentScreenPos().Y;
	if (IsValid(PC))
	{
		PC->GetMousePosition(MouseX, MouseY);
	}

	const bool bWasMarquee = SelectionState.IsMarqueeDragging();
	const FVector2D StartPos = SelectionState.GetMarqueeStartScreenPos();
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

	AActor* const HitActor = FIFTacticalCursorQuery::FindBestPawnUnderCursor(
		GetWorld(),
		RayOrigin,
		RayDir,
		CommandTraceDistance,
		PawnClickToleranceRadius,
		TacticalCameraRig,
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
			if (SelectionState.ToggleSoldierInSelection(
				HitSoldier,
				bIsTacticalModeActive,
				[this](AIFSoldierCharacter* S) { BindSoldierDeath(S); },
				[this](AIFSoldierCharacter* S) { UnbindSoldierDeath(S); }))
			{
				BroadcastSelectionCount();
			}
		}
		else
		{
			SelectionState.ReplaceSelection(
				{ HitSoldier },
				bIsTacticalModeActive,
				[this](AIFSoldierCharacter* S) { BindSoldierDeath(S); },
				[this](AIFSoldierCharacter* S) { UnbindSoldierDeath(S); });
			BroadcastSelectionCount();
		}
	}
	else if (!bShiftDown)
	{
		ClearSelection();
		if (UIFBuildingComponent* const Building = GetBuildingComponent())
		{
			if (AIFStructure* const FriendlyStruct = FIFTacticalCursorQuery::FindBestStructureUnderCursor(
				GetWorld(), RayOrigin, RayDir, CommandTraceDistance, PlayerPawn, TacticalCameraRig, false))
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

	TArray<AIFSoldierCharacter*> BoxedSoldiers;
	FIFTacticalSelectionState::GatherSoldiersInMarqueeBox(World, PC, PlayerPawn, StartPos, EndPos, BoxedSoldiers);

	const bool bShiftDown = IsSelectModifierDown();
	if (!BoxedSoldiers.IsEmpty() || !bShiftDown)
	{
		if (UIFBuildingComponent* const Building = GetBuildingComponent())
		{
			Building->ClearSelectedStructure();
		}
	}

	if (bShiftDown)
	{
		if (SelectionState.AddToSelection(
			BoxedSoldiers,
			bIsTacticalModeActive,
			[this](AIFSoldierCharacter* S) { BindSoldierDeath(S); }))
		{
			BroadcastSelectionCount();
		}
	}
	else
	{
		SelectionState.ReplaceSelection(
			BoxedSoldiers,
			bIsTacticalModeActive,
			[this](AIFSoldierCharacter* S) { BindSoldierDeath(S); },
			[this](AIFSoldierCharacter* S) { UnbindSoldierDeath(S); });
		BroadcastSelectionCount();
	}
}

void UIFTacticalCommandComponent::SetHighlightedHostileTarget(AIFSoldierCharacter* HostileSoldier)
{
	SelectionState.SetHighlightedHostileTarget(
		HostileSoldier,
		[this](AIFSoldierCharacter* H) { BindHostileDeath(H); },
		[this](AIFSoldierCharacter* H) { UnbindHostileDeath(H); });
}

void UIFTacticalCommandComponent::ClearHighlightedHostileTarget()
{
	SelectionState.ClearHighlightedHostileTarget([this](AIFSoldierCharacter* H) { UnbindHostileDeath(H); });
}

void UIFTacticalCommandComponent::SetHighlightedHostileStructure(AIFStructure* HostileStructure)
{
	SelectionState.SetHighlightedHostileStructure(
		HostileStructure,
		[this](AIFStructure* S) { BindStructureDestroyed(S); },
		[this](AIFStructure* S) { UnbindStructureDestroyed(S); });
}

void UIFTacticalCommandComponent::ClearHighlightedHostileStructure()
{
	SelectionState.ClearHighlightedHostileStructure([this](AIFStructure* S) { UnbindStructureDestroyed(S); });
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

void UIFTacticalCommandComponent::BindHostileDeath(AIFSoldierCharacter* Hostile)
{
	if (IsValid(Hostile))
	{
		Hostile->OnCharacterDied.AddUniqueDynamic(this, &UIFTacticalCommandComponent::HandleHighlightedHostileDied);
	}
}

void UIFTacticalCommandComponent::UnbindHostileDeath(AIFSoldierCharacter* Hostile)
{
	if (IsValid(Hostile))
	{
		Hostile->OnCharacterDied.RemoveDynamic(this, &UIFTacticalCommandComponent::HandleHighlightedHostileDied);
	}
}

void UIFTacticalCommandComponent::BindStructureDestroyed(AIFStructure* Structure)
{
	if (IsValid(Structure))
	{
		Structure->OnStructureDestroyed.AddUniqueDynamic(this, &UIFTacticalCommandComponent::HandleHighlightedStructureDestroyed);
	}
}

void UIFTacticalCommandComponent::UnbindStructureDestroyed(AIFStructure* Structure)
{
	if (IsValid(Structure))
	{
		Structure->OnStructureDestroyed.RemoveDynamic(this, &UIFTacticalCommandComponent::HandleHighlightedStructureDestroyed);
	}
}

void UIFTacticalCommandComponent::HandleSelectedSoldierDied(AIFBaseCharacter* DeadCharacter)
{
	AIFSoldierCharacter* const DeadSoldier = Cast<AIFSoldierCharacter>(DeadCharacter);
	if (SelectionState.HandleSelectedSoldierDied(DeadSoldier, [this](AIFSoldierCharacter* S) { UnbindSoldierDeath(S); }))
	{
		BroadcastSelectionCount();
	}
}

void UIFTacticalCommandComponent::HandleHighlightedHostileDied(AIFBaseCharacter* DeadCharacter)
{
	if (SelectionState.GetHighlightedHostileTarget() == DeadCharacter)
	{
		ClearHighlightedHostileTarget();
	}
}

void UIFTacticalCommandComponent::HandleHighlightedStructureDestroyed(AIFStructure* DestroyedStructure, AActor*)
{
	if (SelectionState.GetHighlightedHostileStructure() == DestroyedStructure)
	{
		ClearHighlightedHostileStructure();
	}
}

void UIFTacticalCommandComponent::PruneInvalidSoldiers()
{
	if (SelectionState.PruneInvalidSoldiers([this](AIFSoldierCharacter* S) { UnbindSoldierDeath(S); }))
	{
		BroadcastSelectionCount();
	}
}

void UIFTacticalCommandComponent::UpdateHUDMarqueeRect(bool bActive)
{
	if (AIFTacticalHUD* const HUD = GetTacticalHUD())
	{
		HUD->SetMarqueeSelectionRect(
			bActive,
			SelectionState.GetMarqueeStartScreenPos(),
			SelectionState.GetMarqueeCurrentScreenPos());
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

	bCommandHandledOnPress = false;

	if (UIFBuildingComponent* const Building = GetBuildingComponent(); IsValid(Building) && Building->IsInBuildPlacementMode())
	{
		Building->CancelBuildPlacement();
		bCommandHandledOnPress = true;
		return;
	}

	PruneInvalidSoldiers();
	if (SelectionState.IsSelectionEmpty())
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

	AActor* const TargetActor = FIFTacticalCursorQuery::FindBestPawnUnderCursor(
		GetWorld(),
		RayOrigin,
		RayDir,
		CommandTraceDistance,
		PawnClickToleranceRadius,
		TacticalCameraRig,
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

		TArray<AIFSoldierCharacter*> AliveSquad;
		TArray<FVector> AliveLocations;
		FVector UnusedCentroid = FVector::ZeroVector;
		SelectionState.GatherAliveSelectedSquad(AliveSquad, AliveLocations, UnusedCentroid);

		if (TargetActor == PlayerPawn)
		{
			ClearHighlightedHostileTarget();
			FIFTacticalOrderDispatcher::DispatchFollowOrder(AliveSquad, PlayerPawn, FollowOrderRadius);

			const FVector MarkerLoc = PlayerPawn->GetActorLocation();
			SpawnOrderMarkerVFX(EIFTacticalOrderType::FollowCommander, MarkerLoc, PlayerPawn);
			OnOrderIssued.Broadcast(EIFTacticalOrderType::FollowCommander, MarkerLoc);
			return;
		}

		FIFTacticalOrderDispatcher::DispatchClusterAttackOrder(
			GetWorld(),
			PlayerPawn,
			AliveSquad,
			AliveLocations,
			TargetActor,
			AttackOrderClusterRadius,
			AttackOrderTargetSpreadPenalty);

		SetHighlightedHostileTarget(Cast<AIFSoldierCharacter>(TargetActor));
		const FVector MarkerLoc = TargetActor->GetActorLocation();
		SpawnOrderMarkerVFX(EIFTacticalOrderType::AttackTarget, MarkerLoc, TargetActor);
		OnOrderIssued.Broadcast(EIFTacticalOrderType::AttackTarget, MarkerLoc);
		return;
	}

	if (AIFStructure* const HostileStructure = FIFTacticalCursorQuery::FindBestStructureUnderCursor(
		GetWorld(), RayOrigin, RayDir, CommandTraceDistance, PlayerPawn, TacticalCameraRig, true))
	{
		CancelFormationDragPreview();
		ClearHighlightedHostileTarget();
		bCommandHandledOnPress = true;

		TArray<AIFSoldierCharacter*> AliveSquad;
		TArray<FVector> AliveLocations;
		FVector UnusedCentroid = FVector::ZeroVector;
		SelectionState.GatherAliveSelectedSquad(AliveSquad, AliveLocations, UnusedCentroid);

		FIFTacticalOrderDispatcher::DispatchStructureAttackOrder(AliveSquad, HostileStructure);
		SetHighlightedHostileStructure(HostileStructure);

		const FVector MarkerLoc = HostileStructure->GetActorLocation();
		SpawnOrderMarkerVFX(EIFTacticalOrderType::AttackTarget, MarkerLoc);
		OnOrderIssued.Broadcast(EIFTacticalOrderType::AttackTarget, MarkerLoc);
		return;
	}

	FHitResult GroundHit;
	if (FIFTacticalCursorQuery::TraceGroundUnderCursor(
		GetWorld(), RayOrigin, RayDir, CommandTraceDistance, TacticalCameraRig, GroundHit, &GroundTraceScratchHits))
	{
		bIsCommandPressed = true;
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
	if (!FIFTacticalCursorQuery::TraceGroundUnderCursor(
		GetWorld(), RayOrigin, RayDir, CommandTraceDistance, TacticalCameraRig, GroundHit, &GroundTraceScratchHits))
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
	SelectionState.GatherAliveSelectedSquad(ActiveSquad, SoldierLocations, SquadCentroid);

	if (ActiveSquad.IsEmpty())
	{
		CancelFormationDragPreview();
		return;
	}

	const double NowRealTime = FPlatformTime::Seconds();
	const bool bSameFormationAndCount = bHasPreviewCache
		&& LastPreviewFormationType == ActiveFormationType
		&& LastPreviewUnitCount == ActiveSquad.Num();
	if (bSameFormationAndCount
		&& (FVector::DistSquared2D(CurrentGroundPoint, LastPreviewGroundPoint) < FMath::Square(8.f)
			|| (NowRealTime - LastPreviewUpdateRealTime) < 0.05))
	{
		return;
	}

	LastPreviewGroundPoint = CurrentGroundPoint;
	LastPreviewFormationType = ActiveFormationType;
	LastPreviewUnitCount = ActiveSquad.Num();
	LastPreviewUpdateRealTime = NowRealTime;
	bHasPreviewCache = true;

	FIFFormationSolveParams Params;
	Params.FormationType = ActiveFormationType;
	Params.UnitCount = ActiveSquad.Num();
	Params.AnchorPoint = FormationDragStartPoint;
	Params.DragEndPoint = CurrentGroundPoint;
	Params.bIsDrag = true;
	Params.bAllowPathfindingFallback = false;
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
	if (!FIFFormationSolver::SolveFormation(World, Params, PreviewFormationSlots, FormationCenter))
	{
		return;
	}

	if (UInstancedStaticMeshComponent* const PreviewISM = EnsureFormationPreviewComponent())
	{
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
	SelectionState.GatherAliveSelectedSquad(ActiveSquad, SoldierLocations, SquadCentroid);

	if (ActiveSquad.IsEmpty())
	{
		return;
	}

	FIFTacticalFormationOrderConfig Config;
	Config.FormationType = ActiveFormationType;
	Config.AnchorPoint = AnchorPoint;
	Config.DragEndPoint = DragEndPoint;
	Config.bIsDrag = bIsDrag;
	Config.CameraForward = ResolvePreviewCameraForward();
	Config.LateralSpacing = FormationLateralSpacing;
	Config.RankSpacing = FormationRankSpacing;
	Config.LineAspectRatio = FormationLineAspectRatio;
	Config.MinLineColumns = FormationMinLineColumns;
	Config.MaxLineColumns = FormationMaxLineColumns;
	Config.MinShapeRadius = FormationMinShapeRadius;
	Config.MaxShapeRadius = FormationMaxShapeRadius;
	Config.MoveOrderAcceptanceRadius = MoveOrderAcceptanceRadius;

	FVector FormationCenter = AnchorPoint;
	if (!FIFTacticalOrderDispatcher::DispatchFormationMoveOrder(
		World, ActiveSquad, SoldierLocations, SquadCentroid, Config, FormationCenter))
	{
		return;
	}

	SpawnOrderMarkerVFX(EIFTacticalOrderType::MoveTo, FormationCenter + FVector(0.f, 0.f, 5.f));
	OnOrderIssued.Broadcast(EIFTacticalOrderType::MoveTo, FormationCenter);
}

void UIFTacticalCommandComponent::SpawnOrderMarkerVFX(EIFTacticalOrderType OrderType, const FVector& Location, AActor* AttachTarget)
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

	UParticleSystemComponent* SpawnedPSC = nullptr;
	if (IsValid(AttachTarget) && IsValid(AttachTarget->GetRootComponent()))
	{
		float OverheadZ = 115.f + OrderMarkerOverheadOffsetCm;
		if (const ACharacter* const TargetChar = Cast<ACharacter>(AttachTarget))
		{
			if (const UCapsuleComponent* const Capsule = TargetChar->GetCapsuleComponent())
			{
				OverheadZ = Capsule->GetScaledCapsuleHalfHeight() + OrderMarkerOverheadOffsetCm;
			}
		}

		SpawnedPSC = UGameplayStatics::SpawnEmitterAttached(
			ParticleAsset,
			AttachTarget->GetRootComponent(),
			NAME_None,
			FVector(0.f, 0.f, OverheadZ),
			FRotator::ZeroRotator,
			FVector(OrderMarkerScale),
			EAttachLocation::KeepRelativeOffset,
			true,
			EPSCPoolMethod::None,
			true);

		if (IsValid(SpawnedPSC))
		{
			// Keep world orientation fixed so the attached marker tracks overhead without spinning as the actor turns.
			SpawnedPSC->SetUsingAbsoluteRotation(true);
			SpawnedPSC->SetWorldRotation(FRotator::ZeroRotator);
		}
	}
	else
	{
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

		SpawnedPSC = UGameplayStatics::SpawnEmitterAtLocation(
			World,
			ParticleAsset,
			SpawnLocation,
			FRotator::ZeroRotator,
			FVector(OrderMarkerScale),
			true,
			EPSCPoolMethod::None,
			true);
	}

	if (IsValid(SpawnedPSC))
	{
		ActiveOrderMarkerPSC = SpawnedPSC;
		OrderMarkerExpireRealTime = FPlatformTime::Seconds() + static_cast<double>(OrderMarkerDurationSeconds);
		SyncOrderMarkerTimeDilation(UGameplayStatics::GetGlobalTimeDilation(this));
	}
}

void UIFTacticalCommandComponent::SyncOrderMarkerTimeDilation(float EffectiveWorldDilation)
{
	UParticleSystemComponent* const PSC = ActiveOrderMarkerPSC.Get();
	UWorld* const World = GetWorld();
	if (!IsValid(PSC) || !IsValid(World))
	{
		return;
	}

	const double RemainingRealSeconds = OrderMarkerExpireRealTime - FPlatformTime::Seconds();
	if (RemainingRealSeconds <= 0.0)
	{
		CleanupOrderMarkerVFX();
		return;
	}

	const float ClampedDilation = FMath::Max(EffectiveWorldDilation, 0.05f);
	PSC->CustomTimeDilation = 1.f / ClampedDilation;
	const float TimerDelay = FMath::Max(0.05f, static_cast<float>(RemainingRealSeconds) * ClampedDilation);
	World->GetTimerManager().SetTimer(
		OrderMarkerTimerHandle,
		this,
		&UIFTacticalCommandComponent::CleanupOrderMarkerVFX,
		TimerDelay,
		false);
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
	OrderMarkerExpireRealTime = 0.0;
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
