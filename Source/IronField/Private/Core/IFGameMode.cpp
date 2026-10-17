#include "Core/IFGameMode.h"

#include "Building/IFStronghold.h"
#include "Character/IFPlayerCharacter.h"
#include "Core/IFBestRunSave.h"
#include "Core/IFGameInstance.h"
#include "Core/IFLog.h"
#include "Core/IFPlayerController.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Wave/IFWaveManager.h"

AIFGameMode::AIFGameMode()
{
	// C++ supplies the safe base-class defaults; BP_GameMode overrides these with the real
	// Blueprint assets (Player Controller Class / Default Pawn Class) in its Class Defaults.
	PlayerControllerClass = AIFPlayerController::StaticClass();
	DefaultPawnClass = AIFPlayerCharacter::StaticClass();
}

void AIFGameMode::BeginPlay()
{
	Super::BeginPlay();

	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
	{
		if (AIFWaveManager* const WaveManager = Registry->GetActor<AIFWaveManager>(IFRegistryRoles::WaveManager))
		{
			HandleWaveManagerRegistered(WaveManager);
		}

		if (AIFStronghold* const Stronghold = Registry->GetActor<AIFStronghold>(IFRegistryRoles::Stronghold))
		{
			HandleStrongholdRegistered(Stronghold);
		}

		if (!BoundWaveManager || !BoundStronghold)
		{
			Registry->OnActorRegistered.AddDynamic(this, &AIFGameMode::HandleActorRegistered);
		}
	}
}

void AIFGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UWorld* const World = GetWorld();
	if (World)
	{
		if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			Registry->OnActorRegistered.RemoveDynamic(this, &AIFGameMode::HandleActorRegistered);
		}
	}

	if (BoundWaveManager)
	{
		BoundWaveManager->OnAllWavesCompleted.RemoveDynamic(this, &AIFGameMode::HandleAllWavesCompleted);
		BoundWaveManager = nullptr;
	}

	if (BoundStronghold)
	{
		BoundStronghold->OnStrongholdDestroyed.RemoveDynamic(this, &AIFGameMode::HandleStrongholdDestroyed);
		BoundStronghold = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

void AIFGameMode::HandleActorRegistered(FName ActorRole, AActor* Actor)
{
	if (ActorRole == IFRegistryRoles::WaveManager)
	{
		HandleWaveManagerRegistered(Cast<AIFWaveManager>(Actor));
	}
	else if (ActorRole == IFRegistryRoles::Stronghold)
	{
		HandleStrongholdRegistered(Cast<AIFStronghold>(Actor));
	}
}

void AIFGameMode::HandleWaveManagerRegistered(AIFWaveManager* WaveManager)
{
	if (!WaveManager || BoundWaveManager)
	{
		return;
	}

	BoundWaveManager = WaveManager;
	WaveManager->OnAllWavesCompleted.AddDynamic(this, &AIFGameMode::HandleAllWavesCompleted);
}

void AIFGameMode::HandleStrongholdRegistered(AIFStronghold* Stronghold)
{
	if (!Stronghold || BoundStronghold)
	{
		return;
	}

	BoundStronghold = Stronghold;
	Stronghold->OnStrongholdDestroyed.AddDynamic(this, &AIFGameMode::HandleStrongholdDestroyed);
}

void AIFGameMode::HandleAllWavesCompleted()
{
	UE_LOG(LogIronField, Log, TEXT("[IF-GameMode] All waves completed."));
	ShowGameOver(EIFGameResult::Victory);
}

void AIFGameMode::HandleStrongholdDestroyed(AIFStronghold*)
{
	UE_LOG(LogIronField, Log, TEXT("[IF-GameMode] Stronghold destroyed."));
	ShowGameOver(EIFGameResult::Defeat);
}

void AIFGameMode::ShowGameOver(EIFGameResult Result)
{
	UWorld* const World = GetWorld();
	if (!World)
	{
		return;
	}

	SaveBestRun(Result);

	AIFPlayerController* const PlayerController = Cast<AIFPlayerController>(World->GetFirstPlayerController());
	if (!PlayerController)
	{
		return;
	}

	PlayerController->ShowGameOverScreen(Result);
}

void AIFGameMode::SaveBestRun(EIFGameResult Result)
{
	if (!BoundWaveManager)
	{
		return;
	}

	UIFGameInstance* const GameInstance = Cast<UIFGameInstance>(GetGameInstance());
	if (!GameInstance)
	{
		return;
	}

	const int32 WaveNumber = FMath::Max(1, BoundWaveManager->GetCurrentWave());
	const int32 KillCount = BoundWaveManager->GetKillCount();
	const EIFRunMode RunMode = GameInstance->GetRunMode();
	const FString SlotName = UIFBestRunSave::GetSlotName();

	UIFBestRunSave* Save = Cast<UIFBestRunSave>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	if (!Save)
	{
		Save = NewObject<UIFBestRunSave>(this);
	}
	if (!Save)
	{
		return;
	}

	// Higher wave wins; kills break ties so late pushes still record.
	if (RunMode == EIFRunMode::Normal)
	{
		if (WaveNumber > Save->BestNormalWave || (WaveNumber == Save->BestNormalWave && KillCount > Save->BestNormalKills))
		{
			Save->BestNormalWave = WaveNumber;
			Save->BestNormalKills = KillCount;
		}
		if (Result == EIFGameResult::Victory)
		{
			Save->bNormalCleared = true;
		}
	}
	else
	{
		if (WaveNumber > Save->BestUnlimitedWave || (WaveNumber == Save->BestUnlimitedWave && KillCount > Save->BestUnlimitedKills))
		{
			Save->BestUnlimitedWave = WaveNumber;
			Save->BestUnlimitedKills = KillCount;
		}
	}

	UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
}
