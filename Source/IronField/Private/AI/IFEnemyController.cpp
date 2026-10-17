#include "AI/IFEnemyController.h"

#include "AI/IFEnemyAIData.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "Character/IFEnemyCharacter.h"
#include "Combat/IFCombatComponent.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Core/IFLog.h"
#include "Engine/World.h"
#include "Stats/IFHealthComponent.h"
#include "Wave/IFWaveManager.h"

namespace
{
	constexpr float FallbackAggressionSpread = 0.35f;
}

const UIFEnemyAIData* AIFEnemyController::GetAIData() const
{
	return AIData ? AIData.Get() : GetDefault<UIFEnemyAIData>();
}

bool AIFEnemyController::IsReadyForNewAttack() const
{
	if (LastAttackEndedTime < 0.f)
	{
		return true;
	}

	const UWorld* const World = GetWorld();
	if (!World)
	{
		return true;
	}

	return (World->GetTimeSeconds() - LastAttackEndedTime) >= CurrentReattackCooldownSeconds;
}

void AIFEnemyController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (const IGenericTeamAgentInterface* const TeamAgent = Cast<IGenericTeamAgentInterface>(InPawn))
	{
		SetGenericTeamId(TeamAgent->GetGenericTeamId());
	}
	InitializeControlledPawn();
}

void AIFEnemyController::InitializeControlledPawn()
{
	if (!AIData)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-AI] %s has no AIData assigned — using UIFEnemyAIData CDO defaults."), *GetName());
	}

	if (BehaviorTreeAsset)
	{
		RunBehaviorTree(BehaviorTreeAsset);
	}
	else
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-AI] %s has no BehaviorTreeAsset; enemy will idle but still count alive for the wave."), *GetName());
	}

	if (const UWorld* const World = GetWorld())
	{
		if (UIFActorRegistrySubsystem* const Subsystem = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			CachedWaveManager = Subsystem->GetActor<AIFWaveManager>(IFRegistryRoles::WaveManager);
			if (!CachedWaveManager)
			{
				Subsystem->OnActorRegistered.AddDynamic(this, &AIFEnemyController::HandleActorRegistered);
			}
		}
	}

	if (!CachedWaveManager)
	{
		UE_LOG(LogIronField, Log, TEXT("[IF-AI] %s possessed before WaveManager registered; subscribed to OnActorRegistered."), *GetName());
	}

	if (CachedWaveManager)
	{
		CachedWaveManager->OnPlayerDowned.AddDynamic(this, &AIFEnemyController::HandlePlayerDowned);
	}

	RollPawnAggression();
	BindOwnDelegates();
}

void AIFEnemyController::RollPawnAggression()
{
	// Roll this enemy's personality: wave bias shifts the population mean
	// (e.g. wave 1 sieges), spread keeps packs mixed.
	if (AIFEnemyCharacter* const EnemyChar = Cast<AIFEnemyCharacter>(GetPawn()))
	{
		const UIFEnemyAIData* const Data = GetAIData();
		const float Spread = Data ? Data->AggressionSpread : FallbackAggressionSpread;
		const float Bias = CachedWaveManager ? CachedWaveManager->GetSiegeBiasForWave() : 0.f;
		EnemyChar->RollAggression(Bias, Spread);
	}
}

void AIFEnemyController::OnUnPossess()
{
	UnbindOwnDelegates();
	CachedCombatComponent = nullptr;
	CachedWaveManager = nullptr;
	Super::OnUnPossess();
}

void AIFEnemyController::BindOwnDelegates()
{
	const APawn* const ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	CachedCombatComponent = ControlledPawn->FindComponentByClass<UIFCombatComponent>();
	if (UIFCombatComponent* const Combat = CachedCombatComponent)
	{
		Combat->OnCombatStateChanged.AddDynamic(this, &AIFEnemyController::HandleOwnCombatStateChanged);
	}

	CachedHealthComponent = ControlledPawn->FindComponentByClass<UIFHealthComponent>();
	if (UIFHealthComponent* const Health = CachedHealthComponent)
	{
		Health->OnHealthDepleted.AddDynamic(this, &AIFEnemyController::HandleOwnHealthDepleted);
	}
}

void AIFEnemyController::UnbindOwnDelegates()
{
	if (UIFCombatComponent* const Combat = GetControlledCombatComponent())
	{
		Combat->OnCombatStateChanged.RemoveAll(this);
	}

	if (UIFHealthComponent* const Health = CachedHealthComponent)
	{
		Health->OnHealthDepleted.RemoveAll(this);
	}
	CachedHealthComponent = nullptr;

	if (CachedWaveManager)
	{
		CachedWaveManager->OnPlayerDowned.RemoveAll(this);
	}

	if (const UWorld* const World = GetWorld())
	{
		if (UIFActorRegistrySubsystem* const Subsystem = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			Subsystem->OnActorRegistered.RemoveAll(this);
		}
	}
}

void AIFEnemyController::HandleActorRegistered(FName ActorRole, AActor* Actor)
{
	if (ActorRole != IFRegistryRoles::WaveManager)
	{
		return;
	}

	AIFWaveManager* const InWaveManager = Cast<AIFWaveManager>(Actor);
	if (!InWaveManager || CachedWaveManager == InWaveManager)
	{
		return;
	}

	CachedWaveManager = InWaveManager;
	CachedWaveManager->OnPlayerDowned.AddDynamic(this, &AIFEnemyController::HandlePlayerDowned);

	RollPawnAggression();
}

void AIFEnemyController::HandleOwnCombatStateChanged(ECombatState PreviousState, ECombatState NewState)
{
	if (PreviousState == ECombatState::Attacking && NewState == ECombatState::Idle)
	{
		if (const UWorld* const World = GetWorld())
		{
			LastAttackEndedTime = World->GetTimeSeconds();
			const UIFEnemyAIData* const Data = GetAIData();
			if (!Data)
			{
				return;
			}
			const float MinCooldown = FMath::Min(Data->MinReattackCooldownSeconds, Data->MaxReattackCooldownSeconds);
			const float MaxCooldown = FMath::Max(Data->MinReattackCooldownSeconds, Data->MaxReattackCooldownSeconds);
			CurrentReattackCooldownSeconds = FMath::RandRange(MinCooldown, MaxCooldown);
		}
	}

	ApplyMovementSpeedForState(NewState);
}

void AIFEnemyController::HandleOwnHealthDepleted()
{
	ClearFocus(EAIFocusPriority::Gameplay);
	ClearFocus(EAIFocusPriority::Default);

	if (UBrainComponent* const Brain = GetBrainComponent())
	{
		Brain->StopLogic(TEXT("Enemy died"));
	}

	if (APawn* const ControlledPawn = GetPawn())
	{
		ControlledPawn->DetachFromControllerPendingDestroy();
	}
}

void AIFEnemyController::HandlePlayerDowned()
{
	UBlackboardComponent* const BB = GetBlackboardComponent();
	if (!BB || !CachedWaveManager)
	{
		return;
	}

	if (Cast<AActor>(BB->GetValueAsObject(TargetActorKeyName)) == CachedWaveManager->GetPlayerActor())
	{
		BB->SetValueAsObject(TargetActorKeyName, nullptr);
	}
}

void AIFEnemyController::ApplyMovementSpeedForState(ECombatState State)
{
	if (AIFEnemyCharacter* const EnemyChar = Cast<AIFEnemyCharacter>(GetPawn()))
	{
		EnemyChar->ApplyMovementSpeedForState(State);
	}
}
