# JRPGFramework — Documentation

**Legend of Legaia Remake | Unreal Engine 5.8 | C++ | LIX Studios**

A modular plugin implementing the systems of a Legend of Legaia–style JRPG. The plugin is
independent of the game project: it defines structs, subsystems and the UI, but references no
specific assets — textures, meshes and sounds live in the project.

New here? Read [`ARCHITECTURE.md`](ARCHITECTURE.md), then
[`../SETUP.md`](../SETUP.md) to get it building.

---

## Where to start

| Document | What it is |
|---|---|
| [`../SETUP.md`](../SETUP.md) | Build from a clean clone: prerequisites, Ultralight SDK, WebUI, packaging |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | **How the plugin is put together**, the data flows, and the 47 rules that must not be broken |
| [STRUCTURE.md](STRUCTURE.md) | File-by-file tree: what exists (✅), what is a stub (🟡), what is planned (⬜) |
| [BATTLE_ROADMAP.md](BATTLE_ROADMAP.md) | The largest unimplemented system, in 8 phases |

## Guides — how to use each system

| Guide | Covers |
|---|---|
| [BUILDING.md](Guides/BUILDING.md) | Packaging the plugin, the `-Rocket` flag, what breaks a packaged build |
| [WORLD_STATE.md](Guides/WORLD_STATE.md) | Event flags, chests, Revival Trees |
| [SAVE_LOAD.md](Guides/SAVE_LOAD.md) | 15 save slots, Gold, playtime, New Game, who spawns the player on a load |
| [INVENTORY.md](Guides/INVENTORY.md) | `DT_Items` column reference, add/remove/use, `UseItem` routing |
| [ITEMS_SCREEN.md](Guides/ITEMS_SCREEN.md) | The inventory UI: filters, "Recent", discarding, what the description shows |
| [SHOP.md](Guides/SHOP.md) | `DT_Shops`, buying/selling, platinum_card, flag gating |
| [PARTY.md](Guides/PARTY.md) | Roster vs formation, XP split among survivors, the Party screen |
| [STATUS.md](Guides/STATUS.md) | Equipment, conditions, Ra-Seru, affinity, the `effective ( base )` stat |
| [AUDIO.md](Guides/AUDIO.md) | BGM persisting across maps, 4 volume channels |
| [CAMERA.md](Guides/CAMERA.md) | 13 presets, automatic UI restore, shakes, level cameras and zones |
| [MENU_AND_OPTIONS.md](Guides/MENU_AND_OPTIONS.md) | Main menu and the options screen (visuals done, controls not wired) |
| [WEBUI_API.md](Guides/WEBUI_API.md) | The shell's API, states, JS contracts, editing workflow |
| [DEV_MENU.md](Guides/DEV_MENU.md) | The dev screen and its command line (compiled out in Shipping) |

## Reference — knowledge not derivable from the code

| Document | What it is |
|---|---|
| [PROGRESSION.md](Reference/PROGRESSION.md) | XP curve, stat growth and difficulty, derived from the original disc and validated against it |
| [ULTRALIGHT_INTEGRATION.md](Reference/ULTRALIGHT_INTEGRATION.md) | How the HTML UI actually renders: dedicated thread, custom D3D11 GPU driver, shared texture |

## Data

**The CSV is the source of truth.** Edit it in LegaiaStudio
(`python Extras/LegaiaStudio/studio.py`) and import into Unreal as a DataTable. The TOMLs in
`Data/gamedata/` were one-way importers: they brought the original's content across once and
remain for reference.

| File | Rows | Row Struct |
|---|---|---|
| [DT_Items.csv](Data/DT_Items.csv) | 225 | `FItemData` |
| [DT_Shops.csv](Data/DT_Shops.csv) | 32 | `FShopData` |
| [DT_Characters.csv](Data/DT_Characters.csv) | 4 | `FCharacterData` |
| [DT_LevelCurve.csv](Data/DT_LevelCurve.csv) | 98 | `FLevelCurveRow` |
| [DT_StatGrowth.csv](Data/DT_StatGrowth.csv) | 32 | `FStatGrowthRow` |
| [DT_GrowthCurve.csv](Data/DT_GrowthCurve.csv) | 98 | `FGrowthCurveRow` |
| [DT_Difficulty.csv](Data/DT_Difficulty.csv) | 3 | `FDifficultyScaling` |
| [DT_CameraPresets.csv](Data/DT_CameraPresets.csv) | 13 | `FCameraPresetRow` (import as `DT_Camera`) |

`Data/_backup/` holds the copies LegaiaStudio makes on every save.

## Tools

[LegaiaStudio](../Extras/LegaiaStudio/README.md) — a local web panel that edits the CSVs
record by record and runs the builds.

`python Extras/generate_status_map.py` regenerates the status map in the root README
(`Docs/status/map.en.svg` and `map.pt-BR.svg`). Update the tables at the top of the script when a
system or a battle phase changes state.

---

## Conventions

- Every subsystem inherits `UGameInstanceSubsystem`; public functions are
  `UFUNCTION(BlueprintCallable)`.
- Subsystems talk through the GameInstance (`GetSubsystem<>()`), never by direct reference.
- **Every player-facing string is in English.** Code comments and logs are in Portuguese.
- `shell.html` is generated — edit `Content/UI/WebUI/src/` and run
  `python Extras/build_webui.py`.
