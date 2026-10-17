#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFPlayerCharacter.h"
#include "Combat/IFPlayerCombatComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Pickup/IFHealPickup.h"
#include "Stats/IFHealthComponent.h"
#include "Wave/IFWaveManager.h"

// F3: Block stamina depletion
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFBlockStaminaDepletionTest, "IronField.Combat.BlockStaminaDepletion", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFBlockStaminaDepletionTest::RunTest(const FString& Parameters)
{
	UIFPlayerCombatComponent* const Combat = NewObject<UIFPlayerCombatComponent>();
	TestNotNull(TEXT("Combat component must be valid"), Combat);
	if (!Combat)
	{
		return false;
	}

	// Start block (default unassigned montage and no stamina component allows logic-only block)
	Combat->StartBlock();
	TestTrue(TEXT("Combat state should be Blocking after StartBlock"), Combat->IsBlocking());

	// Stamina depletion must transition combat back to Idle (F3 fix)
	Combat->HandleStaminaDepleted();
	TestFalse(TEXT("Combat state must not be Blocking after stamina depletion"), Combat->IsBlocking());
	TestTrue(TEXT("Combat state should be Idle after stamina depletion"), Combat->IsIdle());

	return true;
}

// F5: Heal pickup full-health rejection
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFHealPickupFullHealthTest, "IronField.Pickup.FullHealthRejection", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFHealPickupFullHealthTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("Test world must be valid"), World);
	if (!World)
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	AIFPlayerCharacter* const Player = World->SpawnActor<AIFPlayerCharacter>();
	AIFHealPickup* const Pickup = World->SpawnActor<AIFHealPickup>();
	TestNotNull(TEXT("Player must be spawned"), Player);
	TestNotNull(TEXT("Pickup must be spawned"), Pickup);

	if (Player && Pickup)
	{
		UIFHealthComponent* const Health = Player->GetHealthComponent();
		TestNotNull(TEXT("Player health component must be valid"), Health);

		if (Health)
		{
			// At full health (100%), pickup must be rejected (F5 fix)
			Health->SetMaxHealth(100.f, true);
			TestTrue(TEXT("Player health should be full"), Health->GetHealthPercent() >= 1.f);
			TestFalse(TEXT("Full-health player must reject heal pickup"), Pickup->TryHealActor(Player));

			// When damaged (< 100%), pickup must be accepted
			Health->ApplyDamage(25.f);
			TestTrue(TEXT("Player health should be below max after damage"), Health->GetHealthPercent() < 1.f);
			TestTrue(TEXT("Damaged player must consume heal pickup"), Pickup->TryHealActor(Player));

			// When dead, pickup must be rejected
			Health->ApplyDamage(1000.f);
			TestTrue(TEXT("Player must be dead"), Health->IsDead());
			TestFalse(TEXT("Dead player must reject heal pickup"), Pickup->TryHealActor(Player));
		}

		// Non-player actor must reject heal pickup
		AActor* const NonPlayerActor = World->SpawnActor<AActor>();
		TestFalse(TEXT("Non-player actor must reject heal pickup"), Pickup->TryHealActor(NonPlayerActor));
		if (NonPlayerActor)
		{
			NonPlayerActor->Destroy();
		}

		Pickup->Destroy();
		Player->Destroy();
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// F1: Wave corpse despawn delay & flush safety
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFWaveCorpseCleanupTest, "IronField.Wave.CorpseCleanupSafety", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFWaveCorpseCleanupTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("Test world must be valid"), World);
	if (!World)
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	AIFWaveManager* const WaveManager = World->SpawnActor<AIFWaveManager>();
	AIFBaseCharacter* const Enemy = World->SpawnActor<AIFBaseCharacter>();
	TestNotNull(TEXT("WaveManager must be spawned"), WaveManager);
	TestNotNull(TEXT("Enemy must be spawned"), Enemy);

	if (WaveManager && Enemy)
	{
		// Killing enemy must NOT destroy corpse immediately on the same frame (F1 fix)
		WaveManager->HandleEnemyDied(Enemy);
		TestFalse(TEXT("Corpse should not be destroyed immediately on death frame"), Enemy->IsActorBeingDestroyed());

		// Explicit CleanupWaveCorpses destroys pending corpses
		WaveManager->CleanupWaveCorpses();
		TestTrue(TEXT("Corpse should be marked pending kill/destroyed after CleanupWaveCorpses"), Enemy->IsActorBeingDestroyed());

		// BeginWaveFromDefinition flushes any pending corpses from prior waves
		AIFBaseCharacter* const Enemy2 = World->SpawnActor<AIFBaseCharacter>();
		if (Enemy2)
		{
			WaveManager->HandleEnemyDied(Enemy2);
			TestFalse(TEXT("Enemy2 should not be destroyed immediately"), Enemy2->IsActorBeingDestroyed());

			FWaveDefinition EmptyWave;
			WaveManager->BeginWaveFromDefinition(EmptyWave);
			TestTrue(TEXT("Enemy2 corpse should be flushed on BeginWaveFromDefinition"), Enemy2->IsActorBeingDestroyed());
		}

		WaveManager->Destroy();
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// S4: Unified actor registry subsystem
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFActorRegistrySubsystemTest, "IronField.Core.ActorRegistrySubsystem", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFActorRegistrySubsystemTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("Test world must be valid"), World);
	if (!World)
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>();
	TestNotNull(TEXT("Registry subsystem must be valid"), Registry);

	if (Registry)
	{
		// Initial state: no actor registered for Player role
		TestFalse(TEXT("Player role should not be registered initially"), Registry->HasActor(IFRegistryRoles::Player));
		TestNull(TEXT("GetActor for Player role should return null initially"), Registry->GetActor(IFRegistryRoles::Player));

		// Register an actor
		AActor* const DummyPlayer = World->SpawnActor<AActor>();
		TestNotNull(TEXT("DummyPlayer actor must be spawned"), DummyPlayer);

		if (DummyPlayer)
		{
			Registry->RegisterActor(IFRegistryRoles::Player, DummyPlayer);
			TestTrue(TEXT("Player role should be registered after RegisterActor"), Registry->HasActor(IFRegistryRoles::Player));
			TestEqual(TEXT("GetActor should return registered actor"), Registry->GetActor(IFRegistryRoles::Player), DummyPlayer);

			// Unregister with matching actor
			Registry->UnregisterActor(IFRegistryRoles::Player, DummyPlayer);
			TestFalse(TEXT("Player role should not be registered after UnregisterActor"), Registry->HasActor(IFRegistryRoles::Player));
			TestNull(TEXT("GetActor should return null after unregistration"), Registry->GetActor(IFRegistryRoles::Player));

			DummyPlayer->Destroy();
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
