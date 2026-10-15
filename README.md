# IronField

Wave-defense action game built with Unreal Engine 5.8 and C++. The player defends a stronghold against waves of melee and mage enemies.

Gameplay logic is implemented in C++; Blueprints carry assets, animation montages, and per-instance tuning values only.

## Run Modes

- **Normal:** authored wave sequence; clearing all waves is a victory.
- **Unlimited:** waves generated from a single base definition with per-wave scaling; defeat-only ending.

The mode is selected in the main menu and stored on the game instance across travel to the gameplay level.

## Features

- Third-person melee combat: combo chain, directional block with chip damage and stamina-based guard break, hold-to-spin area attack
- Enemy AI with scored target selection (distance, per-agent aggression, target commitment, player-hit retaliation)
- Wave director with spawn queue, concurrency cap, inter-wave timing, and kill tracking
- Main menu with mode select, gameplay HUD, game-over screen with run statistics
- Stamina economy governing sprint, block, and spin

## Requirements

- Unreal Engine 5.8
- C++ toolchain for the target platform (Visual Studio 2022 with the UE workload on Windows)
- First-party gameplay assets are versioned under `Content/IronField/`. Third-party marketplace packs (`Content/ThirdParty/`) are kept local-only per `.gitignore` and marketplace licensing terms.

## Getting Started

1. Clone the repository.
2. Open `IronField.uproject` in Unreal Engine 5.8 and build the `IronField` module when prompted.
3. Play the `MainMenu` map for the full flow, or open `MainLevel` directly for gameplay iteration.

## Controls

Bindings are defined by the Enhanced Input mapping context (`Content/IronField/Input/IMC_Default`); actions live in `Content/IronField/Input/`.

| Action | Binding | Behavior |
| --- | --- | --- |
| Move | `WASD` / left stick | Camera-relative |
| Look | Mouse / right stick | Yaw on controller; boom pitch clamped with optional invert |
| Jump | `Space` | Character jump |
| Sprint | `Shift` (hold) | Forward movement only; drains stamina while active and requires stamina to start |
| Attack | Left mouse button | Combo chain |
| Block | Right mouse button (hold) | Directional; a fraction of blocked damage chips through, and each blocked hit costs stamina, breaking guard at zero |
| Spin attack | `X` (hold) | Sustained area attack with a per-target re-hit interval; drains stamina while active |

## Gameplay Systems

### Player

Health with a down/revive/get-up cycle, and stamina that regenerates after a delay since the last drain. Only the stronghold falling ends a run; player death drops the player as an enemy target until revive. Movement speeds, camera limits, and revive timing are tuned in `AIFPlayerCharacter` (`BP_Player` may override).

### Combat

- Melee hits resolve through a single weapon collision volume per combatant, enabled only inside animation-notify windows and resolved once per target per attack window; spin uses interval-based re-hits instead.
- Collision responses are configured on the Blueprint components. Blocking and stamina are player-only mechanics.
- Mages are interruptible: incoming hits play hit reactions instead of casting through damage.
- The stronghold accepts damage from AI-controlled attackers only.

### Enemy AI

Behavior trees share a `TargetActor` blackboard key. The find-target service scores the player and the stronghold each tick on four factors: distance falloff (same curve for both candidates), per-agent aggression rolled at possession and shifted by a wave-level siege bias, commitment to the current target (score bonus, switch lock, challenger threshold), and retaliation after player-caused hits (score multiplier that bypasses the lock). Range checks use radii-aware surface-to-surface distance shared across attack, kite, and retreat logic.

Defaults live in `UIFEnemyAIData`; per-type assets (e.g. `DA_EnemyAI_Mage`) override them. Enemy health, damage, range, and speeds are tuned in `AIFMeleeEnemyCharacter` / `AIFMageEnemyCharacter` and their Blueprints.

### Waves

`AIFWaveManager` flattens each wave into a spawn queue released on a timer under a concurrency cap, with a delayed first spawn, an inter-wave breather, and an opening grace. Spawn positions come from `AIFEnemySpawnPoint` actors with a random offset. The manager tracks per-wave totals, alive counts, and cumulative run kills, and broadcasts wave/kill events consumed by the HUD and game mode. Unlimited mode generates every wave from one base definition with per-wave count and stat scaling.

World subsystems (`Player`, `Stronghold`, `WaveManager`) expose registration delegates so HUD, AI, and wave systems resolve level actors without hard references.

### UI

- HUD: player and stronghold stat bars, wave info line, wave-start banner, damage flash.
- Game-over screen: victory/defeat title plus run statistics sourced from the wave manager.
- Main menu: mode select plus best-run line (not yet wired to a save system).

## Configuration

| Area | Where to tune |
| --- | --- |
| Player movement, camera, revive timing | `AIFPlayerCharacter` class defaults / `BP_Player` |
| Block, spin, stamina costs | `UIFPlayerCombatComponent` / `BP_Player` |
| Enemy AI targeting and cooldowns | `UIFEnemyAIData` assets (e.g. `DA_EnemyAI_Mage`) |
| Enemy health, range, speeds | `AIFMeleeEnemyCharacter` / `AIFMageEnemyCharacter` / enemy Blueprints |
| Wave composition, pacing, scaling | `AIFWaveManager` / `BP_WaveManager` |
| Stronghold health | `AIFStronghold` / `BP_Stronghold` |

## Project Structure

```text
Source/IronField/          Game module (Public/Private by system:
                           AI, Building, Character, Combat, Core, Stats, UI, Wave)
Content/IronField/         Project-authored assets (characters, UI, maps, input)
Content/ThirdParty/        Required marketplace packs (meshes, animations)
Config/                    Engine, game, and input settings
IronField.uproject         Project descriptor (UE 5.8)
```

## Packaging

`File > Package Project > Windows (64-bit)`. Verify the packaged build from a clean folder before distribution.
