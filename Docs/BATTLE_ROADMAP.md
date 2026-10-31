# Battle System Roadmap

**JRPGFramework — Legend of Legaia Remake**
Approved 2026-09-13. Phases 0 to 4 are done (2026-10-30): the battle data (`DT_Enemies`,
`DT_Arts`, `DT_Bosses`), every monster and the party's battle clips in Unreal, enemies roaming the
map and a fight assembled over it. Next is Phase 5: combat itself.

This is the largest unimplemented system left in the plugin, and the only one with stub
files already in place. (Dialogue, Cutscene and Localization are also unwritten - see
[STRUCTURE.md](STRUCTURE.md).) Everything
else — Core, Save/Load, Inventory, World State, Shop, Audio, Camera, Party, Status, WebUI —
is done and will be consumed by the battle system rather than rewritten.

---

## Current state

Battle is **71 lines of stubs**:

| File | Lines | Content |
|---|---|---|
| `Public/Battle/BattleSubsystem.h` | 25 | Declares `ApplyBattleItemEffect` only |
| `Private/Battle/BattleSubsystem.cpp` | 11 | A single `UE_LOG("TODO")` |
| `Public/Arts/ArtsSubsystem.h` | 24 | Declares `TeachArtToCharacter` only |
| `Private/Arts/ArtsSubsystem.cpp` | 11 | A single `UE_LOG("TODO")` |

The inventory already routes item use into `ApplyBattleItemEffect`; the call arrives and
does nothing.

---

## Where the design comes from

There is a separate reverse-engineering project, [`legend-of-legaia-re`][re], written in
Rust: ~815k lines, 98.2% of the original game's functions ported, 10,136 tests, and
~11,400 lines of specification-grade documentation about combat alone. It also extracts
and exports the original meshes, animations and textures from the disc as glTF.

**This is not a port.** The RE project is read as a *specification*: you learn how the
original solves a problem, then write it from scratch in C++/Unreal with the design
changes this remake wants. What is actually taken from it is **data** (tables validated
against the disc) and **assets** (meshes, animations, textures, sounds).

[re]: https://github.com/andrewaltimit/legend-of-legaia-re

### Deliberate departures from the original

| Topic | Original | This remake |
|---|---|---|
| **AP** | Spent on every directional command — a long-standing player complaint | Regular Arts **add** AP; Hyper / Super / Miracle **spend** it (the Legaia 2 model) |
| **Party size** | Hard-coded 3 | Configurable; Serus join as support members |
| **Enemy count** | 4–5 fixed seats | Configurable, scales with party size to stay balanced |
| **Encounters** | Random while walking (mist) | Visible enemies roaming the map, with a chase radius and respawn |
| **Where fights happen** | In the field scene; only the camera changes | Same — battle layers on top of the existing maps |
| **Action state machine** | ~50 states (mixes logic, camera and effect VM) | 8 phases; presentation delegated to AnimMontage / Sequencer |
| **Parry** | None — the defender does nothing while it is hit | **Planned, design open** (2026-10-31): a timed button during the enemy's attack. Why it shapes Phase 5: [State machine](#state-machine); what is still to decide: [Open questions](#open-questions-to-settle-in-practice-not-on-paper) |

---

## Infrastructure already available

None of this needs to be written — only called.

| Needed for | Already in |
|---|---|
| Combatant record (HP/MP/AP, 8 stats, 5 equip slots, conditions, SeruLevel) | `FJRPGPartyMember` (`Public/Party/JRPGPartyTypes.h`) |
| Damage, heal, restore MP, `SetAP`, revive, full restore | `UPartySubsystem` |
| XP split among survivors, level-up with jitter | `UPartySubsystem::AwardBattleXP` / `LevelUpMember` |
| Effective stat (base + equipment + Ra-Seru) | `UStatusSubsystem::GetEffectiveStat` |
| Ra-Seru bonus (1..9) | `UStatusSubsystem::GetSeruBonus` |
| Elemental affinity multiplier | `UStatusSubsystem::GetElementMultiplier` |
| Status conditions | `UStatusSubsystem::ApplyCondition` / `HasCondition` |
| Enemy scaling by difficulty | `UCoreSubsystem::ScaleEnemyHP/ATK/DEF` |
| XP curve and stat growth | `UCoreSubsystem` + `DT_GrowthCurve` / `DT_StatGrowth` |
| Frame a group, orbit, camera shake | `UCameraSubsystem::FrameGroup` / `Orbit` / `PlayCameraShake` (`BattleWide` preset already declared) |
| Resolve a DataTable with fallback | `UCoreSubsystem::ResolveDataTable` (`Private/Core/CoreSubsystem.cpp:33`) |
| Event flags (gate encounters on progress) | `UWorldStateSubsystem` |
| Block the SAVE tab during battle | `UWorldStateSubsystem::SetIsInField(false)` |
| JS↔C++ bridge and the SPA shell | `UWebUIBridge` + `UWebUISubsystem` |
| BGM with crossfade and volume channels | `UAudioSubsystem` |

---

## Phases

Each phase ends in something you can run and watch work.

| Phase | Scope | Status |
|---|---|---|
| **0** | Extract assets from the disc (RE release tools + `legaia-extract`) | **Done** |
| **1** | Battle data (TOML → CSV → DataTable) | **Done** (2026-10-30) — `DT_Enemies` (179 rows), `DT_Arts` (81), `DT_Bosses` (30). Magic moved to Phase 8 |
| **2** | PS1 3D assets (GLB → Blender → FBX → SkeletalMesh) | **Done** (2026-10-30) — all 179 monster models with sounds and notifies, the party's battle clips (real names, game speed, sounds), equipment pieces |
| **3** | Enemies roaming the map (zone, roaming, trigger) | **Done** (2026-10-27) — `AJRPGFieldEnemy`, `AJRPGEncounterZone` (sphere/box/spline), tested in the editor |
| **4** | Battle stage (spawn, formation, camera) | **Done** (2026-10-27) — `AJRPGBattleArena` assembles the fight, `UJRPGBattleSceneSubsystem`, `BeginBattle`/`EndBattle`, `AJRPGBattlePawn`, `AJRPGBattleWaves` + JSON/Studio; tested in the editor and on the Muscle Dome map. Party-size scaling moved to Phase 8 |
| **5** | Basic combat (turns, damage, death, victory) | Not started |
| **6** | Directional Arts + the new AP | Not started |
| **7** | Presentation (HUD, attack camera, sound, items, escape) | Not started |
| **8** | Magic and Seru | Not started |

```
Phase 0 (extraction)
   ├─► Phase 1 (data) ───────────┐
   └─► Phase 2 (3D assets) ──────┤
                                  ▼
                         Phase 3 (roaming enemies)
                                  ▼
                         Phase 4 (battle stage)
                                  ▼
                         Phase 5 (basic combat)
                                  ▼
                         Phase 6 (Arts + new AP)
                                  ▼
                         Phase 7 (presentation)
                                  ▼
                         Phase 8 (magic and Seru)
```

Phases 1 and 2 are independent and can run in parallel. From 3 onward it is sequential.

---

## Phase 0 — Extract assets from the disc

**Goal:** a valid `extracted/` folder. No Unreal work yet.

**Done 2026-10-26**, and simpler than planned: the RE project publishes prebuilt Windows
tools on its Releases page, so **no Rust toolchain is needed**. Download
`legaia-tools-<ver>-x86_64-pc-windows-gnu.zip` (v1.10.0 was used), check `SHA256SUMS`, and:

```bash
legaia-extract "<path to Legend of Legaia (USA).bin>" --out extracted
```

It verifies the disc (SHA-256 + region), writes 1233 PROT entries, the XA audio as WAV and
the dialog font — ~900 MB. Step by step in [Guides/PS1_ASSETS.md](Guides/PS1_ASSETS.md).

**Acceptance (met):** `asset monster-archive extracted/PROT/0867_*.BIN --id 10 --glb gimard.glb`
produces a file that opens in Blender with a textured mesh and a list of animations — 11
pieces, 9 named clips (Idle, Move, Damaged, Knocked Down, Getting Up, Block, Attack #1/#2,
Action 0x23).

> **Licensing:** the RE project's *code* is MIT/Unlicense, but anything extracted from the
> disc is Sony-derived. That project keeps `extracted/` and `glb-export/` gitignored and
> prints a warning on every export. Converted assets stay local and out of any distribution.

---

## Phase 1 — Battle data

**Goal:** the DataTables exist and open in LegaiaStudio. Zero gameplay.

### Source tables

Copy from the RE project's `data/gamedata/` into `Docs/Data/gamedata/`:

| File | Rows |
|---|---|
| `enemies.toml` | 177 enemies (26 flagged `boss = true`) |
| `arts.toml` | 60 Arts |
| `bosses.toml` | 30 boss fights |
| `magic.toml` | 29 spells — table and system both in Phase 8 |

Two things are not what they look like:

- **`bosses.toml` is not the sibling of `enemies.toml`.** Boss stats already live in
  `enemies.toml` (`boss = true`); this file is the *encounter* layer (`meth_id`,
  `attacks`, `exp_reward`, `gold_reward`, `item_reward`, `recommended_level`).
- **`arts.toml` has no `power` field.** Per-hit damage comes from the **Art Record on the
  disc**: up to 4 *power bytes*, each encoding both the multiplier (12/18/20/22/28) **and**
  the defense target (UDF or LDF), plus `dmg_timing` (which animation frame each hit fires
  on) and `anim_index`.

### The disc already links each enemy to its model

Found while doing Phase 0: the monster archive (`PROT 0867`) is one slot per monster id, and
each slot holds the name, the **raw** stats, gold/EXP, drop + chance, the capturable Seru +
catch rate, the size class, the spell list **and** the mesh, texture and clips — all
together. `asset monster-archive <0867>` (no `--id`) lists all 186.

So `MonsterArchiveID` does not have to be typed by hand: join `enemies.toml` to the disc
listing **by name** (normalized; the disc writes level variants as `Gimard $2` where the TOML
says `Gimard Lv2`). Multi-form bosses share a name across several ids (Gaza, Cort, Songi) — the
generator must report them and leave them to a human.

The two sources do not hold the same numbers, and that is expected: `enemies.toml` records the
**in-battle** values (what the player fights), the disc the raw record the battle loader
boosts. The RE project pins the factors (`crates/gamedata/tests/enemy_stats_vs_disc.rs`):
HP, SPD, AGL ×1 · UDF, LDF ×2 · ATK ×5/4 · INT ×9/8 · EXP ×3/4 · Gold ×5/16 (±1 rounding).
Store the in-battle values; an enemy that exists only on the disc gets the raw values times
those factors.

### New structs

Hard rule: `Extras/LegaiaStudio/studio.py` discovers schemas by regex over the headers.
One `UPROPERTY` per line, no `meta=(...)` with nested parentheses, struct closing with
`\n};` and inheriting `: public FTableRowBase`. Use `Public/Shop/ShopData.h` as the model.

**`Public/Battle/EnemyData.h`** — **done** (2026-10-26). Column reference in
[Guides/ENEMIES.md](Guides/ENEMIES.md).

```cpp
USTRUCT(BlueprintType) struct FJRPGEnemyData : public FTableRowBase {
  FName Key; FText DisplayName; FString Location; bool bIsBoss; EJRPGElement Element;
  int32 BaseHP, BaseMP, BaseAGL, BaseATK, BaseUDF, BaseLDF, BaseSPD, BaseINT;  // in-battle
  int32 EXP, Gold;
  FName DropItem;    int32 DropChance;    // DropChance comes from the disc
  FName StealItem;   int32 StealChance;
  FName CaptureSeru; int32 CatchRate;     // from the disc
  int32 MonsterArchiveID = -1;            // PROT 0867 slot, joined automatically
  TSoftObjectPtr<USkeletalMesh> Mesh;     // filled in Phase 2
  FJRPGMonsterAnimSet Anims;
};
```

Differences from the first sketch: the stats take the `Base` prefix of `FCharacterData` (a
member named `INT` collides with the Windows `INT` typedef); drop chance and the capturable
Seru come from the disc; `EJRPGCombatantType` moves to Phase 5's `BattleTypes.h`, where it is
used.

`FJRPGMonsterAnimSet` holds one `TSoftObjectPtr<UAnimSequence>` per semantic tag (`Idle`,
`Walk`, `Flinch1`, `Flinch2`, `Knockdown`, `GetUp`, `Ready`, `Recover`, `Defeat`, `Block`) —
a `TMap` does not import from CSV cleanly — plus `Attacks`, an array of the attack clips.
It is a plain `USTRUCT`, not a row: LegaiaStudio's schema regex skips nested templates such
as `TArray<TSoftObjectPtr<...>>` inside a row struct, and shows the whole set as one field.

**`Public/Arts/ArtsData.h`** — **done** (2026-10-30), 81 rows. Column reference and how to add
Arts in [Guides/ARTS.md](Guides/ARTS.md). What changed from the sketch below: no `Directions`
(only `Command`; Noa's mirroring is resolved at input time), the per-hit `HitPower` /
`HitTarget` read from the disc's Art records, a `Part` kind for the pieces only played inside a
sequence, and a `Sequence` that makes Supers, Miracles and new combined Arts plain data.

```cpp
UENUM(BlueprintType) enum class EJRPGArtKind   : uint8 { Regular, Hyper, Super, Miracle };
UENUM(BlueprintType) enum class EJRPGArtButton : uint8 { Arms, RaSeru, High, Low };

USTRUCT(BlueprintType) struct FJRPGArtData : public FTableRowBase {
  FName Key; FName Character; FText DisplayName; EJRPGArtKind Kind;
  TArray<FString> Command;      // tokens: Arms / Ra-Seru / High / Low
  TArray<int32>   Directions;   // bytes 1..4, already in the owner's scheme
  int32 ActionConstant;         // 0x1B..0x32, stored as decimal
  int32 APGain  = 0;            // Regular: how much AP it ADDS
  int32 APCost  = 0;            // Hyper/Super/Miracle: how much it SPENDS
  int32 CommandCost = 0;        // cost against the turn's command gauge
  TSoftObjectPtr<UAnimSequence> Anim;   // filled in Phase 2
};
```

`APGain` / `APCost` are the design inversion. The converter seeds defaults from the TOML's
`ap` (Regular → `APGain = ap / 2`; the rest → `APCost = ap`); final balance is tuned in
LegaiaStudio, not in code.

Also: **`Public/Battle/BossFightData.h`** (`FJRPGBossData`) — **done** (2026-10-30), 30 rows, the fight
layer: fighters as `DT_Enemies` keys, `SpecialAttacks` + `SpecialAttackMP`, reward, level, Muscle
Dome `Tournament` ([Guides/ENEMIES.md](Guides/ENEMIES.md#4-boss-fights--dt_bosses)). `MagicData.h` (`FJRPGSpellData`)
moved to Phase 8 with the spell system.

### Converters

The scripts in `Extras/`: `generate_enemies_csv.py`, `generate_arts_csv.py` and
`generate_bosses_csv.py` (**done**); `generate_magic_csv.py` goes with Phase 8.

**`Extras/legaia_toml.py` (done) does not parse TOML by hand**: it uses `tomllib` from the
standard library (Python 3.11+), which reads hex, inline tables and multi-line arrays the way
the RE project's own crate does — the three traps below simply do not happen. It also holds
the CSV helpers (CRLF, `---` header, Unreal arrays, backups). The older generators keep their
own parsers for now.

The traps a hand-written parser hits, kept for the record:

- **Hex.** `parse_toml_file` does `int(v)` inside a try/except, and `int("0x29")` raises
  `ValueError` — `action_constant` would silently become the string `"0x29"`. Needs an
  `int(v, 16)` branch, and the CSV stores decimal.
- **Inline tables.** `bosses.toml` uses `attacks = [{ name = "Fire Breath", mp = 70 }]`,
  which breaks comma splitting. Dedicated regex, flattened into `AttackNames` +
  `AttackMPCosts`.
- **Unreal CSV arrays.** `TArray<int32>` → `(1,2,4)`; `TArray<FString>` →
  `("Arms","Ra-Seru","High")`. Precedent: `EquipOthers` in `generate_csv.py`.

Columns a **human** fills in (`MonsterArchiveID`, `Mesh`, `Anims`, `Anim`, `APGain`,
`APCost`) must survive regeneration: the generator reads the existing CSV and re-merges by
row name. Without that, Phase 2's work is lost on every run. `generate_enemies_csv.py` does
this (and keeps rows created in LegaiaStudio, backing the old CSV up first).

> Known debt: the existing `generate_*.py` scripts have a **wrong** hardcoded path
> (`CODE\JRPGFramework\Docs\Data`, missing the `5.8`). Fix that in the same pass.

**Acceptance:** `DT_Enemies`, `DT_Arts`, `DT_Bosses` imported under `/Game/Data/`;
LegaiaStudio lists the new structs; a Blueprint calling `GetRowNames()` returns 179 / 81 / 30
(`DT_Enemies` is 177 TOML rows + Comm and Evil Bat, which exist only on the disc; `DT_Arts` is
the 60 Arts + 21 sequence pieces).

---

## Phase 2 — PS1 3D assets into Unreal

**Goal:** one `USkeletalMesh` + `UAnimSequence`s per enemy, playing in the editor.

### Why it is not just dragging a GLB in

PS1 meshes are **rigid and flat — no hierarchy**. Each object stores vertices around its
own joint origin and the pose is `v_world = R_bone * v_local + T_bone`: an *absolute*
model-space transform, animation channel `i` driving object `i`, one to one. That maps
**exactly** onto a flat Unreal skeleton (root + N direct-child bones, each piece weighted
100% to its own bone). Lossless conversion, not an approximation.

The obstacle is the importer. Confirmed by reading the engine
(`Engine/Plugins/Interchange/Runtime/Source/Import/Private/Gltf/InterchangeGltfTranslator.cpp`,
lines 237-314 and 1400-1474): without a `Skindex`, a node never becomes a `Joint` and you
get a `UStaticMesh` plus a level sequence. And the RE exporters emit no `skin` at all.

### Why the plan changed: Blender and FBX, not a skin injector

The plan was `Extras/glb_inject_skin.py`, a stdlib post-processor adding a `skin` to the
`.glb`. Doing it for real with Gimard showed a second problem the injector cannot solve:

- The RE exporters write **one vertex per triangle corner** — UV and colour are per corner
  in the PS1's TMD. Gimard: 894 vertices for 200 positions, 356 faces in 269 islands. The
  game project hit the same thing with DuckStation dumps (no UV unwrap, post-process effects
  broken on the seams) and fixed it by hand with a merge-by-distance.
- Welding them back is exact (PS1 positions are integers), but **a `.glb` cannot keep a
  shared vertex across a UV seam**, and Unreal's glTF importer creates one vertex per glTF
  vertex (`GLTFMeshFactory.cpp`, `CreateVertex` per position index). Only FBX stores UV per
  corner on shared vertices.

So Phase 2 is **`Extras/ps1_glb_to_fbx.py`**, run inside headless Blender (5.2): import the
`.glb`, weld each piece, build an armature with one bone per piece (the flat skeleton
described above, root + N children, 100% weights), bake the piece animations onto the bones,
export FBX. The math the injector section warned about — pieces pre-transformed by their rest
pose — is what Blender's bake does for free.

Getting it right took five rounds with Gimard; each one is now a rule
([ARCHITECTURE.md](ARCHITECTURE.md) rules 48–52) and a row in
[Guides/PS1_ASSETS.md](Guides/PS1_ASSETS.md#what-the-converter-does-and-why): identity
armature (the glTF root's 180° X turned the clips upside down), no scale on the armature node
(Unreal applies it to the mesh and not the clips — they came out huge, then invisible),
clips on whole frames at 30 fps, and closing the loops.

### Enemies

`asset monster-archive extracted/PROT/0867_*.BIN --id N --glb out.glb` already exports
geometry + texture + **every** animation. 186 of 194 slots have a mesh. Actions carry
**semantic tags**: `0`=idle, `1`=walk, `2`/`3`=flinch, `4`=knockdown, `5`=get-up,
`7`/`8`/`9`=ready/recover/defeat, `0x0B`=block — exactly the shape of
`FJRPGMonsterAnimSet`. The exporter already names the clips (`Idle`, `Move`, `Damaged`,
`Knocked Down`, `Getting Up`, `Block`, `Attack #1`, ...); the converter turns them into
`AS_Gimard_Idle`, `AS_Gimard_KnockedDown`, `AS_Gimard_Attack1`, `AS_Gimard_Action_0x23`.

Still to write: a batch over the 186 ids (export → convert → import), the import step
through `UAssetImportTask` or the editor's MCP rather than by hand.

### Heroes in battle form

The kernels exist (`battle_char_assembly::loadout::build` +
`character_gltf::build_character_glb`) but are only exposed through the RE project's web
viewer, and v1.10.0 still has no CLI for them. **Used: the site's Arts page**, which runs as
WASM against your own disc image and has a *Download .glb with all battle animations* button.
Every battle clip of Vahn, Noa and Gala is in — actions, the default section's swings, Spirit,
win poses, starters and every Art — with its real name, the game's speed and its sounds
([Guides/PS1_ASSETS.md › The party's clips and sounds](Guides/PS1_ASSETS.md#the-partys-clips-and-sounds)).
Still missing: the swings of each equipped weapon.

Measured: Vahn 17 pieces, Noa 18, Gala 17. The battle mesh is **assembled per equipment
loadout**, so export with the intended loadout set.

### Scale

**0.433005 cm per PS1 unit**, the same for monsters and the party (one unit on the disc). The
`100/128 = 0.78125` assumed here was 1.8× too big. The factor was set in the editor with
battle Vahn/Noa/Gala and Gimard side by side, checked by eye against the game in DuckStation:
battle Vahn ~422 units → 183 cm, Gimard ~414 → 179 cm (same height as in the original). It is
the converter's default, and the models are placed at scale 1.0.

### Assets already in the host project are NOT for battle

The host project's character models are PS1 meshes **ripped with DuckStation's 3D dump**,
re-rigged and re-animated by hand. They exist for **field / world** gameplay.

| Asset | Used for |
|---|---|
| `VahnORiginal/` — `AB_Vahn`/`BS_Vahn`, idle, walk, run, plus `AB_VahnFight`, `Vahn_fight_idle`, `vahn_punch/Kick/low_kick/Hit` + Montages | Field. The punch/kick montages are world gameplay, not turn-based combat |
| `NOA/`, `Gala/` (`GALA_RIG` + Skeleton + PhysicsAsset) | Field |
| `ENEMIES/GIMARD/`, `NPC/TETSU/`, `NPC/Maku` | Field |
| 127 maps | This is where battles happen — no new map needed |

This is not a style preference: **field form and battle form are different meshes on the
disc.** Field comes from PROT pack 0874; battle is assembled per equipment from the player
files (PROT 0863..0866), with a different bone count and its own animation bank (the Arts).
The RE project documents this in `docs/formats/character-mesh.md`, with a disc-gated test
confirming battle geometry is **absent** from the field pack.

So Phase 2 produces a **separate** asset set — everything battle lives under
`/Game/Legaia/Battle/` (`Enemies/<Name>/`, `Party/<Name>/`, `Sounds/`, `Materials/`) — and
touches nothing already in the host. No field rig is reused and no retargeting is needed.

**Acceptance:** Gimard exists as a `USkeletalMesh` with 10+ named `UAnimSequence`s and
plays in the preview without loose pieces *(met: 9 clips, loops seamless)*; battle-form Vahn
exists with the full Arts bank *(met: all three, every clip; the equipped weapons' swings are
still out)*.

> **Validate with ONE enemy before looping over 186** — done with Gimard. The fallback named
> here (headless Blender as a GLB→FBX step) became the main path.

---

## Phase 3 — Enemies roaming the map

**Goal:** visible enemies that wander, chase and disappear. Still **no battle** — contact
prints what happened on screen.

**Written 2026-10-26** — usage in [Guides/FIELD_ENEMIES.md](Guides/FIELD_ENEMIES.md). The
model comes from `DT_Enemies` (the main enemy's `Mesh` + `Anims`), so every enemy whose row
gets a model in Phase 2 appears on the field with nothing else to do; Gimard is the first.

Design settled 2026-10-26, after Phases 0/2 showed what the disc provides. The roaming enemy
is **only a visual that starts a fight**: who fights is a list of `DT_Enemies` keys, decided
apart from the model on the map. So there is **one class with no specific enemy in it** —
not a Blueprint per enemy, and not `InitFromEnemyData` swapping the stats in.

### `AJRPGFieldEnemy` (one generic class)

An `ACharacter` (CharacterMovement + navmesh `MoveTo` through an `AAIController`; `AIModule`
is already a dependency because of the party followers), with a plain state enum — no
Behavior Tree: `Idle`, `Wander`, `Chase`, `Return`, `Hidden`.

- **Visuals** — `TArray<FJRPGFieldEnemyVisual>` (mesh + optional AnimBP, or Idle/Walk/Run
  sequences played directly), one picked at random on spawn and on each respawn. One entry
  for now; the converted monsters fill it later.
- **Detection** — detect radius, a larger lose radius, a give-up time, a leash distance from
  home/zone, optional line of sight, walk vs chase speed. The target is the pawn the player
  controls (follows the R3 switch); party followers never trigger it.
- **Encounter** — who fights: a list of `FJRPGEncounterEnemy { EnemyKey, LevelMin/Max,
  Weight, CountMin/Max, bAlwaysJoins }` plus enemies-per-battle min/max. `bAlwaysJoins` is the
  "main enemy"; the rest are drawn by weight. Rolled on contact.
- **Contact** → later: the classic 90s screen shatter → battle → the enemy hides and respawns
  after a delay. **For now:** hide, respawn after the delay, and print every event on screen
  (spawn, detected, lost, gave up, contact with the rolled list and levels, hidden, respawned).
- Freezes while a menu is open (`UWebUISubsystem::OnUIStateChanged`), and carries a
  `UJRPGMistDisturberComponent`.

**Standalone:** drop one in a map and it works alone — its placement is home, it has its own
encounter list and respawn delay.

### `AJRPGEncounterZone` (spawn area)

- `Shape`: **Sphere** (radius), **Box** (extent) or **Spline** (a closed outline drawn by
  hand — any shape, concave included; added 2026-10-27 together with the arena's area, both
  on `FJRPGSplineArea`), drawn in the editor.
- The enemy class to spawn, an optional visuals override, the encounter list, `MaxAlive`,
  respawn delay min/max, and an optional `RequiredFlagID` (WorldState gate).
- Spawns on navmesh points **inside the shape**; wander and return stay inside it.
- Draws the encounter for the enemies it spawned; reuses the same actor on respawn.
- Where the fight happens is Phase 4's business — settled as `AJRPGBattleArena`.

`UBattleSubsystem` gets a small `TryStartEncounter()` grace window, so two enemies touched
at the same moment do not start two encounters — the seed of Phase 4, still without knowing
any `AActor`.

### The level hook

`LevelMin/Max` rolls the enemy's level; the level scales the `DT_Enemies` stats on top of the
existing `UCoreSubsystem::ScaleEnemyHP/ATK/DEF`. The per-level curve is new and stays
configurable — the disc table only supplies base values.

**Acceptance:** a standalone enemy and a zone (sphere, then box) in a host map; enemies wander
inside it; enter the radius and get chased; touch one and see the rolled enemy list on screen;
it disappears and respawns after the delay; a follower touching it does nothing.

---

## Phase 4 — The battle stage

**Goal:** contact assembles a fight scene over the current map, then tears it down. Still
**no combat** — enter, position, frame, leave.

Battle **loads no map**. That is not a shortcut: it is how the original works — the fight
happens in the scene where the encounter triggered and only the camera changes. It also
matches the split this project already has (world in Blueprint, systems in C++).

Spell scenes that "go somewhere else" (Seru summons, Phase 8) do not change this: they are
Sequencer shots, optionally on a small sublevel streamed in at runtime
(`ULevelStreamingDynamic::LoadLevelInstance`) away from the fight, with the map still loaded.

### `AJRPGBattleArena` — where on the map (written 2026-10-27)

The maps are not flat and some need more than one fighting spot, so where a fight is
assembled is **placed by hand**, not computed from the contact point alone. One actor,
dragged into the map:

- **Formation preview in the editor.** It draws every seat the fight will use: a capsule
  coloured by the ground under it, a ring and an arrow coloured by side (blue heroes, orange
  enemies). It redraws every editor frame, so sculpting the landscape updates it live.
  `PreviewPartyCount` / `PreviewEnemyCount` set how many to draw; `PreviewPartyMesh` /
  `PreviewEnemyMesh` optionally put the battle models on the seats.
- **Ground check per seat.** A trace against `WorldStatic` puts each seat on the ground:
  **green** ok, **yellow** more than `MaxHeightDifference` from the floor under the arena's
  centre, **red** no floor / steeper than `MaxSlopeDegrees` / the seat capsule is inside a wall
  or prop. Red and yellow are where the map needs flattening. "Validate Ground" and Map Check
  list them in the Message Log.
- **Automatic formation** (below: the original's seats, or an arc) or **manual seats**:
  `bManualSeats` turns `PartySeats` / `EnemySeats` into handles dragged in the viewport;
  "Copy Formation To Manual Seats" starts them from the automatic one. An index with no manual
  seat falls back to the automatic formation.
- **Area** (`AreaSpline`): the part of the map that uses this arena, drawn as a closed spline
  seen from above — any shape, Alt+drag adds a point. `AreaHeightLimit` for maps with floors.
  "Snap Area To Ground" only moves the outline down onto the terrain so it is visible.

**Which arena a fight uses** (`AJRPGBattleArena::FindArenaFor(ContactLocation)`):

1. Among the arenas whose area contains the contact point, the highest `Priority`; ties go to
   the nearest.
2. Outside every area: the nearest arena with `bUseAsFallback`.
3. No arena in the map: none — the stage assembles the fight where the contact happened.

The scene is assembled at the arena **under the transition** (a fade to black now, the
screen shatter in Phase 7), so the jump is never seen. The field party is not moved: it is
hidden where it stood and shown again there when the fight ends. `AJRPGFieldEnemy` picks the
arena on contact (`FJRPGFieldEncounter::Arena`) and, with `bPrintDebug`, draws its formation
in PIE for a few seconds. The Muscle Dome test map is the simplest case: one arena in the
ring.

Per arena, for the fight itself: `HideDuringBattle` (actors hidden while a fight happens
there — a tree in front of the camera) and `BattleCamera` (a camera placed by hand that
replaces the automatic framing).

### Assembling the scene — the arena does it (written 2026-10-27)

Usage in [Guides/BATTLE_STAGE.md](Guides/BATTLE_STAGE.md). **The arena is the only thing placed
in a map.** A first version had a separate stage actor with its own Blueprint and a Project
Settings class; it was folded into the arena the same day — two actors per map doing nearly
the same job, and the hero models configured by hand.

- `UBattleSubsystem::BeginBattle` builds the combatant list (`Setup.Party`, else the active
  party, else `FallbackParty` for PIE without a save; capped at `MaxPartySlots` /
  `MaxEnemySlots`), turns `IsInField` off and fires `OnBattleStarted`. Field enemies stop.
- `UJRPGBattleSceneSubsystem` (a world subsystem — exists by itself, nothing to configure)
  picks the arena (`FindArenaFor(Setup.Location)`; a map with no arena gets a temporary one at
  the contact point, the party facing the enemy) and fades to black over its `TransitionTime`.
- **Screen fully black:** `OnFieldHidden` — every field enemy hides and goes home (standalone:
  its placement; zone: a random point of the zone), so nobody is glued to the player when the
  fight ends; the one touched waits for its own respawn. Then `AJRPGBattleArena::AssembleBattle`:
  hides and freezes the field party and `HideDuringBattle`, turns the field controls off,
  spawns one `AJRPGBattlePawn` per combatant at `GetFormation`, points the camera (its
  `BattleCamera`, else `FrameGroup` anchored on an enemy so the camera sits behind the party).
- Models come from the DataTables: `DT_Characters.BattleMesh` / `BattleAnims` for heroes,
  `DT_Enemies.Mesh` / `Anims` for enemies.
- `EndBattle` → black → `TeardownBattle` (everything back exactly as it was) → `OnFieldShown`
  → screen opens. `EncounterGraceTime` also counts from the end.

Console: `JRPG.Battle.Start <EnemyKey> [Count] [Level]` and `JRPG.Battle.End
[victory|defeat|escape|abort]`.

`UBattleSubsystem` **never knows about `AActor`** — it speaks only through delegates, the
same separation the rest of the plugin keeps.

### Muscle Dome — the test map

`AJRPGBattleWaves`: fights in a row. A list of encounters (one per wave), the party, the
arena; a won wave calls the next after a delay, a lost / escaped / aborted one stops. It runs
on the field GameMode — no separate battle GameMode, pawn or controller — so it tests the same
path a field encounter takes. With no combat yet, a wave ends from the console.

The waves live in a JSON (`WavesFile`, `Content/Data/Waves/*.json`) read on every play and
edited in **LegaiaStudio › Ondas**. Packaged with *Additional Non-Asset Directories*, the
testers can change the waves without a new build.

### The original's seats (`Formation = Original`, the default)

The original places every combatant from two static `SCUS_942.54` tables read by
`FUN_800513F0` (RE: `docs/subsystems/battle.md` "Stage seats",
`crates/engine-battle/src/battle_seats.rs`, pinned against seven battle save states). Seats are
`(x, z)` in PSX units on a flat stage; the party faces `+Z`, monsters face `-Z`, and the camera
orbits the origin between the rows. Party slot 0 is the leader.

| Count | Party (`0x800775C8`) | Monsters, normal (`0x80077608`) | Monsters, alternate |
|---|---|---|---|
| 1 | `(0,-800)` | `(0,800)` | same |
| 2 | `(300,-800)` `(-300,-800)` | `(-300,800)` `(300,800)` | same |
| 3 | `(0,-825)` `(600,-775)` `(-600,-775)` | `(-600,825)` `(0,750)` `(600,825)` | `(0,900)` `(-600,700)` `(600,700)` |
| 4 | — | `(-900,900)` `(-300,800)` `(300,800)` `(900,900)` | `(0,1000)` `(-600,800)` `(600,800)` `(0,600)` |

**Scale.** The seats and the battle meshes are in the **same unit**: the battle base matrix
draws actors *and* their stage translations at 4× under one camera (RE
`battle-stage-camera.md`), so the ratio between a seat and a model's size is the disc's. The
meshes come in at **0.433005 cm per PSX unit** (`Extras/ps1_glb_to_fbx.py`, calibrated with
battle Vahn ≈ 422 units → 183 cm), so the seats use the same factor (`OriginalCmPerUnit`):

| | PSX units | cm |
|---|---|---|
| 1 vs 1: each side from the centre | 800 | 346.4 |
| 1 vs 1: hero to monster | 1600 | 692.8 (≈ 3.8 × Vahn's height) |
| 3 heroes: wing offset / wing depth / leader depth | 600 / 775 / 825 | 259.8 / 335.6 / 357.2 |
| 4 monsters: outer pair | (±900, 900) | (±389.7, 389.7) |

Axes into the arena: PSX `z` → arena `+X` (forward for the party), PSX `x` → arena `+Y` (the
party's right). The alternate family (`bAlternateEnemyRows`) is the original's no-escape /
ambush seating. The original seats at most 3 heroes and 4 monsters; bigger counts (Seru as
members, more enemies) use the arc below.

Not reproduced by the arena, belongs to Phase 5: the original **recentres the formation at
every round start** (`FUN_801DB318`: X/Z extents over the party and the living monsters, an
axis wider than `0x800` squashed to `0x800`, then the centroid subtracted), which is what pulls
a wandered formation back into frame. A 3-on-1 fight therefore reads `-13` in Z off the table.

### Flexible formation

Beyond the original's counts — or with `Formation = Arc` — the formation is computed from the
actual count. It lives on the arena (`AJRPGBattleArena::GetSeatTransform` / `ResolveSeat` /
`GetFormation`):

```cpp
// Arc around the arena. Heroes behind the centre (-X) facing +X, enemies in front facing -X.
FTransform GetSeatTransform(EJRPGBattleSide Side, int32 Index, int32 Count) const;

UPROPERTY(EditAnywhere) float PartyRadius     = 350.f;
UPROPERTY(EditAnywhere) float EnemyRadius     = 475.f;
UPROPERTY(EditAnywhere) float PartyArcDegrees = 90.f;
UPROPERTY(EditAnywhere) float EnemyArcDegrees = 120.f;
```

3 or 6 members, 2 or 8 enemies — everything accommodates itself.

### Configurable counts

```cpp
UPROPERTY(EditAnywhere, Category="JRPG|Battle") int32 MaxPartySlots = 3;   // Serus raise this
UPROPERTY(EditAnywhere, Category="JRPG|Battle") int32 MaxEnemySlots = 5;
UPROPERTY(EditAnywhere, Category="JRPG|Battle") bool  bScaleEnemiesWithPartySize = true;
```

No fixed array of 8: a `TArray<FJRPGCombatant>` with a `Side` and a `Type`
(`Character` / `Seru` / `Enemy`). Serus join as support members — that is why the party can
grow — and `bScaleEnemiesWithPartySize` is the counterweight the zone applies when drawing.

Written: `MaxPartySlots` / `MaxEnemySlots` on `UBattleSubsystem`. **Moved to Phase 8**:
`bScaleEnemiesWithPartySize`, together with the Seru members that are the only way the party
grows past 3 (the roster has 4 characters and the original fights with 3).

**Acceptance:** contact removes player control, the camera frames the fight, 3 heroes spawn
on one side and N enemies on the other in their `Idle` poses; a debug command ends it and
restores everything. Same in the Muscle Dome: wave after wave with `JRPG.Battle.End victory`.
*Met 2026-10-27* — 1 to 5 enemies (the original's seats up to 4, the arc at 5). The 5-hero
formation check goes with the Seru members to Phase 8.

---

## Phase 5 — Basic combat

**Goal:** a fight that works end to end with plain attacks. No Arts, no magic.

> **Ask before writing any code in this phase.** Whether PvP / online / local co-op must stay
> possible changes how the combat core is written. Do not decide it alone in either
> direction: ask the user, and record the answer in
> [PvP, online and co-op](#pvp-online-and-co-op--undecided-ask-before-phase-5). The hit
> timing is already settled either way (each hit resolved at its frame on the logic's timeline,
> not by the animation — see "State machine"); the rest of the sketches below (`PhaseTimer` in
> the combatant, one fighter at a time) are the version that closes that door.

### `Public/Battle/BattleTypes.h`

```cpp
UENUM(BlueprintType) enum class EJRPGBattleSide   : uint8 { Party, Enemy };
UENUM(BlueprintType) enum class EJRPGBattleCommand: uint8 { None, Attack, Arts, Item, Magic, Escape };
UENUM(BlueprintType) enum class EJRPGActionPhase  : uint8 {
    Idle, Windup, Advance, Strike, Chain, Recovery, Return, Done
};

USTRUCT(BlueprintType) struct FJRPGCombatant {
  int32 SlotIndex; EJRPGBattleSide Side; EJRPGCombatantType Type;
  FName Key;                  // party member, seru, or DT_Enemies row
  int32 Level;
  int32 HP, MaxHP, MP, MaxMP;
  int32 ATK, UDF, LDF, SPD, INT, AGL;
  EJRPGElement Element;
  int32 AP;                   // persistent meter (Arts add, specials spend)
  int32 CommandGauge;         // per-turn input budget, seeded from AGL
  int32 InitiativeKey;
  EJRPGActionPhase Phase; float PhaseTimer;
  int32 TargetSlot; int32 JuggleCounter; bool bGuarding; bool bAlive;
  TArray<FName> Conditions;
};

USTRUCT(BlueprintType) struct FJRPGBattleEvent {
  FName EventType;   // Damage / Miss / Death / TurnStart / APChanged / ArtRecognized
  int32 SlotIndex, TargetSlot, Value; FText Text;
};
```

### `UBattleSubsystem`, expanded

`UGameInstanceSubsystem` + `FTickableGameObject` (`IsTickable()` true only during a fight,
`ETickableTickType::Conditional`).

```cpp
bool BeginBattle(const TArray<FJRPGEncounterSpawn>& Enemies, const FTransform& Anchor, int32 Seed);
void EndBattle(bool bVictory);
bool SubmitCommand(int32 Slot, EJRPGBattleCommand Command, int32 TargetSlot);
const TArray<FJRPGCombatant>& GetCombatants() const;
int32 GetActiveSlot() const;

FOnBattleStarted OnBattleStarted;   // for UJRPGBattleSceneSubsystem
FOnBattleEvent   OnBattleEvent;     // for the UI
FOnSlotChanged   OnSlotChanged;     // for the camera
FOnBattleEnded   OnBattleEnded;
```

### Turn order

The original's initiative key is `key = SPD + roll + 1 + wounded_bonus`, where the bonus is
the missing HP shifted (shift 4 below a quarter of max, 5 below half). Implement that and
leave room to tune — sorting by descending key is what produces the original's feel.

### Damage formula

The original resolves it as follows, and it is a good base because the *feel* of combat
depends on the shape of it:

```
BaseOffense = ATK_base + (ATK of the equipment that command uses) / 2
Offense     = (BaseOffense + rand % (BaseOffense/8 + 1)) * Power / 16
              + CurrentHP / 256
              + (juggle * ATK) / 64
If Art:       Offense = Offense * 13 / 10
Element:      x1.04 on opposed pairs, x0.96 on same element
Defense     = DEF + rand % (DEF/8 + 1)       // UDF on high hits, LDF on low ones
Damage      = Offense - Defense, capped at 9999
Floor:        when defense would win it does not clamp to 1 — it rebuilds the
              attack on top of the defense
```

Two things matter more than the rest:

- **Truncated integer arithmetic everywhere.** `int32`/`int64` and explicit `>>`; never
  `float`, never `FMath::RoundToInt`.
- **ATK without equipment.** `GetEffectiveStat("ATK")` already folds equipment in (it is
  the menu number). Combat needs **base** ATK + Ra-Seru bonus, and folds equipment per
  command at the moment of the hit. Confusing the two inflates all damage invisibly.

Since this is not a 1:1 port, all of it lives in
`UBattleFormulas : public UBlueprintFunctionLibrary` as pure static functions with exposed
constants — rebalancing should not mean recompiling the state machine.

Art power is per hit: `0x16–0x1A` → UDF × 12/18/20/22/28, `0x1B–0x1F` → LDF × the same,
`0x0C–0x15` → the same scale in the "alt" range (misses floating or short enemies). So
`bTargetsUDF` comes from the hit's own power byte, not from the command.

### State machine

`EJRPGActionPhase`, short and readable, driven by timers and AnimNotify:
`Windup → Advance → Strike → (Chain) → Recovery → Return → Done`.

**Decided (2026-10-31), whatever the PvP answer: each hit is resolved at its own frame, by the
logic's timeline — not by the animation, and not all up front.**

- **Not all up front**, because a **parry** is planned (see "Deliberate departures"): a hit's
  result depends on a button the defender presses *during* the attack, so it cannot exist
  before that moment. (Without a timed mechanic, resolving up front and at the hit would feel
  the same to the player — the number only shows when the hit lands either way.)
- **Not by the animation**, because a notify fires when the playback gets there: frame rate,
  hitches and montage blends move it, a missing clip never fires it, and two machines never fire
  it at the same time.

So the action runs on a **logic timeline**: a fixed-step frame counter owned by the battle, in
whole frames at the game's pace (15 × rate frames/s, [Guides/PS1_ASSETS.md](Guides/PS1_ASSETS.md#the-partys-clips-and-sounds)).
Each clip's length, rate and hit frames are **asset data** — the notify times on the
`UAnimSequence`, read by the logic, the same on every machine. When the counter reaches hit N's
frame, `ResolveHit(N)` runs with the fight's state, its RNG and every input received so far
(a parry pressed before the window closed), and emits that hit's events. The state machine above
is the **presentation** of that timeline: the pawn plays the clip in step with the counter, and
the `Strike` notify shows the event the logic already emitted. A clip that lags or is missing
only shows late — the fight never hangs on a notify.

This matches the original more closely than it looks. There the anim tick (`FUN_80047430`)
calls the damage kernel (`FUN_801EC3E4`) on each hit-event frame — damage is rolled **at the
moment of the hit** (RE `docs/subsystems/battle-action-queue.md`, "Damage is one power byte per
animation hit event"). The difference is only who owns the clock: there the animation, here the
logic. Two things follow:

- **The juggle window** comes for free. A defender is juggleable while its current clip is still
  before its first event frame (`actor[+0x1F7]`, `record[+0x10]`); the kernel's juggle bonus
  grows only then. On the logic timeline that is the defender's reaction clip counted in frames —
  the same rule, frame by frame, as on the PS1.
- **The RNG order.** The original draws from one BIOS `rand` shared with presentation (the
  camera variant's `rand % 2` takes from the same stream). The remake keeps the fight's stream
  for the rules only; matching the original's draw sequence is not a goal.

See also [PvP, online and co-op](#pvp-online-and-co-op--undecided-ask-before-phase-5).

### Staying in the arena

Clips carry the body forward (Slash Kick, the Supers), so a long turn moves the attacker — the
original could push fighters off the stage. **Fighters stay where their action left them**: no
walk back to a seat after the turn (it looks bad and slows the fight). What keeps everyone in is
a set of rules, not trimming the Arts:

1. **The target is the wall.** The original's range law (`FUN_8004E2F0`, the in-range back-off
   `0x80047C3C`) holds an attacker at the edge of its reach while it strikes: its clips cannot
   carry it through the enemy. The remake keeps that — forward movement stops at reach distance.
2. **The arena bounds clamp everyone.** Knockback, the arrival shove and the attacker's own root
   movement are all clamped to the arena's area (`AJRPGBattleArena`). A target pinned against the
   edge stops the attacker by rule 1, so the whole fight stays inside.
3. **Fighters keep their distance.** A minimum spacing between any two combatants (allies
   included), so a party that drifted forward does not stack on the enemy line.
4. **Per-Art override.** A new big Art can be marked to play in place (no root movement) or to
   cap its travel; the camera frames the action, wherever it is.

Root movement itself is the disc's: per clip, a root speed (`+0x0C`) and an end step (`+0x0E`)
(see [Guides/ARTS.md](Guides/ARTS.md)). The Art tester lines clips up by the body instead and has
**Reset Position**.

### Enemy AI

Start honest and dumb: pick a random living target weighted toward low HP, and attack.
The RE project documents the real selector (`FUN_801E9FD4`) when you want to go further.

**Acceptance:** touch a Gimard → turn order computed → choose Attack → animation plays,
damage shows, HP drops → the enemy retaliates → kill it → `Defeat` animation, victory, XP
distributed through `AwardBattleXP`, back to the map, pawn gone.

---

## Phase 6 — Directional Arts and the new AP

**Goal:** the heart of Legaia, with the AP inversion.

### Two meters, not one

| Meter | Scope | Role |
|---|---|---|
| `CommandGauge` | Per turn, seeded from `AGL` | Caps how many directional commands fit in a turn. This is what makes high AGL mean long combos — the original's feel |
| `AP` | Persistent, `0..100`, already in `FJRPGPartyMember` | **Regular Arts add. Hyper / Super / Miracle spend.** This is the improvement |

The original spent AP on every command, which is the complaint this remake fixes. Here the
player builds AP by performing Arts and burns it on the big ones.

### `UArtsSubsystem`

```cpp
static int32 DirectionByteFor(FName Character, EJRPGArtButton Button);
// Vahn/Gala: Arms=1 RaSeru=2 High=4 Low=3 | Noa: Arms=2 RaSeru=1 (left-handed, mirrored)

TArray<int32> RecognizeArtSequence(FName Character, const TArray<int32>& Input) const;
TArray<FName> GetKnownArts(FName Character) const;
void TeachArtToCharacter(const FString& CharacterName, FName ArtKey);   // preserved
```

Recognition is **greedy longest-match**, left to right, over the Arts the character
actually **knows**: at each position, the longest Art whose `Directions` prefix the
remaining input is consumed and emitted; a position that starts no Art is skipped as a
connector.

> `arts.toml` already stores `directions[]` in the owner's scheme, so the table needs no
> scheme column — Noa's mirroring only matters at *input* time. Validation for the
> converter: recompute `directions[]` from `command[]` and compare against the TOML.

### The input window

`SubmitCommand(Slot, Arts, …)` opens the window; each button debits `CommandCost` from
`CommandGauge` and pushes a byte; `CommitArtsChain` recognizes the chain, resolves the
hits, credits `APGain` for regular Arts and debits `APCost` for the specials.

Hyper / Super / Miracle appear **unavailable** (greyed out) when AP does not cover
`APCost` — the feedback has to be visible before the player builds the sequence.

**Acceptance:** enter `Arms, Ra-Seru, High` as Vahn → "Hyper Elbow" is recognized, the right
animation plays, damage uses the Art's power and AP **rises**. Fill AP and fire a Super Art
→ AP **drops** and damage is much larger. With insufficient AP, the Super shows as blocked.

---

## Phase 7 — Presentation

**Goal:** the battle stops looking like a prototype.

- **HUD in `shell.html`.** A new `battle` screen: party HP/MP/AP strip, enemy row, the AP
  bar with its cost markers, damage log. Add `Battle` to `EJRPGUIState` **at the end of the
  enum** (values are serialized in Blueprints). New `UWebUIBridge` callbacks:
  `OnBattleCommand`, `OnBattleArtsButton`, `OnBattleArtsCommit`, `OnBattleTargetChanged`.
  C++→JS always goes through `UWebUISubsystem`, never `ExecuteJS` from `UBattleSubsystem`.
- **Attack camera.** `FocusOnActor` on turn start, `PlayCameraShake` on `Strike`, `Orbit`
  on victory.
- **Sound.** Impacts, Art voice lines and battle BGM extracted via the RE project's `vab` /
  `xa` crates, wired into `UAudioSubsystem`.
- **Remaining commands:** Item (reuses `ApplyBattleItemEffect` and `UInventorySubsystem`),
  Escape (roll based on party SPD vs enemies), Guard.
- **Rewards:** gold, drop and steal are already in `DT_Enemies`.

**Acceptance:** a full fight with sound, reactive camera, usable items and working escape,
with nothing debug-looking on screen.

---

## Phase 8 — Magic and Seru

**Goal:** close the system.

Last on purpose: it depends on everything above being stable and carries the most new rules.

- `DT_Magic` (`magic.toml`, 29 spells → `FJRPGSpellData`, `generate_magic_csv.py`) and the spell
  system over it.
- **Spell power is missing** — it is not in `magic.toml`. It comes from the disc's
  move-power table (PROT 0898, stride 26), so an extra extraction pass produces
  `DT_MovePower`.
- Seru capture in battle and learning the matching spells.
- **Serus as party support members** — this is where Phase 4's larger party gets real
  content. With them: `bScaleEnemiesWithPartySize` (more heroes, more enemies rolled) and the
  5-hero formation check left over from Phase 4's acceptance.
- Ra-Seru summons (200–255 MP, gated behind sidequests).

---

## PvP, online and co-op — undecided, ask before Phase 5

**Status:** an idea, not planned and not decided (raised 2026-10-31). Nothing here is to be
built in Phases 5–8. The only question for now is whether the combat core leaves the door
open.

> **For whoever implements Phase 5, human or agent:** before writing the combat core, **ask
> the user** whether PvP / online / local co-op should stay possible, and show them both
> versions — the Phase 5 plan as written, and
> [Phase 5 with the door open](#phase-5-with-the-door-open--what-changes-in-the-plan) — so
> they know what each answer means. Do not assume either answer. Write the decision
> and its date in this section.

### What the idea is

- **Battle-only modes.** The Muscle Dome (`AJRPGBattleWaves`) as an arena: co-op against
  waves of monsters, or PvP with each player bringing the party from their own save. Field
  co-op (walking a map together) is **out of scope** — it would touch field enemies, Mist,
  WorldState, map travel, the save, the camera, dialogue and cutscenes.
- **Local co-op.** Same screen, no network: player 2 enters Noa's commands, player 3 Gala's.
  No split screen needed, which matters because the Ultralight UI is one shell per process.
- **Online without a server of our own.** A **listen server**: one player hosts, the others
  join. Rooms through **EOS lobbies** (free, P2P relay, independent of any store — the
  preferred option), Steam lobbies (needs an AppID, and Sony-derived assets cannot ship there)
  or a typed IP (port forwarding or Tailscale/Radmin).

### Why it has to be asked now

A turn-based fight is the easiest genre to network: only **commands** travel ("Vahn: Arms,
High, Low → slot 2"), never positions — plus, with the parry, the defender's timed press. The
host runs the fight's timeline and sends each hit's events as they happen; the clients only play
animations. That works only if the core is written as
**commands and inputs in → resolution on the logic timeline → events out**, with the
presentation consuming the events. Writing
it that way costs almost nothing in Phase 5 (it is the same code, organised differently, and
it also makes the core testable without opening a map). Converting a core whose damage comes
out of animation notifies means rewriting the resolution.

### If the answer is yes: what the core cannot do

| Do not | Because | Do instead |
|---|---|---|
| Apply damage, HP, deaths or AP in an AnimNotify or timer callback | Host and clients do not run their animations in sync (frame rate, hitches, montage blends) | `ResolveHit(N)` on the logic timeline emits `FJRPGBattleEvent`s; `Strike` and `JRPGAnimNotify_ChainCut` only *show* them |
| Roll with `FMath::Rand` / `FMath::RandRange` or any global stream | Each machine rolls something different | One `FRandomStream` inside the battle state, seeded from `BeginBattle`'s `Seed`; only the resolution consumes it. Cosmetic randomness (which flinch clip, particles) uses a separate stream |
| Keep presentation in the combatant (`EJRPGActionPhase Phase`, `float PhaseTimer` in `FJRPGCombatant`) | Ties the rules to frame time, and floats diverge between machines | `FJRPGCombatant` holds rules only, all integer; phase and timers live in the pawn / presentation |
| Let the UI bridge mutate the battle directly (`SubmitCommand` straight from `UWebUIBridge`) | Online, the command has to travel to the host first; local co-op needs to know who sent it | Every command is a value struct (`FJRPGBattleCommand`: issuing player, slot, command, target, Art bytes) through one entry point that checks the issuer owns the slot and validates the rest (rule 22: never trust the payload) |
| Send an Arts chain button by button to the rules | Each button would be a round trip | The input window stays local; `CommitArtsChain` sends the whole chain as one command |
| Read `UPartySubsystem` / `UInventorySubsystem` / `UStatusSubsystem` during resolution | In PvP the opponent's party is not in your GameInstance | Snapshot everything into the combatants at `BeginBattle`; resolution reads only the battle state; write back (HP, XP, items used) at `EndBattle` |
| Use `GetFirstPlayerController()` in battle code (`AJRPGBattleArena`, `UJRPGBattleSceneSubsystem` do it today) | On a listen server it is always the host's controller | Pass the player explicitly; which player owns which slot lives in the battle state |
| Let positions feed the rules (traces, distance after root motion, "staying in the arena") | Actor positions differ per machine | Targeting, range and hits are decided by slot; spacing and arena clamping stay presentation |
| Advance the logic by the frame's `DeltaTime`, or let time dilation / a menu pause stop it | Variable steps land a hit on a different frame per machine; the host cannot be paused for everyone | A fixed-step integer frame counter owned by the battle; online it never pauses (single player may) |
| Run enemy AI outside the resolution | Each machine would pick different targets | AI is a step of the resolution, using the battle stream, on the host only |
| Judge a parry on the host's clock | The defender sees the attack late by the network delay; a press on time on their screen would arrive late | The defender's machine judges the press against its own playback and sends "parried hit N"; the host holds hit N until that input or a deadline (see "The transport") |

Even with a yes, Phase 5 does **not** add replication, lobbies or EOS — it only keeps the
core from closing the door.

### Phase 5 with the door open — what changes in the plan

The rules themselves stay: the damage formula, the turn order, the dumb AI and "Staying in the
arena" are the same. What changes is how Phase 5 is organised, item by item. On screen, in
single player, the result is identical.

**The round.** The sketch's `GetActiveSlot` / `OnSlotChanged` read as one fighter at a time,
choosing and acting. With the door open, a round has two steps:

```
Commands (everyone chooses; hidden from the other side) → Execution (the logic timeline, hit by hit; parries arrive during it)
```

The original already almost works this way (all three heroes' commands first, then everyone
acts by initiative). In PvP both sides choose at the same time without seeing each other.

**`BattleTypes.h`**

| In the sketch | With the door open |
|---|---|
| `EJRPGBattleSide { Party, Enemy }` | Stays, but the rules ask "same team?": add `int32 Team` and `int32 OwnerPlayer` (-1 = AI). Victory is "a whole team is down", not "the enemies are dead" |
| `EJRPGActionPhase Phase; float PhaseTimer` in `FJRPGCombatant` | **Out of the combatant**, into `AJRPGBattlePawn`. The combatant holds rules only, all integer |
| Stats read through `GetEffectiveStat` at the moment of the hit | A **snapshot** at `BeginBattle`: base ATK + Ra-Seru bonus, and each equipment piece's ATK kept separate. This also retires the "ATK double-counted" risk |
| `FJRPGBattleEvent { FName EventType; …; FText Text }` | `EventType` becomes an enum; `FText` goes (the UI builds the text); add `ActionIndex` / `HitIndex` so a notify knows which event to show |
| — | **`FJRPGBattleCommand`** `{ PlayerIndex, Slot, Command, TargetSlot, ArtBytes, ItemKey }` |
| — | **`FJRPGBattleInput`** `{ PlayerIndex, Slot, Kind (Parry…), HitIndex, Frame }` — what arrives during execution |
| — | **`FJRPGBattleState`** `{ Combatants, FRandomStream Rng, Round, TurnOrder, Frame }` — the whole fight in one struct, the logic timeline's counter included |

**`UBattleSubsystem`**

| In the sketch | With the door open |
|---|---|
| `BeginBattle(Enemies, Anchor, Seed)` (the code already has `BeginBattle(Setup)`) | Stays `BeginBattle(Setup)`, but `FJRPGBattleSetup` gains `Seed`, and `TArray<FName> Party` (read from the local `UPartySubsystem`) becomes teams carrying their snapshots — the opponent's party arrives as data, not from your save |
| `SubmitCommand(Slot, Command, TargetSlot)` | `SubmitCommand(const FJRPGBattleCommand&)`, which checks the issuer owns the slot |
| — | `SubmitInput(const FJRPGBattleInput&)` — the parry, during execution |
| — | `StepFrame()` advances the logic timeline one fixed frame; `ResolveHit(N)` runs when a hit's frame comes |
| `OnBattleEvent`, one event at a time | Stays, with `ActionIndex` / `HitIndex` in the event — the presentation shows each one when its clip gets there |
| `FTickableGameObject` | Stays, but as a **fixed-step accumulator** calling `StepFrame()` — never the raw `DeltaTime` into the rules. Open question 4 still applies |
| `EndBattle` writing back to the party | Writes back (HP, XP, items used) only into the **local player's** members |

**Turn order.** Same formula; the `roll` comes from the state's `Rng`. `TArray::Sort` is not
stable, so two equal initiative keys could come out in a different order on each machine: break
ties explicitly by `SlotIndex`.

**Damage formula.** Same formula (the plan already bans floats, which helps). `UBattleFormulas`
functions take the `FRandomStream&` and the snapshot instead of asking a subsystem. The element
"×1.04 / ×0.96" becomes integer (`* 104 / 100`, `* 96 / 100`) — a float from
`GetElementMultiplier` can round differently per machine.

**State machine.** Still there, but **in the pawn, as playback** of the logic timeline
(Phase 5 › State machine, decided either way).

**Enemy AI.** Same logic, but it runs inside the resolution when the enemy's action starts, on
the fight's `Rng`, and finds targets by **team**, not by `Side`.

**Acceptance, added.** An automation test that runs without a map: the same seed, the same
commands and the same inputs at the same frames produce exactly the same event list. That test
is what guarantees PvP later.

**Unchanged.** The encounter roll (`FMath::RandRange` in `JRPGEncounterTypes.cpp`) happens before
the fight — online, the host rolls and sends the finished setup. The Phase 4 arena, pawns and
fade. The existing `GetFirstPlayerController()` calls stay; Phase 5 just must not add new ones.

**Cost.** A little more structure up front (the command and input structs, the fixed-step timeline), and nothing
network-related.

### If the answer is no

The sketches in Phase 5 stand as written — except the hit timing, which is decided (each hit
at its frame on the logic timeline, Phase 5 › State machine). Record the decision here with its
date.
Adding PvP or online later then means reorganising the core around commands and teams, and
this section is the list of what changes.

### The transport, for when it is built

- **Host-authoritative.** `UGameInstanceSubsystem`s do not replicate, so a replicated actor
  (`AJRPGBattleNetState`, say) carries the fight. A client sends its `FJRPGBattleCommand`
  through a Server RPC; the host validates, runs the logic timeline and multicasts each hit's
  events; every machine, host included, plays the same events.
- **The parry over the network** is the one real-time piece, and the hard one. The defender
  sees the attack late by the network delay, so their machine judges the press against its own
  playback and sends "parried hit N" (`FJRPGBattleInput`); the host holds `ResolveHit(N)` until
  that input arrives or a deadline passes (the window plus a latency budget), and accepts it only
  inside a tolerance. Clair Obscur and the like are single-player and do not face this. Settle
  it when the mode exists; local co-op has no such problem.
- **Local co-op** needs no network: route each gamepad (or `ULocalPlayer`) to the slots it
  owns, and have the battle HUD show whose command is being entered.
- **PvP balance** (juggles, Supers, Ra-Seru summons against a human) is its own question, to
  settle once the mode exists.

---

## Risks

| Risk | Severity | Mitigation |
|---|---|---|
| ~~Skin injection rejected by Interchange~~ | Retired — the Blender → FBX converter works (Gimard + party) | Its own traps are rules 48–52 in [ARCHITECTURE.md](ARCHITECTURE.md) |
| PS1 over-bright vertex colours (> 1.0) clamped by the 8-bit vertex colour | Low — flames and glowing blades look duller | Export the colour halved and double it in `M_PS1Monster` |
| ATK double-counted (`GetEffectiveStat` already includes equipment) | **High**, because it is invisible — inflates damage uniformly | Explicit test: Vahn's combatant with a sword equipped must read `ATK == base + Seru bonus` |
| PS1 integer arithmetic translated as float | Medium | `int32`/`int64` throughout the damage kernel, no `FMath::` |
| Larger party breaking balance | Medium | `bScaleEnemiesWithPartySize` from Phase 4, multipliers exposed as `UPROPERTY` |
| Regenerating a CSV wiping hand-made mesh/anim mapping | Medium, destructive | Merge by row name in the generator + timestamped `.bak` in `Docs/Data/_backup/` |
| `bosses.toml` inline tables breaking the parser | Low | Dedicated regex; validate 30/30 rows |
| ~~Importing 186 enemies by hand~~ | Retired — `Extras/ps1_monsters_batch.py` + the editor scripts' `--all` | |

---

## Open questions to settle in practice, not on paper

1. ~~Does Interchange honor a glTF skin?~~ Moot: the pipeline goes through FBX (Phase 2).
2. ~~PSX→cm scale~~ — settled at **0.433005 cm per unit** (Phase 2 › Scale).
3. **`EAutomationTestFlags` in 5.8** was renamed recently — check `AutomationTest.h` before
   writing the first automated test.
4. **`UGameInstanceSubsystem` + `FTickableGameObject`** — a known pattern, but confirm
   whether `ETickableTickType::Conditional` is enough or `IsTickableWhenPaused()` is needed.
5. **The parry** (planned 2026-10-31, design open). To decide: which hits can be parried (every
   enemy hit, or only some attacks), the window in frames and whether it shrinks with
   difficulty, what a parry does (no damage, less damage, a counter, AP), what a miss costs,
   how the window is telegraphed (a flash, a sound, the clip itself), and whether the party's
   Arts can be parried in PvP. The window comes from the attack clip's hit frame, so the enemy
   clips' hit notifies have to be right. Its phase is not set yet (6 or 7).

---

## Next concrete step

**Before Phase 5:** ask the user whether PvP / online / local co-op must stay possible
([the section](#pvp-online-and-co-op--undecided-ask-before-phase-5) says why and what each
answer means).

**Phase 5:** basic combat on the Muscle Dome — combatant stats, the original's turn order,
the integer damage kernel, the action state machine, a dumb enemy AI, victory / defeat. In
parallel, the party's battle clips (attack, hit, KO) that it needs.
