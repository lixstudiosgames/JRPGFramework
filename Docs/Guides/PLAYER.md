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
| `UJRPGFieldPartyComponent` | `Public/Field/JRPGFieldPartyComponent.h` | On the controller: puts the formation on the map, makes it follow the leader, switches who is controlled (R3) |
| `AJRPGPartyFollowerController` | `Public/Field/JRPGPartyFollowerController.h` | The `AAIController` the followers are possessed by, for `MoveTo` (the logic is in the component) |

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
| Switch character (press) | R, right stick click (R3) |

On the gamepad, sprint shares the bottom face button with interacting, as in the original: holding it
runs, and only a new press interacts. Running into an NPC with the button held does nothing until it is
released and pressed again. The input assets set `bConsumeInput` off on both actions so the shared
button reaches both.

To change keys, or to use Enhanced Input's player remapping, create the assets (an `InputAction`
Axis2D for moving, a Bool one for sprinting, and an `InputMappingContext`) and set them on the
controller Blueprint. With no `MappingContext` set, any action left empty still uses the default.
**Once you set your own `MappingContext`, every action needs its own asset mapped in it**
(including `SwitchLeaderAction`): a default action is not part of your context, so no key reaches
it. The controller logs a warning naming the empty field.

| Property / function | Effect |
|---|---|
| `MappingContext`, `MoveAction`, `SprintAction`, `InteractAction`, `MenuAction`, `SwitchLeaderAction` | Optional assets (above) |
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

## Party in the field

The other members of the formation walk behind the leader, and **R3** (R on the keyboard) hands the
control to the next one. A party member is the **same `AJRPGFieldCharacter`** as the player: the
leader is possessed by the player controller, the others by an `AJRPGPartyFollowerController`. The
Anim Blueprint sees the same movement and the same `GetGroundSpeed()` either way, so each character
needs nothing beyond what the player character already has.

### Setting it up

1. **One Blueprint per character**, child of `AJRPGFieldCharacter` (e.g. `BP_Vahn`, `BP_Noa`,
   `BP_Gala`), each with its mesh, Anim Class and **Party Key** set to its `DT_Characters` row
   (`Vahn`, `Noa`, `Gala`).
2. **On the controller Blueprint** (`BP_FieldController`), select the **FieldParty** component and
   fill **Member Classes**: `Vahn -> BP_Vahn`, `Noa -> BP_Noa`, `Gala -> BP_Gala`. A member with no
   class does not appear on the map (the log says so).
3. The GameMode keeps spawning its Default Pawn as before. If its Party Key is empty it counts as the
   first of the formation.
4. **Maps without the party** (World Map, small interiors): untick **Show Party** on that map's
   `AJRPGFieldMapSettings`. Only the leader appears and R3 does nothing.

Who appears is the **formation** (`UPartySubsystem::GetActiveParty`). Recruiting someone, or changing
the formation in the menu, updates the map on the spot (`OnPartyChanged`). If the character being
controlled leaves the formation, the control passes to the first one still in it, in the same place.

### How they follow

`FollowMode` picks one of two ways. **Leash** is the default; a map without a navmesh falls back
to **Trail** on its own.

**Leash: stand still, then walk over on the navmesh.** This is how most 3D party games do it. A
follower stands where it is while the leader is near. Once the leader is more than
`LeashStartDistance` (350) away, the follower waits a short random delay (`LeashReactionMin` to
`LeashReactionMax`, so they never leave on the same frame), then walks over with the engine's
`MoveTo` (the follower controller is an `AAIController`). It heads for a place behind and to one
side of the leader (the same `GatherAngles` as gathering, turned with the leader), so the party
spreads out instead of walking in single file. While the leader keeps walking, the follower matches
its speed and stays a little behind that place, so it never stops and starts. The path is redone
every `LeashRepathInterval` (0.3 s), and only when the place has moved more than
`LeashRepathDistance`. The shortest path on the navmesh also means a follower never walks a loop
the leader made. When the leader stops, followers go to their places beside it and stop within
`LeashArriveRadius`. If a place is not valid, they stop within `LeashStopDistance` of the leader
instead. A follower already close to its place stays put, unless another party member is standing
on top of it.

**Trail: walk the leader's path.** The leader leaves a trail and each follower walks along it,
`FollowSpacing` behind the one in front. A follower only goes where the leader went, so it never
cuts a corner into a wall and does not need a navmesh. It looks like the classic single file.

**Shortcuts** (Trail only, `bTrailShortcuts`, on). A pure trail has one flaw: if the leader goes
off, makes a loop and comes back past the followers, they would walk the whole loop. Four times a
second each follower looks along the trail, from its own place backwards, for the furthest point it
can reach **in a straight line**: no navmesh edge in between (a wall, a drop), or, on a map without a
navmesh, a sphere sweep at body height that hits nothing with floor all along the way. If that point
saves at least `ShortcutMinSaving` of trail and is within `ShortcutMaxDistance`, the follower walks
straight to it and carries on along the trail from there.

**Gathering beside the leader.** After `GatherDelay` with the leader stopped, they move to places
around it (`GatherAngles`, in degrees from the leader's front: by default one on each side, a little
behind, then one behind). A place is only used if it lies on the navmesh with no navmesh edge (wall,
drop) between it and the leader. In Trail mode there must also be no edge between it and the
follower, since the follower walks there in a straight line. Without a navmesh, sphere sweeps do
the same job. A place that does not pass leaves the follower where it is.

**Speed.** Followers can move `FollowerSpeedScale` (1.15x) faster than the leader so they catch up
after a turn or a stop, and they run when the leader runs. In Leash mode they also run when they
are more than `LeashRunDistance` (650) from the leader, and go back to walking near half of that.
In Trail mode they run when they are more than `CatchUpDistance` behind.

**Safety.** A follower teleports to its place when the leader jumps (a portal, a load, a cutscene
moving it more than 600 uu in one frame), when it ends up more than `TeleportDistance` away, or when it
spends `StuckTime` trying to walk without moving (a door closed between them).

### Collision: followers never trigger anything

While following, **every collision component** of the character ignores the Pawn channel, so the
followers walk through the leader, each other and NPCs and never block a narrow corridor. They also
generate **no overlap events**, so no trigger box on the map (doors, portals, events, camera zones,
interaction) fires for them. They still collide with the world (floor, walls). Becoming the leader
restores what the Blueprint had.

Camera zones, interaction and the mist trail already react only to the possessed pawn, so they move
to whoever is in control.

### Switching character

`CycleLeader()` (R3) passes the control to the next member of the formation who is on the map and
within `MaxSwitchDistance`. The new leader takes over **where it stands**: the camera slides to it
(`SwitchBlendTime`) and the old leader becomes a follower in place. Only from the HUD, never with a
menu, shop or dialogue open.

The order of a switch matters, and `DoSwitchLeader` follows it:

1. The old leader becomes a follower while still possessed, so triggers and camera zones get its exit
   as the player's.
2. The controller possesses the new one through `PossessKeepingView`. A plain `Possess` puts the view
   on the controller when the old pawn is released and then cuts to the new one.
3. The view starts its blend.
4. Only then does the new leader leave follower mode. Camera zones and triggers it is standing in
   receive the entry now, so a Level Camera zone wins over the blend from step 3.

If the new leader is standing inside a trigger box, that trigger fires on the switch: it is the
player arriving there.

**Across maps.** The chosen character is stored in `UPartySubsystem::SetFieldLeader`, in memory only.
On the next map the controller takes the pawn the GameMode spawned and puts the chosen character in
its exact place (the portal's PlayerStart). The save does **not** store it: loading a save starts with
the first of the formation.

### Cutscenes

The party is already on the map, but its members are spawned at runtime, so a Level Sequence cannot
reference them as level actors. Get them from the component and bind them at runtime:

| Function | Use |
|---|---|
| `GetPartyPawn(Key)` / `GetLeader()` / `GetFollowers()` | The characters on the map, to bind into a Level Sequence at runtime |
| `SetFollowEnabled(false)` | Followers stand still, so the Sequencer can move them without the party fighting it. Turning it back on teleports no one |
| `SnapFollowersToLeader()` | Puts everyone back next to the leader at the end |
| `SwitchLeader(Key)` | Passes the control to a specific member at any distance |
| `RefreshParty()` | Re-checks the map against the formation (it already runs on `OnPartyChanged`) |
| `OnLeaderChanged(NewLeader, OldLeader)` | Fires after every switch |

From the controller Blueprint, it is the **FieldParty** component.

### Testing from the dev menu

| Command | Effect |
|---|---|
| `party.field` | Who leads, who follows, distance and speed of each |
| `party.lead` | The same as R3 |
| `party.lead <key>` | Takes control of that member at any distance (`party.lead Noa`) |

Recruit and activate first (`party.recruit Noa`, `party.active Noa 1`): the follower appears next
to the leader right away.
