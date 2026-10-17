#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Character/IFBaseCharacter.h"
#include "Character/IFPlayerCharacter.h"
#include "Combat/IFCombatComponent.h"
#include "Combat/IFMageCombatComponent.h"
#include "Combat/IFMeleeCombatComponent.h"
#include "Combat/IFPlayerCombatComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Building/IFStronghold.h"
#include "Character/IFMeleeEnemyCharacter.h"
#include "Combat/IFCombatTargetingUtils.h"
#include "Combat/IFTeamTypes.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Pickup/IFHealPickup.h"
#include "Stats/IFHealthComponent.h"
#include "Stats/IFStaminaComponent.h"
#include "Stats/IFStatUtils.h"
#include "Wave/IFSpawnQueue.h"
#include "Wave/IFWaveManager.h"
#include "Wave/IFWaveScalingUtils.h"

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

// S1: Team hostility & attitude checks
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFTeamHostilityTest, "IronField.Combat.TeamHostility", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFTeamHostilityTest::RunTest(const FString& Parameters)
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
	AIFStronghold* const Stronghold = World->SpawnActor<AIFStronghold>();
	AIFMeleeEnemyCharacter* const Enemy = World->SpawnActor<AIFMeleeEnemyCharacter>();
	AIFMeleeEnemyCharacter* const Enemy2 = World->SpawnActor<AIFMeleeEnemyCharacter>();

	TestNotNull(TEXT("Player must be spawned"), Player);
	TestNotNull(TEXT("Stronghold must be spawned"), Stronghold);
	TestNotNull(TEXT("Enemy must be spawned"), Enemy);
	TestNotNull(TEXT("Enemy2 must be spawned"), Enemy2);

	if (Player && Stronghold && Enemy && Enemy2)
	{
		// Team IDs
		TestEqual(TEXT("Player team must be 0"), Player->GetGenericTeamId().GetId(), static_cast<uint8>(0));
		TestEqual(TEXT("Stronghold team must be 0"), Stronghold->GetGenericTeamId().GetId(), static_cast<uint8>(0));
		TestEqual(TEXT("Enemy team must be 1"), Enemy->GetGenericTeamId().GetId(), static_cast<uint8>(1));
		TestEqual(TEXT("Enemy2 team must be 1"), Enemy2->GetGenericTeamId().GetId(), static_cast<uint8>(1));

		// Attitude checks
		TestTrue(TEXT("Player and Stronghold must be friendly"), IFTeamUtils::AreFriendly(Player, Stronghold));
		TestFalse(TEXT("Player and Stronghold must not be hostile"), IFTeamUtils::AreHostile(Player, Stronghold));
		TestTrue(TEXT("Player and Enemy must be hostile"), IFTeamUtils::AreHostile(Player, Enemy));
		TestTrue(TEXT("Enemy and Stronghold must be hostile"), IFTeamUtils::AreHostile(Enemy, Stronghold));
		TestTrue(TEXT("Enemies must be friendly to each other"), IFTeamUtils::AreFriendly(Enemy, Enemy2));
		TestFalse(TEXT("Enemies must not be hostile to each other"), IFTeamUtils::AreHostile(Enemy, Enemy2));

		// Combat targeting validity
		TestNull(TEXT("Player cannot attack Stronghold"), IFCombatTargetingUtils::GetValidAttackTargetHealth(Player, Stronghold));
		TestNotNull(TEXT("Enemy can attack Stronghold"), IFCombatTargetingUtils::GetValidAttackTargetHealth(Enemy, Stronghold));
		TestNotNull(TEXT("Player can attack Enemy"), IFCombatTargetingUtils::GetValidAttackTargetHealth(Player, Enemy));
		TestNull(TEXT("Enemy cannot attack another Enemy"), IFCombatTargetingUtils::GetValidAttackTargetHealth(Enemy, Enemy2));

		Enemy2->Destroy();
		Enemy->Destroy();
		Stronghold->Destroy();
		Player->Destroy();
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// S2, U2: Damage scaling centralization
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFDamageScaleTest, "IronField.Combat.DamageScale", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFDamageScaleTest::RunTest(const FString& Parameters)
{
	UIFCombatComponent* const BaseCombat = NewObject<UIFCombatComponent>();
	UIFMeleeCombatComponent* const MeleeCombat = NewObject<UIFMeleeCombatComponent>();
	UIFMageCombatComponent* const MageCombat = NewObject<UIFMageCombatComponent>();

	TestNotNull(TEXT("BaseCombat must be valid"), BaseCombat);
	TestNotNull(TEXT("MeleeCombat must be valid"), MeleeCombat);
	TestNotNull(TEXT("MageCombat must be valid"), MageCombat);

	if (BaseCombat && MeleeCombat && MageCombat)
	{
		// Baseline default is 1.0f (U2 requirement)
		TestEqual(TEXT("Base combat default DamageScale must be 1.0"), BaseCombat->GetDamageScale(), 1.0f);
		TestEqual(TEXT("Melee combat default DamageScale must be 1.0"), MeleeCombat->GetDamageScale(), 1.0f);
		TestEqual(TEXT("Mage combat default DamageScale must be 1.0"), MageCombat->GetDamageScale(), 1.0f);

		// Setting scale works across all component types
		BaseCombat->SetDamageScale(1.5f);
		TestEqual(TEXT("Base combat DamageScale should be 1.5"), BaseCombat->GetDamageScale(), 1.5f);

		MeleeCombat->SetDamageScale(2.0f);
		TestEqual(TEXT("Melee combat DamageScale should be 2.0"), MeleeCombat->GetDamageScale(), 2.0f);

		MageCombat->SetDamageScale(0.5f);
		TestEqual(TEXT("Mage combat DamageScale should be 0.5"), MageCombat->GetDamageScale(), 0.5f);

		// Negative clamp to 0.0f
		BaseCombat->SetDamageScale(-1.0f);
		TestEqual(TEXT("Negative scale must be clamped to 0.0"), BaseCombat->GetDamageScale(), 0.0f);
	}

	return true;
}

// U7: Stat utilities unit tests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFStatUtilsTest, "IronField.Stats.StatUtils", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFStatUtilsTest::RunTest(const FString& Parameters)
{
	// ComputePercent
	TestEqual(TEXT("Full percentage"), IFStatUtils::ComputePercent(100.f, 100.f), 1.0f);
	TestEqual(TEXT("Half percentage"), IFStatUtils::ComputePercent(50.f, 100.f), 0.5f);
	TestEqual(TEXT("Zero current"), IFStatUtils::ComputePercent(0.f, 100.f), 0.0f);
	TestEqual(TEXT("Zero max guard"), IFStatUtils::ComputePercent(50.f, 0.f), 0.0f);
	TestEqual(TEXT("Negative max guard"), IFStatUtils::ComputePercent(50.f, -10.f), 0.0f);

	// ApplyClampedValue
	float Value = 50.f;
	const bool bChanged1 = IFStatUtils::ApplyClampedValue(Value, 100.f, 75.f);
	TestTrue(TEXT("Value should change to 75"), bChanged1);
	TestEqual(TEXT("Value is 75"), Value, 75.f);

	// Over-max clamping
	const bool bChanged2 = IFStatUtils::ApplyClampedValue(Value, 100.f, 150.f);
	TestTrue(TEXT("Value should change to 100"), bChanged2);
	TestEqual(TEXT("Value clamped to max"), Value, 100.f);

	// Under-zero clamping
	const bool bChanged3 = IFStatUtils::ApplyClampedValue(Value, 100.f, -20.f);
	TestTrue(TEXT("Value should change to 0"), bChanged3);
	TestEqual(TEXT("Value clamped to 0"), Value, 0.f);

	// No-change returns false
	const bool bChanged4 = IFStatUtils::ApplyClampedValue(Value, 100.f, 0.f);
	TestFalse(TEXT("No-change should return false"), bChanged4);
	TestEqual(TEXT("Value remains 0"), Value, 0.f);

	return true;
}

// S3: Spawn queue unit tests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFSpawnQueueTest, "IronField.Wave.SpawnQueue", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFSpawnQueueTest::RunTest(const FString& Parameters)
{
	FIFSpawnQueue Queue;

	// Initial empty state
	TestFalse(TEXT("Empty queue should have no pending"), Queue.HasPending());
	TestEqual(TEXT("Empty queue pending count is 0"), Queue.NumPending(), 0);
	TestEqual(TEXT("Empty queue total queued is 0"), Queue.TotalQueued(), 0);
	TestNull(TEXT("Dequeue on empty returns null"), Queue.Dequeue());
	TestNull(TEXT("Peek on empty returns null"), Queue.Peek());

	// Enqueue items
	Queue.Enqueue(AIFBaseCharacter::StaticClass());
	Queue.Enqueue(AIFPlayerCharacter::StaticClass());

	TestTrue(TEXT("Queue should have pending"), Queue.HasPending());
	TestEqual(TEXT("Pending count is 2"), Queue.NumPending(), 2);
	TestEqual(TEXT("Total queued is 2"), Queue.TotalQueued(), 2);

	// Peek does not advance cursor
	TestEqual(TEXT("Peek returns first class"), Queue.Peek(), TSubclassOf<APawn>(AIFBaseCharacter::StaticClass()));
	TestEqual(TEXT("Pending count after peek is still 2"), Queue.NumPending(), 2);

	// Dequeue first
	TSubclassOf<APawn> First = Queue.Dequeue();
	TestEqual(TEXT("Dequeue returns first class"), First, TSubclassOf<APawn>(AIFBaseCharacter::StaticClass()));
	TestEqual(TEXT("Pending count is now 1"), Queue.NumPending(), 1);
	TestTrue(TEXT("Queue still has pending"), Queue.HasPending());

	// Dequeue second
	TSubclassOf<APawn> Second = Queue.Dequeue();
	TestEqual(TEXT("Dequeue returns second class"), Second, TSubclassOf<APawn>(AIFPlayerCharacter::StaticClass()));
	TestEqual(TEXT("Pending count is now 0"), Queue.NumPending(), 0);
	TestFalse(TEXT("Queue has no pending left"), Queue.HasPending());

	// Dequeue past end
	TestNull(TEXT("Dequeue past end returns null"), Queue.Dequeue());

	// Reset
	Queue.Enqueue(AIFBaseCharacter::StaticClass());
	Queue.Reset();
	TestFalse(TEXT("Reset queue has no pending"), Queue.HasPending());
	TestEqual(TEXT("Reset queue count is 0"), Queue.NumPending(), 0);

	return true;
}

// U1: Wave scaling math unit tests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFScalingMathTest, "IronField.Wave.ScalingMath", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFScalingMathTest::RunTest(const FString& Parameters)
{
	// Default multiplier 1.0 (baseline identical gameplay)
	TestEqual(TEXT("Wave 0 default health scale"), IFWaveScalingUtils::ComputeHealthScale(0, 1.0f), 1.0f);
	TestEqual(TEXT("Wave 5 default health scale"), IFWaveScalingUtils::ComputeHealthScale(5, 1.0f), 1.0f);
	TestEqual(TEXT("Wave 0 default damage scale"), IFWaveScalingUtils::ComputeDamageScale(0, 1.0f), 1.0f);
	TestEqual(TEXT("Wave 5 default damage scale"), IFWaveScalingUtils::ComputeDamageScale(5, 1.0f), 1.0f);

	// Scaled multiplier
	TestEqual(TEXT("Wave 0 scaled health"), IFWaveScalingUtils::ComputeHealthScale(0, 1.2f), 1.0f);
	TestEqual(TEXT("Wave 1 scaled health"), IFWaveScalingUtils::ComputeHealthScale(1, 1.2f), 1.2f);
	TestEqual(TEXT("Wave 2 scaled health"), IFWaveScalingUtils::ComputeHealthScale(2, 1.2f), 1.44f);

	TestEqual(TEXT("Wave 0 scaled damage"), IFWaveScalingUtils::ComputeDamageScale(0, 1.5f), 1.0f);
	TestEqual(TEXT("Wave 1 scaled damage"), IFWaveScalingUtils::ComputeDamageScale(1, 1.5f), 1.5f);
	TestEqual(TEXT("Wave 2 scaled damage"), IFWaveScalingUtils::ComputeDamageScale(2, 1.5f), 2.25f);

	// ScaleEnemyCount
	TestEqual(TEXT("Base count preserved for wave 0"), IFWaveScalingUtils::ScaleEnemyCount(5, 0, 1.15f), 5);
	TestEqual(TEXT("Scale factor 1.0 preserves count"), IFWaveScalingUtils::ScaleEnemyCount(5, 5, 1.0f), 5);
	TestEqual(TEXT("Wave 1 scaled count (5 * 1.15 = 6)"), IFWaveScalingUtils::ScaleEnemyCount(5, 1, 1.15f), 6);

	return true;
}



