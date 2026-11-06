# The battle stage

How a fight is assembled over the current map and taken down again. Phase 4 of
[BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md).

A fight starts, the scene is assembled, the heroes and the enemies stand in their seats in `Idle`. Since
Phase 5 the fight is **played** (written, to build and test: [BATTLE.md](BATTLE.md)); this guide is
still the stage.

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
     combatant at its seats (`GetFormation`), and points the camera — its `BattleCamera`, else the
     battle camera (`AJRPGBattleCamera`, below), else `UCameraSubsystem::FrameGroup` over every combatant.
   - The screen opens.
4. `EndBattle(Result)` fades to black; the arena destroys the pawns and puts the field back
   exactly as it was (controls, camera, what was hidden); `OnFieldShown` brings the field
   enemies back at home (a zone enemy goes to a point of the zone **away from the player**, farther than its detect
   radius); the screen opens. The grace period counts **from this moment** (`NoteFieldReturned`):
   for `PostBattleGraceTime` (5 s) no new encounter starts and no field enemy chases, and one that was already touching the
   player when the grace ends triggers then. `EncounterGraceTime` (3 s) still separates two encounters.

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
| `bUseBattleCamera` | true | Without `BattleCamera`: the battle camera follows the fight (below). Off = one still framing of everyone (`FrameGroup`). |
| `BattleCameraClass` | `AJRPGBattleCamera` | A Blueprint child to tune distances, turn speed, lens. |
| `CameraPreset` | `BattleWide` | The still framing (`bUseBattleCamera` off). |
| `CameraPadding` | 1.15 | Room around the combatants (still framing). |
| `bCameraBehindParty` | true | Still framing behind the heroes looking at the enemies; off = the other way. |
| `ArtsShoutTimeDilation` | 0.67 | The original's Arts "slow": on the PS1 the CD read of a known Art's voice shout stretched the frames. The whole fight (rules, animations, camera) runs at this rate for `ArtsShoutSeconds` from a known Art's starter. The dash (Hyper / Super / Miracle / a new Art) does not open it: it already is the original's slow-motion (the fight frozen, Arts after it at 1/4). 1 = off. |
| `ArtsShoutSeconds` | 1.0 | How long that slow lasts, in real seconds, from the starter. |
| `TransitionTime` | 0.35 | Fade to black in and out; the scene changes with the screen fully black. 0 = hard cut. |
| `TransitionColor` | black | |
| `PawnClass` | `AJRPGBattlePawn` | A Blueprint child for entry effects, a name over the head… |

A map with no arena uses these defaults on the temporary one.

### The battle camera (`AJRPGBattleCamera`)

The original's battle camera, from the RE (`Docs/Reference/BATTLE_RE_SWEEP.md` section 7). The arena spawns it at assembly
(already on its first pose, while the screen is black) and destroys it at teardown.

- **Every frame** it picks a shot from the state of the fight, builds that shot's pose from the live combatants, and moves
  toward it with the original's **ease-out**: each 30 Hz step covers `1 / ChaseSteps` of what is left (6; 7 for the far
  shot). The target is rebuilt every frame, so the camera chases moving fighters and never stops dead at a state change.
- **A pose** is the original's: a focus point, the world direction the camera looks (`Yaw`), a
  tilt (`Pitch`) and the view-space offset `TR` (side shift, eye height, eye distance). The original's numbers are PS1
  units; camera offsets are in the battle's 4x scale, so `cm = TR / 4 * OriginalCmPerUnit`, and "prescale" is x1.6.
- **Lens** (`bOriginalLens`): 64° horizontal at 4:3 with the vertical held (about 50°); a wider screen shows more at the
  sides.
- **Formation recentre**: the camera moves its focus with `OnFormationShifted` in the same frame, as the original does, so
  `bRecentreFormation` no longer shows a jump.

| Shot | When | Pose |
|---|---|---|
| **Far** (case 9) | Menu Start / Tactics / Escape / Final screens; no menu (console commands); between actions (after the end hold) | The whole formation's box centre on the floor; distance `prescale(max(width * 3, 0x800))` (1 v 1: ~831 cm), eye ~139 cm up, tilt 2.8°. The yaw is passed through (it keeps the direction of the shot before) and **orbits** at -10.5°/s (`bOrbit`). |
| **Member** (case 0) | Menu Command and Attack (arts entry) screens | Close-up of the hero being commanded: focus on his feet, looking at him from the front 21° to one side (`0x8F0 - facing`), shifted so he sits left of centre (`TR.x -0x200`), ~266 cm away, eye at his row of the original's height table (Vahn 1152 ≈ 125 cm, Noa 960, Gala 1408; `MemberLiftUnits`), tilt 2.8°. |
| **TargetEnemy** (case 1) | Menu Target screen | Behind and above the hero, looking from the camera's focus to the enemy under the cursor (`0x800 - bearing`): focus on the hero, ~355 cm away, eye ~166 cm up, tilt 22.5°. Moving the cursor swings the camera to the next enemy. |
| **Action** (case 6) | The actor's wind-up and walk (`Windup` / `Advance`), a Spirit turn; the end hold of an action without a hit | The actor's live position on the floor (the camera walks with him), the action yaw, level, eye ~139 cm up; distance from the monster's size class `clamp(class << 7, 0xC00, 0x1400)` (~532..887 cm; the target's class when a hero attacks, the monster's own when it attacks), Spirit `0x800` (~355 cm). |
| **TwoShot** (case 7) | The strikes (`Strike` / `Chain`, and anything after the first strike); `Recovery` with the target standing | Midpoint of the two bodies on the floor; the action yaw minus `0x700`, reached **one way only** when the strikes begin (from case 6): if the short arc goes back it takes the long arc (the camera swings around to the other side); back from an Art's camera, or between 7 and 8, it takes the short arc. Close option: level, eye `+0x40`, 3/5 of the distance. |
| **OnTarget** (case 8) | `Recovery` with the target on its knockdown / get-up or dead; the end hold (`DoneHoldSeconds`, 1 s) of a hit action | The target's body on the floor (the actor if the target is gone); yaw as case 7 (Far option: the yaw it has). **Close option only:** a dead target is framed falling (eye drops, levels and pushes in over ~0.4 s, then a flat pose once it lands), a standing one is re-aimed by its height (party: eye at its full height while animating, the close-up height minus `0x140` idle; monster: 7/8 of its height while animating, held while idle), with eye floors `0x280` / `0x300` turning the rest into tilt. |
| **Art** (the per-Art camera) | Over 6 / 7 / 8 while a hero plays an Art (or the Super / Miracle dash) that has an "arm" in the original, the target alive, option not Far | The original's 13 arms (`FUN_801D71B8`): seeded on the hero (TR `(0, 0x400, 0x400)`, `0x600` high for Gala), each arm cuts between framings by the clip's cursor and adds rows of **`DT_ArtCamera`** plus two ramps; much quicker eases (1, 3 or 6 display frames: 1 is a cut). |

**The action yaw** (the original's ladder `ctx[+0x6DA]`, "angle - actor facing"): `0x800` at the start of any action (from the
front), `0x200` for an action with a hit (three-quarters from behind), and at a hero's first strike `0x280` or `0xA80` (one
of two sides, coin flip). It then drifts on its own (`DriftUnitsPerSecond`, ~5°/s). **Four angles per action** (`ctx[+0xD]`,
rolled at the start, `bRandomStyle`): bit 0 films from the other side (half turn), bit 1 from low (tilt `+0x80`, eye `0x400`); a
hero's first strike resets it to 0, Spirit only rolls the low angle.

**`CameraOption`** (the original's Battle Camera option; Normal by default, as in the original): Close adds case 7's pull-in and
case 8's fall / re-aim; Far drops the yaw drift, keeps the yaw it has on case 8 and skips the per-Art camera. The player sets
it in **Options > Game > Battle Camera**; it is saved in the settings slot `JRPGSettings` (`UBattleSubsystem::
Get/SetBattleCameraOption`) and the camera follows it (`bFollowGameOption`; off = the camera's own `CameraOption`).

**The per-Art camera.** Which arm runs: Vahn - the dash (`0x1A`), Burning Flare (`0x1C`), Fire Blow (`0x1D`), Tornado Flame
(`0x1E`, and its Miracle part `0x2A`); Noa - the dash, Hurricane Kick (`0x1D`) and its third part (`0x1E`), Vulture Blade
(`0x1F`), Frost Breath (`0x20`), Jurassic Blow 1 / 2 (`0x2C` / `0x2D`); Gala - the dash, Explosive Fist (`0x1C`), Lightning Storm
(`0x1D`), Thunder Punch (`0x1E`, Miracle part `0x2A`). The id is the Art's `ActionConstant` (`DT_Arts`); every other Art keeps
the action shot, as in the original. The cuts run in the world's time, so the arena's `ArtsShoutTimeDilation` slows them with
the Art, as the original's frame stretch did. The table column is a coin flip per action (Gala always column 0), so the same Art is
filmed two ways. The clip cursor is the asset time x 7.5 x the disc rate (`DT_ArtHits.Rate`) in sixteenths of a keyframe.
Approximations: the dash's arm picks its framing by the clip's loop count, which the original repeats and ours plays once, so
the count runs down from `StarterLoopCount` (5) over the clip; the roll of Lightning Storm's arm is not applied; the pitch is
kept within ±89°. **`DT_ArtCamera`**: `python Extras/generate_art_camera_csv.py --extracted <LegaiaRE/extracted>` (20 rows of
two numbers from PROT 0898), import in `/Game/Data/` as a DataTable of `FJRPGArtCameraTrackData`. Without it the arms run with
the rows at 0 (a warning in the log).

"Facing-relative" yaws (case 0, and the action shots) are the original's `angle - facing`: the camera looks along the
character's front turned by `-angle` (0 = from behind, `0x800` = at his face). The original's world is Y-down, so the side
could come out mirrored: `bMirrorYaw` swaps it.

Tuning on the class: `DistanceScale` (every eye distance; 1 = the original's), `ChaseSteps` / `FarChaseSteps` (0 = hard
cut), `StartYaw` (relative to the arena; the original opens at 0, both rows in line; 20° by default), `OrbitDegreesPerSecond`,
`bMirrorYaw`, `bMenuShots` (off = the menu stays on the far shot), the `Member*` / `Target*` numbers, `bActionShots`,
`CameraOption`, `bFollowGameOption`, `bRandomStyle`, the `Action*` / `Style*` / `*YawUnits` numbers, `DoneHoldSeconds`,
`bArtShots`, `StarterLoopCount`, `StarterDiscRate`, `FieldOfView43`,
`bPrintDebug` (shot, slots and pose on screen).

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
| `JRPG.Battle.End [victory\|defeat\|escape\|abort]` | Ends the current fight (victory by default; it pays the rewards of every enemy, living ones too, to test them). In the Muscle Dome, `victory` calls the next wave. |
| `JRPG.Battle.Attack [enemy]`, `JRPG.Battle.Arts <buttons> [enemy]`, `JRPG.Battle.Auto` | Play the fight without a HUD ([BATTLE.md](BATTLE.md) section 2) |

---

## 6. Known limits (Phase 4)

- No HUD (Phase 7): the fight is played from the console ([BATTLE.md](BATTLE.md)); `JRPG.Battle.End` still ends it from outside.
- The heroes only have `Idle` and `Walk` so far (Phase 2).
- A monster with morph targets (Green Slime) crashes PIE when the project's GPU skin cache is
  off (engine assertion in `SkeletalRenderGPUSkin.cpp`). Turn on **Project Settings ›
  Rendering › Support Compute Skin Cache**.
