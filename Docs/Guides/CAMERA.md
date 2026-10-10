# Camera System

`UCameraSubsystem` owns the game camera: **focusing on characters** (shop NPCs, dialogue)
with **ready-made presets**, **automatic restore** when the UI closes, **camera shake**,
**hand-placed level cameras** with a **ready trigger zone**, and the groundwork for battle
(group framing / orbit).

The player and NPCs are the game's Blueprints — the subsystem works with any `Actor`.
Everything is exposed in Blueprint under **`JRPG|Camera|*`**. No setup is mandatory: the
built-in presets work out of the box and the DataTable is optional.

---

## 1. The shop flow (the most common case)

**Shopkeeper NPC Blueprint (Interact):**
```
CameraSubsystem → FocusOnActor(
    Target Actor = self,          ← the NPC (do NOT leave this pin empty!)
    Preset = Shop Front,
    Blend Time = 0.75,
    Auto Restore on UIClose = ✔,
    Fade Out Time = 0.75)         ← the blend on the way BACK (0 = hard cut)
WebUISubsystem → OpenShop(ShopID)
```

When the player closes the shop the camera **returns on its own**, using `FadeOutTime`. In a
top-down game, `FadeOutTime = 0` avoids the camera "travelling" all the way back up.

---

## 2. Focus functions (`JRPG|Camera|Focus`)

| Function | Behavior |
|---|---|
| `FocusOnActor(Actor, Preset, BlendTime, bAutoRestoreOnUIClose, FadeOutTime)` | Focus an actor with a built-in preset (enum) |
| `FocusOnTarget(TargetID, ...)` | Same, resolving the actor through the registry (§5) |
| `FocusOnActorWithPreset(Actor, PresetID, ...)` | Preset by NAME: a DataTable row, falling back to a built-in of the same name |
| `FocusWithCustomParams(Actor, Params, ...)` | A `CameraPresetRow` struct assembled in BP on the spot |
| `FocusOnLevelCamera(CameraActor, BlendTime, bAutoRestoreOnUIClose, FadeOutTime)` | Focus a camera placed BY HAND in the level (§7) |
| `RestoreToPlayerCamera(BlendTime)` | Hand the camera back to the player pawn |
| `IsCameraFocusActive()` / `GetCurrentFocusActor()` | (pure) current state |
| `GetFramingPreset(Preset)` | (pure) a preset's effective values, DataTable overrides applied |
| `SetCameraPresetTable(DataTable)` | Register DT_Camera (call once at startup — §4) |

**Common parameters:**
- `BlendTime` — the transition INTO focus.
- `bAutoRestoreOnUIClose` — `true` means closing the WebUI (returning to HUD) hands the
  camera back automatically. The last focus call wins.
- `FadeOutTime` — the blend on the way BACK during auto-restore (**0 = hard cut**).

---

## 3. Framing presets (13 built in)

Convention: `YawDeg` is relative to the target's facing — **180 = head on**, **< 180 = left
side**, **negative = right side**. `FocusOffset Z` is the height of the focus point (chest,
for a ~180–190cm character).

| Preset | Dist | Pitch | Yaw | FocusZ | FOV | Typical use |
|---|---|---|---|---|---|---|
| `Default` | 600 | 15 | 180 | 120 | 60 | Generic |
| `CloseUp` | 180 | 5 | 180 | 155 | 45 | Face |
| `Conversation` | 320 | 8 | 160 | 140 | 50 | Opening a dialogue |
| `OverShoulder` | 240 | 6 | 145 | 145 | 45 | Shot-reverse-shot |
| `ShopFront` | 350 | 10 | 180 | 130 | 55 | Shop NPC |
| `ThreeQuarter` | 420 | 12 | 135 | 130 | 55 | Classic 3/4 |
| `TopDown` | 900 | 70 | 180 | 100 | 60 | Overhead |
| `Wide` | 1200 | 18 | 180 | 120 | 70 | Establishing shot |
| `BattleWide` | 1000 | 22 | 180 | 110 | 65 | Battle mode groundwork |
| `Side45Left` | 350 | 10 | 135 | 130 | 55 | 45° from the left |
| `Side45Right` | 350 | 10 | -135 | 130 | 55 | 45° from the right |
| `PortraitLeft` | 140 | 2 | 165 | 140 | 22 | Telephoto portrait + **DOF** |
| `PortraitRight` | 140 | 2 | -165 | 140 | 22 | Telephoto portrait + **DOF** |

**`CameraPresetRow` fields:** `Distance`, `PitchDeg`, `YawDeg`, `FocusOffset`,
`LookAtOffset` (composition), `FOV`, `bFollowTarget` + `FollowLagSpeed` (follows a moving
target with smoothing; 0 = snap), `bEnableDepthOfField` + `Aperture` (focus pinned on the
character at the preset's exact distance, background blurred — aperture 1.2 is very
cinematic; works on ANY preset).

---

## 4. Preset DataTable (optional)

1. Import `Docs/Data/DT_CameraPresets.csv` as a DataTable with row struct
   **`CameraPresetRow`** → name it `DT_Camera` in the project's Data folder.
2. Register it once at startup (GameMode/Level BP, alongside `SetShopDataTable`):
   `CameraSubsystem → SetCameraPresetTable(DT_Camera)`.

- A row named after a **built-in preset** (`ShopFront`, `CloseUp`, …) **overrides** the
  hardcoded values — tune distance/angle/FOV **without recompiling**.
- A row with a **new name** (`MySpecialShop`) becomes your own preset →
  `FocusOnActorWithPreset(NPC, "MySpecialShop")`.
- Changed the struct (new columns)? Reimport with **Reimport With New File** pointing at the
  plugin's CSV — a plain Reimport re-reads the path from the first import.

---

## 5. Target registry (`JRPG|Camera|Targets`)

Optional — lets you focus by ID without a direct actor reference.

| Function | Behavior |
|---|---|
| `RegisterCameraTarget(TargetID, Actor, Type)` | Call it in the BP's BeginPlay (Type: Player/NPC/Enemy/Prop). Re-registering overwrites |
| `UnregisterCameraTarget(TargetID)` | Removes |
| `GetCameraTarget(TargetID)` | (pure) the actor, nullptr if destroyed |
| `GetCameraTargetsByType(Type)` | (pure) every live actor of that type |
| `ScanWorldForCameraTargets()` | Scans actors with the `AutoDetectTag` actor tag (default `CameraTarget`); type inferred from extra tags (`Player`/`Enemy`/`Prop`) |

Properties: `bAutoDetectTargetsByTag` (rescans on each map) and `AutoDetectTag`. References
are weak: a destroyed actor drops out by itself, and the registry clears on every map change.

---

## 6. Camera shake (`JRPG|Camera|Shake`)

Works on **the player camera AND a focused camera** — it is a modifier on the
PlayerCameraManager. Procedural Perlin shake: smooth, no metronome feel.

```
PlayCameraShake(Preset, Scale)            ← ready-made variation
PlayCameraShakeCustom(Params, Scale)      ← your own JRPGCameraShakeParams struct
StopAllCameraShakes(bImmediate)           ← stop everything (false = smooth fade)
GetShakePresetParams(Preset)              ← (pure) built-in params to customize on top of
```

| Preset | Duration | Feel |
|---|---|---|
| `Light` | 0.3s | Light tremble |
| `Medium` | 0.5s | Medium impact |
| `Heavy` | 0.8s | Strong impact |
| `Explosion` | 1.0s | Explosion (+ FOV pulse) |
| `Earthquake` | 4.0s | Slow earthquake |
| `HitImpact` | 0.2s | Sharp punch/hit (good for battle) |
| `Rumble` | **loop** | Continuous tremor until `StopAllCameraShakes` |
| `Handheld` | **loop** | Slow handheld drift — enable during dialogue/shop to give a static camera some life |

`Scale` multiplies the intensity (0.5 = half, 2 = double). Multiple shakes run at once
(e.g. looping `Handheld` with `HitImpact` on top).

---

## 7. Hand-placed level cameras and trigger zones

**By hand:** place a `CameraActor` (or CineCamera) anywhere in the map, then:
```
CameraSubsystem → FocusOnLevelCamera(LevelCamera, BlendTime, AutoRestore, FadeOutTime)
```
Closing the UI returns to the player (auto-restore). `RestoreToPlayerCamera` returns manually.

**Ready-made zone (`JRPGCameraZone`)** — no tags or Blueprints needed:

1. Drag a **JRPG Camera Zone** into the level and resize the box.
2. Pick the **Mode**:
   - **Level Camera**: point **Level Camera** at your placed camera; the view switches to it.
   - **Player Camera** (with `AJRPGFieldCharacter`): the view stays on the character's camera and
     changes its arm, angle, yaw, FOV and lag to the zone's **Player Camera** settings.
3. Tune **Blend In Time** (entering) and **Fade Out Time** (leaving; 0 = hard cut).
4. Optional: **Priority** for overlapping zones, and **Control** to change where "up" points inside
   the zone (see [PLAYER.md](PLAYER.md#camera-and-controls)).

Detection compares against the **possessed pawn**, which is more reliable than a tag. The zone's
`bAutoRestoreOnUIClose` defaults to `false` (entering and leaving the box is what decides). The
class is `Blueprintable`, so you can subclass it for extra logic.

**Zones in a row (with `AJRPGFieldCharacter`).** The character keeps a stack of the zones it is in.
The active one is the highest **Priority**, and on a tie the last one entered. Leaving a zone goes
back to whatever is still active: in a corridor of touching zones, leaving the previous one never
undoes the one ahead, and leaving the last one returns to the map's base camera
(`AJRPGFieldMapSettings`). Without the field character, a zone is a plain enter/leave switch to its
Level Camera.

**Loading inside a zone:** handled automatically — the zone syncs with the pawn's real
position during the map's first seconds (a load teleports the player AFTER BeginPlay and the
teleport's overlap can be missed). If the save was made inside the zone, its camera engages
on load, and leaving the box restores normally.

---

## 8. Dialogue (ready for the dialogue system)

```
StartConversation(NPC, PlayerOverride = none, BlendTime)  ← frames the NPC (Conversation)
FocusSpeaker(bNPCSpeaking, BlendTime)                     ← on every line change
EndConversation(BlendTime)                                ← back to the player
IsConversationActive()                                    ← (pure)
```

`FocusSpeaker` performs a **shot-reverse-shot**: over-the-shoulder from behind the LISTENER
aiming at the SPEAKER, alternating sides correctly (the 180° rule — both shots stay on the
same side of the action line). Closing the dialogue UI also restores the camera.

---

## 9. Battle / group framing (`JRPG|Camera|Battle`)

```
FrameGroup(Actors[], BlendTime, Padding, BasePreset)   ← fits everyone on screen
OrbitAroundActor(Actor, DegreesPerSecond, Preset, BlendTime)
StopOrbit()                                            ← freeze at the current angle
```

- `FrameGroup` computes the combined bounds and pulls back until everyone fits
  (`Padding` 1.15 = 15% slack). Auto-restore is **off** by default — battle owns the cycle.
- A negative orbit speed reverses direction. `RestoreToPlayerCamera` ends any mode.

---

## 10. Events (`JRPG|Camera|Events`)

| Event | Fires when |
|---|---|
| `OnCameraFocusChanged(FocusActor, PresetID)` | The camera focused something (PresetID is the preset name, or `"LevelCamera"`/`"Custom"`) |
| `OnCameraRestored()` | The camera returned to the player (manually or automatically) |
| `OnCameraShakeFinished(Preset)` | A shake ended |

`DefaultRestoreBlendTime` (default 0.75) is the blend used by automatic restores that have no
`FadeOutTime` of their own (conversation, destroyed target).

---

## 11. Troubleshooting

- **"focus with no valid actor"** in the log: the node's **Target Actor** pin is empty — wire
  `self` (the NPC reference) into it, not to be confused with the Target pin (the subsystem).
- **Camera spawns on top of the player:** a head-on preset (Yaw 180) places the camera where
  the player usually stands. Use `Side45Left/Right`, change the row's `YawDeg` (e.g. 155), or
  increase `Distance`.
- **Framing too high or too low:** adjust `FocusOffset Z` in the row (~65% of the character's
  height; height = Capsule Half Height × 2).
- **NPC facing away from the camera:** the preset is relative to the actor's facing — check
  the NPC's rotation in the level (the arrow is where it "looks", the X axis).
- **"expected column ... not found"** when reimporting the CSV: you are reading an old copy —
  use **Reimport With New File** pointing at `Docs/Data/DT_CameraPresets.csv`.
- **Target destroyed while focused:** the camera returns to the player on its own (Warning in
  the log) — expected behavior, no crash.
- **A new pin does not appear after recompiling:** right-click the node → **Refresh Node**.
