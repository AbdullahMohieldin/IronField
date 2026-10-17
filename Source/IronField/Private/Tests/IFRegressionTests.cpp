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
#include "AI/IFTargetScoring.h"
#include "Wave/IFSpawnQueue.h"
#include "Wave/IFWaveManager.h"
#include "Wave/IFWaveScalingUtils.h"
#include "Tests/IFTestAutomationTypes.h"

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

		// Neutral team (255) attitude checks
		const FGenericTeamId NeutralTeam(IFTeamIds::Neutral);
		const FGenericTeamId PlayerTeam(IFTeamIds::Player);
		const FGenericTeamId EnemyTeam(IFTeamIds::Enemy);
		TestEqual(TEXT("Player vs Neutral attitude is Neutral"), IFTeamUtils::SolveAttitude(PlayerTeam, NeutralTeam), ETeamAttitude::Neutral);
		TestEqual(TEXT("Enemy vs Neutral attitude is Neutral"), IFTeamUtils::SolveAttitude(EnemyTeam, NeutralTeam), ETeamAttitude::Neutral);
		TestEqual(TEXT("Player vs Enemy attitude is Hostile"), IFTeamUtils::SolveAttitude(PlayerTeam, EnemyTeam), ETeamAttitude::Hostile);
		TestEqual(TEXT("Player vs Player attitude is Friendly"), IFTeamUtils::SolveAttitude(PlayerTeam, PlayerTeam), ETeamAttitude::Friendly);

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

