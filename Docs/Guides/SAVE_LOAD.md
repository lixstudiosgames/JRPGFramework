# Save/Load, Gold and Playtime

Two pieces, both `UGameInstanceSubsystem` (they survive map changes):

| Subsystem | Role |
|---|---|
| `UCoreSubsystem` (`Core/CoreSubsystem.h`) | Source of truth for global state: **Gold**, **playtime**, **current map**, **player position**, **bIsNewGame** and the New Game flow |
| `USaveSubsystem` (`Save/SaveSubsystem.h`) | Orchestrates the **15 save slots** on disk (`JRPGSave_0`..`JRPGSave_14`): gathers everything on save, restores everything on load |

What a save persists (`UJRPGSaveGame`): version + timestamp, map (technical name for
`OpenLevel` plus a display name), player position/rotation, Gold, playtime, the full
inventory, event flags, opened chests, Revival Trees, and party display data.

---

## 1. Blueprint setup

1. **New Game (main menu):** `Get Game Instance Subsystem → CoreSubsystem → StartNewGame`
   **BEFORE** `OpenLevel` of the starting map. This clears inventory/world/gold/playtime,
   starts the clock and sets `bIsNewGame = true`.
2. **Save statue (Actor BP):** on Interact → `WebUISubsystem → OpenSaveLoad`. That is all —
   the UI handles the grid, overwrite prompt and load.
3. **Each map's BeginPlay (optional):**
   `CoreSubsystem → SetCurrentMapDisplayName("Rim Elm")` so the save UI shows a readable
   name instead of the technical level name.
4. **Load:** nothing to wire — `LoadGame` opens the saved map itself and, once the level
   finishes loading, restores the data and teleports the player.

## 2. Gold

- `AddGold(Amount)` — adds (amounts ≤ 0 are ignored).
- `RemoveGold(Amount)` — returns **false and changes nothing** when the balance is too low
  (what the shop relies on).
- `GetGold()` — current value.
- **`OnGoldChanged(NewGold)`** — bind it in the HUD to refresh the on-screen value.

## 3. Playtime

- `StartPlaytimeTracking()` — already called by `StartNewGame` and re-anchored after a load;
  only call it directly in special flows.
- `GetPlaytimeSeconds()` / `GetPlaytimeFormatted()` ("HH:MM:SS").
- Tick-free: accumulated time plus a delta from the platform clock, computed on demand.

## 4. bIsNewGame

- `IsNewGame()` — **true** from `StartNewGame` until a save is loaded (the load's
  `RestoreFromSave` sets it false). Useful for cutscenes or setup exclusive to a new game.
- `SetIsNewGame(bool)` — manual control if you need it.

## 5. Save/Load from code (no UI)

- `SaveGame(SlotIndex)` — gathers EVERYTHING automatically and writes (0..14).
- `LoadGame(SlotIndex)` — false if the slot is empty, corrupt or an incompatible version.
- `DoesSlotExist(i)` / `DeleteSlot(i)` / `GetSlotMetadata(i)` / `GetAllSlotsMetadata()`.
- Files land in `<Project>/Saved/SaveGames/JRPGSave_N.sav`.

## 6. The statue UI flow

`OpenSaveLoad()` → pick **Save** or **Load** → 5×3 grid (15 slots) → detail card below
(portrait, map, TIME, GOLD, date, party) → confirm:

- **Save into an empty slot:** writes immediately (sound + flash on the slot).
- **Save over an existing slot:** an "Overwrite this record?" dialog defaulting to **No**.
- **Load an occupied slot:** closes the screen, changes map and restores everything.
- **Escape/B:** backs out one level at a time (confirm → grid → choice → close).

## 7. How Load works internally (for debugging)

1. `LoadGame` validates the slot and stores the save in `PendingLoadSave` (a `UPROPERTY`, so
   it is protected from GC).
2. **BGM is cut** (`AudioSubsystem->StopBGM(0)`). It persists across maps on purpose — that
   is what avoids a cut when walking through a door — but on a load that would leave the
   track from wherever you were playing in the place you loaded into. A hard cut rather than
   a fade: the component is created with `bAutoDestroy=false`, so a `FadeOut` would leave a
   stopped component behind.
3. `OpenLevel(MapName, bAbsolute=true)` — **nothing** is restored before the new map exists.
   It **always reloads**, even into the map you are already in: without that, a load "in
   place" would leave standing everything the map created since it opened (an opened chest
   would stay open on screen, a moved NPC would stay moved). The log says when this happens.
4. `PostLoadMapWithWorld` fires → inventory/world/party through their subsystems,
   Gold/playtime/map through `CoreSubsystem->RestoreFromSave`, and the player is teleported
   (retrying per tick if the pawn has not spawned yet — up to 10 attempts).
5. If after a 3-tick grace period there is still no pawn, SaveSubsystem **spawns one
   itself** at the saved position and possesses it. **This is the normal path**, not a
   failure — see below.
6. `WebUISubsystem` rebuilds the UI shell in the new world (`OpenLevel` destroys the widgets).

## 8. Known gotchas

- The map name is saved **without** the PIE prefix (`UEDPIE_0_`), so the same name works in
  a packaged build.
- A `SaveVersion` newer than supported → the slot is treated as empty (never a crash).
- No possessed pawn at save time → position saved as the origin (warning in the log).
- If playtime was never started, the save records 0s and logs a warning — check
  `StartNewGame`.

## 9. Who spawns the player on a Load

These lines appear in the log on every load and are **not errors**:

```
LogGameMode: FindPlayerStart: PATHS NOT DEFINED or NO PLAYERSTART with positive rating
LogSpawn: Warning: SpawnActor failed because of collision at the spawn location [X=0 Y=0 Z=0]
LogGameMode: Warning: SpawnDefaultPawnAtTransform: Couldn't spawn Pawn of type ...
```

The project's GameMode picks a `PlayerStart` **by tag** — that is how each portal decides
where the player appears in the destination map. **A load comes from no portal**, so there
is no tag, the GameMode finds no PlayerStart and spawns nobody. Those three lines are just
it trying the origin fallback and giving up.

The Load is what spawns: `SpawnPlayerPawnAt()` creates the pawn (the GameMode's
`DefaultPawnClass`), **places it at the saved coordinate**, and only then possesses it — in
that order.

**Why not place an untagged `PlayerStart` in every map** (which would let the GameMode spawn
on its own): it would also become the destination for any portal whose tag does not match.
The player would appear in a random corner instead of the problem surfacing, and a broken
portal would go unnoticed. The Load knows exactly where the player was — it holds the right
information.

Implementation details:

| | |
|---|---|
| Grace period | 3 ticks, only so it does not fight a GameMode that spawns a tick later. It is not waiting for something that will arrive |
| Collision | `AlwaysSpawn`, not `Adjust*` — the position comes from the save, which is where the player was standing; letting the engine push them to "somewhere free nearby" would make them reappear away from where they saved |
| Log | `Pawn spawned by Load at (X,Y,Z)`, at Log level. It only becomes `Error` if even that fails, which means the GameMode's `DefaultPawnClass` is the problem |
