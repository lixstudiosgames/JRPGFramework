# The battle stage

How a fight is assembled over the current map and taken down again. Phase 4 of
[BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md).

There is **no combat yet** (Phase 5). A fight starts, the scene is assembled, the heroes and
the enemies stand in their seats in `Idle`, and it ends from the console.

---

## 1. Who does what

| Piece | Job |
|---|---|
| `UBattleSubsystem` | Starts and ends a fight (`BeginBattle` / `EndBattle`), builds the combatant list, blocks SAVE during it. Knows no actor — only `OnBattleStarted` / `OnBattleEnded`. |
| `AJRPGBattleStage` | Listens to those delegates and does everything with actors: picks the arena, hides the field, spawns the pawns, frames the camera, puts it all back. One per map. |
| `AJRPGBattleArena` | Where on the map the fight goes (see [FIELD_ENEMIES.md](FIELD_ENEMIES.md) › Where the fight happens). |
| `AJRPGBattlePawn` | One combatant in the scene: the **battle** model (not the field one) in its seat. |
| `AJRPGFieldEnemy` | Starts a fight on contact (`bStartBattle`); every field enemy hides and freezes while one is on. |
| `AJRPGBattleWaves` | Fights in a row, Muscle Dome style — the test map. |

A fight, start to end:

1. Contact (or a wave, or the console) calls `UBattleSubsystem::BeginBattle(Setup)`. The party
   is `Setup.Party`, or the active party, or — PIE straight into a map with no save —
   `FallbackParty` (Vahn, Noa, Gala). At most `MaxPartySlots` heroes and `MaxEnemySlots`
   enemies.
2. The stage fades to black (`TransitionTime`), then:
   - picks the arena with `AJRPGBattleArena::FindArenaFor(Setup.Location)`; a map with no arena
     gets a temporary one at the contact point, the party facing the enemy;
   - hides and freezes the field party (the controlled character and the followers) and the
     arena's `HideDuringBattle`;
   - turns the field controls off (`AJRPGFieldPlayerController::SetFieldInputEnabled(false)`);
   - spawns one `AJRPGBattlePawn` per combatant at the arena's seats (`GetFormation`);
   - points the camera: the arena's `BattleCamera` if set, otherwise
     `UCameraSubsystem::FrameGroup` over every combatant;
   - fades back in.
3. `EndBattle(Result)` fades out, destroys the pawns, shows the field again exactly as it was
   (controls, camera, what was hidden), and fades in. For `EncounterGraceTime` seconds after
   that no new encounter starts, so the enemy next to you does not pull you straight back in.

---

## 2. Setting it up in the project

**One stage per map, created by itself.** `UJRPGBattleStageSubsystem` spawns one in every map
that does not already have one placed. The class comes from **Project Settings › Plugins ›
JRPG Battle › Stage Class**. Empty, it is the plugin's `AJRPGBattleStage`, which has no hero
models.

**`BP_BattleStage`** — a Blueprint child of `JRPGBattleStage`, set as the Stage Class:

- `PartyVisuals`: one entry per hero key (`Vahn`, `Noa`, `Gala`) with the battle model —
  `SK_Vahn_Battle` + `AS_Vahn_Battle_Idle` from `/Game/Legaia/Battle/Party/Vahn/`, and so on.
  This moves to `DT_Characters` once the Arts bank exists (Phase 6).
- `FallbackVisual`: for a hero missing from the map, or a `DT_Enemies` row still without a
  `Mesh`. Empty, the pawn is only its capsule (and the stage says so on screen).
- Enemies need nothing here: the model is the row's `Mesh` + `Anims.Idle` in `DT_Enemies`.

| Property | Default | |
|---|---|---|
| `PawnClass` | `AJRPGBattlePawn` | A Blueprint child for entry effects, a name over the head… |
| `TransitionTime` | 0.35 | Fade to black in and out; the scene changes in the dark. 0 = hard cut. |
| `CameraPreset` | `BattleWide` | Framing when the arena has no `BattleCamera`. |
| `CameraPadding` | 1.15 | Room around the combatants. |
| `bCameraBehindParty` | true | Camera behind the heroes looking at the enemies; off = the other way. |
| `bPrintDebug` | true | Arena, seats and missing models on screen. |

**Per arena** (`AJRPGBattleArena`, category *Battle*):

- `HideDuringBattle`: actors hidden while a fight happens there (a tree in front of the
  camera, an NPC standing in a seat).
- `BattleCamera`: a CameraActor / CineCameraActor placed by hand. Set, it replaces the
  automatic framing for fights in this arena.

**Field enemies:** `bStartBattle` (default on) — off, contact only announces the encounter, as
in Phase 3.

---

## 3. The test map: Muscle Dome

`AJRPGBattleWaves` runs fights in a row. Drag one into the map:

- `Waves`: one encounter per wave, rolled like the field ones. A fixed wave is a single line
  with `bAlwaysJoins` and `CountMin = CountMax`.
- `Party`: who fights (keys, in formation order). Empty = the active party.
- `Arena`: where. Empty = the arena picked from the actor's position.
- `bAutoStart`, `StartDelay`, `DelayBetweenWaves`, `bLoop`.

Winning a wave calls the next one; losing, escaping or aborting stops them. It runs on the
field GameMode: the party stands in the ring between waves.

---

## 4. Console (PIE)

| Command | |
|---|---|
| `JRPG.Battle.Start <EnemyKey> [Count] [Level]` | A fight where the player stands. `JRPG.Battle.Start gimard 3` |
| `JRPG.Battle.End [victory\|defeat\|escape\|abort]` | Ends the current fight (victory by default). In the Muscle Dome, `victory` calls the next wave. |

---

## 5. Known limits (Phase 4)

- No combat, no HUD: the fight only ends from the console.
- Hero models come from the stage's `PartyVisuals`; the heroes only have `Idle` and `Walk`
  so far (Phase 2).
- A monster with morph targets (Green Slime) crashes PIE when the project's GPU skin cache is
  off (engine assertion in `SkeletalRenderGPUSkin.cpp`). Turn on **Project Settings ›
  Rendering › Support Compute Skin Cache**.
