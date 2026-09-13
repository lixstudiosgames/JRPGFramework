# Architecture

**JRPGFramework — Legend of Legaia Remake | Unreal Engine 5.8 | C++ | LIX Studios**

A modular plugin implementing the systems of a Legend of Legaia–style JRPG. The plugin is
independent of the game project: it defines structs, subsystems and the UI, but references no
specific assets — textures, meshes and sounds live in the project.

Per-system usage lives in [`Guides/`](Guides/). The file-by-file tree is in
[`STRUCTURE.md`](STRUCTURE.md).

---

## Ground rules

- Every subsystem inherits `UGameInstanceSubsystem` (the game uses `GI_Main`).
- Public functions are `UFUNCTION(BlueprintCallable)` so Blueprints can reach them.
- Subsystems talk to each other through the GameInstance (`GetSubsystem<>()`), never by
  direct reference.
- Game assets (DataTables, sounds, textures) live in the project, not the plugin.
- The UI is HTML/CSS/JS through **Ultralight** (GPU mode) in a single generated shell.
- There is **no custom Editor module** — standard Unreal DataTables only.
- **The CSVs in `Data/` are the source of truth** for game data. Edit them through
  **LegaiaStudio** (`python Extras/LegaiaStudio/studio.py`) and import into Unreal.
- **Every player-facing string is in English.** Code comments and logs are in Portuguese.

---

## Subsystem map

```
GI_Main (BP, inherits UJRPGGameInstance)
 │  (subsystems self-register)
 ├── UCoreSubsystem        ← Gold, playtime, current map, player position, New Game,
 │                            bIsNewGame, XP curve, stat growth, difficulty
 ├── USaveSubsystem        ← 15 save slots on disk; gathers and restores everything
 ├── UInventorySubsystem   ← player items (DT_Items)
 ├── UWorldStateSubsystem  ← event flags, opened chests, Revival Trees, bIsInField
 ├── UShopSubsystem        ← buy/sell (DT_Shops); platinum_card and event-flag gates
 ├── UAudioSubsystem       ← BGM persisting across maps + volumes (JRPGSettings on disk)
 ├── UCameraSubsystem      ← NPC focus (13 presets + DOF), automatic UI restore, shakes,
 │                            hand-placed cameras + JRPGCameraZone, battle groundwork
 ├── UWebUISubsystem       ← owns the HTML UI (Ultralight shell)
 │    └── UWebUIBridge     ← JS→C++ bridge
 ├── UPartySubsystem       ← roster (who is yours) + formation (who fights), level/XP/BASE
 │                            stats, HP/MP/AP. Owns the RECORD
 ├── UStatusSubsystem      ← equipment, conditions, Ra-Seru, affinity, EFFECTIVE stats.
 │                            Owns the RULES — writes into the Party's record
 ├── UArtsSubsystem    🟡  ← stub
 └── UBattleSubsystem  🟡  ← stub
```

## Data flows

**Saving.** `USaveSubsystem` is the orchestrator — on save it READS from everyone (Core +
Inventory + WorldState + Party) and writes one `UJRPGSaveGame` to disk; on load it WRITES back
into everyone. No subsystem writes the game save on its own. (Audio settings have their
**own** save, `JRPGSettings`, managed by AudioSubsystem.)

**Using an item.** `InventorySubsystem::UseItem` routes the effect by category: consumable →
Party, permanent stat → Status (`ApplyPermanentStatUpgrade`, the Waters), art book → Arts,
battle item → Battle (the last two still stubs that log). It fires the `OnItemUsed` delegate
for Blueprints.

**Shopping.** `UShopSubsystem` consumes Gold (Core), items (Inventory) and conditions
(WorldState); the shop UI talks to it through WebUISubsystem/Bridge.

**Party vs Status.** One record, two owners. Party stores it; Status writes `Equipment`,
`Conditions` and `SeruLevel` through `FindMemberForWrite()`. See
[`Guides/STATUS.md`](Guides/STATUS.md).

---

## Rules that must not be broken

A consolidation of decisions and already-fixed bugs. Breaking any of these reintroduces a
problem someone already paid for.

### Render / Ultralight

1. **`#ul-repaint-anchor`** (1px, alpha 0.02, in `shell.template.html`) keeps the page from
   ever being empty. Without it, hiding the last section leaves Ultralight with no draw
   commands and the final frame is never presented — **the UI freezes on screen**.
   `kickUIRepaint()` (`00_core.js`) runs after every close AND open (opening from a nearly
   empty page could lose the frame too — the shop was invisible until you navigated). Do not
   remove either one.
2. **`setInterval` does not fire reliably in-game** (works in a browser and in Node, fails in
   game). Visible periodic updates use a `requestAnimationFrame` loop that only touches the
   DOM when the value changes (`fpsMonitor`/`playtimeTicker`). One-shot `setTimeout` works.
   **But `requestAnimationFrame` only runs when the page has damage** — an animation that
   changes no pixel (a counter ticking by itself) stalls. For those, use chained `setTimeout`
   counting the steps; that is how the shop's gold counter started working.
3. Ultralight SDK headers **never** under `Public/` (their macros corrupt UE headers).
4. Never touch a UObject on the UL thread — JSC callbacks only extract data and dispatch via
   `ExecuteOnGameThread`.
5. Thread/Renderer/View are process singletons — do not stop and recreate them between PIE
   sessions.
6. KeyedMutex protocol: UL acquires(0) → draws → releases(1); UE acquires(1) → blits →
   releases(0). Never acquire or release without drawing.

### Shell / JS

7. `shell.html` is GENERATED — edit only `src/` and run `build_webui.py`.
8. The JS bridge is ALWAYS resolved lazily through `getBridge()` — capturing it in a const
   goes stale between PIE sessions (the build fails if it detects one).
9. Every screen module exposes `reset()` and registers in `resetUIShell()` (the document
   survives between PIE sessions; inherited state must be cleared).
10. Game data strings in `ExecuteJS` ALWAYS go through `JRPGWebUI::ToJSStringLiteral`; raw
    numbers are fine.
11. The JS↔C++ contracts are listed in [`Guides/WEBUI_API.md`](Guides/WEBUI_API.md) — do not
    rename them.
12. Layout is a 1920×1080 canvas × `--scale-factor` (set by `updateUIScaleFactor` on
    load/resize/open). New screens follow the same pattern. A background that must cover the
    whole screen on ultrawide **cannot** live inside the scaled canvas.
13. **`position: fixed` inside an element with a `transform` anchors to that element, not the
    viewport.** Since the UI root is transform-scaled, "fixed on screen" means leaving the
    transformed subtree — this broke the hints footer three times.
14. **Ultralight/WebKit renders `radial-gradient(ellipse …)` as a circle.** Do not use it.
15. **No `<use>` / SVG sprites** — they fail silently and there are no devtools to find out.
    Icons are inline `<path>`.
16. A component appearing on more than one screen (`.jrpg-card`, `.jrpg-tab`, `.jrpg-hints`)
    lives in `00_global.css`. Do not duplicate it in a screen's CSS.

### Save / state

17. New `EJRPGUIState` values ALWAYS go at the end of the enum (serialized in Blueprints).
18. `PendingLoadSave` MUST be a `UPROPERTY` (GC runs during LoadMap).
19. Loading through the UI: C++ restores the input mode BEFORE `OpenLevel` (JS hides the
    screen and calls `onloadslot` WITHOUT `closemenu`).
20. Load restoration happens only in `PostLoadMapWithWorld`, never before the new map exists.
21. Gold/playtime/map/position/bIsNewGame live in **CoreSubsystem** (a design decision) — the
    SaveSubsystem only orchestrates.
22. Indices and IDs coming from JS are validated in C++ (slots 0..14; purchases only from the
    RESOLVED stock with gates applied). Never trust the payload.

### Shop / audio

23. A purchase is atomic: if `AddItem` fails after `RemoveGold`, the gold is refunded.
24. Audio settings live in the **"JRPGSettings"** slot (`UJRPGSettingsSave`) — NEVER mixed
    with the 15 game saves. New settings fields go into that save.
25. In Blueprint `PlaySound` nodes, wire the **effective** getters (Master × channel) — the
    plain getters exist only so the options UI can show slider positions.

### Progression

26. The **formula** in `GetXPForLevel` is the runtime source of truth; `DT_LevelCurve` is
    derived from it for reference in the Studio. Changed the formula? Check against L2=121,
    L37→L38=535546, L99=9646483 and the new-game thresholds (Vahn 121, Noa 102, Gala 140).
27. Stat growth **does not land on `MaxValue`** at L99, and that is correct — see
    [`Reference/PROGRESSION.md`](Reference/PROGRESSION.md).
28. New `EJRPGDifficulty` values ALWAYS go at the end of the enum (serialized in BP and in the
    save).
29. Difficulty does not touch Vahn's, Noa's or Gala's stats — only enemies and rewards. The
    **only** exception is `FStatGrowthRow`'s `MinDifficulty`, which unlocks growth for someone
    who does not level up in the original (Terra). Any further exception must go through that
    same field — no hardcoded gate by name.

### Data

30. **The CSV is the source of truth**, not the TOML. The `generate_*.py` scripts rewrite the
    whole file — running one after hand-editing wipes the edit. That is why LegaiaStudio does
    not expose them.
31. LegaiaStudio backs up into `Docs/Data/_backup/` on every save and preserves each file's
    line endings. A round trip must stay byte-identical.
32. **An `FString` array in CSV needs quotes on every item**: `("Knives","Swords")`. Without
    them the importer refuses with *"Missing opening `"` in string property value"*. An
    **enum** array is the opposite — `(Fire,Wind)`, no quotes. `DT_Items` always got this
    right in `EquipOthers`; the character generator was the one that got it wrong.

### Names (UHT)

33. **No plugin header may share a FILE NAME with an engine header**, even in a different
    folder. `Party/PartyTypes.h` collided with the `OnlineFramework/Party` plugin.
34. **No reflected type may share its unprefixed name** with an engine type: `FPartyMember`
    and `UPartyMember` are both `PartyMember` to UHT. That is why nearly everything here uses
    the `JRPG` prefix — `Party/` in particular is a dangerous namespace.

### Dynamic UI

35. **Do not capture a `NodeList` in a `const` for elements generated at runtime.** The menu's
    status cards started being born from `JRPGSetParty()`, and the `menuCards` captured at load
    left exactly those out of the animations — the menu froze half-open. Use a function that
    queries at call time.
36. **Animating a generated element goes by CLASS, never by id.** The old rules were
    `#card-status-vahn.animate-in` and friends: any character outside those three (Terra) was
    invisible forever.
37. **Every screen returning to the pause menu must call
    `bridge.onuistatechanged('menu_open')`** on close. Without it, C++ still believes the
    previous screen is open and refuses the next one — the menu locks up.
38. **Every player-facing string is in ENGLISH.** Comments and logs stay in Portuguese. The
    whole shell followed this; the Party and Items screens came out mixed and were fixed.
39. **Gameplay rules do not live in the UI.** The screen asks the subsystem and draws the
    answer — it never infers. `CanDiscardItem` and `CanSellItem` exist for this reason: when JS
    inferred "can discard" from the category, it allowed Art Books that C++ then refused. The
    payload carries the finished answer (`candiscard`).

### Party / Status

40. **One record, two owners — and the record belongs to Party.** `UStatusSubsystem` has NO
    array of its own: it writes into `FJRPGPartyMember` through `FindMemberForWrite()`.
    Splitting the data would create two lists in the save to keep in sync, and one day they
    diverge.
41. **Only `UStatusSubsystem` writes `Equipment`, `Conditions` and `SeruLevel`.** The fields
    live in Party because there is a single save, but the rule belongs to Status. Writing them
    directly bypasses `CanEquip` validation and the delegate the UI listens to.
42. **Every Status write calls `NotifyMemberChanged()`.** That is what fires `OnPartyChanged`;
    without it the UI does not redraw and the player sees a stale value.
43. **The stat cap is always the LAST step of the calculation.** Adding the percentage after
    the clamp would let AGL exceed 280 — the maximum number of art blocks the command bar
    supports (see `CoreSubsystem::SetStatCap`).
44. **An optional `FName` heading to the UI goes through `JRPGWebUI::OptionalNameToJS()`.**
    `FName::ToString()` on `NAME_None` returns the word **"None"** — a non-empty, therefore
    *truthy*, string in JS. The Items screen's `if (it.art)` passed, and every sword announced
    "Teaches Art: None". The helper sends `''`, which is falsy. For a DataTable row KEY (never
    None), plain `ToJSStringLiteral` is still fine.
45. **A screen that is already open does not refresh itself.** The menu's status cards were
    only born in `OpenMenu()`. Anyone who entered the Party screen, changed the formation and
    came back through the UI (without passing `OpenMenu` again) saw the old formation. Now
    `HandleUIStateChangedFromJS('menu_open')` rebuilds them — returning to the menu is the
    single point EVERY screen passes through.
46. **An audio component with `bAutoDestroy = false` must be retired by hand.** BGM is created
    that way to survive a map change; the price is that `Stop()` does not destroy it. Dropping
    the reference leaves a stopped `UAudioComponent` behind — one per track change. Always go
    through `RetireBGMComponent()`, which turns `bAutoDestroy` back on before the fade (or
    destroys immediately on a hard cut).
47. **A text heuristic does not become data.** `generate_csv.py` read the English description
    to infer fields: it saw the word "maximum" in *"Permanently raises maximum HP by 16"* and
    wrote `HealHP = 9999` on the Waters. The UI promised a full heal that C++ never applied.
    When the generator lacks the data, the right answer is to leave it blank and map it by
    hand.

---

## Data

**The CSV is the source of truth.** Edit it in LegaiaStudio and import into Unreal as a
DataTable. The TOMLs in `Data/gamedata/` were one-way importers: they brought the original's
content across once and remain for reference.

| File | Rows | Row Struct |
|---|---|---|
| `Data/DT_Items.csv` | 225 | `FItemData` |
| `Data/DT_Shops.csv` | 32 | `FShopData` |
| `Data/DT_Characters.csv` | 4 | `FCharacterData` (Vahn/Noa/Gala/Terra) |
| `Data/DT_LevelCurve.csv` | 98 | `FLevelCurveRow` (reference — runtime uses the formula) |
| `Data/DT_StatGrowth.csv` | 32 | `FStatGrowthRow` (3 from the disc + Terra, designed) |
| `Data/DT_GrowthCurve.csv` | 98 | `FGrowthCurveRow` (extracted from the disc) |
| `Data/DT_Difficulty.csv` | 3 | `FDifficultyScaling` |
| `Data/DT_CameraPresets.csv` | 13 | `FCameraPresetRow` (import as `DT_Camera`) |

`Data/_backup/` holds the copies LegaiaStudio makes on every save.

## Tools

| Tool | Purpose |
|---|---|
| `Extras/LegaiaStudio/` | Local web panel to edit the CSVs record by record; reads the schema from the C++ structs, backs up automatically, runs the builds with a live log |
| `Extras/build_webui.py` | Generates `shell.html` from `src/` |
| `Extras/build_plugin.py` | Packages the plugin |
| `Extras/extract_growth_from_disc.py` | Reads `SCUS_942.54` → `DT_StatGrowth` + `DT_GrowthCurve` |
| `Extras/generate_*.py` | TOML → CSV importers (one-way) |
| `Extras/download_fonts.py` | Verifies/downloads the WebUI's local fonts |
