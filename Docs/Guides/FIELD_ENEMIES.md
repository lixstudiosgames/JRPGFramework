# Enemies on the field

Visible enemies that wander the map, chase the player and start an encounter on contact —
the remake's replacement for the original's random encounters. Phase 3 of
[BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md).

On contact the enemy rolls who fights, prints it on screen, disappears, starts the fight —
assembled by the battle arena ([BATTLE_STAGE.md](BATTLE_STAGE.md)) — and comes back after its
respawn delay. When a fight starts every field enemy stops; once the screen is black they hide
and **go home** (standalone: where it was placed; zone: a random point of the zone), so nobody
is right next to the player when the fight ends. A respawn that falls inside a fight waits for
it to leave the screen. `bStartBattle` off brings back Phase 3's behaviour: contact only
announces the encounter.

---

## 1. The idea: the enemy on the map is only a visual

Who fights is **not** the model walking around. It is the **encounter**: a list of
[`DT_Enemies`](ENEMIES.md) keys with levels and counts, rolled the moment the player is
touched. So there is one class for every enemy — `AJRPGFieldEnemy` — and one Blueprint child
in the project serves them all.

The model it shows is, in order:

1. one of its `Visuals`, picked at random (or the zone's `Visuals`), if any are set;
2. otherwise the model of the encounter's **main enemy** (`bAlwaysJoins`) in `DT_Enemies` —
   its `Mesh` and `Anims.Idle` / `Anims.Walk`; with no main enemy, one of the others that has a
   model, by weight;
3. otherwise the `FallbackVisual`.

So a `DT_Enemies` row that gets its `Mesh` and `Anims` (Phase 2's batch) appears on the field
by itself, with nothing else to set. A row still without a model falls back and says so on
screen.

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

Drop a `BP_FieldEnemy` in the map and fill its **Encounter**. Where it is placed is home: it
wanders around it (`WanderRadius`), and after an encounter it comes back there after
`RespawnDelayMin..Max` (unless `bRespawn` is off).

### A zone

Drop a **`JRPGEncounterZone`**:

| Property | |
|---|---|
| `Shape` | **Sphere** (`Radius`), **Box** (`BoxExtent`, rotates with the actor) or **Spline** (`AreaSpline`: a closed outline drawn by hand, any shape — concave included). Drawn in the editor |
| `FloorSearchDistance` | Sphere/Spline: how far above and below the zone actor to look for the ground — the zone may float or sit half underground (2000) |
| `EnemyClass` | `BP_FieldEnemy` |
| `Encounter` | Who fights when any of its enemies touches the player |
| `Visuals` | Optional: overrides the model for this zone's enemies |
| `MaxAlive` | How many walk the zone at once |
| `RespawnDelayMin/Max` | An enemy that touched the player comes back after this, at a new point |
| `RequiredFlagID` / `bRequiredFlagValue` | The zone only spawns with this WorldState event flag at this value |

Enemies spawn on navmesh points **inside the shape**, wander inside it, and when they give up a
chase they walk back into it. The zone's `Encounter` replaces each enemy's own.

**Spline shape.** Select the zone, then the `AreaSpline` points: drag them, Alt+drag one to
add a point, right-click a point for more options. It starts as a 20 m square with straight
corners; curved points work too. Only the outline **seen from above** counts — the points'
height does not — so the spline may cut through hills. **Snap Area To Ground** (button in the
details panel) moves the points down onto the terrain so the outline is easy to see; it does
not change the area. A spline with fewer than 3 points falls back to a sphere of `Radius`.

### Where the fight happens

On contact the enemy picks the **battle arena** for the fight — the `JRPGBattleArena` whose
area contains the contact point, or the nearest one — and passes it in
`FJRPGFieldEncounter::Arena` (the battle stage picks the same one). With `bPrintDebug` it
prints the arena's name and draws its formation for a few seconds. See
[BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md#ajrpgbattlearena--where-on-the-map-written-2026-10-27).

### The encounter

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
   `EncounterGraceTime` (3 s) ago, so two enemies touched together do not start two fights.
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
