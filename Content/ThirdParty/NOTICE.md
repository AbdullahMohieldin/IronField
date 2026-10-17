# Third-Party Asset Attribution & Notices

This directory contains third-party assets utilized in the IronField project.

---

### 1. Potion Meshes & Materials
- **Location:** `Content/ThirdParty/PotionMeshes/`
- **Tracked Assets:**
  - `potion_003_green.uasset` (Static Mesh)
  - `M_Potion_Green.uasset` (Material Instance)
  - `Material_001.uasset` (Base Material)
  - `Atlas.uasset` (Texture Atlas)
- **Usage:** Visual representation of `BP_HealPickup` in arena levels.
- **License / Source:** Unreal Engine Marketplace asset pack. Distributed for project demonstration under standard Epic Games Content License Agreement / Marketplace EULA.

---

### 2. Healing Sound FX
- **Location:** `Content/ThirdParty/PotionSound/`
- **Tracked Assets:**
  - `Healing_Spell_Impact_1.uasset` (Sound Cue / Wave)
- **Usage:** Audio feedback when player character consumes `BP_HealPickup`.
- **License / Source:** Unreal Engine Marketplace audio asset pack. Distributed for project demonstration under standard Epic Games Content License Agreement / Marketplace EULA.

---

### 3. Untracked Local Marketplace Content
All other local packs under `Content/ThirdParty/` (e.g., character meshes, environment textures, particle systems) are excluded from version control via `.gitignore` to adhere to redistribution limitations and repository size constraints.
