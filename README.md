# IronField

A modular C++ gameplay systems demo built in Unreal Engine 5.8. 

The project is structured as an architectural showcase demonstrating decoupled communication, component-based character mechanics, heuristic AI targeting, and event-driven wave management.

## Core Systems

### 1. Decoupled Subsystem Architecture
- Level actors register with dedicated `UWorldSubsystem` instances (`UIFPlayerSubsystem`, `UIFStrongholdSubsystem`, `UIFWaveManagerSubsystem`).
- Gameplay systems, HUD widgets, and AI controllers discover target actors through subsystems via dynamic delegates, preventing hard actor references and cyclic dependencies.

### 2. Component-Based Combat Pipeline
- **`UIFCombatComponent`**: Controls weapon trace volumes, animation-notify hit windows (`AnimNotifyState_IFAttackWindow`), and per-swing hit uniqueness tracking.
- **`UIFPlayerCombatComponent`**: Implements buffered combo chaining, directional blocking with stamina-based guard breaking and chip damage, and sustained spin attacks with per-target re-hit intervals.
- **`UIFHealthComponent` & `UIFStaminaComponent`**: Self-contained stat components managing damage mitigation, death/revive lifecycles, and delayed stamina regeneration economics.

### 3. Heuristic AI Targeting & Behavior Trees
- Enemy controllers (`AIFEnemyController`) utilize Behavior Trees decoupled from pawn archetypes.
- Custom target selection service (`UIFBTService_FindTarget`) scores candidates dynamically based on:
  - Distance falloff using radii-aware surface-to-surface distance (`IFCombatTargetingUtils`).
  - Target commitment bonuses with hysteresis locks to prevent target flickering.
  - Per-agent aggression factors and wave-level siege biases.
  - Direct retaliation multipliers triggered upon taking player damage.

### 4. Wave Director & Spawner
- `AIFWaveManager` coordinates spawn scheduling through a concurrency-capped queue with opening grace periods and inter-wave breathers.
- Pre-warms enemy character assets asynchronously using `FStreamableManager` prior to wave start to eliminate combat frame drops.
- Supports authored progression (Normal Mode) and procedural scaling curves (Unlimited Mode).

### 5. Persistence
- `UIFGameInstance` handles seamless cross-level state transfer between the main menu and arena.
- `UIFBestRunSave` serializes run statistics to disk via binary `USaveGame`.

## Architecture & Boundary Rules

- **C++**: All gameplay logic, state machines, math, combat mitigation, scoring equations, and timers.
- **Blueprints**: Asset assignment, animation montage references, visual effects, and UI layouts.
- **DataAssets**: Tunable configuration values (`UIFEnemyAIData`) allowing data-driven balancing without modifying code.

## Project Structure

```text
Source/IronField/
├── AI/          BehaviorTree tasks, services, decorators, and AI controller
├── Building/    Stronghold actor and objective logic
├── Character/   Base, player, melee, and mage character classes
├── Combat/      Combat components, targeting utilities, and anim notifies
├── Core/        GameMode, GameInstance, and World Subsystems
├── Stats/       Health and stamina components
├── UI/          HUD widgets and SaveGame serialization
└── Wave/        Wave manager, spawner logic, and spawn point actors
```

## Controls

| Action | Input | Description |
| --- | --- | --- |
| Move | `WASD` / Left Stick | Camera-relative character movement |
| Look | Mouse / Right Stick | Camera rotation and pitch |
| Jump | `Space` | Character jump |
| Sprint | `Shift` (Hold) | Forward movement acceleration; drains stamina |
| Attack | `Left Mouse Button` | Combo attack chain |
| Block | `Right Mouse Button` (Hold) | Directional block; mitigates damage, absorbs hits via stamina |
| Spin Attack | `X` (Hold) | Area attack loop; drains stamina |

## Build & Requirements

- **Engine Version**: Unreal Engine 5.8
- **Toolchain**: Visual Studio 2022 (MSVC v143)
- **Target Platform**: Windows 64-bit (`Shipping` / `Development`)

To build from source:
1. Clone the repository.
2. Right-click `IronField.uproject` and select **Generate Visual Studio project files**.
3. Open `IronField.sln` and compile the `IronField` project in `Development Editor` or `Shipping`.
