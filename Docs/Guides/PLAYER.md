# Player (Field Character)

The character that walks the map, seen from above. It has nothing to do with battle.

| Piece | Where | What it does |
|---|---|---|
| `AJRPGFieldCharacter` | `Public/Field/JRPGFieldCharacter.h` | The `ACharacter`: top-down camera, walk/sprint, faces where it walks, mist trail |
| `AJRPGFieldPlayerController` | `Public/Field/JRPGFieldPlayerController.h` | Reads the input (Enhanced Input) and moves the pawn relative to the camera on screen |
| `AJRPGFieldMapSettings` | `Public/Field/JRPGFieldMapSettings.h` | One per map: that map's base camera, controls and speeds |
| `FJRPGFieldCameraSettings`, `EJRPGFieldControlMode` | `Public/Field/JRPGFieldTypes.h` | A camera setup (arm, pitch, yaw, FOV, lag, focus offset) and where "up" comes from |
| `IJRPGInteractable` | `Public/Field/JRPGInteractable.h` | The interface for anything the player interacts with (NPC, door, chest, save point...) |
| `UJRPGInteractionComponent` | `Public/Field/JRPGInteractionComponent.h` | Picks who to interact with and calls their `Interact` |

The split is deliberate. The character only knows how to move; the controller owns the input.
The same character can later be possessed by an AI, such as a party member following the leader.

---

## Setting it up

1. **Character Blueprint.** Create a Blueprint child of `AJRPGFieldCharacter` (e.g.
   `BP_FieldCharacter`). Set the **Mesh** (skeletal mesh, rotated -90 yaw and lowered to the capsule
   bottom as usual) and its **Anim Class**.
2. **Controller Blueprint.** Create a Blueprint child of `AJRPGFieldPlayerController` (e.g.
   `BP_FieldController`). Nothing has to be set: the input works out of the box (below).
3. **GameMode.** Set them as **Default Pawn Class** and **Player Controller Class**. To keep a
   GameMode's own logic (choosing the `PlayerStart` by teleport tag, for example), make a child of
   that GameMode and change only those two fields.

4. **Each map (optional).** Drop a **JRPG Field Map Settings** in the level for that map's base
   camera, controls and speeds. Without one, the character's own defaults apply.

Save/Load works with no extra setup: it reads the pawn from the player controller.

## Camera and controls

The camera works in layers:

1. **Base camera**: `BaseCamera` on the character, replaced at BeginPlay by the map's
   `AJRPGFieldMapSettings` if there is one. This replaces calling a config function from each
   map's Level Blueprint. `SetBaseCamera(Settings, BlendTime)` still changes it at runtime.
2. **Camera zones** on top (`AJRPGCameraZone`). A **Player Camera** zone changes this character's
   arm; a **Level Camera** zone switches to a camera placed in the level. Zones stack: see
   [CAMERA.md](CAMERA.md#7-hand-placed-level-cameras-and-trigger-zones). Spawning inside a zone (a
   save load, a portal) starts with that zone's camera, with no blend from the base.
3. Changes between arm setups **blend** (the zone's Blend In / Fade Out times). Returning from a
   level camera snaps the arm and lets the view blend.

**The map's "north".** If a map was built with north along +Y, set the base camera's **Yaw** to 90
instead of rotating the map. Rotating a whole map moves its sun/sky direction, navmesh, streaming
cells and every position stored in saves and portals. With camera-relative controls, the camera yaw
is all that is needed: walking follows it.

**Where "up" comes from** (`EJRPGFieldControlMode`), per map (`AJRPGFieldMapSettings`) or per zone
(`bOverrideControl`):

| Mode | "Up" on the stick | Use |
|---|---|---|
| **Camera Relative** (default) | Away from the camera on screen | Almost everywhere: any map, any yaw |
| **Fixed Yaw** | A fixed world direction (`ControlYaw`) | Caves and fixed cameras, where "up" should stay the same side of the map whatever the camera does |

On top of either mode, the controller **holds the direction through camera changes**
(`bHoldDirectionOnCameraChange`, on). While the stick stays pressed, "up" keeps the direction from
when the walk started, so crossing into a zone with another camera angle never turns the character
around mid-step. Releasing the stick picks up the new camera.

## What it comes with

| Component | Default |
|---|---|
| `CameraBoom` (SpringArm) | Absolute rotation, so it never spins with the character. No collision test (a shrinking arm makes a top-down camera jump behind roofs and trees). Set from `BaseCamera`: pitch -50, length 1800, lag 2.5 |
| `FieldCamera` | FOV 55 (from `BaseCamera`). `UCameraSubsystem::RestoreToPlayerCamera` comes back to it |
| `MistDisturber` | `UJRPGMistDisturberComponent`: parts the JRPGMist around the character and leaves the trail. See [MIST.md](MIST.md) |
| `Interaction` | `UJRPGInteractionComponent`: picks the interaction target (below) |
| Character Movement | Faces where it walks (`bOrientRotationToMovement`, 500°/s), acceleration 1000, braking 2000, friction 8 |
| Capsule | Radius 25, half height 96 |

## Character (`AJRPGFieldCharacter`)

| Property / function | Effect |
|---|---|
| `WalkSpeed` / `SprintSpeed` | uu/s walking (300) and sprinting (700). `WalkSpeed` also updates `MaxWalkSpeed` in Details. A map can override both |
| `BaseCamera`, `BaseControlMode`, `BaseControlYaw` | The base camera and controls (above). The Blueprint viewport shows `BaseCamera` |
| `SetBaseCamera(Settings, BlendTime)` / `SetBaseControl(Mode, Yaw)` | Change the base at runtime. Inside a zone, the new base applies when leaving it |
| `GetCurrentCamera()` / `GetActiveCameraZone()` / `GetControlReferenceYaw(ScreenYaw)` | The arm setup right now, the zone in charge, and the yaw that is "up" |
| `bCanSprint` | Off: the sprint button does nothing (World Map, towns) |
| `bDeclareFieldOnBeginPlay` | On: calls `WorldStateSubsystem::SetIsInField(true)` at BeginPlay, which unlocks Save in the pause menu. This character only exists in field maps; turn it off if the map declares it itself |
| `SetSprinting(bool)` / `IsSprinting()` | Sprint on/off (the controller calls it while the button is held) |
| `GetGroundSpeed()` | Horizontal speed in uu/s, for the Anim Blueprint (idle/walk/run) |
| `OnSprintChanged` | Fires when sprinting starts or stops |

## Controller (`AJRPGFieldPlayerController`)

**Movement is relative to the camera on screen** by default, using only its yaw: "up" means "away
from the camera", with the character's camera, a level camera or a zone, in any map or angle. A map or
zone can switch to a fixed world direction instead (above).

**Input without assets.** When `MappingContext`, `MoveAction` or `SprintAction` are empty, the
controller creates them at runtime:

| Action | Keys |
|---|---|
| Move (Axis2D) | WASD, arrow keys, gamepad left stick (with dead zone) |
| Sprint (hold) | Left Shift, gamepad bottom face button (A on Xbox, X on PlayStation) |
| Interact (press) | E, the same gamepad bottom face button |
| Menu (press) | Tab, gamepad top face button (Y on Xbox, Triangle on PlayStation) |

On the gamepad, sprint shares the bottom face button with interacting, as in the original: holding it
runs, and only a new press interacts. Running into an NPC with the button held does nothing until it is
released and pressed again. The input assets set `bConsumeInput` off on both actions so the shared
button reaches both.

To change keys, or to use Enhanced Input's player remapping, create the assets (an `InputAction`
Axis2D for moving, a Bool one for sprinting, and an `InputMappingContext`) and set them on the
controller Blueprint. Anything left empty still uses the default.

| Property / function | Effect |
|---|---|
| `MappingContext`, `MoveAction`, `SprintAction`, `InteractAction`, `MenuAction` | Optional assets (above) |
| `OpenGameMenu()` | Opens the game menu (`UWebUISubsystem::OpenMenu`) only from the HUD, stopping the sprint first. The menu action calls it. With the menu open the UI owns the input and closes it |
| `bHoldDirectionOnCameraChange` | Keeps "up" from when the walk started while the stick stays pressed (on) |
| `MappingPriority` | Priority of the Field context (0). Menus on top use a higher one |
| `SetFieldInputEnabled(bool)` / `IsFieldInputEnabled()` | Removes the Field context for a cutscene, dialogue or menu, and drops the sprint. The UI keeps receiving input |

The controller also keeps the **control rotation** facing where the character faces. The save stores
the control rotation (`CoreSubsystem::GetPlayerSaveTransform`), so without it the character would come
back from a load facing the wrong way.

The mouse cursor is hidden by default (`bShowMouseCursor`): the field is played on keyboard or gamepad.

## Interaction

**Making something interactable.** In its Blueprint, *Class Settings → Implemented Interfaces → JRPG
Interactable*, and implement the **Interact** event (`InteractingActor` is the character). Give it a
collision that overlaps the character's capsule (a Box with overlap on Pawn): its size is the reach.
The actor decides for itself whether it accepts (a chest already opened, an NPC that only talks after
an event): the player only reports that it interacted.

**Who gets picked.** `UJRPGInteractionComponent` looks at the actors overlapping the character that
are interactable, and picks the **closest one in front** of the character, never an arbitrary one. A
target behind counts as `FacingWeight` (200 uu) further away per side it is turned from.

| Property / function / event | Effect |
|---|---|
| `TryInteract()` | Interacts with the current target and returns it (or nullptr). The controller calls it |
| `GetCurrentTarget()` | Who it would interact with right now |
| `OnTargetChanged(NewTarget)` | The target changed (came into reach, left it, the character turned): show or hide a prompt. Checked 10 times a second |
| `OnInteracted(Target)` | Fired after calling `Interact` |
| `FacingWeight` | How much being behind counts, in uu (0 = distance only) |
| `LegacyInterfaces` | Old Blueprint interfaces also accepted (below) |

The controller calls `TryInteract` on the **press** (`Started`), never while the button is held. On the
gamepad that button also sprints, so arriving at an NPC while running with it held does nothing until
it is released and pressed again.

**Migrating old Blueprints.** `LegacyInterfaces` lists old Blueprint interfaces whose **Interact has no
parameters**. Actors that still implement one keep working: the component calls their `Interact` by
name. Move each Blueprint to *JRPG Interactable* when you next touch it (swap the interface in Class
Settings and wire the new event), and empty the list when none are left.
