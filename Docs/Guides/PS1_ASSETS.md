# PS1 battle models into Unreal

How the original game's **battle** models — the 186 monsters and the party's battle form —
get from the disc into the game project as `USkeletalMesh` + one `UAnimSequence` per clip.
This is Phase 2 of [BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md).

> **Everything extracted from the disc is Sony-derived.** The disc image, `extracted/`, the
> `.glb`, `.fbx` and `.png` files and the imported `.uasset`s stay local and **never enter this
> repository**. Only the scripts and the docs do.

The field models in the game project (Vahn, Noa, Gala, NPCs) are a different thing: DuckStation
3D dumps, re-rigged by hand. Field form and battle form are different meshes on the disc — see
[BATTLE_ROADMAP.md › Phase 2](../BATTLE_ROADMAP.md#assets-already-in-the-host-project-are-not-for-battle).

---

## What you need

| Tool | Where |
|---|---|
| Your own disc image | `Legend of Legaia (USA).bin` (raw Mode2/2352) |
| legend-of-legaia-re tools | The Windows zip from the project's [Releases page][rel] (`legaia-tools-<ver>-x86_64-pc-windows-gnu.zip`). Check it against `SHA256SUMS`. **No Rust toolchain needed.** Validated with v1.10.0 |
| Blender 5.2+ | Runs headless; the converter uses its glTF importer and FBX exporter |
| The game project open in the editor | For the import |

Keep the RE tools, `extracted/` and the exports in a folder of their own, next to the
repository — not inside it.

[rel]: https://github.com/andrewaltimit/legend-of-legaia-re/releases

---

## 1. Extract the disc

```bash
legaia-extract "<path>/Legend of Legaia (USA).bin" --out extracted
```

Takes a few minutes and writes ~900 MB. It verifies the disc (SHA-256 and region), splits
`PROT.DAT` into 1233 entries and more. The monster archive is
`extracted/PROT/0867_battle_data.BIN`.

## 2. Export the models as `.glb`

**Monsters** — one id per call:

```bash
asset monster-archive extracted/PROT/0867_battle_data.BIN                 # list every id, name and stat
asset monster-archive extracted/PROT/0867_battle_data.BIN --id 10 --anim  # one monster's clips
asset monster-archive extracted/PROT/0867_battle_data.BIN --id 10 --glb 010_gimard.glb
```

186 of the 194 slots hold a monster. Level variants are separate ids named with a `$`:
`Gimard` (10), `Gimard $2` (11), `Gimard $3` (12) — `enemies.toml` calls them `Gimard Lv2/Lv3`.

**The party in battle form** (Vahn, Noa, Gala) has **no command-line export**. Open the RE
project's [Arts page](https://andrewaltimit.github.io/legend-of-legaia-re/arts.html), load
your disc image (it is read locally in the browser), pick the character and use **Download .glb
with all battle animations**. The file carries Idle, Walk / Approach, the hit reactions, the four
weapon swings and every Art.

## 3. Convert to `.fbx`

```bash
blender -b --factory-startup --python Extras/ps1_glb_to_fbx.py -- \
    <in.glb> <out.fbx> <Name> [--clips Idle,Walk] [--loop-clips Idle,Walk,Move] [--cm-per-unit X]
```

```bash
blender -b --factory-startup --python Extras/ps1_glb_to_fbx.py -- 010_gimard.glb Gimard.fbx Gimard
blender -b --factory-startup --python Extras/ps1_glb_to_fbx.py -- \
    legaia-vahn-battle-arts.glb Vahn_Battle.fbx Vahn_Battle --clips Idle,Walk
```

It writes `<out>.fbx` and `T_<Name>.png` (the texture atlas) side by side, and logs every clip
with its frame count.

## 4. Import into Unreal

| | Monsters | Party (battle form) |
|---|---|---|
| Folder | `/Game/Legaia/Enemies/<Name>/` | `/Game/Legaia/Battle/Party/<Name>/` |
| Mesh | `SK_<Name>` | `SK_<Name>_Battle` |
| Clips | `AS_<Name>_<Clip>` | `AS_<Name>_Battle_<Clip>` |

1. Import the `.fbx` as a **Skeletal Mesh** with animations on, materials and textures **off**.
   The skeleton is a `root` bone plus one `part_NN` bone per rigid piece (11 for Gimard, 17 for
   Vahn). Unreal names the clips `SK_<Name>_Anim_Armature_AS_...` — rename them to `AS_...`.
2. Import `T_<Name>.png` and set **Filter = Nearest**, **Mip Gen = NoMipmaps**,
   **LOD Group = Pixels2D** (the PS1 look; bilinear smears the 4bpp pages).
3. Create `MI_<Name>` from **`/Game/Legaia/Enemies/M_PS1Monster`**, set `BaseTexture`, and
   assign it to the mesh's only material slot.
4. Place it with **scale 1.0**. Nothing else is needed.

### `M_PS1Monster`

One master material for every monster and party member:

| Setting | Value | Why |
|---|---|---|
| Shading Model | Default Lit | Unlit (what the PS1 actually did) glows in the dark in a lit scene |
| Blend Mode | Masked, clip 0.5, Two Sided | The texture alpha cuts the holes; the PS1 drew both faces |
| Used with Skeletal Mesh | on | |
| Base Color | `BaseTexture.RGB × VertexColor ^ 2.2` | The PS1 multiplies texel × packet colour in display space; the vertex colour reaches the material sRGB-encoded, hence the 2.2 |
| Opacity Mask | `BaseTexture.A` | |
| Roughness / Specular | 1 / 0 | No plastic sheen |
| Emissive | `Base Color × EmissiveStrength` (scalar, default 0) | For anything that should glow on purpose |

---

## What the converter does, and why

Each row is a problem that was actually hit with Gimard and the party.

| Problem | Symptom | What the converter does |
|---|---|---|
| The RE exporters write one vertex per triangle **corner** (UV and colour are per corner in the PS1's TMD) | Every triangle is loose: no UV unwrap, post-process effects break on the seams. Gimard: 894 vertices for 200 positions, 356 faces in 269 islands | Welds each piece by position (0.01). Positions are integers on the PS1, so the weld is exact: one closed mesh per piece, UV and colour kept per corner |
| A `.glb` cannot share a vertex across a UV seam, and Unreal's glTF importer creates one vertex per glTF vertex (`GLTFMeshFactory.cpp`) | The weld would be lost on import | Exports **FBX**, which stores UV per corner on shared vertices |
| The models are rigid pieces, no `skin` | Unreal would import a `UStaticMesh` | Builds an armature, one bone per piece, each piece 100% on its bone; the piece animations are baked onto the bones (Copy Location + Copy Rotation, then `nla.bake`) |
| The glTF root node is rotated 180° about X | Clips upside down and at the wrong scale | The armature has an **identity** transform; bones are built in world space |
| Any scale left on the armature node — the FBX `global_scale`, or the ×100 a metre scene gets | Unreal applies it to the mesh and **not** to the clips: clips far bigger than the mesh, or so much smaller the mesh vanishes | The Blender scene is in **centimetres** (`scale_length 0.01`), the scale is baked into the data, the FBX is written with `global_scale 1` |
| Monsters key at 14 fps (nominal — the original advances a key per action step, not on a clock); the party at multiples of 1/30 s | *"Animation length 2.786 is not compatible with import frame-rate 24 fps"* | Monster keys become 2 frames each at 30 fps (15 keys/s); party clips go in at 30 directly; every clip is baked on whole frames at 30 fps |
| The disc's cycles end **one step before** frame 0 — the original wraps by itself | A jump every time Idle/Walk loops | For the loop clips (Idle, Walk, Move) it appends a frame equal to the first, one step after the last. Clips that already close (Gimard's Idle) are left alone. Attacks and reactions are never touched |

## Scale

**0.433005 cm per PS1 unit**, the same for monsters and the party — on the disc they are in
the same unit. It was set in the editor with the party and Gimard side by side and checked by
eye against the game running in DuckStation. It is the converter's default.

| Model | PS1 units | In Unreal |
|---|---|---|
| Vahn (battle) | ~422 | 183 cm |
| Noa (battle) | ~390 | 169 cm |
| Gala (battle) | ~503 | 218 cm |
| Gimard | ~414 | 179 cm |

Gimard and battle Vahn are the same height in the original, and stay so here. The `100/128`
the roadmap assumed made everything 1.8× too big.

## Checking a conversion

- The converter's log: the weld count, and `ciclo fechado: <clip>` for each loop it closed.
- Open a loop clip and watch the wrap — there must be no jump.
- Open an attack clip next to the mesh: same size, upright.
- The Output Log must show no `FBXImport: Error: Animation length ... not compatible`.

## Known gaps

- **Re-importing replaces the assets.** The editor's import refuses to overwrite, so a new
  version means deleting mesh, skeleton and clips and importing again — placed actors lose the
  mesh. Do it before anything references them.
- **Vertex colour above 1.0.** The PS1's over-bright packet colours (flames, glowing blades) go
  up to ~1.99 and the glTF carries them as floats; Blender's importer and Unreal's 8-bit vertex
  colour clamp them at 1.0. Gimard does not use them. A fix is to export the colour halved and
  multiply by 2 in the material.
- **Arts with irregular key spacing** (1/22.5 s, 1/52.5 s) are sampled on whole frames; not yet
  checked in the editor.
- **No batch.** One model at a time for now; the 186 monsters and the party's full Arts banks
  come next.
