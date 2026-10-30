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

### Clip morphs (the slimes)

Some monsters are not only rigid pieces. While certain clips play, the game adds **per-vertex
deltas** to the mesh, weighted by a trapezoid (ramp up from frame k1, full from k2 to k3, ramp
down to k4) — the RE calls them *clip morphs* (`monster-animation.md`, `FUN_800495C8`). The
Green / Oak / Acid Slime are one single object whose whole life is morphs: every clip has one or
two (Idle wobbles with two at different rhythms, Move hops, Damaged squashes). Evil Fly has one,
on its 0x10 spell. 313 lanes on the disc in all. **The RE's `.glb` does not carry them**: without
them a slime is a rigid lump sliding around.

Pass the monster's block and the converter brings them:

```bash
asset monster-archive extracted/PROT/0867_battle_data.BIN --id 7 --dump-block 007_green_slime.block
blender -b --factory-startup --python Extras/ps1_glb_to_fbx.py -- \
    007_green_slime.glb Green_Slime.fbx Green_Slime --block 007_green_slime.block
```

Each lane becomes a shape key `Morph_NN` (the deltas taken to the piece's rest space; the
vertex is found by position — the `.glb` positions are the TMD's). Its weight is a `Morph_NN`
property on the armature, keyed per clip from the trapezoid and tied to the shape key by a
driver, so every FBX take carries its own curves. In Unreal: **Morph Targets** on the mesh and a
**`Morph_NN` curve** in each `AS_` clip. A block without morphs changes nothing.

### Semi-transparency

`--block` also brings what the `.glb` flattens into one opaque material: which faces the PS1
draws **semi-transparent**. It is per primitive group — the group's `mode` bit 1 is the GPU's
ABE, and the blend (ABR) is in the texture block's `tsb`, bits 5-6. The disc uses two:

| ABR | Blend | Slot | Master | Who |
|---|---|---|---|---|
| — | opaque | `PS1_Opaque` → `MI_<Name>` | `M_PS1Monster` | everything else |
| 0 | ½ background + ½ monster | `PS1_Translucent` → `MI_<Name>_Translucent` | `M_PS1MonsterTranslucent` (Translucent, `Opacity` 0.5) | the slimes' body, Amethyst/Garnet/Topaz, Evil Shadow/Shade/Nightmare, the bees' wings — 2313 prims |
| 1 | background + monster | `PS1_Additive` → `MI_<Name>_Additive` | `M_PS1MonsterAdditive` (Additive, Unlit, `AdditiveStrength` 1) | Orb, Vera, Rogue, Barra, the gems' glow — 558 prims |

The converter finds each face's primitive by the TMD indices of its corners and puts it in
that slot; a monster with no semi-transparent face keeps its single slot. `ue_setup_monster.py`
gives each slot its MI by the slot name. The Green Slime: 208 translucent faces (the body),
24 opaque (the core you see through it). Some objects mix both (Vera, Evil Shadow) — hence per
face, not per piece.

The MCP's `import_file` drops morph targets — import these with `ue_setup_monster.py --source`
(below), which goes through Interchange with morph targets on. Import into an **empty** folder:
re-importing over clips that came without curves keeps them without curves.

## 4. Import into Unreal

| | Monsters | Party (battle form) |
|---|---|---|
| Folder | `/Game/Legaia/Battle/Enemies/<Name>/` | `/Game/Legaia/Battle/Party/<Name>/` |
| Mesh | `SK_<Name>` | `SK_<Name>_Battle` |
| Clips | `AS_<Name>_<Clip>` | `AS_<Name>_Battle_<Clip>` |

1. Import the `.fbx` as a **Skeletal Mesh** with animations on, materials and textures **off**.
   The skeleton is a `root` bone plus one `part_NN` bone per rigid piece (11 for Gimard, 17 for
   Vahn). Unreal names the clips `SK_<Name>_Anim_Armature_AS_...` — rename them to `AS_...`.
2. Import `T_<Name>.png` and set **Filter = Nearest**, **Mip Gen = NoMipmaps**,
   **LOD Group = Pixels2D** (the PS1 look; bilinear smears the 4bpp pages).
3. Create `MI_<Name>` from **`/Game/Legaia/Battle/Materials/M_PS1Monster`**, set `BaseTexture`, and
   assign it to the mesh's only material slot.
4. Place it with **scale 1.0**. Nothing else is needed.

Steps 1–3 are one command in the editor's console (**Cmd** mode). With `--source` it also
imports `<Name>.fbx` (mesh, clips and morph targets) and `T_<Name>.png` from that folder;
without it, it works on what is already imported:

```
py "<repo>/Extras/ue_setup_monster.py" --source "<exports/monsters>" Green_Slime
py "<repo>/Extras/ue_setup_monster.py" Gobu_Gobu Evil_Fly
```

Then fill the monster's `Mesh` and `Anims` in `DT_Enemies` (Idle, Walk = `Move`, Flinch1 =
`Damaged`, Knockdown, GetUp, Block, Attacks) — the field enemy shows it from there. For the
whole archive, see the next section.

### All the monsters at once

Steps 2–4 and the sounds for every id, in four commands:

```bash
python Extras/ps1_monsters_batch.py --extracted extracted --asset-exe <tools>/asset.exe     --blender "<Blender>/blender.exe" --out exports/monsters/all        # ~10 min, 8 Blenders
python Extras/ps1_monster_sounds.py --extracted extracted --asset-exe <tools>/asset.exe     --out exports/sounds/Sounds --all
python Extras/generate_enemy_anims_csv.py exports/monsters/all/monsters.json
```

```
py "<repo>/Extras/ue_setup_monster.py" --source "<...>/exports/monsters/all/fbx" --max 60 --all
py "<repo>/Extras/ue_import_monster_sounds.py" "<...>/exports/sounds/Sounds" --all
```

Then reimport `DT_Enemies` from `Docs/Data/DT_Enemies.csv`.

| Step | What it does |
|---|---|
| `ps1_monsters_batch.py` | Per id: the block (`--dump-block`, always — morphs and semi-transparency need it), the `.glb`, the FBX. Writes `monsters.json`: name, clips, which are attacks. Re-running redoes only what is missing (`--force` for all) |
| `ue_setup_monster.py --all` | Every `<Name>.fbx` of the folder with no `SK_<Name>` in the project yet; the ones already there stay. `--max N` caps one call (~4 s per monster) |
| `ue_import_monster_sounds.py --all` | Every monster of `Enemies/` that has a mesh in the project. The common sounds go in once per run |
| `generate_enemy_anims_csv.py` | `Mesh` and `Anims` of every row with that `MonsterArchiveID` — roles in [ENEMIES.md](ENEMIES.md#model-phase-2) |

What the disc throws at a batch:

- **186 monsters in 194 slots.** 187–194 are empty.
- **Names repeat.** Bosses and rematches reuse a lower id's name (Songi 76/136/179, Tetsu
  79/175, Gaza 165/166, Cort 180/181). Those ids get `_<id>`: `Songi_136`, `Cort_181`
  (`REPEATED_NAME_IDS` in `ps1_monster_sounds.py`, which both scripts share).
- **Byte-identical blocks.** Evil Bat 176–178 = 140, Cort 183–186 = 180: no copy is made;
  `monsters.json` gives them `same_as` and the lower id's assets. 179 models in all.
- **A piece missing from most clips.** Koru's `object_15` only has keys in action 0x15; in
  every other clip it stays at its node's pose.
- **Repeated actions.** Two actions with the same id are `Action_0x13_1` / `_2`, like
  `Attack1` / `Attack2`.

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
| Emissive | `Base Color × EmissiveStrength` (scalar, default 0.2) | A light floor: the PS1 lit monsters with a fixed ambient, so they never went black. At 0.2 a monster keeps ~20% of its own colour in the dark and barely changes in light. Raise it per MI for anything that should glow on purpose |

## 5. Sounds

Each monster action on the disc carries a **cue track**: up to 8 "at frame X, play sound Y".
`Extras/ps1_monster_sounds.py` decodes those sounds to WAV, names them by what they do and
writes where each one plays, so they become AnimNotifies on the clips from step 4.

```bash
python Extras/ps1_monster_sounds.py --extracted extracted --asset-exe <tools>/asset.exe \
    --out exports/sounds/Sounds --ids 10,16      # or --all
```

| Output | What |
|---|---|
| `Enemies/<Name>/SW_<Name>_<Use>.wav` | The monster's **own** sounds (its bank in `monster.snd`), named by the clip that uses them most: `SW_Gimard_Move`, `SW_Gimard_Damaged`, `SW_Gimard_Attack`. Two for the same clip get `_F<frame>`; never-played ones `_Unused<n>` |
| `Enemies/<Name>/sounds.json` | The monster's sounds and every notify of its clips — clip `AS_<Name>_<Clip>`, frame, time, sound, folder |
| `Battle/Common/SW_Battle_Cue<NN>.wav` | **Common** battle sounds that many monsters play (hits, swishes), one file each, named by number until someone names them by ear |
| `Battle/Common/common.json` | Where each common sound comes from and who uses it |

Then, in the editor's console (**Cmd** mode), once per batch of monsters:

```
py "<repo>/Extras/ue_import_monster_sounds.py" "<...>/exports/sounds/Sounds" Gimard Drake_Ghost
```

| In Unreal | |
|---|---|
| `/Game/Legaia/Battle/Enemies/<Name>/Sounds/` | The monster's SoundWaves |
| `/Game/Legaia/Battle/Sounds/Common/` | The common SoundWaves — only the ones its clips use |
| `AS_<Name>_<Clip>` | A notify track **Sounds** with one `AnimNotify_PlaySound` (Follow on) per cue; a second sound on the same frame goes on **Sounds 2** |

Running it again replaces the SoundWaves and rebuilds the sound tracks — it never duplicates.
Tracks holding any other kind of notify are left alone.

The SoundWaves are the **battle** ones: 2D, heard everywhere, volume 1, no attenuation. On the
field, `AJRPGFieldEnemy` plays the same notifies in 3D with a short radius instead — see
[FIELD_ENEMIES.md](FIELD_ENEMIES.md#3-behaviour).

How the game resolves a cue, and what the script reproduces:

- Cue ≥ 0x64 or 0x1B..0x47 → a row of `bse.dat` (PROT 0888), played from the **monster's**
  bank. Cue < 0x1B or 0x48..0x63 → a row of the static table in `SCUS_942.54`, played from the
  bank its category picks (battle = PROT 0869) — these are the common ones.
- A row names a program, a tone, a note and a voice count: the tones are layered and played at
  that note. Each tone has its own centre note, so the rate is per tone:
  `44100 · 2^((note − centre − fine/128) / 12)`.
- Each tone is mixed at the volume the game's key-on gives it: bank master × program volume ×
  tone volume, each /127 (`FUN_80067550` in the RE's `audio.md`) — 0.2 to 0.9. The raw samples
  are near full scale, about twice as loud as the game. The cue's key-on velocity is left out
  (full). Output is 44.1 kHz through the SPU's **4-point Gaussian interpolation** (the 512-entry
  coefficient table the psx-spx hardware docs publish — the RE has it too, `engine-audio/spu/gauss.rs`).
  Linear interpolation sounds muffled next to the game; repeating samples (nearest) hisses on
  sounds played far below their pitch (the party's hit `0x4C` plays at 0.14×).
- The sample bodies start **4 bytes after** where the RE's `vab extract` places them (a chunk
  header sits between the VAB header and the bodies); at its offset ~95% of the ADPCM blocks
  are invalid. The script has its own decoder.
- PS1 frame N plays at **N / 15 s** in the Unreal clip (each key is 2 frames at 30 fps, see
  below).

Checked by ear against the game for Gimard (Tail Fire start/continuation, walk, hit/get-up) and
Drake Ghost (hit, strong attack).

### The party's clips and sounds

The party's battle form comes from the RE site's Arts page `.glb` (step 2): every clip of the
character, one per record, named by the record's internal string ("Beatdunk" is really Fire Blow)
or its id in hex. Convert it whole with `ps1_glb_to_fbx.py` (no `--clips`), import the FBX on the
existing `SK_<Name>_Battle_Skeleton` (animations on) and move the clips next to the mesh as
`AS_<Name>_Battle_<Clip>`. Then:

```bash
xa demux-disc-all "<disc>.bin" -o exports/xa          # the CD-XA voices, one WAV per channel
python Extras/ps1_party_sounds.py --extracted extracted --tools <tools> --xa exports/xa     --out exports/sounds/Sounds Vahn Noa Gala
```

```
py "<repo>/Extras/ue_import_monster_sounds.py" "<...>/exports/sounds/Sounds" Vahn Noa Gala
```

`Party/<Name>/sounds.json` carries, besides the notifies, a `clips` list the editor script
applies first: each clip gets **the name of what it is** and the **game's speed**.

| Record | Name | RateScale |
|---|---|---|
| actions 0..0xB | `Idle`, `Walk`, `Flinch1/2`, `Knockdown`, `GetUp`, `Dying` (7: falls and kneels dead), `Down` (8: the clip a dead hero holds), `UseItem` (9: items and Seru magic), `Block` | 2 |
| swings 0xC..0xF | `SwingL/R/D/U` (the default section's — the equipped weapon's are not imported yet) | 2 |
| 0x10 | `Spirit` | 2 |
| 0x11..0x18 | win poses, by the per-character table at SCUS `0x800788A0`: `Victory1/2` (healthy), `VictoryAlt1/2`, `VictoryWeak1/2` (low HP); the two no one uses are `VictoryExtra1/2` | 2 |
| 0x19 / 0x1A | `ArtStarter` / `SpecialStarter` (Super and Miracle) | 2 / 0.5 |
| 0x1B.. | the Art: the id is its Action Constant (`VahnsCraze`, `BurningFlare`, `FireBlow`, `TornadoFlameHyper`…; RE `art/tables.rs`) | 1 |

Speed: the anim tick advances `(frame_dt · scale · rate) >> 1` per frame in 12.4, scale 8 =
**15 × rate frames/s** — the monsters' pace. An Art runs at scale 4 (half; the whole fight slows
with it), the SpecialStarter at 2; the `.glb` bakes everything at 7.5 × rate, hence the scales.
`VahnsCraze` / `NoasArk` / `BironRage` are only the Miracle's opening: in the game the Miracle is
a string of strikes and Arts.

Sounds (`FUN_8004FE5C` on a party slot):

- `0xC8..0xFF` (not `0xFA`) is the **voice**, CD-XA: Vahn `0xC8..0xD7` (XA1, XA27), Noa
  `0xD8..0xE7` (XA3, XA28), Gala `0xE8..0xF7` (XA5, XA29). The last id of each block is the shout,
  which the game flips between channels 7 and 6: `SW_<Name>_Voice<ID>_A` is on the notify, `_B`
  is imported for a random SoundCue. In `/Game/Legaia/Battle/Party/<Name>/Sounds/`.
- `< 0x48` → static row id − 1, `0x48..0x63` → static row id, `≥ 0xA7` → `bse.dat` in the battle
  bank (PROT 0869): `SW_Battle_Cue<ID>` in `Battle/Sounds/Common`, the same files the monsters use.
- `0x64..0xA6` play from VAB slot 7. In a fight that is the **monster's** bank (left out). In a
  win pose it is the **victory voice**: the results sequencer loads the `monster.snd` tail clip
  the table at SCUS `0x80078867 + action + character·8` gives (clip = byte − 1), and the pose's
  `0xA1` plays it — `SW_<Name>_VictoryVoice<clip>`, at that frame.
- Not on the clips: the voice and fanfare an Art's execution picks (`FUN_8004C140`), and the
  Spirit flourish (`0x10` → `0x11`) playing `0xA1` from the monster's bank.

---

## 6. Equipment pieces (the party)

The original draws no separate item mesh. Each player battle file has five **sections** (body,
head, weapon, Ra-Seru, feet), and equipping an item swaps whole **objects of the skeleton**: the
Survival Knife replaces Vahn's right hand with another hand holding a blade, an armour re-sculpts
the torso. Each section always swaps the same objects (measured on the RE site, every item of
every section):

| Section | Vahn | Noa | Gala |
|---|---|---|---|
| Body | 0, 1 | 0, 1 | 0, 1 |
| Head | 2 | 2, 3 | 2 |
| Weapon | 3, 4, 5 | 7, 8, 9 | 3, 4, 5 |
| Ra-Seru | 6, 7, 8 | 4, 5, 6 | 6, 7, 8 |
| Feet | 9 – 14 | 10 – 15 | 9 – 14 |

Noa is left-handed: in her file sections 2 and 3 are swapped (2 = Ra-Seru Terra, 3 = weapons),
and her weapon arm is the left one. The weapon ids the three share (34–51) are **separate models
per character** — extract each one.

Past the skeleton (15 bones for Vahn and Gala, 16 for Noa) some items add **extra objects**: Noa's
loose weapon, her Terra's extra, Gala's high Ozma tiers. They have no animation channel of their
own — they ride the bone before them in the section — and their index shifts with what else is
equipped. So the cutter matches each extra to the bone whose rest pose it shares, puts it in that
bone's section and weights it to that bone: no extra bones in the skeleton.

So a hero is cut into **pieces**: per section, the objects above, taken from the character
assembled with that one item. They go on the same skeleton as `SK_<Name>_Battle`, so every clip
animates every piece. In battle the pieces follow the hidden `BattleMesh` (Leader Pose) —
[BATTLE_STAGE.md](BATTLE_STAGE.md) › Equipment on the model.

**1. The assembled characters.** `legaia-engine export-glb --items` misses bones on ~23 records
(Ra-Seru tiers, some weapons: it keeps only what the item's palette touches), so the source is
the RE site's [Characters page](https://andrewaltimit.github.io/legend-of-legaia-re/characters.html),
whose wasm assembles any loadout (`set_equipped_character` + `equipped_character_glb`). With
the disc loaded there, one `.glb` per item (that item equipped, the rest default) plus
`default.glb`, Idle only, into `equip-site/<name>/` with a `manifest.json` (character, section,
id, name, file) one level up.

**2. Cut the pieces** (headless Blender):

```bash
blender -b --factory-startup --python Extras/ps1_equip_pieces.py -- \
    legaia-vahn-battle-arts.glb equip-site/vahn pieces/vahn Vahn
```

The `arts.glb` is the one `SK_Vahn_Battle` came from: the skeleton is rebuilt from it exactly,
and each file's rest pose is checked against it. Out: `<Name>_<Piece>_<Item>.fbx` +
`T_<…>.png` per piece (`Vahn_Weapon_SurvivalKnife`, `Vahn_RaSeru_Lv3`, `Vahn_Body_Default`…)
and `pieces.json`. The section table lives in the script (`SECTIONS`), with the three heroes;
`ITEM_KEYS` maps the few items whose disc name is not their `DT_Items` key (Noa's Feral is
`noa_feral`, Gala's Mace `gala_mace`).

**3. Import** each FBX into `/Game/Legaia/Battle/Party/<Name>/Pieces/` as a Skeletal Mesh on
`SK_<Name>_Battle_Skeleton` (no materials, textures or animations), its texture with
**Filter = Nearest**, and an `MI_` child of `M_PS1Monster` with `BaseTexture` on the mesh slot.
The five `Default` pieces share one texture.

**4. The DataTable:**

```bash
python Extras/generate_equip_visuals_csv.py pieces/vahn
```

writes `Docs/Data/DT_EquipVisuals.csv` (rows of other characters are kept); import it into
`/Game/Data/` as `FJRPGEquipVisualRow`.

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
- **The party's Arts banks per equipped weapon** are not in yet (the swings of the default
  section only — see "The party's clips and sounds").
