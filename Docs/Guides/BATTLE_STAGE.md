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
   player when the grace ends triggers then. `EncounterGraceTime` (3 s) still separates two encounters. Field enemies also
   wait for the player to move away from where he stands when the screen opens
   ([FIELD_ENEMIES.md](FIELD_ENEMIES.md) › On contact).

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
| `RaSeru` | `RaSeruLevel` (not an equip slot; 0 = no Ra-Seru yet) | `<Hero>_RaSeru_Lv<N>` (the arm grows with the level; above the hero's last row — Vahn 9, Noa 8, Gala 7 — the last one); `Default` at 0 |
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
| `TransitionTime` | 0.35 | The camera fade: on the way out to black; on the way in, from the colour the field transition ended on (white or black), or to / from `TransitionColor` with the `Fade` style. 0 = hard cut. |
| `TransitionColor` | black | The colour of the `Fade` style and of the way out. |
| `TransitionStyle` / `TransitionSubStyle` | Auto / -1 | The field-to-battle transition for fights here when the fight does not pick one (below). |
| `TransitionHoldSeconds` / `TransitionRevealSeconds` | 0.6 / 0.5 | After the effect, the end colour stays full this long with the fight already assembled underneath (the original's CD load), then fades into the fight; the intro's sweep starts with that fade. |
| `TransitionPlaybackRate` | 1 | The effect's speed (1 = the port's 60 frames a second; less = slower). |
| `TransitionSoundFadeSeconds` | 1.5 | The battle-start sound fades out over this once the fight shows (0 = cut). |
| `TransitionCurtainScale` | 3 | The curtain style's working image: 320 x 240 times this (two CPU passes a frame). |
| `TransitionSound` / `ScriptedTransitionSound` | `SW_Battle_Static1F` / `SW_Battle_Static4D` | The battle-start sound on the transition's first frame (the original's static cue `0x1F`, `0x4D` on a scripted fight), in `/Game/Legaia/Battle/Sounds/Common/`. Empty = silent. |
| `bBattleIntro` | true | The original's opening between the screen opening and the first prompt (below). Off = the fight runs as soon as the scene is up. |
| `IntroDriftFrames` / `IntroCloseInFrames` | 34 / 31 | The camera sweep: vsyncs of the rise and pull-in, then of the close-in on the arena centre. |
| `IntroNamesFrames` / `IntroNamesAdvantageFrames` | 90 / 120 | How long the enemy names stay up (with a formation advantage, its line shows too and they stay longer). |
| `PawnClass` | `AJRPGBattlePawn` | A Blueprint child for entry effects, a name over the head… |

A map with no arena uses these defaults on the temporary one.

### The field-to-battle transition

The original's overlay PROT 0979 (`Reference/BATTLE_RE_SWEEP.md` 10.3, step 5b; the RE's `battle-stage-camera.md` "Field-to-battle
intro presentation" and `cutscene.md` "Field-to-battle transition"). When a fight starts, `UJRPGBattleSceneSubsystem`:

1. plays the battle-start sound and stops the field music; the battle music comes in `BattleMusicLeadSeconds` (1 s, on the
   arena) before the screen has finished opening (the end of the reveal fade), not with the start sound that would cover its
   first bars;
2. captures the last frame of the viewport and draws one of five styles over it, on a black base, at 60 frames a second, for
   **132 frames** (2.2 s; the swirl 252, 4.2 s), each with its fade ramp at the end (white for spin-up, swirl and scatter sub 2,
   black for the rest);
3. with the screen fully on that colour, assembles the fight underneath, holds the colour (`TransitionHoldSeconds`) and fades it
   into the fight (`TransitionRevealSeconds`); the intro's sweep and the start sound's fade-out (`TransitionSoundFadeSeconds`)
   begin with that fade. With the `Fade` style the camera fade opens the fight (`TransitionTime`) and the sound fades then.

| Style | Look | Who gets it (the original's pick, `FUN_801CE8CC`) |
|---|---|---|
| `Shatter` | The screen breaks into 16 x 16 boxes that tumble away (sub 0 back, 1 forward, 2 radial from the centre) | **The default** of random encounters; sub 1 for the Piuras (`0x13..0x15`) on a scripted fight; sub 2 for the bees on Rim Elm / Octam maps and the Golem on Mt. Rikuroa (map arms the plugin cannot see: set it on the field enemy) |
| `Scatter` | 8 x 8 patches fly off diagonally, additive | Songi 1st / 2nd / 3rd (sub 0 / 1 / 2, the 3rd whites out) |
| `SpinUp` | The patches spin faster and fade, behind a shock-wave ring; whites out | Every other scripted fight |
| `Curtain` | Columns and rows pull apart with a fading trail | Tetsu (training and rematch), Gaza |
| `Swirl` | Rings rush in and out like a drain, then roll; whites out | Evolved Cort (Juggernaut) |
| `Fade` | The camera fade to `TransitionColor` (the plugin's old one) | Only when chosen |
| `Cut` | No transition | Only when chosen |

**Who picks**: `FJRPGBattleSetup::TransitionStyle` / `TransitionSubStyle` (the field enemy has the same two, and copies them);
Auto falls to the arena's; Auto there runs the original's pick from the fight (scripted or not) and its first enemy
(`DT_Enemies.MonsterArchiveID`). A sub-style set on the fight also applies to the automatic pick.

How it is drawn (`UJRPGBattleTransition`, the simulation in `FJRPGBattleIntroEffect`, both private): the kernels are the RE port's
(the user's HTML replica, `LegaiaRE/viewer/batalha-intro`), in PS1 units on a 320 x 240 screen stretched to the viewport; a Slate
overlay above everything (z 200) draws them as textured triangles with affine UVs, like the PS1. One blend (pre-multiplied) gives
the original's three by the vertex alpha: opaque, additive (ABR 1, exact), half; the black subtractive fade (ABR 2) is a darken by
alpha. The curtain runs its two passes on the CPU into a texture. Approximations (as the replica's): standard sine / cosine, the
port's LCG, a hand-drawn shade ramp for the shatter's side faces, the scatter's `0x101010` wash not modelled. A capture that fails
(no viewport) falls back to the `Fade`.

Test: the dev menu's Battle tab has a *Transição* choice; on the console, the 5th argument of `JRPG.Battle.Start`
(`JRPG.Battle.Start gimard 1 1 roll swirl`, `shatter2`, `scatter2`, `curtain`...).

### The battle intro

The original's opening (`Reference/BATTLE_RE_SWEEP.md` 2.1), in `UJRPGBattleSceneSubsystem` (`GetIntroStage`,
`OnIntroStageChanged`): once the scene is assembled and the screen opens,

1. **Sweep** (`IntroDriftFrames + IntroCloseInFrames`, ~1.1 s): the camera's `Entry` shot (below);
2. **Names** (90 frames, 120 with a formation advantage): one label per group of equal enemies in a row, over the group
   (`Killer Bee * 3`), and the formation line at the top: `<leader>'s team surprised the enemy.` (`<leader> surprised the
   enemy.` alone) on a pre-emptive strike, `Ambushed!` on a back attack. Drawn by the WebUI's battle HUD layer
   (`JRPGBattleHUD.intro`, [WEBUI_API.md](WEBUI_API.md#battle-hud-layer)).
3. **None**: the rules start (`HoldLogic(false)`; an action that already started, as a back attack's monster, enters its pawn
   now) and the command menu opens.

Nothing asks for a command under the sweep or the names, as in the original (its battle tick does not run yet). A fight that
ends during the intro (the console's `JRPG.Battle.End`) cuts it.

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
| **Entry** (the sweep of `FUN_80046A20`) | The battle intro (above) | Focus on the arena centre on the floor, yaw `EntryYawDegrees` from the arena (0 = behind the party, at the enemies). First the rise: the init pose (tilt 60, `TR (0, 1280, 7168)`) moving `TR.y += 48`, `TR.z -= 64` per vsync, set each frame; then case 2 (tilt 0, `TR (0, 0x600, prescale(0x700))`, ~166 cm up, ~310 cm away) by the usual ease-out, held through the names. The far shot then starts its orbit from this yaw. |
| **Far** (case 9) | Menu Start / Tactics / Escape / Final screens; no menu (console commands); between actions (after the end hold) | The whole formation's box centre on the floor; distance `prescale(max(width * 3, 0x800))` (1 v 1: ~831 cm), eye ~139 cm up, tilt 2.8°. The yaw is passed through (it keeps the direction of the shot before) and **orbits** at -10.5°/s (`bOrbit`). |
| **Member** (case 0) | Menu Command and Attack (arts entry) screens | Close-up of the hero being commanded: focus on his feet, looking at him from the front 21° to one side (`0x8F0 - facing`), shifted so he sits left of centre (`TR.x -0x200`), ~266 cm away, eye at his row of the original's height table (Vahn 1152 ≈ 125 cm, Noa 960, Gala 1408; `MemberLiftUnits`), tilt 2.8°. |
| **TargetEnemy** (case 1) | Menu Target screen | Behind and above the hero, looking from the camera's focus to the enemy under the cursor (`0x800 - bearing`): focus on the hero, ~355 cm away, eye ~166 cm up, tilt 22.5°. Moving the cursor swings the camera to the next enemy. |
| **Action** (case 6) | The actor's wind-up and walk (`Windup` / `Advance`), a Spirit turn; the end hold of an action without a hit | The actor's live position on the floor (the camera walks with him), the action yaw, level, eye ~139 cm up; distance from the monster's size class `clamp(class << 7, 0xC00, 0x1400)` (~532..887 cm; the target's class when a hero attacks, the monster's own when it attacks), Spirit `0x800` (~355 cm). |
| **TwoShot** (case 7) | The strikes (`Strike` / `Chain`, and anything after the first strike); `Recovery` with the target standing | Midpoint of the two bodies on the floor; the action yaw minus `0x700`, reached **one way only** when the strikes begin (from case 6): if the short arc goes back it takes the long arc (the camera swings around to the other side); back from an Art's camera, or between 7 and 8, it takes the short arc. Close option: level, eye `+0x40`, 3/5 of the distance. |
| **OnTarget** (case 8) | `Recovery` with the target on its knockdown / get-up or dead; the end hold (`DoneHoldSeconds`, 1 s) of a hit action | The target's body on the floor (the actor if the target is gone); yaw as case 7 (Far option: the yaw it has). **Close option only:** a dead target is framed falling (eye drops, levels and pushes in over ~0.4 s, then a flat pose once it lands), a standing one is re-aimed by its height (party: eye at its full height while animating, the close-up height minus `0x140` idle; monster: 7/8 of its height while animating, held while idle), with eye floors `0x280` / `0x300` turning the rest into tilt. |
| **Art** (the per-Art camera) | Over 6 / 7 / 8 while a hero plays an Art (or the Super / Miracle dash) that has an "arm" in the original, the target alive, option not Far | The original's 13 arms (`FUN_801D71B8`): seeded on the hero (TR `(0, 0x400, 0x400)`, `0x600` high for Gala), each arm cuts between framings by the clip's cursor and adds rows of **`DT_ArtCamera`** plus two ramps; much quicker eases (1, 3 or 6 display frames: 1 is a cut). |
| **Victory** (the results sequencer `FUN_8004E568`) | From the fight being won until the screen is black (`bVictoryShots`; the arena's `bVictorySequence`) | On the hero who poses (the leader; the first hero standing if he is down), focus on his body on the floor. First the wait (`UBattleSubsystem::VictoryLoadFrames`, 80): case 8's stand-off, level, `TR (0, 0x400, prescale(0x280 * 5/2))`, the yaw from the last action's ladder. Then, with the pose playing: case 6's battle-over arm, from the front (`0x800 - facing`), raw distance `0x500`, eye at 5x the height of his body centre with the `0x280` floor, moved by the **win-pose camera script** of that hero and pose (an accumulator running 8 a display frame from the pose's start), so the shot keeps moving through the hold. |

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
`bArtShots`, `StarterLoopCount`, `StarterDiscRate`, `FieldOfView43`, `bEntryShots` and the `Entry*` numbers,
`bPrintDebug` (shot, slots and pose on screen).

**The orbit preview in the editor** (arena's `bPreviewCameraOrbit`, on by default; Preview group). The arena draws the far
shot's orbit, the camera that turns while nobody acts: a ring at eye height around the focus of the preview formation
(`PreviewPartyCount` / `PreviewEnemyCount`), with a shadow of it on the floor and a cross on the focus. It uses the battle
camera's own maths (`AJRPGBattleCamera::ComputeFarFraming` on the `BattleCameraClass` default object, so `DistanceScale`,
`FarSpanFactor`, `FarLiftUnits` and `FarPitchUnits` change it). Each stretch is **green** when free, **orange** when the view
from there to the focus goes through something, and **red** when the eye is inside something (a sphere of
`PreviewCameraProbeRadius`, against static geometry). `PreviewCameraSegments` sets how many points are tested. **Validate
Ground** and Map Check list it as a problem. It is the formation at rest: in a fight the orbit shrinks while an attacker walks
up to his target. Nothing is drawn with a hand-placed `BattleCamera` or with `bUseBattleCamera` off.

The preview's traces (the formation on the ground and the ring, a few hundred per arena) are kept and only redone when
the arena changes (moved, edited) or every 0.5 s, to catch the scenery around it moving; the frames in between only
redraw. Tracing every frame cost ~2 ms of the editor's game thread per arena.

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
