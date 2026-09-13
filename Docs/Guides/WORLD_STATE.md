# World State (Event Flags, Chests, Revival Trees)

`UWorldStateSubsystem` tracks everything the world remembers: story flags, opened chests and
Genesis Tree stages. Like every subsystem here it inherits `UGameInstanceSubsystem`, so it
initializes automatically and survives level transitions.

To reach it from any Blueprint (GameInstance, player character, chests, NPCs): right-click
and type **Get WorldStateSubsystem**, or use **Get Game Instance Subsystem** and pick
`WorldStateSubsystem`.

---

## Event flags

Replaces scattering dozens of loose booleans around. Any flag is stored and read
dynamically by a unique `FName`.

| Function | Purpose |
|---|---|
| `SetEventFlag(FlagID, Value, Metadata)` | Set a flag. `Metadata` is an optional debug description |
| `GetEventFlagValue(FlagID)` | Returns the flag's value; `false` if it was never set |
| `HasEventFlag(FlagID)` | Whether the flag was ever registered |
| `GetAllEventFlags()` | Array of `FEventFlag` — IDs, values and metadata (used by Save/Load) |

## Chests

Stores whether a chest was already opened so the player cannot loot it twice.

| Function | Purpose |
|---|---|
| `SetChestOpened(ChestID, bOpened)` | Record a chest's state |
| `IsChestOpened(ChestID)` | Whether it was opened |
| `GetOpenedChests()` | Array of every opened chest ID |

In practice: call `IsChestOpened` on your chest Blueprint's `BeginPlay`. If it returns true,
switch the mesh/animation to the open state and disable interaction.

## Revival Trees (Genesis Trees)

Manages the stages of the Genesis Trees, a central theme of Legend of Legaia.

| Function | Purpose |
|---|---|
| `SetRevivalTreeState(TreeID, Stage)` | Set a tree's stage |
| `GetRevivalTreeState(TreeID, OutState)` | Fetch it as an `FRevivalTreeState`; returns false if unknown |
| `GetAllRevivalTreeStates()` | Every tree's state |

`ERevivalTreeStage`: `Idle` (dormant, covered by the Mist) · `Awake` (woken by the Ra-Seru) ·
`Grow` · `Purification` · `FullRestored`.

## Field vs battle

`SetIsInField(bool)` / `GetIsInField()` — this gates the SAVE tab in the UI. Set it false
when entering a battle or a cutscene.

## New Game

`ResetWorldState()` clears every event flag, opened chest and tree state back to factory
defaults. Call it when starting a New Game (`CoreSubsystem::StartNewGame` already does).

---

## Recommended ID conventions

To avoid name collisions across Blueprint calls:

| Category | Pattern | Example |
|---|---|---|
| Story events | `bEv_[EventName]` | `bEv_DrakeCastleRestored` |
| Chests | `Chest_[Level]_[Item]` | `Chest_RimElm_HealingLeaf` |
| Genesis Trees | `Tree_[Location]` | `Tree_MtRikuroa` |
