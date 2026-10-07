# Mist (JRPGMist)

A living white ground mist: broad soft fog with thin strands stretched along the wind, slow swirls, and a
mist that parts around the player and closes behind them. It flows around the scenery on its own and is
stirred by anything that moves and carries a component.

It is not a new fog. It adds density to the **Volumetric Fog** the map already has. In this project that
is the height fog owned by Ultra Dynamic Sky. That is why the mist is lit by the same sun, moon and lamps,
and changes color with the time of day.

| Piece | Where | What it does |
|---|---|---|
| `JRPGMist.ush` | `Shaders/` | The HLSL: domain warp, strands, height, distance field, parting and swirls |
| `AJRPGMistVolume` | `Public/World/JRPGMistVolume.h` | The box you drop in a level. Holds every look parameter |
| `UJRPGMistDisturberComponent` | `Public/World/JRPGMistDisturberComponent.h` | Goes on whatever moves and should stir the mist |
| `UJRPGMistSubsystem` | `Public/World/JRPGMistSubsystem.h` | Picks who fills the shader's 8 slots each frame and writes them to the MPC |
| `M_JRPGMist`, `MF_JRPGMist`, `MI_JRPGMist_Default`, `MPC_JRPGMist`, `T_JRPGMistNoise` | `Content/World/Mist/` | The material assets, shipped with the plugin |
| `generate_mist_noise.py` | `Extras/` | Generates `Extras/Mist/T_JRPGMistNoise.png` |

---

## Modes

Pick one in the volume's **Mode** dropdown. Changing it loads that mode's preset (density, height and
its own parameters) and hides the other modes' options in Details. At runtime, call
`ApplyModePreset(NewMode)` from Blueprint.

| Mode | What it draws | Direction / center |
|---|---|---|
| **Ground Mist** | Broad soft mist with strands and slow swirls (the reference look) | Drifts with `Wind` |
| **Flow Lines** | A few lines curving in S shapes with trails running along them, like smoke in a wind tunnel. They bend around objects | The actor's arrow (rotate it in yaw), or toward `FlowTarget` if set |
| **Pulse Rings** | Concentric rings sent out in pulses from the center, plus optional radial lines | The actor's pivot is the center |
| **Ground + Flow** | Ground Mist and Flow Lines together, carried by **one** wind: point the arrow right and both go right. The mist drifts at `GroundDriftRatio` (30%) of the lines' speed, because fast broad mist smears under the fog's temporal reprojection | Same as Flow Lines |

Thin features blur in volumetric fog: one grid cell covers ~16 screen pixels. Keep `LineWidth` above
~30 uu and `RingWidth` above ~40 uu, or lines read as rain streaks.

**Flow Lines are a fake fluid, not a simulation.** The *paths* stay in place and only sway slowly;
the streaks are what run along them, like smoke following a current. (Moving the curves themselves made
each whole line slide sideways in X, which doesn't read as flow.) A layer of large eddies, wobbling in
place, bends the paths, stretches the streaks and makes the line width breathe. It is all math in the
shader, with one extra texture read.

## How it works

**Shape.** The shader samples a tiling noise texture in world XY, because the camera looks down. The main
layers are *pseudo-3D*: two reads offset by height slices and blended. Pure 2D noise is identical from top
to bottom, so a thick mist turned into vertical streaks.
1. A slow, rotating **warp field** offsets every other sample. That is what turns noise into swirls.
2. A **base layer** gives broad soft mist.
3. A **strand layer** is stretched along the wind and sharpened with a ridge, `pow(1 - |2n - 1|, k)`. It
   only shows over part of the area (`RibbonCoverage`). Strands everywhere look like marble.
4. Density is full up to near `Thickness` and fades at the top (`TopSoftness`), measured from the
   terrain or the pivot. It also **fades at the box's side walls**.

**Scenery, with no component.** The shader reads the **Global Distance Field**. The project generates
mesh distance fields (`r.GenerateMeshDistanceFields=True`).
- Mist never enters walls.
- Mist pools at the base of houses, rocks and fences.
- The flow bends along surfaces, so it goes around a house instead of through it.
- Movable static meshes, such as a cart or a door, are part of the Global DF too.

**Things that move, with a component.** Skeletal meshes have no distance field, and physics does not
reach the fog on its own, so anything animated or simulated needs `UJRPGMistDisturberComponent`.
- The mist parts around its owner and swirls to either side of its path, like a wake.
- Strength scales with the owner's speed. A barrel at rest does nothing, and a rolling one cuts through.
- The player-controlled pawn also leaves a trail that closes over `TrailLifetime` seconds (4 by default).

**Fixed cost.** The shader always has 8 slots. The player takes 6: the current position plus 5 trail
points. The 3 left go to the other disturbers closest to the camera. Adding NPCs never makes the shader
more expensive.

---

## The assets

They ship with the plugin, in `Content/World/Mist/`, and the C++ looks for them at those exact paths:

| Asset | What it is |
|---|---|
| `T_JRPGMistNoise` | Tiling noise. sRGB off, Masks, Wrap/Wrap, NoMipmaps |
| `MPC_JRPGMist` | 16 vector parameters, `Slot0`…`Slot7` and `Dir0`…`Dir7`, written by `UJRPGMistSubsystem` |
| `MF_JRPGMist` | The whole graph, including the Custom node that calls `JRPGMist.ush` |
| `M_JRPGMist` | Volume / Additive. Only a call to `MF_JRPGMist` wired to the outputs |
| `MI_JRPGMist_Default` | Instance of `M_JRPGMist`, the default of `AJRPGMistVolume` |

Nothing has to be built by hand. The recipe below is for rebuilding or changing them.

### Why the graph lives in a Material Function

Editing a Material recompiles it on **every** change, and the Custom node has 31 inputs that the editor
only accepts one at a time. Adding them straight into the Material fired one recompile per input, and
the editor crashed inside the shader preprocessor (300+ cancelled shader jobs). A Material Function
doesn't compile on its own, so the graph is built there and the Material compiles once.

**If you change the graph, edit `MF_JRPGMist`, not `M_JRPGMist`.**

### Rebuilding `T_JRPGMistNoise`

```bash
python Extras/generate_mist_noise.py
```

Import `Extras/Mist/T_JRPGMistNoise.png` into `Content/World/Mist/` with sRGB **off**, Compression
**Masks (no sRGB)**, X/Y tiling **Wrap**, Mip Gen **NoMipmaps**. The shader always reads mip 0.

### `MF_JRPGMist`, the graph

**Custom node:**
- Output Type: **CMOT Float 2**.
- Include File Paths: `/Plugin/JRPGFramework/JRPGMist.ush`.
- Code:

```hlsl
float4 S[8] = { Slot0, Slot1, Slot2, Slot3, Slot4, Slot5, Slot6, Slot7 };
float4 D[8] = { Dir0, Dir1, Dir2, Dir3, Dir4, Dir5, Dir6, Dir7 };
return JRPGMistFromPacked3(WorldPos, Time, NoiseTex, NoiseTexSampler,
    ShapeA, ShapeB, Ribbons, Scene, Interaction, BoxXY, Flow, FlowLines, FlowDash, FlowEddy,
    SurfaceDist, SurfaceGrad, S, D);
```

**The 31 inputs**, named exactly:

| Input | Connected to |
|---|---|
| `WorldPos` | Absolute World Position (`XYZ`) |
| `Time` | Time |
| `NoiseTex` | Texture Object `T_JRPGMistNoise`, sampler type **Masks** (a Masks texture in a Color sampler fails to compile) |
| `ShapeA`, `ShapeB`, `Ribbons`, `Scene`, `Interaction`, `BoxXY`, `Flow`, `FlowLines`, `FlowDash`, `FlowEddy` | Vector Parameters with the same names (`RGBA` output). `AJRPGMistVolume` overwrites them; `Flow`/`FlowLines`/`FlowDash` change meaning with the mode (see `JRPGMistFromPacked3` in the shader) |
| `SurfaceDist` | Quality Switch: **Default** = `DistanceToNearestSurface`, **Low** = constant `100000` |
| `SurfaceGrad` | Quality Switch: **Default** = `DistanceFieldGradient`, raw, **Low** = constant `(0,0,0)`. Don't add a `Normalize` node: the shader normalizes safely, and `Normalize` returns NaN wherever the gradient is zero |
| `Slot0`…`Slot7`, `Dir0`…`Dir7` | Collection Parameter nodes from `MPC_JRPGMist` |

**The 3 Function Outputs:**

| Output | Value |
|---|---|
| `Albedo` | Vector Parameter `Albedo` (`RGB`) |
| `Extinction` | Custom `.r` (Component Mask R) |
| `Emissive` | `RibbonGlow` (`RGB`) × Custom `.g` + `SelfLight` (`RGB`) × Custom `.r` |

### `M_JRPGMist`

| Setting | Value |
|---|---|
| Material Domain | **Volume** |
| Blend Mode | **Additive** (required by Volume) |

The graph is one Material Function Call node with `MF_JRPGMist`:

| Function output | Material pin |
|---|---|
| `Albedo` | Base Color (Albedo) |
| `Extinction` | Extinction (internally `MP_SubsurfaceColor`) |
| `Emissive` | Emissive Color |

---

## Using it in a map

1. **Ultra Dynamic Sky:**
   - turn on **Volumetric Fog** on its height fog;
   - make sure **Sky Light Mode is not *Capture Based* with real-time capture**. UDS's own README says
     real-time capture is incompatible with volumetric fog. *Cubemap with Dynamic Color Tinting* works
     on every platform.
2. **Drop an `AJRPGMistVolume` and rest it on the ground.**
   - The actor's pivot is the **center of the mist floor**. Put it on the terrain, not floating and not
     buried: density falls off with height from the pivot, so a pivot 200 uu under the ground leaves
     almost nothing above it.
   - **Size comes from the actor's scale.** Use the scale gizmo or *Scale* in Details; 1 = 100 uu.
     The default is 40 × 40 × 4 = 4000 × 4000 × 400 uu. A blue outline shows the box in the editor
     (hidden in game), because a volume material is invisible in the viewport on its own.
   - Keep the box unrotated, or at most yawed. The side fade follows the axis-aligned bounds.
3. **Add `UJRPGMistDisturberComponent` to the player pawn's Blueprint.** It detects that it is on the
   player and leaves the trail on its own. `IdleStrength` ~0.3 keeps a small clearing around a
   standing player.
4. **Add the same component to NPCs, enemies and physics props.** They part the mist but never leave a
   trail; only the player does.

### Holes, pits and slopes

With `bFollowGround` on, the mist's height is measured from the nearest surface, not from the pivot. It
hugs the terrain: it runs down slopes, settles into pits and stays thin over high ground.

- **The mist only exists inside the box.** To fill a hole, put the pivot at the **bottom of the hole**
  and make the box tall enough to reach above the surrounding ground plus `Thickness`.
- Near walls the distance to the nearest surface is small, so the mist gets denser along them. Inside a
  narrow pit it fills the pit.
- With material quality Low there is no distance field, and height falls back to the pivot. That is a
  flat layer that won't enter holes.
5. **Lamps:** give them `Volumetric Scattering Intensity`. Enable **Cast Volumetric Shadow** on 1–2
   lights per area at most, because it is the most expensive part of volumetric fog.

## Dark places and night

Volumetric fog only **scatters** light. With nothing lighting it, at night or in a dark corridor, the
mist disappears. Two settings fix that, in the `Light` category:

| Setting | What it does | When to use |
|---|---|---|
| `SelfLight` + `SelfLightColor` | The mist emits a little light, proportional to its own density, so dense parts glow more and the shape (strands, swirls, the player's trail) stays readable | Interiors, caves, dungeons. Start at **0.1**; 0.4 already looks spot-lit |
| `bAutoNightBoost` (on) + `NightSelfLight` | Reads the strongest directional light (UDS's sun or moon) **once**, right after the level starts, and adds up to `NightSelfLight` (0.1) the darker it is. Above `DaylightLevel` (8) it adds nothing. Nothing runs during play | Outdoor levels with a fixed time of day |
| `bTrackTimeOfDay` (off) | Keeps reading every 0.5 s and fades smoothly as the light changes. Microseconds per volume | A dynamic day/night cycle |
| `SetSelfLight(Value)` (Blueprint) | Sets the constant self light immediately | Event-driven: your time-of-day system calls it when the hour changes |

The automatic boost measures the **sky** light. Inside a castle the sun can be up while walls block it,
and the volume can't know that, so set `SelfLight` on volumes placed indoors. `GetSceneLightLevel()` (Blueprint)
shows the value it reads.

Real lights still look best. Give lamps and the UDS moon some `Volumetric Scattering Intensity` and the
mist picks up their color and shadows; self light is the floor that keeps it from vanishing. It is one
material parameter (emissive = `RibbonGlow` × strands + `SelfLight` × density), so it costs nothing.

## Parameters (`AJRPGMistVolume`)

| Group | Parameter | Effect |
|---|---|---|
| Look | `Density` | Extinction at the floor. Keep it small, because volumetric fog accumulates along the view ray |
| | `Albedo` | How much light the mist scatters, per channel. ~0.95 = white (the default): the final color is the light hitting it. Lower values darken and tint it |
| | `RibbonGlow` | Self-glow on the strands. Black = off. A faint blue reads as "magical" without any light |
| | `Thickness` | Thickness of the mist in uu: dense up to near it, fading at the top. Raise this (not the density) for a deeper mist; keep the box taller than it |
| | `TopSoftness` | How much of the thickness is a soft fade at the top (0.05..1) |
| | `bFollowGround` | Measures height from the terrain (distance field) instead of the pivot, so the mist follows slopes and flows down into holes. On by default |
| | `NoiseScale` | Size of one noise tile in uu. Bigger = bigger swirls |
| | `Wind` | Drift in uu/s; the mist moves *with* it. **Keep it slow**: fast motion smears under temporal reprojection |
| | `WarpStrength`, `WarpSpin` | How much and how fast the swirls twist |
| | `RibbonSharpness` | Higher = thinner strands |
| | `RibbonAmount` | Weight of the strands over the base mist |
| | `RibbonStretch` | How long the strands get along the wind |
| | `RibbonCoverage` | Fraction of the area that has strands |
| | `EdgeFade` | Fade width at the box's side walls |
| Scene | `PoolDistance` | Distance from a surface where pooling and flowing-around start |
| | `PoolAmount` | How much thicker the mist gets next to objects |
| | `FlowAround` | How strongly the flow follows surfaces |
| Interaction | `ClearStrength` | How much the mist parts around a disturber (0..1) |
| | `SwirlStrength` | Maximum swirl angle (radians) |

**Flow Lines** (`Flow` category, also used by Ground + Flow): `FlowTarget`, `FlowSpeed`,
`LineSpacing`, `LineWidth` (uu), `LineCoverage` (fewer lines when lower), `LineCurve` and `CurveLength`
(the S curves), `CurveDrift` (how fast the curves change), `EddyStrength` and `EddySize` (the fake-fluid
eddies), `LineDensity` (lines vs. mist), `TrailLength` and `TrailFill` (the streaks), `AvoidDistance` and
`AvoidStrength` (how the lines bend around objects). Ground + Flow adds `GroundDriftRatio`.

**Pulse Rings** (`Pulse` category): `PulseInterval` (seconds between rings), `PulseSpeed`,
`RingWidth`, `PulseMaxRadius` (where the wave fades out), `RingWiggle`, `RadialLines` (0 = rings only),
`RadialAmount`, `RadialSharpness`, `RadialSpin`.

Every parameter is `BlueprintReadWrite`. After changing values at runtime, call `ApplyMistParameters()`.

### Trail (`UJRPGMistDisturberComponent`)

| Parameter | Effect |
|---|---|
| `Radius` | Size of the clearing around the owner (220 uu) |
| `Strength`, `MinSpeed`, `FullSpeed`, `IdleStrength` | How strongly it parts the mist, scaled by speed |
| `SwirlScale` | 0 = only parts the mist, 1 = full swirl |
| `bLeavesTrail` | On by default. Only the **player-controlled pawn** leaves a trail; it is picked automatically |
| `TrailLifetime` | Seconds until the trail closes (4) |
| `TrailWidening` | How much the trail widens as it closes (0.8 = almost doubles) |

## Performance

Target: **≤ ~1 ms** for the whole VolumetricFog pass at 1080p on a **GTX 1060 / RX 580**. Measure with
`ProfileGPU` (the *VolumetricFog* group) or `stat gpu`.

- **The shader itself is cheap.** It compiles to ~300 instructions with 4 texture reads and runs only
  inside the box. Most of the cost is the volumetric fog grid and the lights that cast volumetric shadows.
- **Grid resolution is the main knob.**
  - `r.VolumetricFog.GridPixelSize`: 16 by default. 8–12 looks sharper and costs a lot more.
  - `r.VolumetricFog.GridSizeZ`: 64–128.
  - Set them per scalability level in the game's `Config/DefaultScalability.ini`, in the group where the
    engine's `BaseScalability.ini` already defines them (check before editing).
- **A short volumetric fog View Distance** on the height fog, ~4000–6000 uu, puts the Z slices where a
  top-down camera actually looks.
- **The Low material quality** drops the distance field via the Quality Switch.

| Hardware | Measured | Settings |
|---|---|---|
| _to be filled_ | | |

## Troubleshooting

| Symptom | Cause |
|---|---|
| Nothing shows | In order of likelihood: the pivot is under the terrain (the floor is the pivot), `Density` too low (it is per meter, start at 1), the volume beyond the height fog's Volumetric Fog *View Distance*, Volumetric Fog off on UDS's height fog, or `MI_JRPGMist_Default` missing (log: `JRPGMistVolume`) |
| The box snaps back to its size | Fixed: size is the actor scale now. Older placed volumes saved with `BoxExtent` should be deleted and placed again |
| Mist doesn't react to the player | `MPC_JRPGMist` missing or misnamed parameters (log: `JRPGMistSubsystem`), or no `UJRPGMistDisturberComponent` on the pawn |
| Ghosting / smearing | Wind or WarpSpin too fast for temporal reprojection. Slow them down |
| A hard wall of mist | Box pitched/rolled, or `EdgeFade` too small |
| Mist inside a house | That mesh has no distance field (check *Generate Mesh Distance Fields* and the mesh's DF resolution), or material quality is Low |
| Material fails to compile with "file not found" | The plugin's `Shaders/` folder is missing next to the `.uplugin`. `build_plugin.py` copies it |
