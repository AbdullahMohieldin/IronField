#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Stats/IFHealthComponent.h"
#include "Stats/IFStaminaComponent.h"
#include "Stats/IFStatUtils.h"
#include "AI/IFTargetScoring.h"
#include "Wave/IFSpawnQueue.h"
#include "Wave/IFWaveScalingUtils.h"
#include "Tests/IFTestAutomationTypes.h"

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

		// WhenRegistered: order independence
		// Test A: subscription before registration (deferred)
		bool bDeferredCalled = false;
		AActor* ReceivedActorA = nullptr;
		Registry->WhenRegistered(IFRegistryRoles::WaveManager, nullptr, [&bDeferredCalled, &ReceivedActorA](AActor* InActor)
		{
			bDeferredCalled = true;
			ReceivedActorA = InActor;
		});
		TestFalse(TEXT("Deferred callback not called before registration"), bDeferredCalled);

		AActor* const DummyWaveManager = World->SpawnActor<AActor>();
		Registry->RegisterActor(IFRegistryRoles::WaveManager, DummyWaveManager);
		TestTrue(TEXT("Deferred callback called on registration"), bDeferredCalled);
		TestEqual(TEXT("Deferred callback received correct actor"), ReceivedActorA, DummyWaveManager);

		// Test B: subscription after registration (immediate)
		bool bImmediateCalled = false;
		AActor* ReceivedActorB = nullptr;
		Registry->WhenRegistered(IFRegistryRoles::WaveManager, nullptr, [&bImmediateCalled, &ReceivedActorB](AActor* InActor)
		{
			bImmediateCalled = true;
			ReceivedActorB = InActor;
		});
		TestTrue(TEXT("Immediate callback called synchronously for existing actor"), bImmediateCalled);
		TestEqual(TEXT("Immediate callback received correct actor"), ReceivedActorB, DummyWaveManager);

		// Register an actor for Player role
		AActor* const DummyPlayer = World->SpawnActor<AActor>();
		TestNotNull(TEXT("DummyPlayer actor must be spawned"), DummyPlayer);

		if (DummyPlayer)
		{
			Registry->RegisterActor(IFRegistryRoles::Player, DummyPlayer);
			TestTrue(TEXT("Player role should be registered after RegisterActor"), Registry->HasActor(IFRegistryRoles::Player));
			TestEqual(TEXT("GetActor should return registered actor"), Registry->GetActor(IFRegistryRoles::Player), DummyPlayer);

			// Duplicate registration overwrite (logs warning and updates pointer)
			AActor* const OverwritePlayer = World->SpawnActor<AActor>();
			Registry->RegisterActor(IFRegistryRoles::Player, OverwritePlayer);
			TestEqual(TEXT("Overwritten actor is now registered"), Registry->GetActor(IFRegistryRoles::Player), OverwritePlayer);

			// Unregister
			Registry->UnregisterActor(IFRegistryRoles::Player, OverwritePlayer);
			TestFalse(TEXT("Player role should not be registered after UnregisterActor"), Registry->HasActor(IFRegistryRoles::Player));
			TestNull(TEXT("GetActor should return null after unregistration"), Registry->GetActor(IFRegistryRoles::Player));

			OverwritePlayer->Destroy();
			DummyPlayer->Destroy();
		}

		DummyWaveManager->Destroy();
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
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
	Queue.Enqueue(APawn::StaticClass());
	Queue.Enqueue(APawn::StaticClass());

	TestTrue(TEXT("Queue should have pending"), Queue.HasPending());
	TestEqual(TEXT("Pending count is 2"), Queue.NumPending(), 2);
	TestEqual(TEXT("Total queued is 2"), Queue.TotalQueued(), 2);

	// Peek does not advance cursor
	TestEqual(TEXT("Peek returns first class"), Queue.Peek(), TSubclassOf<APawn>(APawn::StaticClass()));
	TestEqual(TEXT("Pending count after peek is still 2"), Queue.NumPending(), 2);

	// Dequeue first
	TSubclassOf<APawn> First = Queue.Dequeue();
	TestEqual(TEXT("Dequeue returns first class"), First, TSubclassOf<APawn>(APawn::StaticClass()));
	TestEqual(TEXT("Pending count is now 1"), Queue.NumPending(), 1);
	TestTrue(TEXT("Queue still has pending"), Queue.HasPending());

	// Dequeue second
	TSubclassOf<APawn> Second = Queue.Dequeue();
	TestEqual(TEXT("Dequeue returns second class"), Second, TSubclassOf<APawn>(APawn::StaticClass()));
	TestEqual(TEXT("Pending count is now 0"), Queue.NumPending(), 0);
	TestFalse(TEXT("Queue has no pending left"), Queue.HasPending());

	// Dequeue past end
	TestNull(TEXT("Dequeue past end returns null"), Queue.Dequeue());

	// Reset
	Queue.Enqueue(APawn::StaticClass());
	Queue.Reset();
	TestFalse(TEXT("Reset queue has no pending"), Queue.HasPending());
	TestEqual(TEXT("Reset queue count is 0"), Queue.NumPending(), 0);

	// Concurrency cap simulation: max 2 active enemies
	const int32 ConcurrencyCap = 2;
	int32 ActiveEnemies = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		Queue.Enqueue(APawn::StaticClass());
	}
	TestEqual(TEXT("5 items enqueued"), Queue.NumPending(), 5);

	// Spawn up to concurrency cap
	while (ActiveEnemies < ConcurrencyCap && Queue.HasPending())
	{
		Queue.Dequeue();
		++ActiveEnemies;
	}
	TestEqual(TEXT("Active enemies at concurrency cap"), ActiveEnemies, 2);
	TestEqual(TEXT("3 items remaining in queue"), Queue.NumPending(), 3);

	// One enemy dies, next spawns
	--ActiveEnemies;
	if (ActiveEnemies < ConcurrencyCap && Queue.HasPending())
	{
		Queue.Dequeue();
		++ActiveEnemies;
	}
	TestEqual(TEXT("Active enemies back at cap"), ActiveEnemies, 2);
	TestEqual(TEXT("2 items remaining in queue"), Queue.NumPending(), 2);

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

// S5, S6: Pure target scorer unit tests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFTargetScorerTest, "IronField.AI.TargetScorer", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFTargetScorerTest::RunTest(const FString& Parameters)
{
	// 1. Distance falloff monotonicity
	const float DesireNear = FIFTargetScoring::DistanceDesire(100.f, 800.f);
	const float DesireMid = FIFTargetScoring::DistanceDesire(400.f, 800.f);
	const float DesireFar = FIFTargetScoring::DistanceDesire(1600.f, 800.f);

	TestEqual(TEXT("Zero distance desire is 1.0"), FIFTargetScoring::DistanceDesire(0.f, 800.f), 1.0f);
	TestTrue(TEXT("Nearer distance has strictly greater desire than mid"), DesireNear > DesireMid);
	TestTrue(TEXT("Mid distance has strictly greater desire than far"), DesireMid > DesireFar);
	TestTrue(TEXT("Falloff stays strictly positive"), DesireFar > 0.f);

	// 2. Commitment lock hysteresis
	FIFTargetScoreInput Input;
	Input.bPlayerValid = true;
	Input.bStrongholdValid = true;
	Input.bCurrentValid = true;
	Input.CurrentTarget = EIFTargetCandidate::Player;
	Input.PlayerDistance = 2400.f;
	Input.StrongholdDistance = 200.f; // Stronghold much closer -> raw score 0.8 beats hysteresis threshold (0.25 * 2.16 = 0.54)
	Input.Aggression = 0.5f;
	Input.bHasGrudge = false;
	Input.TimeSinceLastSwitch = 1.0f; // Within 3s lock
	Input.CommitLockSeconds = 3.0f;
	Input.CommitScoreBonus = 1.6f;
	Input.SwitchThreshold = 1.35f;

	// While time < CommitLockSeconds and no grudge, commitment lock prevents switch
	FIFTargetScoreResult LockedResult = FIFTargetScoring::ScoreTarget(Input);
	TestEqual(TEXT("Commitment lock keeps current player target"), LockedResult.DesiredTarget, EIFTargetCandidate::Player);
	TestFalse(TEXT("Locked target did not switch"), LockedResult.bTargetSwitched);

	// When time >= CommitLockSeconds, challenger with sufficient score steals target
	Input.TimeSinceLastSwitch = 4.0f; // Past 3s lock
	FIFTargetScoreResult UnlockedResult = FIFTargetScoring::ScoreTarget(Input);
	TestEqual(TEXT("Unlocked challenger steals target"), UnlockedResult.DesiredTarget, EIFTargetCandidate::Stronghold);
	TestTrue(TEXT("Unlocked target switched"), UnlockedResult.bTargetSwitched);

	// 3. Grudge bypasses lock and multiplies desire
	Input.TimeSinceLastSwitch = 1.0f; // Reset to locked
	Input.bHasGrudge = true;          // Player hit enemy
	Input.RetaliationBonus = 3.0f;
	FIFTargetScoreResult GrudgeResult = FIFTargetScoring::ScoreTarget(Input);
	TestEqual(TEXT("Grudge bypasses lock to target player"), GrudgeResult.DesiredTarget, EIFTargetCandidate::Player);
	TestTrue(TEXT("Grudge boosted player raw score"), GrudgeResult.PlayerRawScore > 0.f);

	// 4. Invalid candidates handling
	Input.bPlayerValid = false;
	Input.bStrongholdValid = false;
	Input.bCurrentValid = false;
	FIFTargetScoreResult EmptyResult = FIFTargetScoring::ScoreTarget(Input);
	TestEqual(TEXT("All invalid returns None"), EmptyResult.DesiredTarget, EIFTargetCandidate::None);

	return true;
}

// Phase 3: Comprehensive Health Component test
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFHealthComponentTest, "IronField.Stats.Health", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFHealthComponentTest::RunTest(const FString& Parameters)
{
	UIFHealthComponent* const Health = NewObject<UIFHealthComponent>();
	TestNotNull(TEXT("Health component must be valid"), Health);
	if (!Health)
	{
		return false;
	}

	// 1. Initial state & SetMaxHealth
	Health->SetMaxHealth(100.f, true);
	TestEqual(TEXT("MaxHealth is 100"), Health->GetMaxHealth(), 100.f);
	TestEqual(TEXT("HealthPercent is 1.0"), Health->GetHealthPercent(), 1.0f);
	TestFalse(TEXT("Not dead initially"), Health->IsDead());

	// 2. Damage deduction
	Health->ApplyDamage(30.f);
	TestEqual(TEXT("HealthPercent after 30 damage is 0.7"), Health->GetHealthPercent(), 0.7f);

	// 3. Clamp limits: healing cannot exceed MaxHealth
	Health->ApplyHealing(50.f);
	TestEqual(TEXT("Health clamped to MaxHealth after overheal"), Health->GetHealthPercent(), 1.0f);

	// 4. Invincibility window
	Health->SetInvincible(true);
	TestTrue(TEXT("Invincible is true"), Health->IsInvincible());
	Health->ApplyDamage(50.f);
	TestEqual(TEXT("Invincible ignores damage"), Health->GetHealthPercent(), 1.0f);
	Health->SetInvincible(false);

	// 5. Single death broadcast & clamp to 0
	UIFTestAutomationListener* const HealthListener = NewObject<UIFTestAutomationListener>();
	Health->OnDeath.AddDynamic(HealthListener, &UIFTestAutomationListener::HandleDeath);
	Health->OnHealthDepleted.AddDynamic(HealthListener, &UIFTestAutomationListener::HandleHealthDepleted);

	Health->ApplyDamage(200.f);
	TestTrue(TEXT("Dead after fatal damage"), Health->IsDead());
	TestEqual(TEXT("Health percent clamped to 0"), Health->GetHealthPercent(), 0.0f);
	TestEqual(TEXT("OnDeath broadcast once"), HealthListener->DeathCount, 1);
	TestEqual(TEXT("OnHealthDepleted broadcast once"), HealthListener->HealthDepletedCount, 1);

	// Further damage while dead does NOT broadcast death again
	Health->ApplyDamage(50.f);
	TestEqual(TEXT("OnDeath not rebroadcast while already dead"), HealthListener->DeathCount, 1);
	TestEqual(TEXT("OnHealthDepleted not rebroadcast while already dead"), HealthListener->HealthDepletedCount, 1);

	// 6. Revive state reset
	Health->SetReviveHealth(40.f);
	Health->Revive();
	TestFalse(TEXT("No longer dead after revive"), Health->IsDead());
	TestEqual(TEXT("Health restored to ReviveHealth (40%)"), Health->GetHealthPercent(), 0.4f);

	// Reviving a living character does nothing
	Health->Revive();
	TestEqual(TEXT("Reviving living character does nothing"), Health->GetHealthPercent(), 0.4f);

	// 7. SetMaxHealth shrink clamps current health
	Health->SetMaxHealth(20.f, false);
	TestEqual(TEXT("New MaxHealth is 20"), Health->GetMaxHealth(), 20.f);
	TestEqual(TEXT("Current health clamped to new MaxHealth (100%)"), Health->GetHealthPercent(), 1.0f);

	return true;
}

// Phase 3: Comprehensive Stamina Component test
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIFStaminaComponentTest, "IronField.Stats.Stamina", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FIFStaminaComponentTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	TestNotNull(TEXT("Test world must be valid"), World);
	if (!World)
	{
		return false;
	}

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	AActor* const OwnerActor = World->SpawnActor<AActor>();
	TestNotNull(TEXT("OwnerActor must be valid"), OwnerActor);
	if (!OwnerActor)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	UIFStaminaComponent* const Stamina = NewObject<UIFStaminaComponent>(OwnerActor);
	TestNotNull(TEXT("Stamina component must be valid"), Stamina);
	if (!Stamina)
	{
		OwnerActor->Destroy();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	Stamina->RegisterComponent();

	// 1. Initial state
	TestEqual(TEXT("Initial stamina is 100%"), Stamina->GetStaminaPercent(), 1.0f);
	TestTrue(TEXT("Has 50 stamina"), Stamina->HasStamina(50.f));

	// 2. Cost consumption & tick auto-enable
	const bool bConsumed = Stamina->TryConsumeStamina(40.f);
	TestTrue(TEXT("TryConsumeStamina succeeded for 40"), bConsumed);
	TestEqual(TEXT("Stamina is 60%"), Stamina->GetStaminaPercent(), 0.6f);
	TestTrue(TEXT("Tick enabled while regenerating"), Stamina->IsComponentTickEnabled());

	// TryConsumeStamina fails if not enough stamina
	const bool bFailedConsume = Stamina->TryConsumeStamina(70.f);
	TestFalse(TEXT("TryConsumeStamina failed for 70 (insufficient)"), bFailedConsume);
	TestEqual(TEXT("Stamina unchanged at 60%"), Stamina->GetStaminaPercent(), 0.6f);

	// 3. Regen delay timer
	// Ticking while TimeSinceLastStaminaDrain < StaminaRegenDelay (1.75s) should NOT regenerate
	Stamina->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Stamina does not regen before delay elapsed"), Stamina->GetStaminaPercent(), 0.6f);

	// Ticking past delay should regenerate (StaminaRegenRate is 30/s)
	Stamina->TickComponent(1.75f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Stamina regenerated past delay"), Stamina->GetStaminaPercent() > 0.6f);

	// Regenerate all the way to max -> tick should auto-disable
	Stamina->TickComponent(5.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Stamina reached 100%"), Stamina->GetStaminaPercent(), 1.0f);
	TestFalse(TEXT("Tick auto-disabled when stamina reaches max"), Stamina->IsComponentTickEnabled());

	// 4. Drain to depletion and single depletion broadcast
	UIFTestAutomationListener* const StaminaListener = NewObject<UIFTestAutomationListener>();
	Stamina->OnStaminaDepleted.AddDynamic(StaminaListener, &UIFTestAutomationListener::HandleStaminaDepleted);

	Stamina->StartContinuousDrain(50.f);
	TestTrue(TEXT("Is draining stamina"), Stamina->IsDrainingStamina());

	// Drain 1s: 100 - 50 = 50
	Stamina->TickComponent(1.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Stamina at 50%"), Stamina->GetStaminaPercent(), 0.5f);
	TestEqual(TEXT("Depleted not broadcast at 50%"), StaminaListener->StaminaDepletedCount, 0);

	// Drain 2s: 50 - 100 <= 0 -> hits 0 and stops continuous drain
	Stamina->TickComponent(2.0f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Stamina at 0%"), Stamina->GetStaminaPercent(), 0.0f);
	TestEqual(TEXT("Depleted broadcast once"), StaminaListener->StaminaDepletedCount, 1);
	TestFalse(TEXT("Continuous drain stopped on depletion"), Stamina->IsDrainingStamina());

	// Ticking again while at 0 should not broadcast depletion again
	Stamina->DrainStamina(0.5f);
	TestEqual(TEXT("Depleted not rebroadcast when already at 0"), StaminaListener->StaminaDepletedCount, 1);

	OwnerActor->Destroy();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
