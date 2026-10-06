# Mist (JRPGMist)

A living ground mist: broad soft fog with thin strands stretched along the wind, slow swirls, and a
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

## How it works

**Shape.** The shader samples a tiling noise texture in world XY, because the camera looks down.
1. A slow, rotating **warp field** offsets every other sample. That is what turns noise into swirls.
2. A **base layer** gives broad soft mist.
3. A **strand layer** is stretched along the wind and sharpened with a ridge, `pow(1 - |2n - 1|, k)`. It
   only shows over part of the area (`RibbonCoverage`). Strands everywhere look like marble.
4. Density falls off **exponentially with height** from the box floor, and **fades at the box's side
   walls**.

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
- The player (`bLeavesTrail = true`) also leaves a trail that closes over 2 seconds.

**Fixed cost.** The shader always has 8 slots. The player takes 5: the current position plus 4 trail
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

Editing a Material recompiles it on **every** change, and the Custom node has 27 inputs that the editor
only accepts one at a time. Adding them straight into the Material fired 27 back-to-back recompiles, and
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
return JRPGMistFromPacked(WorldPos, Time, NoiseTex, NoiseTexSampler,
    ShapeA, ShapeB, Ribbons, Scene, Interaction, BoxXY, SurfaceDist, SurfaceGrad, S, D);
```

**The 27 inputs**, named exactly:

| Input | Connected to |
|---|---|
| `WorldPos` | Absolute World Position (`XYZ`) |
| `Time` | Time |
| `NoiseTex` | Texture Object `T_JRPGMistNoise`, sampler type **Masks** (a Masks texture in a Color sampler fails to compile) |
| `ShapeA`, `ShapeB`, `Ribbons`, `Scene`, `Interaction`, `BoxXY` | Vector Parameters with the same names (`RGBA` output). `AJRPGMistVolume` overwrites them |
| `SurfaceDist` | Quality Switch: **Default** = `DistanceToNearestSurface`, **Low** = constant `100000` |
| `SurfaceGrad` | Quality Switch: **Default** = `DistanceFieldGradient`, raw, **Low** = constant `(0,0,0)`. Don't add a `Normalize` node: the shader normalizes safely, and `Normalize` returns NaN wherever the gradient is zero |
| `Slot0`…`Slot7`, `Dir0`…`Dir7` | Collection Parameter nodes from `MPC_JRPGMist` |

**The 3 Function Outputs:**

| Output | Value |
|---|---|
| `Albedo` | Vector Parameter `Albedo` (`RGB`) |
| `Extinction` | Custom `.r` (Component Mask R) |
| `Emissive` | Vector Parameter `RibbonGlow` (`RGB`) × Custom `.g` |

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
2. **Drop an `AJRPGMistVolume`.**
   - Set `BoxExtent` to cover the walkable area. The box floor is the mist floor, and ~200 uu of
     half-height is plenty.
   - Keep the box unrotated.
3. **Add `UJRPGMistDisturberComponent` to the player pawn's Blueprint** with `bLeavesTrail = true`.
   `IdleStrength` ~0.3 keeps a small clearing around a standing player.
4. **Add the same component to NPCs, enemies and physics props** with `bLeavesTrail = false`.
5. **Lamps:** give them `Volumetric Scattering Intensity`. Enable **Cast Volumetric Shadow** on 1–2
   lights per area at most, because it is the most expensive part of volumetric fog.

## Parameters (`AJRPGMistVolume`)

| Group | Parameter | Effect |
|---|---|---|
| Look | `Density` | Extinction at the floor. Keep it small, because volumetric fog accumulates along the view ray |
| | `Albedo` | Color the mist scatters from lights |
| | `RibbonGlow` | Self-glow on the strands. Black = off. A faint blue reads as "magical" without any light |
| | `HeightFalloff` | Height where density drops to ~37%. Lower = hugs the ground |
| | `NoiseScale` | Size of one noise tile in uu. Bigger = bigger swirls |
| | `Wind` | Drift in uu/s. **Keep it slow**: fast motion smears under temporal reprojection |
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

Changing values at runtime? Call `ApplyMistParameters()`.

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
| Nothing shows | Volumetric Fog off on UDS's height fog, the sky light in real-time capture, or `MI_JRPGMist_Default` missing (check the log for `JRPGMistVolume`) |
| Mist doesn't react to the player | `MPC_JRPGMist` missing or misnamed parameters (log: `JRPGMistSubsystem`), or no `UJRPGMistDisturberComponent` on the pawn |
| Ghosting / smearing | Wind or WarpSpin too fast for temporal reprojection. Slow them down |
| A hard wall of mist | Box rotated, or `EdgeFade` too small |
| Mist inside a house | That mesh has no distance field (check *Generate Mesh Distance Fields* and the mesh's DF resolution), or material quality is Low |
| Material fails to compile with "file not found" | The plugin's `Shaders/` folder is missing next to the `.uplugin`. `build_plugin.py` copies it |
