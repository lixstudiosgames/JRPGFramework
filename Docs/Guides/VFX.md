# VFX (JRPGVFX)

**Status: built; the two automation tests pass. The first effects in use are the three Spirit auras
([The Spirit aura](#the-spirit-aura)); the battle hook for them is written, waiting for the next build.**
Sections marked *planned* are decisions, not code.

The remake's effects are **new, modern effects**: particles flying, a fire tornado on the enemy, wind,
lightning. Nothing is ported from the PS1 look. The original is only read for **when and where** each
effect fires (see [What comes from the original](#what-comes-from-the-original)).

| Piece | Where | What it does |
|---|---|---|
| `UJRPGVFXRecipe` | `Public/VFX/JRPGVFXRecipe.h` | One effect as data: a timeline of layers. A Data Asset in the project |
| `FJRPGVFXLayer` and friends | `Public/VFX/JRPGVFXTypes.h` | The layer, its animated tracks, the firing context, the `DT_VFX` row |
| `UJRPGVFXSubsystem` | `Public/VFX/JRPGVFXSubsystem.h` | Plays recipes: spawns, anchors, animates and pools the layers |
| `UJRPGAnimNotify_VFX` | `Public/VFX/JRPGAnimNotify_VFX.h` | Fires a recipe from a frame of a clip |
| `JRPG.VFX.Tracks`, `JRPG.VFX.RecipeLength` | `Private/Tests/JRPGVFXTests.cpp` | Tests of the pure-data part (eases, tracks, recipe length) |

The plugin ships **no** effect assets. Recipes, materials, meshes and Niagara systems live in the project.

---

## The idea: an effect is a stack of layers

A modern effect is never one thing. A mini fire tornado on a monster is:

| Layer | What it is | Who makes it look good |
|---|---|---|
| The funnel | 2 or 3 mesh shells with fire scrolling, eroding and twisting | A **material** with procedural HLSL |
| Embers, smoke, sparks | Particles spiralling up | A **Niagara** system |
| The glow | An orange point light pulsing | The recipe (a `Light` layer) |
| The scorch | The body of the target flashing | The recipe (a `BodyParam` layer) |
| The punch | A camera shake | The recipe (a `CameraShake` layer) |

So the split of work is:

- **Materials own the appearance**: noisy edges, erosion, plasma arcs, glow, fading with age. Written as
  HLSL in `Shaders/` and called from a Custom node, the same way `JRPGMist.ush` is.
- **Niagara owns particle motion**: spawn, velocity, lifetime, size, colour over life. A few generic
  systems driven by user parameters, not one hand-built system per spell.
- **C++ owns orchestration**: what enters when, anchored to whom, with which parameters, and cleaning up.
- **The recipe is the glue**, and it is data: tuning an effect does not need a rebuild.

---

## The recipe

Create one in the Content Browser: **Miscellaneous › Data Asset › JRPGVFXRecipe**.

| Field | Meaning |
|---|---|
| `Duration` | Length of the effect in seconds. `0` = the end of the last layer that has its own duration |
| `bLooping` | The effect stays until `StopEffect` (an aura, a status). See [Looping](#looping-and-stopping) |
| `FadeOutTime` | On a soft stop, how long the effect takes to disappear |
| `FadeParameter` | The scalar the `Mesh` layers' materials receive during the fade (`JRPGVFXFade`, 1 → 0) |
| `Layers` | The layers, in any order |

### A layer

Every layer has a `Label` (only for reading the recipe), a `Type`, a `StartTime` and a `Duration`.
`Duration 0` means "until the recipe ends".

| Type | Puts in the scene | Its own fields | Tracks write to |
|---|---|---|---|
| `Niagara` | A Niagara system | `System`, `SkeletalMeshParameter` | User parameters |
| `Mesh` | A static mesh | `Mesh`, `Material` (all slots; empty = the mesh's own) | Material parameters |
| `Light` | A point light, no shadows | `LightColor`, `LightIntensity` (unitless), `LightRadius` | `Intensity`, `Radius`, `Color` |
| `BodyParam` | Nothing new | — | Material parameters on **every mesh of the anchor actor** |
| `CameraShake` | A shake, fired once at `StartTime` | `ShakePreset`, `ShakeScale` | — |

New layer types always go at the **end** of `EJRPGVFXLayerType` (it is serialized in the recipes).

### Where a layer sits

| Field | Meaning |
|---|---|
| `Anchor` | `Target`, `Instigator` or `Location` (a world point). A missing actor falls back to the context's `Location` |
| `Placement` | On the anchor actor: `Pivot`, `BoundsCenter` or `BoundsBottom` (the floor under it) |
| `Socket` | A socket or bone of the anchor actor. With it, `Placement` is ignored. A socket that does not exist falls back to `Placement` |
| `bAttach` | The layer follows the anchor. Off = it stays where it was born |
| `Offset` | Position, rotation and scale relative to the anchor point. The yaw follows the anchor actor's yaw |
| `bScaleByAnchorSize` + `ReferenceSize` | Scales the layer by the anchor's XY bounds radius divided by `ReferenceSize`, so the same tornado grows on a big monster |
| `bRandomYaw` | A random yaw per play, from the subsystem's cosmetic stream |

The offset's position scales together with the effect: a funnel twice as big sits twice as far from the floor.

### Tracks

A track animates one parameter from `From` to `To` over the layer's life.

- **Scalar track**: `Name`, `From`, `To`, and an `Ease` (`Linear`, `EaseIn`, `EaseOut`, `EaseInOut`,
  `Pulse`) or a `Curve` asset (time 0..1 → blend 0..1) that replaces the ease.
- **Color track**: `Name`, `From`, `To`, `Ease`, and `bTintWithContextColor` to multiply by the colour of
  the play (the element, the hero's colour).

`Pulse` goes to `To` in the middle of the layer and back to `From` at the end.

Reserved scalar names, which never reach a material or a user parameter:

| Name | Applies to | Effect |
|---|---|---|
| `Scale` | `Niagara`, `Mesh`, `Light` | Multiplies the layer's scale |
| `Intensity` | `Light` | Multiplies `LightIntensity` |
| `Radius` | `Light` | Multiplies `LightRadius` |

On a `Light` layer the color track named `Color` sets the light's colour.

### What every Niagara layer receives

Written by the subsystem if the system has a user parameter with that name and type:

| User parameter | Type | When | Value |
|---|---|---|---|
| `Color` | LinearColor | At spawn | The context's colour. **White (the default) writes nothing**: the system keeps its own `Color` |
| `EffectScale` | float | At spawn | Context scale × the anchor-size factor |
| `TargetPosition` | Position | Every frame | The centre of the target |
| `InstigatorPosition` | Position | Every frame | The centre of whoever caused it |

The two positions are refreshed every frame on purpose: a swarm pulled toward the hero keeps following the
hero. `SkeletalMeshParameter` names a Skeletal Mesh data interface user parameter that receives the anchor
actor's mesh, for particles born on the body's surface (dust on death, the Seru absorb).

The system is spawned **deactivated**, receives its parameters, and is activated after: the first
simulation tick already sees the right values.

---

## Playing an effect

```cpp
FJRPGVFXContext Context;
Context.Instigator = Attacker;
Context.Target = Victim;
Context.Color = ElementColor;

UJRPGVFXSubsystem* VFX = UJRPGVFXSubsystem::Get(this);
VFX->PlayEffect(Recipe, Context);              // by asset
VFX->PlayEffectById(TEXT("TornadoFlame"), Context);   // by DT_VFX row
```

| Call | Use |
|---|---|
| `PlayEffect(Recipe, Context)` | Returns a handle. Layers with `StartTime 0` enter on the same frame |
| `PlayEffectById(Id, Context)` | Looks the recipe up in `DT_VFX` (`FJRPGVFXRow`: RowName = id, `Recipe` = soft reference) |
| `StopEffect(Handle, bImmediate)` | Soft (fades over `FadeOutTime`) or at once |
| `StopAllEffects(bImmediate)` | End of a fight, a scene change |
| `IsEffectPlaying(Handle)` | |

`DT_VFX` follows the other tables: `/Game/Data/DT_VFX`, falling back to the plugin's `Data/` folder.

**From a clip.** Add the **Battle VFX** notify (`UJRPGAnimNotify_VFX`) on the frame. It takes a `Recipe`,
or an `EffectId` when there is no recipe set. Whoever plays the clip is the instigator. With
`bOnActionTarget` the target is the target of the action in progress (`AJRPGBattlePawn::GetActionTarget`);
otherwise it is the owner itself. A notify keeps no handle, so it **refuses looping recipes**.

**From the console (PIE).**

```
JRPG.VFX.Play <DT_VFX id | /Game/path/to/Recipe> [scale]
JRPG.VFX.StopAll [soft]
```

`JRPG.VFX.Play` plays on the player's pawn, as both instigator and target.

### Looping and stopping

In a looping recipe, layers with `Duration 0` never end and their tracks repeat every `GetLength()`.
Layers with their own duration play once, at the start (an intro burst before the aura settles).

On a soft `StopEffect`:

- Niagara layers stop emitting at once; live particles finish on their own.
- `Mesh` layers receive `FadeParameter` from 1 to 0. The material has to read it, or the mesh pops out.
- `Light` layers dim to 0.
- `BodyParam` layers ease back to each track's `From`.

When a non-looping `BodyParam` layer ends, the body keeps the **final** value of its tracks. A hit flash
should therefore use `Pulse`, or end at the rest value.

### The Spirit aura

The first effects wired into the fight. When a hero's Spirit step starts, `AJRPGBattlePawn` plays the
`DT_VFX` row `Spirit_<Key>` (`Spirit_Vahn`, `Spirit_Noa`, `Spirit_Gala`) on itself, and stops it softly when
the action ends (or at once if the pawn leaves the world). A hero with no row only logs the warning.

| Row | Recipe | Niagara system | Element |
|---|---|---|---|
| `Spirit_Vahn` | `VFX_Spirit_Vahn` | `NS_Spirit_Vahn` | Fire: flame swirls, embers, smoke |
| `Spirit_Noa` | `VFX_Spirit_Noa` | `NS_Spirit_Noa` | Wind: green spiral, smoke, sparks |
| `Spirit_Gala` | `VFX_Spirit_Gala` | `NS_Spirit_Gala` | Thunder: lightning sprites swirling up a cylinder |

All in `/Game/Legaia/VFX/Spirit/`. Each recipe is looping, one Niagara layer anchored at the hero's feet
(`BoundsBottom`, attached), `FadeOutTime` 0.6. The context colour is left white, so each system keeps its
own HDR colour.

The systems are duplicates of systems from two free packs in the host project (`/Game/_ThidParty/Free_Magic`
and `Free_Spells`) with three layers of ours added: `JRPG_Stars` (four-pointed stars rising and twinkling,
`M_VFX_SpiritStar`) and `JRPG_Gather` (streaks pulled into the chest, `M_VFX_SpiritStreak`). Deleting those
packs breaks the three systems. Changes made to the pack layers:

- Their emitters were `Once` with `Life Cycle Mode: Self`: they play one pass and the system restarts only
  when every emitter has finished. Our infinite layers keep the system alive, so the pack layers would play
  once and go dark. They are set to `Infinite` with a 5 s loop.
- `User.Scale_All` sizes the whole pack part (Vahn 0.75, Noa 0.7, Gala 0.6).
- Gala: the `Ray` emitter removed; the lightning sprites (`Sparks2`) are born on a cylinder and turned and
  lifted every frame by a `SetParameters` HLSL expression. That emitter simulates in **local space**, so the
  expression works around (0,0,0), not the owner's position.

What was learned from the packs, for the next effects:

- **Saturated HDR colours with the opposite channels at zero** (fire `(100, 15, 0)`). This keeps the
  colour alive over a bright background; a low colour mixed toward white goes pastel.
- **Translucent, not additive**, for the mesh layers: colour × a noise mask, opacity apart.
- **Two noise textures**: one panning along a spiral mesh, the other distorting it; a 4-channel Dynamic
  Parameter driven by Niagara (erosion over life, a mask sweeping along the mesh, a random offset, contrast).
- **Flipbooks** for volume: smoke 8×8, flames 2×2, lightning 3×3.
- The engine MCP cannot copy one emitter out of another system, so a pack system is used whole, duplicated.

---

## Authoring effects with Claude

Niagara is a node graph, which an LLM cannot edit directly. The pipeline below avoids the graph. It comes
from a workflow other people use for the same problem ("Automating Unreal Engine Niagara VFX with Claude,
Python, and C++", r/ClaudeCode), adapted to what this project already has.

```
asset script / MCP calls  ->  saved assets (materials, Niagara systems, recipes)  ->  UJRPGVFXSubsystem plays them
```

1. **Start from a working template, never from an empty system.** The engine's stock emitters live in
   `/Niagara/DefaultAssets/Templates/Emitters/`. Copy one into the system per layer and name each one for
   what it does (`Flash`, `Sparks`, `Smoke`, `Embers`).
2. **Describe before changing.** Dump the emitter's real modules and inputs first; names cannot be guessed.
3. **Set with typed setters, then read back.** A change that matched nothing did not apply, and it fails
   silently. Every write is followed by a read of the same value.
4. **The look goes in the material.** Procedural HLSL for edges, erosion, glow and age-based fading. Niagara
   only feeds it motion, lifetime, size and colour.
5. **Compile explicitly and wait.** Niagara compilation and shader compilation are asynchronous; check the
   compile state and the stack issues before judging the result or closing the editor.
6. **Judge it in the FX Lab**, not in the asset editor.

In the original workflow steps 1 to 3 go through a custom C++ editor library exposed to Python. Here the
engine's MCP server already offers that surface (`NiagaraToolsets.NiagaraToolset_System`: create a system
from a template, add an emitter from a template, add a renderer, read and write emitter and module inputs,
`GetStackIssues`, `GetSystemCompileState`). The plugin has no Editor module and should not grow one for
this unless the MCP turns out to be missing something specific. **How far the MCP gets is not verified yet**:
the first effect is the test.

Two things worth trying while building the first systems, both unverified:

- **Lightweight (stateless) emitters.** They are configured through a fixed set of modules with plain
  properties, no graph and no scratch pad, which is the shape a tool can edit. There is a forum report of
  systems made only of stateless emitters failing to save in 5.8.2.
- **Epic's Niagara Examples pack** (5.7) as a source of working systems to parametrise.

### The FX Lab *(planned)*

A map in the host project used only to look at effects. It is the preview: effects are judged in the real
renderer, with the game's own post-process, fog and lighting.

- A target dummy and an instigator dummy, a neutral floor, a fixed camera, a dark and a bright backdrop.
- At least one small and one large target, to check `bScaleByAnchorSize`.
- `JRPG.VFX.Play` plays any recipe there; a recipe is a Data Asset, so editing it and playing again needs no
  restart.

LegaiaStudio does not preview effects. A browser cannot run Niagara or Unreal materials, so anything drawn
there would be a different effect. If the Studio gets a VFX tab later, it edits recipe data and asks the
editor to play it in the Lab.

---

## What comes from the original

Only the trigger points. Almost every battle effect of the original is authored per clip, as up to eight
8-byte effect records in the same action entry that carries the clip's sounds
([`Reference/BATTLE_RE_SWEEP.md`](../Reference/BATTLE_RE_SWEEP.md) §8.2). The plan:

1. Export those records with the sound tracks (`frame`, `code`, `offset`).
2. The import script places a **Battle VFX** notify with an `EffectId` on each clip, beside the sound notifies.
3. `DT_VFX` maps each id to a recipe made from scratch.

No texture, model or look is taken from the disc for effects.

---

## Rules

- **The fight's logic does not know VFX exists.** Effects are fired by the presentation and by clips.
- **Randomness is cosmetic.** It comes from the subsystem's own stream, never from the fight's `Rng`.
- **`UJRPGVFXSubsystem` is a `UTickableWorldSubsystem`**, not a `UGameInstanceSubsystem`: it owns
  components that live and die with the world, like `UJRPGBattleSceneSubsystem`. This is an exception to
  the first ground rule of [`ARCHITECTURE.md`](../ARCHITECTURE.md).
- **Meshes and lights are pooled** on one transient host actor per world; Niagara uses its own pool
  (`ManualRelease`). Effect meshes cast no shadow and stay out of the distance field, so the mist does not
  flow around them.
- **A recipe edited while it plays ends that play.** The layer count no longer matches the running state.

## Checked, and still to check

- Checked: the two automation tests pass (`JRPG.VFX`); a recipe with Niagara, mesh and light layers plays
  through the subsystem in Simulate.
- To check: the Spirit aura in a real fight (after the build that carries the pawn hook).
- To check: a `Battle VFX` notify plays in the animation editor's preview. The subsystem is enabled for
  preview worlds for this; it is the least certain part of the code.
- To check: a pooled Niagara component is returned and reused without keeping the previous play's
  parameters (a white context colour now writes nothing, so a reused component keeps its last `Color`).
- A soft stop on a looping recipe leaves nothing behind (no light, no hidden mesh still attached).
