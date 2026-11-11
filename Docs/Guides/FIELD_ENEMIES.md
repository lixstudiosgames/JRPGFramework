# Enemies on the field

Visible enemies that wander the map, chase the player and start an encounter on contact —
the remake's replacement for the original's random encounters. Phase 3 of
[BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md).

On contact the enemy rolls who fights, prints it on screen, disappears, starts the fight —
assembled by the battle arena ([BATTLE_STAGE.md](BATTLE_STAGE.md)) — and comes back after its
respawn delay. On contact every field enemy stops dead and stands in `Idle` — the one that
touched too, still visible; once the screen is black that one hides until its respawn and the
others hide and **go home** (standalone: where it was placed; zone: a random point of the zone), so nobody
is right next to the player when the fight ends. A respawn that falls inside a fight waits for
it to leave the screen. `bStartBattle` off brings back Phase 3's behaviour: contact only
announces the encounter.

---

## 1. The idea: the enemy on the map is only a visual

There is one class for every enemy — `AJRPGFieldEnemy` — and one Blueprint child in the
project serves them all. **Pick the `Enemy`** (a [`DT_Enemies`](ENEMIES.md) row, from a
dropdown) and that is it: the model, `Idle` and `Walk` come from the row, the editor shows it
right away, and touching it starts a fight against one of it at `LevelMin..LevelMax`.

For a mixed group — several enemies, weights, counts — open the advanced section and fill
**Custom Encounter**; when it has lines it replaces `Enemy`, and the model becomes its **main
enemy** (`bAlwaysJoins`; with none, one of the others that has a model, by weight).

The model it shows is, in order:

1. one of its `Visuals`, picked at random (or the zone's `Visuals`), if any are set (advanced);
2. otherwise the `Enemy` row's `Mesh` and `Anims.Idle` / `Anims.Walk` (or the Custom
   Encounter's main enemy);
3. otherwise the `FallbackVisual` (advanced).

So a `DT_Enemies` row that gets its `Mesh` and `Anims` (Phase 2's batch) appears on the field
by itself. A row still without a model falls back and says so on screen.

---

## 2. Setting it up in the project

**`BP_FieldEnemy`** — a Blueprint child of `JRPGFieldEnemy`:

- `FallbackVisual`: a model for enemies without one yet (the Gimard: `SK_Gimard`,
  `AS_Gimard_Idle`, `AS_Gimard_Move`).
- `MeshYawOffset`: turns the mesh to face the actor's forward. The models converted from the
  PS1 (`Extras/ps1_glb_to_fbx.py`) face Unreal's −Y, so the default **+90** fits all of them.

In the editor every enemy shows the model it will use (the first of its list, or the
encounter's main enemy) and a **red icon** above it; zones show a red ring icon at the centre
and their shape — so they are easy to find on the map. Both are editor-only.
- Speeds, radii and the rest below, if the defaults do not suit.

The map needs a **NavMeshBoundsVolume** covering the area. Without a navmesh the enemies still
work, walking straight at their target.

### A single enemy

Drop a `BP_FieldEnemy` in the map and pick its **Enemy**. Where it is placed is home: it
wanders around it (`WanderRadius`), and after an encounter it comes back there after
`RespawnDelayMin..Max` (40..70 s; unless `bRespawn` is off). After a fight it spends
`PostBattleNoChaseMin..Max` (7..15 s, rolled per enemy) without chasing the player.

### A zone

Drop a **`JRPGEncounterZone`**:

| Property | |
|---|---|
| `Shape` | **Sphere** (`Radius`), **Box** (`BoxExtent`, rotates with the actor) or **Spline** (`AreaSpline`: a closed outline drawn by hand, any shape — concave included). Drawn in the editor |
| `FloorSearchDistance` | Sphere/Spline: how far above and below the zone actor to look for the ground — the zone may float or sit half underground (2000) |
| `Enemy` / `LevelMin` / `LevelMax` | Which `DT_Enemies` row walks the zone, and its level range — model and animations come from the row |
| `Custom Encounter` (advanced) | A mixed group; replaces `Enemy` when it has lines |
| `EnemyClass` (advanced) | `BP_FieldEnemy` |
| `Visuals` (advanced) | Overrides the model for this zone's enemies |
| `BattleArena` (*Battle*) | The arena for this zone's fights: pick a `JRPGBattleArena` in the scene. Empty or disabled = the arena of the contact point |
| `MinAlive` / `MaxAlive` (*Spawn*) | How many walk the zone at once, rolled in this range when the zone starts (2..3) |
| `MinSpacing` / `MaxSpacing` (*Spawn*) | A new enemy is at least `MinSpacing` (400 cm) from the others of the zone and, with `MaxSpacing` (0 = no limit), at most that from the nearest one, so the group neither stacks nor scatters |
| `bAvoidObstacles` / `ObstacleClearance` (*Spawn*) | Never spawn inside objects: the enemy's capsule (+10 cm) at the drawn point must not touch anything static or dynamic (rocks, walls, crates) |
| `RespawnDelayMin/Max` (*Respawn*) | An enemy that touched the player comes back after this (40..70 s), at a new point |
| `PostBattleNoChaseMin/Max` (*Respawn*) | After a fight, each enemy of the zone spends a time rolled in this range (7..15 s) without chasing the player; it still wanders |
| `RequiredFlagID` / `bRequiredFlagValue` (*Unlock*) | The zone only works with this WorldState event flag at this value |
| `DisableFlagID` (*Unlock*) | The zone switches **off** while this flag is set (an area cleared in the story). Works together with `RequiredFlagID`: a zone can open at one point of the story and close at another |
| `bRandomEncounters` / `EncounterRate` / `StepLength` / `Priority` / `LowRateFlagID` / `LowRatePercent` (*Random Encounters*) | Random fights while the player walks in the zone, [below](#random-encounters) |
| `bPreviewSpawns` / `bPreviewMeshes` / `bPreviewOnlyWhenSelected`, **Reroll Preview** (*Preview*) | The editor preview, below |

Enemies spawn on navmesh points **inside the shape**, wander inside it, and when they give up a
chase they walk back into it. The zone's `Enemy` / Custom Encounter replaces each enemy's own.

**Where they spawn.** Each spawn and respawn draws up to 16 floor points inside the shape and keeps
the first one that is away from the player (outside his `DetectRadius` + 2 m), at `MinSpacing` /
`MaxSpacing` from the enemies of the zone in view and, with `bAvoidObstacles`, with the capsule
clear of objects. A point inside an object is never used; if no point meets the distances, the one
that misses them by the least is used (with `bPrintDebug`, a yellow message says so: the zone is
small for its spacing).

**The flags.** The zone checks `RequiredFlagID` and `DisableFlagID` every second (not during a
fight). Switching off, its enemies disappear and their respawns are cancelled; switching on, the
hidden ones come back at once and the missing ones spawn.

**The preview.** In the editor the zone draws where its enemies would spawn: `MaxAlive` points
drawn with the same rules as the game, each with the enemy's capsule, a circle of `MinSpacing / 2`
around it (two circles that do not touch are at least `MinSpacing` apart) and, with
`bPreviewMeshes`, the enemy's model (`Visuals`, or the `DT_Enemies` model of each enemy of the
encounter in turn). **Green** = the `MinAlive` that always spawn, **blue** = the ones that may
spawn (up to `MaxAlive`), **yellow** = no point met the spacing. The draw changes every time the
zone is edited, and with **Reroll Preview**; in the game each start draws again. Points that found
no floor at all are reported in the Output Log.

**The details panel** has one section per actor — *Field Enemy*, *Encounter Zone*, *Battle
Arena* — with at most one level of subsections (*Area*, *Respawn*, *Detection*…); rarely
touched properties sit in each section's advanced part (the arrow at the bottom).

**Spline shape.** Select the zone, then the `AreaSpline` points: drag them, Alt+drag one to
add a point, right-click a point for more options. It starts as a 20 m square with straight
corners; curved points work too. Only the outline **seen from above** counts — the points'
height does not — so the spline may cut through hills. **Snap Area To Ground** (button in the
details panel) moves the points down onto the terrain so the outline is easy to see; it does
not change the area. A spline with fewer than 3 points falls back to a sphere of `Radius`.

### Where the fight happens

On contact the enemy picks the **battle arena** for the fight — its zone's `BattleArena` when the
zone has one (enabled); otherwise the `JRPGBattleArena` whose area contains the contact point, or the
nearest one — and passes it in `FJRPGFieldEncounter::Arena`. The zone's arena also goes in
`FJRPGBattleSetup::Arena`, which the battle stage uses before looking by position. With `bPrintDebug` it
prints the arena's name and draws its formation for a few seconds. See
[BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md#ajrpgbattlearena--where-on-the-map-written-2026-10-27).

### Random encounters

The original's random fights (`FUN_801D9E1C`, RE `docs/formats/encounter.md`): no enemy in view,
a fight starts after a number of steps. Turn on **`bRandomEncounters`** on a zone; the fights are
rolled from the **same list** as its roaming enemies (`Enemy` / Custom Encounter) and set up on its
`BattleArena`. Both work at once; `MaxAlive` 0 leaves only the random ones.

- **The counter.** One step counter for the whole game (`UBattleSubsystem::GetEncounterCounter`),
  carried from zone to zone and map to map, as in the original. It starts at
  `0x3ce + rng%0x1e7 - rng%0x1e7` (488..1460, 974 on average). Every step inside a zone takes the
  zone's rate off it; at 0 or below the fight starts and the counter is drawn again. Entering a
  map with the counter below 487 adds half a fresh draw (the original's scene-entry top-up).
- **`EncounterRate`** is the disc's per-step number (the region's byte `+4`): on average one fight
  every `974 / rate` steps. 0 = never (a counter already at 0 still fires there, as in the
  original). Default 20. The values on the disc (read from every scene's MAN section 0):

  | Where | Rate | Steps per fight |
  |---|---|---|
  | World map (`map01` / `map02` / `map03`) | 42 and 32, per region | ~23 and ~30 |
  | Most dungeons (Bylon, Dolk, Conc, Nilboa, Jou, Rugi…) | 20 | ~49 |
  | Rim Elm's surroundings, Cave, Retock | 24 | ~41 |
  | Quieter areas (Deene, tunnels, Stone) | 10..18 | ~54..97 |
  | Towns, interiors | 0 | never |

  The world map's story groups switch which regions are live, so the same map can feel faster or
  slower after an event.
- **A step** is entering a neighbouring tile of `StepLength` (55.4 cm: the original's 128-unit
  tile). Moving more than one tile at once (a door, a warp) is not a step. The steps above match
  the original only on a map at the PS1 scale: a map built larger needs a `StepLength` larger by
  the same factor.
- **Steps that do not count**: with a menu or dialogue open, with the field control off (doors,
  scenes), during a fight and while the field is hidden, and in the encounter grace after a fight.
- **`LowRateFlagID`** lowers this zone's rate to `LowRatePercent` (50 = half, like the original's
  system flag `0x1E`) while the flag is set. `DisableFlagID` / `RequiredFlagID` switch the random
  fights off with the rest of the zone.
- **Overlapping zones**: the one with the highest `Priority` counts the step (a tie: the one that
  started first). Outside every zone with random encounters nothing is counted.
- `OnRandomEncounter` (Blueprint event on the zone) fires with the rolled enemies;
  `StartRandomEncounter` starts one at once (dev menu, tests). With `bPrintDebug` each step prints
  the rate and the counter on one line, and the fight prints its enemies and arena.

`UJRPGRandomEncounterSubsystem` (world subsystem) does the counting; the rules are pure functions
in `UJRPGEncounterLibrary` (`RerollEncounterCounter`, `TopUpEncounterCounter`,
`StepEncounterCounter`, `ScaleEncounterRate`, `IsEncounterStep`), tested by
`JRPG.Encounter.RandomCounter`. Not yet: the original's encounter-rate option and the accessory
passives (Bad Luck Bell ×4, Good Luck Bell ÷2) and the Incense window.

### The Custom Encounter

| Field | |
|---|---|
| `EnemyKey` | A `DT_Enemies` row — a dropdown in the editor |
| `LevelMin` / `LevelMax` | The level is rolled per enemy |
| `bAlwaysJoins` | The **main enemy**: in every fight, `CountMin..CountMax` of it. Also the model shown |
| `Weight` | For the others: the draw weight |
| `CountMin` / `CountMax` | How many of this line can join one fight |
| `EnemiesPerBattleMin/Max` | Total in the fight. The main ones go in first; the rest is drawn until the total |

Example — "a Gimard, sometimes with a friend": one line `gimard`, `bAlwaysJoins`, count 1–1;
one line `gimard`, weight 1, count 0–1; `EnemiesPerBattle` 1–2.

---

## 3. Behaviour

| State | |
|---|---|
| `Idle` | Waits `WaitTimeMin..Max`, then wanders |
| `Wander` | Walks (`WalkSpeed`) to a random point around home / inside the zone |
| `Chase` | The player came within `DetectRadius` (and line of sight, if `bRequireLineOfSight`): runs at `ChaseSpeed` |
| `Return` | The player stayed beyond `LoseRadius` for `GiveUpTime`, or the enemy got `LeashDistance` from home / out of the zone: walks back |
| `Hidden` | Touched the player: gone until the respawn |

- The target is the pawn the player **controls** — it follows the R3 switch. Party followers
  never trigger an encounter.
- `LoseRadius` is larger than `DetectRadius` so a player on the edge does not toggle the chase.
- **Returning, it still watches**: walk back in front of it and it chases again — once
  `ReacquireDelay` has passed since it gave up, and only if the player is within its leash
  (otherwise it would chase and give up on the spot).
- Speeds: `WalkSpeed` (400, never below 400) and `ChaseSpeed` (450, never below `WalkSpeed`)
  are both above the player's walk (300) — **a walking player gets caught** — and below the
  sprint (700): **sprinting gets away**. Chasing, it keeps full speed up to the contact: the
  path following brakes near the end of the path, and when chasing the end is the player —
  which the enemy never quite reaches, the capsules meet first — so it would trail a player
  walking straight. `HandlePostProcessMove` turns that braking off during the chase only.
- While a menu is open (WebUI state other than `HudOnly`) every enemy freezes.
- The clips switch by speed: Idle stopped, Walk moving, Run (or Walk sped up) chasing, with a
  **crossfade** of `AnimBlendTime` (0.2 s) instead of a hard cut. This is
  `UJRPGFieldEnemyAnimInstance`, a native anim instance with no Anim Blueprint: every PS1
  monster has its own skeleton and an Anim Blueprint is tied to one, so the native one serves
  all of them. Both clips tick through the engine while they blend, so their **AnimNotifies**
  (the monster's sounds, see [PS1_ASSETS.md › Sounds](PS1_ASSETS.md#5-sounds)) fire as usual.
  With an `AnimClass` in the visual, that Anim Blueprint is used instead.
- **Sounds.** The clips' `AnimNotify_PlaySound`s are the battle's (2D, heard everywhere). On the
  field `UJRPGFieldEnemyAnimInstance::HandleNotify` hands each one to
  `AJRPGFieldEnemy::PlayClipSound`, which plays it in 3D attached to the mesh:
  - `SoundAttenuation`: full volume up to 150 cm, natural falloff over 1200 cm;
  - `SoundVolume` (0.7) on top of the notify's volume;
  - one sound at a time per enemy, so a step still playing never gets another on top;
  - `bPlayClipSounds` turns them off.

  The distance counts from the character you control, not from the camera 18 m above
  (`AJRPGFieldPlayerController::bHearFromPawn`, on by default); left/right still follows the
  camera. A hidden enemy pauses its animation and stops its sounds, so nothing keeps playing
  where it touched you.
- `MistDisturber`: they part the JRPGMist and leave a trail, like the player.

## 4. On contact

1. The encounter is rolled (`UJRPGEncounterLibrary::RollEncounter`).
2. `UBattleSubsystem::TryStartEncounter` — refused if another started less than
   `EncounterGraceTime` (3 s) ago, so two enemies touched together do not start two fights, and for
   `PostBattleGraceTime` (5 s) after the field is shown again from a fight (`IsInEncounterGrace`). In that grace
   field enemies do not detect or chase the player; one that keeps touching him triggers when it ends. On top of it,
   each enemy rolls its own `PostBattleNoChaseMin..Max` (7..15 s) without chasing once the field is shown. A zone also
   respawns its enemies away from the player.
3. `OnEncounterTriggered` (Blueprint event) and `OnContact` (override in `BP_FieldEnemy`: the
   place for the 90s screen shatter). `UBattleSubsystem::OnEncounterStarted` fires with the
   rolled enemies.
4. The enemy hides and its respawn is scheduled.

`TriggerEncounter` and `HideEnemy` are callable from Blueprint, for testing.

## 5. On-screen messages

Turn on `bPrintDebug` (off by default) on an enemy, or on a zone — which also covers the
enemies it spawns — and every event prints on screen and in the log, in this form:

```
EncounterZone Zone_Gimard: 3 inimigo(s) criados
FieldEnemy BP_FieldEnemy_C_1 [gimard]: apareceu (gimard: SK_Gimard)
FieldEnemy BP_FieldEnemy_C_1 [gimard]: detectou o jogador — perseguindo
FieldEnemy BP_FieldEnemy_C_1 [gimard]: perdeu o jogador de vista
FieldEnemy BP_FieldEnemy_C_1 [gimard]: desistiu — voltando
FieldEnemy BP_FieldEnemy_C_1 [gimard]: ENCONTRO! Gimard Lv2 (HP 99), Gimard Lv1 (HP 99)
FieldEnemy BP_FieldEnemy_C_1 [gimard]: sumiu — a zona agenda a volta
FieldEnemy BP_FieldEnemy_C_1 [gimard]: reapareceu (gimard: SK_Gimard)
```


---

## What comes next

- The screen shatter in `OnContact` (Phase 7; the stage fades to black meanwhile).
- The level scaling the stats (`DT_Enemies` holds the base values).
