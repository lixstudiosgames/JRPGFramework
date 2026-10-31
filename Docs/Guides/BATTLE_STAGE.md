# The battle stage

How a fight is assembled over the current map and taken down again. Phase 4 of
[BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md).

There is **no combat yet** (Phase 5). A fight starts, the scene is assembled, the heroes and
the enemies stand in their seats in `Idle`, and it ends from the console.

**The only thing placed in a map is the arena.** Everything else exists by itself.

---

## 1. Who does what

| Piece | Job |
|---|---|
| `UBattleSubsystem` (GameInstance) | Starts and ends a fight (`BeginBattle` / `EndBattle`), builds the combatant list, blocks SAVE during it. Knows no actor — only `OnBattleStarted` / `OnBattleEnded`. |
| `UJRPGBattleSceneSubsystem` (World) | Exists in every played map, nothing to place or set. Picks the arena, fades to black, tells the field when the screen is black, asks the arena to assemble / tear down. Holds the console commands. |
| `AJRPGBattleArena` | Where the fight goes **and who assembles it**: hides the field, spawns the combatants in its seats, points the camera, puts everything back. All per-arena settings live here. |
| `AJRPGBattlePawn` | One combatant in the scene: the **battle** model (not the field one) in its seat. |
| `AJRPGFieldEnemy` | Starts a fight on contact (`bStartBattle`); stops when one starts, hides and goes home when the screen is black. |
| `AJRPGBattleWaves` | Fights in a row, Muscle Dome style — the test map. Reads its waves from a JSON the LegaiaStudio edits. |

A fight, start to end:

1. Contact (or a wave, or the console) calls `UBattleSubsystem::BeginBattle(Setup)`. The party
   is `Setup.Party`, or the active party, or — PIE straight into a map with no save —
   `FallbackParty` (Vahn, Noa, Gala). At most `MaxPartySlots` heroes and `MaxEnemySlots`
   enemies. **Everything stops on the spot:** the player loses control, the party stops dead
   (no sliding), and every field enemy — the one that touched included — stops and goes to
   `Idle` (stopped, not frozen mid-stride). Nobody disappears yet.
2. The scene subsystem picks the arena — `AJRPGBattleArena::FindArenaFor(Setup.Location)`; a
   map with no arena gets a temporary one at the contact point, the party facing the enemy —
   and fades to black over the arena's `TransitionTime`.
3. With the screen **fully black**:
   - `OnFieldHidden`: the enemy that touched hides and schedules its respawn; every other field
     enemy hides and goes home — a standalone one to where it was placed, a zone one to a random
     point of its zone. Nobody is left glued to the player for when the fight ends.
   - The arena hides and freezes the field party (the controlled character and the followers)
     and its `HideDuringBattle`, turns the field controls off, spawns one `AJRPGBattlePawn` per
     combatant at its seats (`GetFormation`), and points the camera — its `BattleCamera`, or
     `UCameraSubsystem::FrameGroup` over every combatant.
   - The screen opens.
4. `EndBattle(Result)` fades to black; the arena destroys the pawns and puts the field back
   exactly as it was (controls, camera, what was hidden); `OnFieldShown` brings the field
   enemies back at home; the screen opens. For `EncounterGraceTime` seconds after that no new
   encounter starts.

---

## 2. The models: straight from the DataTables

Nothing to configure per fight or per map.

- **Heroes:** `DT_Characters` › `BattleMesh` + `BattleAnims` (`Idle`, `Walk`) — the Phase 2
  battle models, `/Game/Legaia/Battle/Party/<Name>/`. Vahn, Noa and Gala come filled in
  `Docs/Data/DT_Characters.csv`; reimport the DataTable after updating the plugin.
  `Extras/generate_characters_csv.py` keeps these two columns when it regenerates the CSV.
- **What the hero wears:** with rows in `DT_EquipVisuals`, the hero is assembled from five
  pieces — Body, Head, Weapon, Ra-Seru, Feet — chosen by what is equipped (see below).
- **Enemies:** `DT_Enemies` › `Mesh` + `Anims.Idle`.

### Equipment on the model

The original has no item meshes: every equipment section swaps whole objects of the skeleton
(a Survival Knife replaces Vahn's right hand with another hand that holds a blade). So a hero is
five **pieces**, each a skeletal mesh on the same skeleton as `BattleMesh`
([PS1_ASSETS.md](PS1_ASSETS.md) › Equipment pieces).

| Piece | Chosen by | `DT_EquipVisuals` row |
|---|---|---|
| `Body` | `Armor` slot | `<Hero>_Body_<item key>` |
| `Head` | `Helmet` slot | `<Hero>_Head_<item key>` |
| `Weapon` | `Weapon` slot | `<Hero>_Weapon_<item key>` |
| `RaSeru` | `SeruLevel` (not an equip slot) | `<Hero>_RaSeru_Lv<N>` |
| `Feet` | `Shoes` slot | `<Hero>_Feet_<item key>` |

An empty slot, or an item with no row, uses `<Hero>_<Piece>_Default` (a warning is logged for
the second). A hero with no `Default` row at all keeps the whole `BattleMesh`, as before.
Noa is left-handed, but pieces go by role: her `Weapon` piece is her left arm.

In the fight, `AJRPGBattlePawn::Mesh` still plays the clips but is hidden; each piece is a
component that follows its pose (Leader Pose). The hidden mesh keeps refreshing its bones
(`AlwaysTickPoseAndRefreshBones`) — otherwise the pieces would vanish with it.

**Accessories** have no model in the original. `DT_Characters` › `AccessoryVisuals` holds one
generic static mesh per accessory slot (`[0]` = Accessory 1 … `[2]` = Accessory 3), each with
the bone it hangs from (`part_NN`) and an offset — per hero, because the bodies differ. It shows
while that slot holds any accessory. Empty array = no accessory is drawn.

`DT_EquipVisuals.csv` is written by `Extras/generate_equip_visuals_csv.py`; import it as a
DataTable of `FJRPGEquipVisualRow` in `/Game/Data/`.

A combatant without a model appears as its capsule only, and the arena says why on screen
(not in `DT_Characters`, `BattleMesh` empty, `Mesh` empty in `DT_Enemies`…).

---

## 3. The arena's battle settings

`AJRPGBattleArena`, category *Battle* (the arena itself — seats, ground, area — is in
[FIELD_ENEMIES.md](FIELD_ENEMIES.md) › Where the fight happens):

| Property | Default | |
|---|---|---|
| `HideDuringBattle` | — | Actors hidden while a fight happens here (a tree in front of the camera, an NPC in a seat). |
| `BattleCamera` | — | A CameraActor / CineCameraActor placed by hand; replaces the automatic framing. |
| `CameraPreset` | `BattleWide` | Framing when there is no `BattleCamera`. |
| `CameraPadding` | 1.15 | Room around the combatants. |
| `bCameraBehindParty` | true | Camera behind the heroes looking at the enemies; off = the other way. |
| `TransitionTime` | 0.35 | Fade to black in and out; the scene changes with the screen fully black. 0 = hard cut. |
| `TransitionColor` | black | |
| `PawnClass` | `AJRPGBattlePawn` | A Blueprint child for entry effects, a name over the head… |

A map with no arena uses these defaults on the temporary one.

**Field enemies:** `bStartBattle` (default on) — off, contact only announces the encounter, as
in Phase 3.

---

## 4. The test map: Muscle Dome

`AJRPGBattleWaves` runs fights in a row. Drag one into the map:

- `WavesFile`: the JSON with the waves, relative to the project's `Content` folder —
  `Data/Waves/muscle_dome.json`. It is read **on every play**: edit in the Studio, save, play.
  Empty, the actor uses the `Waves` / `Party` properties in the Details panel. "Import Waves
  File" copies the file into those properties (saved with the map).
- `Arena`: where. Empty = the arena picked from the actor's position.
- `bAutoStart`.

Winning a wave calls the next one; losing, escaping or aborting stops them. It runs on the
field GameMode: the party stands in the ring between waves.

### Editing the waves: LegaiaStudio › Ondas

`python Extras/LegaiaStudio/studio.py` → **Batalha › Ondas**. One file per set of waves, in
`<Unreal project>/Content/Data/Waves/` (`waves_dir` in `studio.config.json` changes it):

- party as chips, in formation order (empty = the save's active party);
- per wave: a name, enemies per fight min–max, and the enemies picked from `DT_Enemies` with
  level range, count range, weight and *Sempre entra* (joins every fight);
- what each wave can roll, and what would go wrong in the game (nobody can join, unknown key,
  enemy without a model);
- saving keeps a backup in `<Unreal project>/Saved/LegaiaStudio/WavesBackup/`.

### The JSON

```json
{
  "name": "Muscle Dome",
  "party": ["Vahn", "Noa", "Gala"],
  "loop": false,
  "startDelay": 2,
  "delayBetweenWaves": 2,
  "waves": [
    {
      "name": "Gimard",
      "enemiesMin": 1,
      "enemiesMax": 1,
      "enemies": [
        { "key": "gimard", "levelMin": 1, "levelMax": 1, "countMin": 1, "countMax": 1,
          "weight": 1, "alwaysJoins": true }
      ]
    }
  ]
}
```

Each wave is an `FJRPGEncounterSetup`, rolled like the field encounters: the `alwaysJoins`
enemies go in first (`countMin..countMax` each), the rest are drawn by `weight` up to
`enemiesMin..enemiesMax`. A fixed wave is one line with `alwaysJoins` and `countMin = countMax`.

**Packaged builds:** add `Data/Waves` to *Project Settings › Packaging › Additional Non-Asset
Directories to Package*. The testers can then change the waves without a new build.

---

## 5. Console (PIE)

| Command | |
|---|---|
| `JRPG.Battle.Start <EnemyKey> [Count] [Level]` | A fight where the player stands. `JRPG.Battle.Start gimard 3` |
| `JRPG.Battle.End [victory\|defeat\|escape\|abort]` | Ends the current fight (victory by default). In the Muscle Dome, `victory` calls the next wave. |

---

## 6. Known limits (Phase 4)

- No combat, no HUD: the fight only ends from the console.
- The heroes only have `Idle` and `Walk` so far (Phase 2).
- A monster with morph targets (Green Slime) crashes PIE when the project's GPU skin cache is
  off (engine assertion in `SkeletalRenderGPUSkin.cpp`). Turn on **Project Settings ›
  Rendering › Support Compute Skin Cache**.
