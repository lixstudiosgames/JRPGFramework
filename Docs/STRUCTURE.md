# JRPGFramework — Plugin Structure

Legend of Legaia Remake | Unreal Engine 5.8 | C++ | LIX Studios

Legend: ✅ implemented · 🟡 stub (file exists, no real state or logic) · no mark = planned (file does **not** exist yet)

```
JRPGFramework/
├── JRPGFramework.uplugin ✅                     ← EngineVersion 5.8.1, single Runtime module
├── SETUP.md ✅                                  ← build from a clean clone (incl. Ultralight SDK)
│
├── Source/JRPGFramework/
│   ├── JRPGFramework.Build.cs ✅                ← links the Ultralight SDK + copies its DLLs
│   │
│   ├── Public/
│   │   ├── JRPGFramework.h ✅
│   │   │
│   │   ├── Core/ ✅
│   │   │   ├── CoreSubsystem.h ✅               ← Gold (Add/Remove/Get + OnGoldChanged), playtime
│   │   │   │                                       (tick-free), current map, player position for
│   │   │   │                                       saves, StartNewGame/bIsNewGame, XP curve, stat
│   │   │   │                                       growth and difficulty
│   │   │   ├── ProgressionTypes.h ✅            ← EJRPGDifficulty + FDifficultyScaling,
│   │   │   │                                       FLevelCurveRow, FStatGrowthRow, FGrowthCurveRow
│   │   │   ├── JRPGGameInstance.h ✅            ← GameInstance base class (GI_Main inherits it)
│   │   │   └── JRPGTypes.h ✅                   ← global enums and structs
│   │   │
│   │   ├── Save/ ✅                             ← SAVE/LOAD COMPLETE
│   │   │   ├── SaveSubsystem.h ✅               ← 15 slots; SaveGame gathers everything itself;
│   │   │   │                                       LoadGame opens the saved map and restores in
│   │   │   │                                       PostLoadMapWithWorld
│   │   │   ├── JRPGSaveGame.h ✅                ← version, timestamp, map, position, gold, playtime,
│   │   │   │                                       inventory, flags, chests, trees, party display
│   │   │   └── SaveTypes.h ✅                   ← FSaveSlotMetadata + FSavePartyMemberDisplay
│   │   │
│   │   ├── Inventory/ ✅
│   │   │   ├── InventorySubsystem.h ✅          ← add/remove/query/use + save-ready
│   │   │   └── ItemData.h ✅                    ← FItemData (DT_Items row) + FInventorySlot
│   │   │
│   │   ├── World/ ✅
│   │   │   ├── WorldStateSubsystem.h ✅         ← event flags, chests, Revival Trees, bIsInField
│   │   │   │                                       (gates the SAVE tab) + reset/load
│   │   │   ├── EventFlag.h ✅
│   │   │   └── RevivalTreeState.h ✅
│   │   │
│   │   ├── UI/ ✅                               ← WEBUI VIA ULTRALIGHT 1.4 (GPU mode, D3D11)
│   │   │   ├── WebUISubsystem.h ✅              ← owns the shell: OpenDevMenu/RunDevCommand (dev
│   │   │   │                                       only, gone in Shipping), InitializeUIShell,
│   │   │   │                                       ShowItemPopup, OpenMenu/CloseMenu,
│   │   │   │                                       OpenSaveLoad/OpenSaveScreen/OpenLoadScreen,
│   │   │   │                                       CanSaveNow, QuitToMainMenu, OpenShop,
│   │   │   │                                       OpenOptions, OpenMainMenu, SetUICursorVisible,
│   │   │   │                                       SetUIState, ReloadShell
│   │   │   ├── WebUIBridge.h ✅                 ← JS→C++ bridge (12 callbacks)
│   │   │   ├── WebContainerWidget.h ✅          ← UUserWidget hosting the browser
│   │   │   ├── JRPGWebBrowser.h ✅              ← UMG UWidget wrapping SUltralightBrowser
│   │   │   └── WebUIScripting.h ✅              ← ToJSStringLiteral (safe escaping for ExecuteJS)
│   │   │
│   │   ├── Shop/ ✅                             ← SHOP COMPLETE
│   │   │   ├── ShopSubsystem.h ✅               ← atomic purchase validated against resolved stock,
│   │   │   │                                       selling (BuyPrice/2), featured items gated by
│   │   │   │                                       platinum_card, availability by event flag
│   │   │   └── ShopData.h ✅                    ← FShopData (DT_Shops row) + FShopStockEntry
│   │   │
│   │   ├── Camera/ ✅                           ← CAMERA COMPLETE
│   │   │   ├── CameraSubsystem.h ✅             ← actor focus with presets, automatic restore when
│   │   │   │                                       the UI closes, hand-placed level cameras, target
│   │   │   │                                       registry, shot-reverse-shot conversation,
│   │   │   │                                       FrameGroup/Orbit (battle groundwork), shakes
│   │   │   ├── CameraData.h ✅                  ← 13 framing presets, 8 shakes, FCameraPresetRow
│   │   │   ├── JRPGCameraActor.h ✅             ← managed camera (one per map, transient)
│   │   │   ├── JRPGCameraShakeModifier.h ✅     ← procedural Perlin shake via UCameraModifier
│   │   │   └── JRPGCameraZone.h ✅              ← box trigger: enter → LevelCamera, exit → player
│   │   │
│   │   ├── Audio/ ✅                            ← AUDIO COMPLETE
│   │   │   ├── AudioSubsystem.h ✅              ← BGM persisting across maps with crossfade +
│   │   │   │                                       4 volume channels (with effective getters)
│   │   │   └── JRPGSettingsSave.h ✅            ← settings USaveGame (slot "JRPGSettings")
│   │   │
│   │   ├── Party/ ✅
│   │   │   ├── CharacterData.h ✅               ← FCharacterData (DT_Characters row): identity,
│   │   │   │                                       Ra-Seru, affinity, weapon classes, XPCurveSlot,
│   │   │   │                                       8 level-1 stats + EJRPGElement
│   │   │   ├── JRPGPartyTypes.h ✅              ← FJRPGPartyMember: one character's record
│   │   │   │                                       (3 states, level/XP, 8 stats, equipment,
│   │   │   │                                       conditions, SeruLevel) + EJRPGEquipSlot (5 slots)
│   │   │   └── PartySubsystem.h ✅              ← roster vs formation, recruit/activate/bench,
│   │   │                                           XP split among survivors, level-up, vitals, save
│   │   │
│   │   ├── Status/ ✅
│   │   │   └── StatusSubsystem.h ✅             ← equipment (5 slots), conditions, Ra-Seru,
│   │   │                                           elemental affinity and the EFFECTIVE stat.
│   │   │                                           Owns the RULES: writes into the Party record,
│   │   │                                           holds no state of its own
│   │   │
│   │   ├── Arts/ 🟡
│   │   │   └── ArtsSubsystem.h 🟡               ← TeachArtToCharacter only (TODO)
│   │   │
│   │   └── Battle/ 🟡
│   │       └── BattleSubsystem.h 🟡             ← ApplyBattleItemEffect only (TODO)
│   │
│   └── Private/
│       ├── JRPGFramework.cpp ✅
│       ├── Core/ ✅        (CoreSubsystem.cpp ✅, JRPGGameInstance.cpp ✅)
│       ├── Save/ ✅        (SaveSubsystem.cpp ✅, JRPGSaveGame.cpp ✅)
│       ├── Inventory/ ✅   (InventorySubsystem.cpp ✅)
│       ├── World/ ✅       (WorldStateSubsystem.cpp ✅)
│       ├── Shop/ ✅        (ShopSubsystem.cpp ✅)
│       ├── Audio/ ✅       (AudioSubsystem.cpp ✅, JRPGSettingsSave.cpp ✅)
│       ├── Camera/ ✅      (CameraSubsystem.cpp ✅, CameraData.cpp ✅, JRPGCameraActor.cpp ✅,
│       │                    JRPGCameraShakeModifier.cpp ✅, JRPGCameraZone.cpp ✅)
│       ├── UI/ ✅          (WebUISubsystem.cpp ✅, WebUIBridge.cpp ✅, WebContainerWidget.cpp ✅,
│       │                    JRPGWebBrowser.cpp ✅, WebUIScripting.cpp ✅,
│       │                    UltralightRenderThread.h/.cpp ✅, UltralightGPUDriverD3D11.h/.cpp ✅)
│       │                    ← the dedicated thread and the GPUDriver are PRIVATE on purpose:
│       │                      Ultralight SDK headers never appear under Public/
│       ├── Party/ ✅       (PartySubsystem.cpp ✅)
│       ├── Status/ ✅      (StatusSubsystem.cpp ✅)
│       ├── Arts/ 🟡        (ArtsSubsystem.cpp 🟡)
│       └── Battle/ 🟡      (BattleSubsystem.cpp 🟡)
│
├── Content/
│   ├── UI/
│   │   ├── WebUI/ ✅                            ← the HTML UI (single SPA shell via Ultralight)
│   │   │   ├── shell.html ✅                    ← GENERATED by Extras/build_webui.py — never edit
│   │   │   ├── src/ ✅                          ← UI SOURCES (edit here, then run the build)
│   │   │   │   ├── shell.template.html ✅       ← skeleton (@@CSS@@/@@SECTIONS@@/@@SCRIPTS@@)
│   │   │   │   │                                   + #ul-repaint-anchor (do not remove)
│   │   │   │   ├── css/ ✅                      ← 00_global (theme + .jrpg-card/.jrpg-tab/
│   │   │   │   │                                   .jrpg-hints + jrpgSelectBreath + kbd-mode),
│   │   │   │   │                                   05_shell, 10_menu, 20_popup, 30_dialogue,
│   │   │   │   │                                   40_options, 50_saveload, 60_shop, 70_mainmenu,
│   │   │   │   │                                   80_party, 90_devmenu
│   │   │   │   ├── sections/ ✅                 ← 10_menu, 20_popup, 30_dialogue, 40_options,
│   │   │   │   │                                   50_saveload, 60_shop, 70_mainmenu
│   │   │   │   └── js/ ✅                       ← 00_core (bridge/sound/scaling/kickUIRepaint),
│   │   │   │                                       05_ui_state (state machine + InputMode),
│   │   │   │                                       10_menu, 20_popup, 30_dialogue, 40_options,
│   │   │   │                                       50_saveload, 60_shop, 70_mainmenu, 99_boot
│   │   │   ├── fonts/ ✅                        ← local Cinzel + MedievalSharp (download_fonts.py)
│   │   │   ├── images/ ✅                       ← 1.webp…15.webp (save slot art) +
│   │   │   │                                       vahn/noa/gala.png (portraits)
│   │   │   └── Concept/ ✅                      ← visual concept HTML (style reference)
│   │   └── SFX/ ✅                              ← SFX_Open/Close/Next/Cancel/Select/Item/Return .wav
│   │
│   └── ThirdParty/Ultralight/                   ← NOT in the repository — see SETUP.md
│
├── Docs/ ✅                                     ← this documentation
│
└── Extras/ ✅                                   ← dev tools (not shipped in the build)
    ├── LegaiaStudio/ ✅                         ← local web panel (studio.py + web/): edits the
    │                                               CSVs record by record, schema read from the C++
    │                                               structs, automatic backup, builds with live log
    ├── build_webui.py ✅                        ← generates shell.html from src/
    ├── build_plugin.py ✅                       ← packages the plugin (isolation + -Rocket flag)
    ├── extract_growth_from_disc.py ✅           ← reads SCUS_942.54 from the disc → DT_StatGrowth +
    │                                               DT_GrowthCurve. Carries Terra's DESIGNED
    │                                               parameters and preserves characters you create
    │                                               in the Studio
    ├── generate_csv.py ✅                       ← item TOMLs → DT_Items.csv
    ├── generate_shops_csv.py ✅                 ← shops.toml → DT_Shops.csv
    ├── generate_characters_csv.py ✅            ← characters.toml → DT_Characters.csv
    ├── generate_progression_csv.py ✅           ← formula → DT_LevelCurve.csv + DT_Difficulty.csv
    └── download_fonts.py ✅                     ← verifies/downloads the WebUI's local fonts
```

> **The `generate_*.py` scripts rewrite the whole CSV** — running one after editing in the
> Studio wipes your edits. They were one-way importers, which is why LegaiaStudio does not
> expose them.

> **In the GAME project** (not the plugin) live the assets: `Content/Data/DT_*.uasset`
> imported from the CSVs above, plus meshes, sounds and textures.

---

## What works today

| System | State | Highlights |
|---|---|---|
| **Core** | ✅ complete | Gold with `OnGoldChanged`, tick-free playtime, map/position for saves, `bIsNewGame` |
| **Progression** | ✅ complete | XP curve with a per-character correction, stat growth for all 4 roster members (3 from the disc + Terra hand-designed), Normal/Hard/Juggernaut difficulty. Runtime state lives in Party |
| **Inventory** | ✅ complete | `DT_Items` (225), stacking up to 99, item use routed to other subsystems |
| **World State** | ✅ complete | Event flags, chests, Revival Trees, `bIsInField`; reset for New Game and atomic load |
| **Save/Load** | ✅ complete | 15 slots, automatic gather/restore, map change + teleport, save versioning |
| **Shop** | ✅ complete | `DT_Shops` (32), atomic buy/sell, featured items gated by platinum_card, flag gating |
| **Audio** | ✅ complete | BGM persisting across maps with crossfade, 4 channels saved in `JRPGSettings` |
| **Camera** | ✅ complete | 13 presets (+DOF), automatic restore when the UI closes, 8 shakes, manual camera + zone, conversation and FrameGroup/Orbit ready |
| **WebUI** | ✅ complete | SPA shell, 7 screens, gamepad + keyboard + mouse with their own rules, responsive scaling |
| **Party** | ✅ complete (data) | Roster vs formation, 3 states per character, XP split among survivors as in the original, level-up with jitter, whole-roster save. The UI does not follow the formation yet |
| **Status** | ✅ complete (data) | Equipment in 5 slots with `CanEquip` read from `DT_Items`, conditions, Ra-Seru (1..9), elemental affinity, and the original's `effective ( base )` display. The equip screen is still missing |
| **Arts / Battle** | 🟡 stubs | They receive calls from the inventory but hold no state. See [BATTLE_ROADMAP.md](BATTLE_ROADMAP.md) |

### Known gaps in shipped systems

- **Equip screen** in the WebUI (Status works; there is no UI for it).
- **Party/shop screens** still show base stats instead of effective ones.
- **Options controls do not apply yet** — the screen is built, the values are not wired to
  `UGameUserSettings` / `AudioSubsystem`.
- **Condition effects in battle** depend on the battle system.

---

## Minimum Blueprint wiring in the game project

1. `GI_Main` inherits from `UJRPGGameInstance` (subsystems self-register).
2. **Map BeginPlay:** `WebUISubsystem → InitializeUIShell` (optionally
   `CoreSubsystem → SetCurrentMapDisplayName`) + Branch on
   `AudioSubsystem → IsBGMPlaying` → false: `PlayBGM(MapBGM)`.
3. **New Game:** `CoreSubsystem → StartNewGame` **before** `OpenLevel` of the starting map.
4. **Pause:** Input Action → `WebUISubsystem → OpenMenu`.
5. **Chest:** `WorldStateSubsystem → IsChestOpened/SetChestOpened` +
   `InventorySubsystem → AddItem` + `ShowItemPopup`.
6. **Save statue:** Interact → `WebUISubsystem → OpenSaveLoad`.
7. **Shopkeeper NPC:** Interact → `CameraSubsystem → FocusOnActor(self, ShopFront)` →
   (variant? Branch on `ShopSubsystem → IsShopAvailable`) →
   `WebUISubsystem → OpenShop(ShopID)` — the camera restores itself when the shop closes.
8. **Field vs battle:** `WorldStateSubsystem → SetIsInField(true/false)` — this gates the
   SAVE tab.
9. **Teleport:** Branch on `IsBGMPlaying AND IsBGMPersistent` → false: `StopBGM(fade)`
   before `OpenLevel`.
10. **PlaySound from BP:** feed `GetEffectiveSFXVolume`/`GetEffectiveAmbientVolume` into the
    Volume Multiplier.
11. **Core tables:** import `DT_StatGrowth`, `DT_GrowthCurve` and `DT_Difficulty` into
    `/Game/Data/` under those exact names — `CoreSubsystem` resolves them in `Initialize`.
    Without the two growth tables nobody gains stats (a warning appears in the log).
12. **Camera presets (optional):** import `Docs/Data/DT_CameraPresets.csv` as `DT_Camera` and
    call `CameraSubsystem → SetCameraPresetTable(DT_Camera)` at startup.
13. **Fixed camera per area (optional):** place a `JRPGCameraZone` in the level pointing its
    `LevelCamera` at a hand-placed CameraActor.
