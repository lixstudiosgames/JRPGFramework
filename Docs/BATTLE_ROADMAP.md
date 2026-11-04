# Battle System Roadmap

**JRPGFramework — Legend of Legaia Remake**
Approved 2026-09-13. Phases 0 to 4 are done (2026-10-30): the battle data (`DT_Enemies`,
`DT_Arts`, `DT_Bosses`), every monster and the party's battle clips in Unreal, enemies roaming the
map and a fight assembled over it. Next is Phase 6, the Arts, and then Phase 5, combat itself
(order swapped 2026-11-04: [Phases](#phases)). The combat core keeps the door open to PvP /
online / co-op ([decided 2026-11-04](#pvp-online-and-co-op--door-open-decided-2026-11-04)).

This is the largest unimplemented system left in the plugin, and the only one with stub
files already in place. (Dialogue, Cutscene and Localization are also unwritten - see
[STRUCTURE.md](STRUCTURE.md).) Everything
else — Core, Save/Load, Inventory, World State, Shop, Audio, Camera, Party, Status, WebUI —
is done and will be consumed by the battle system rather than rewritten.

---

## Current state

The fight is **assembled but not fought** (2026-11-04):

| Exists | Where |
|---|---|
| `BeginBattle(FJRPGBattleSetup)` / `EndBattle(EJRPGBattleResult)` (Victory, Defeat, Escaped, Aborted), the combatant list (who and where, no stats yet), `OnBattleStarted` / `OnBattleEnded` | `UBattleSubsystem` |
| The arena, the pawns in their seats, the fade, the Muscle Dome waves | Phase 4 — `AJRPGBattleArena`, `UJRPGBattleSceneSubsystem`, `AJRPGBattlePawn`, `AJRPGBattleWaves` |
| The data: enemies (raw disc record), Arts, bosses | Phase 1 — `DT_Enemies`, `DT_Arts`, `DT_Bosses` |
| What each version does with an enemy's record: stat boost, victory EXP and Gold | `UJRPGBattleRules` + `EJRPGRuleset` ([Version rules](#version-rules--usa-or-japan)) |
| A turn's clip queue from `DT_Arts` | `UJRPGArtSequenceLibrary` (the Art tester plays it) |

Still stubs: `ApplyBattleItemEffect` (the inventory routes item use into it; the call arrives and
does nothing), `UArtsSubsystem` (only `TeachArtToCharacter`, a `UE_LOG("TODO")`). No turns,
no damage, no rewards yet — that is Phase 5.

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
| XP split among survivors, level-up with jitter | `UPartySubsystem::AwardBattleXP` (takes the **raw** EXP sum; applies the version and the difficulty) / `LevelUpMember` |
| Enemy stats installed and victory EXP / Gold, per version (USA / Japan) | `UJRPGBattleRules` (`InstallEnemyStats`, `VictoryEXPPool`, `VictoryEXPShare`, `VictoryGold`) + `UCoreSubsystem::GetRuleset` |
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
| **5** | Basic combat (turns, damage, death, victory) | Not started — after Phase 6 |
| **6** | Directional Arts + the new AP | **Next** (before Phase 5) |
| **7** | Remaining commands (item, escape, guard, drop / steal) and presentation (HUD, attack camera, sound) | Not started |
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
                         Phase 6 (Arts + new AP)       ← next
                                  ▼
                         Phase 5 (basic combat)
                                  ▼
                         Phase 7 (commands + presentation)
                                  ▼
                         Phase 8 (magic and Seru)
```

Phases 1 and 2 are independent and can run in parallel. From 3 onward it is sequential.

**Phase 6 before Phase 5 (decided 2026-11-04).** The Arts come first so the fight has them to
test with from its first turn. What that means for each phase:

- **Phase 6 does not wait for the combat core.** Recognition, learning, the learned list in the
  save and the AP rules are pure functions over the input and the hero, tested without a map;
  the recognized chain plays in the Art tester (`JRPGArtTester`, [Guides/ARTS.md](Guides/ARTS.md))
  through `UJRPGArtSequenceLibrary`. Nothing in it reads a combatant or deals damage.
- **The parts of Phase 6 that need a fight close in Phase 5**: the input window inside a turn
  (`CommandGauge` seeded from AGL), damage with the Art's power per hit, and AP moving on the
  combatant as its action reaches each Art.
- **The same rules apply to Phase 6** as to the combat core (door open): integer only, no global
  RNG, and the chain leaves the input window as one value (the Art bytes of the future
  `FJRPGBattleCommand`), never button by button.

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
American **scripted-fight** view (what the player fights in a boss fight on the USA disc), the
disc the raw record the battle loader boosts. The RE project pins the factors
(`crates/gamedata/tests/enemy_stats_vs_disc.rs`): HP, SPD, AGL ×1 · UDF, LDF ×2 · ATK ×5/4 ·
INT ×9/8 · EXP ×3/4 · Gold ×5/16 (±1 rounding; the Gold one includes the Golden Book).

**Decided (2026-11-04): the table stores the raw record**, and the factors are code. They are
one version's rules, not data: the USA executable boosts the stats (two profiles — scripted
fight and random encounter) and cuts EXP and Gold; the Japanese and European executables do
neither, on the same bytes. The first version of the table stored the TOML's numbers — the
scripted profile even for random encounters, the Golden Book in every Gold, and an EXP that
`AwardBattleXP` would have cut by 3/4 a second time. See
[Version rules](#version-rules--usa-or-japan). The generator still converts the record and
compares it with the TOML: that check is what proves the name join picked the right record.

### New structs

Hard rule: `Extras/LegaiaStudio/studio.py` discovers schemas by regex over the headers.
One `UPROPERTY` per line, no `meta=(...)` with nested parentheses, struct closing with
`\n};` and inheriting `: public FTableRowBase`. Use `Public/Shop/ShopData.h` as the model.

**`Public/Battle/EnemyData.h`** — **done** (2026-10-26). Column reference in
[Guides/ENEMIES.md](Guides/ENEMIES.md).

```cpp
USTRUCT(BlueprintType) struct FJRPGEnemyData : public FTableRowBase {
  FName Key; FText DisplayName; FString Location; bool bIsBoss; EJRPGElement Element;
  int32 BaseHP, BaseMP, BaseAGL, BaseATK, BaseUDF, BaseLDF, BaseSPD, BaseINT;  // raw record
  int32 EXP, Gold;                        // raw record
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
  TArray<int32>   Directions;   // NOT kept: the struct has Command only (see above)
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

> **Written with the door open to PvP / online / co-op (decided 2026-11-04).** It has to work
> locally (single player, and local co-op stays possible) and leave the base ready for PvP
> later, with no network code in Phases 5–8. So the core is
> **commands and inputs in → resolution on the logic timeline → events out**, and
> [Phase 5 with the door open](#phase-5-with-the-door-open--what-changes-in-the-plan) is the
> plan: the sketches below are written that way, and the table in
> [What the core cannot do](#what-the-core-cannot-do) is binding. Comes **after Phase 6**
> ([Phases](#phases)), so the Arts are there to test from the first turn.

### 5.0 — Contracts first

The first step of Phase 5, before any turn is played: write down (in this section, then in code
and tests) what each piece promises, so the core does not grow silent assumptions. They hold for
single player and PvP alike.

**Data and the version's rules — done (2026-11-04).** `DT_Enemies` is the raw disc record;
`UJRPGBattleRules` installs the stats and computes EXP / Gold for the rules picked by
`EJRPGRuleset` ([Version rules](#version-rules--usa-or-japan)). `BeginBattle` snapshots each
enemy through `InstallEnemyStats(row, Setup.bScriptedFight, ruleset)` and then the difficulty
(`ScaleEnemyHP/ATK/DEF`). `FJRPGBattleSetup` gains `bScriptedFight` (and `Seed`): the original's
flag is per formation, not per boss, so `DT_Bosses` gets a column for it (Tetsu's training and
Lapis are scripted fights with the random-encounter profile).

**The round.** The original's: **every hero's command is entered first, then the round plays
out** in initiative order (enemies included). That is a gameplay decision, not only a network
one — "each fighter chooses when its turn comes" would be a departure and would go in
"Deliberate departures". A round is `Commands → Execution`; `GetActiveSlot` / `OnSlotChanged`
mean "whose command is being entered" during Commands and "who is acting" during Execution.

**The logic timeline** ("State machine" below says why):

| Question | Contract |
|---|---|
| Where an action's frames come from | Built **when the action starts**, before anything plays: from each clip's asset data (length, rate, hit frames from the notifies) into a plain `FJRPGActionTimeline` (total frames, hit frames, chain-cut frames). Validated there: a hit frame past the clip's end, or a clip without hit notifies where `HitPower` says there are hits, is logged |
| A clip that is missing or broken | The timeline falls back to defaults per kind (swing, Art, enemy attack: one hit at a fixed frame, a fixed length), logs it, and the fight goes on. The fallback is data, not a guess at play time |
| When the damage is committed | At the hit's frame, by `ResolveHit(N)`, with every input received up to that frame. Never earlier (the parry) and never by a notify |
| When a parry input counts | Only inside the hit's window (open question 5), stamped with the frame it arrived on; one input per hit; anything outside is ignored, not queued |
| Several hits in one action | Each has its own frame, window and `ResolveHit`; a parry answers one hit |
| Pause / menu | The counter does not advance while the game is paused (single player). Nothing is resolved during a pause |
| Animation that lags | Presentation only: the pawn catches up to the counter, and shows the events already emitted. The logic never waits for a notify |

**Items.** `UInventorySubsystem::UseItem` removes the item **before** routing the effect. The
battle does not call it at command time: choosing Item **reserves** it (counted against the
stack, so two heroes cannot use the last one); the item is **consumed when its action
resolves**; an action that does not happen (the user died, the fight ended, the target is gone
and the item is single-target with no retarget) releases the reservation. The effect goes
through the battle (`ApplyBattleItemEffect` on the combatant snapshot), not the field path.

**Conditions.** Today a condition is a name in `FJRPGPartyMember::Conditions`. The battle needs
duration (rounds or "until cured"), the moment it acts (start / end of the bearer's turn), its
per-turn effect (Poison's damage, Sleep's skipped turn), expiry, and which ones survive the
fight. The contract is written in Phase 5 (a condition struct on the combatant, ticked by the
resolution); the conditions themselves come with Phase 8.

**End of the fight** — what each result writes back:

| | HP / MP | AP | Items used | EXP / Gold / drop | Then |
|---|---|---|---|---|---|
| **Victory** | Kept as they are | Kept | Gone | Paid once | Back to the field |
| **Defeat** | — | — | — | Nothing | Game over (Muscle Dome: the waves stop) |
| **Escaped** | Kept | Kept | Gone | Nothing | Back to the field, grace period |
| **Aborted** (debug, map change) | Untouched: as before the fight | Untouched | Not consumed | Nothing | Back to the field |

So the snapshot is written back only on Victory and Escaped, and `Aborted` throws it away.
**Rewards are paid once**: a flag in the battle state, set when they are paid, checked by
`EndBattle` (a second `EndBattle`, or a victory that arrives twice from two last hits, pays
nothing more). Some original fights pay nothing at all (`_DAT_8007BAC0`, the special battles):
`FJRPGBattleSetup` gets `bNoRewards`.

**Who gets the EXP.** `AwardBattleXP` walks the **active** formation and reads who is dead from
the roster, but the fight uses `Setup.Party` when it is given (the Muscle Dome gives it) and
keeps HP in the combatants. So, in this order: write the survivors' HP back to the roster, then
pay EXP to **the fight's heroes** who are alive — `AwardBattleXP` takes the participants
(an overload with `TArray<FName>`). The sum it gets is the raw EXP of the defeated enemies
(`DT_Enemies.EXP`); the version and the difficulty are applied inside. Gold:
`VictoryGold(raw golds, Golden Book, ruleset)`, then `ScaleGoldReward`, then `AddGold`.

### `Public/Battle/BattleTypes.h`

```cpp
UENUM(BlueprintType) enum class EJRPGBattleSide       : uint8 { Party, Enemy };          // exists
UENUM(BlueprintType) enum class EJRPGBattleCommandKind: uint8 { None, Attack, Arts, Item, Magic, Escape, Guard };
UENUM(BlueprintType) enum class EJRPGBattleInputKind  : uint8 { Parry };
UENUM(BlueprintType) enum class EJRPGBattleEventType  : uint8 {   // the UI builds the text
    RoundStart, ActionStart, Damage, Miss, Heal, Death, APChanged, ArtRecognized, ArtLearned, ActionEnd
};
UENUM(BlueprintType) enum class EJRPGActionPhase      : uint8 {   // presentation: lives in the pawn
    Idle, Windup, Advance, Strike, Chain, Recovery, Return, Done
};

// Exists since Phase 4 with SlotIndex, Side, SideIndex, Type, Key, Level; Phase 5 adds the rest.
// Rules only, all integer: no phase, no timer, nothing the pawn shows. A snapshot taken at
// BeginBattle — resolution never reads UPartySubsystem / UStatusSubsystem / UInventorySubsystem.
USTRUCT(BlueprintType) struct FJRPGCombatant {
  int32 SlotIndex; EJRPGBattleSide Side; int32 SideIndex; EJRPGCombatantType Type;
  int32 Team;                 // the rules ask "same team?"; victory = a whole team is down
  int32 OwnerPlayer = -1;     // who enters its commands; -1 = AI
  FName Key;                  // party member, seru, or DT_Enemies row
  int32 Level;
  int32 HP, MaxHP, MP, MaxMP;
  int32 ATK, UDF, LDF, SPD, INT, AGL;   // heroes: base + Ra-Seru bonus; enemies: InstallEnemyStats + difficulty
  TArray<int32> EquipATK;     // each piece's ATK apart, folded per command at the hit
  EJRPGElement Element;
  int32 AP;                   // persistent meter (Arts add, specials spend)
  int32 CommandGauge;         // per-turn input budget, seeded from AGL
  int32 InitiativeKey;
  int32 TargetSlot; int32 JuggleCounter; bool bGuarding; bool bAlive;
  TArray<FJRPGBattleCondition> Conditions;   // 5.0 › Conditions
};

// One command, one entry point: checked against who owns the slot, never trusted (rule 22).
USTRUCT(BlueprintType) struct FJRPGBattleCommand {
  int32 PlayerIndex, Slot; EJRPGBattleCommandKind Kind; int32 TargetSlot;
  TArray<uint8> ArtBytes;     // the whole chain from the input window (Phase 6)
  FName ItemKey;
};

// What arrives during execution, stamped with the logic frame.
USTRUCT(BlueprintType) struct FJRPGBattleInput {
  int32 PlayerIndex, Slot; EJRPGBattleInputKind Kind; int32 HitIndex, Frame;
};

USTRUCT(BlueprintType) struct FJRPGBattleEvent {
  EJRPGBattleEventType Type;
  int32 SlotIndex, TargetSlot, Value;
  int32 ActionIndex, HitIndex, Frame;   // the presentation shows it when its clip gets there
};

// The whole fight in one struct: same seed + same commands + same inputs = same events.
USTRUCT(BlueprintType) struct FJRPGBattleState {
  TArray<FJRPGCombatant> Combatants; FRandomStream Rng;   // rules only; cosmetic randomness uses another
  int32 Round; TArray<int32> TurnOrder; int32 Frame;
  bool bRewardsPaid;
};
```

### `UBattleSubsystem`, expanded

`UGameInstanceSubsystem` + `FTickableGameObject` (`IsTickable()` true only during a fight,
`ETickableTickType::Conditional`), ticking as a **fixed-step accumulator** that calls
`StepFrame()` — the raw `DeltaTime` never reaches the rules.

```cpp
bool BeginBattle(const FJRPGBattleSetup& Setup);   // exists; Setup gains Seed, bScriptedFight, bNoRewards,
                                                   // and teams carrying their snapshots (the opponent's party arrives as data)
void EndBattle(EJRPGBattleResult Result);         // exists: Victory / Defeat / Escaped / Aborted;
                                                   // writes back only into the local player's members
bool SubmitCommand(const FJRPGBattleCommand& Command);   // checks the issuer owns the slot
bool SubmitInput(const FJRPGBattleInput& Input);         // the parry, during execution
void StepFrame();                                  // one fixed frame; ResolveHit(N) when a hit's frame comes
const FJRPGBattleState& GetState() const;
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
leave room to tune — sorting by descending key is what produces the original's feel. The `roll`
comes from the state's `Rng`; `TArray::Sort` is not stable, so ties break explicitly by
`SlotIndex`.

### Damage formula

The original resolves it as follows, and it is a good base because the *feel* of combat
depends on the shape of it:

```
BaseOffense = ATK_base + (ATK of the equipment that command uses) / 2
Offense     = (BaseOffense + rand % (BaseOffense/8 + 1)) * Power / 16
              + CurrentHP / 256
              + (juggle * ATK) / 64
If Art:       Offense = Offense * 13 / 10
Element:      * 104 / 100 on opposed pairs, * 96 / 100 on same element (integer,
              not the float from GetElementMultiplier)
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
constants — rebalancing should not mean recompiling the state machine. They take the
`FRandomStream&` and the combatant snapshot, never a subsystem.

Art power is per hit: `0x16–0x1A` → UDF × 12/18/20/22/28, `0x1B–0x1F` → LDF × the same,
`0x0C–0x15` → the same scale in the "alt" range (misses floating or short enemies). So
`bTargetsUDF` comes from the hit's own power byte, not from the command.

### State machine

`EJRPGActionPhase`, short and readable, played by the pawn **in step with the logic timeline**
(below) — not driven by its own timers or by AnimNotify:
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
the `Strike` notify shows the event the logic already emitted. A clip that lags only shows late,
and a missing one falls back to default frames (5.0 › The logic timeline) — the fight never
hangs on a notify.

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

See also [PvP, online and co-op](#pvp-online-and-co-op--door-open-decided-2026-11-04).

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
It is a step of the resolution: it runs when the enemy's action starts, draws from the fight's
`Rng`, and finds targets by **team**, not by `Side`.

**Acceptance:** touch a Gimard → turn order computed → choose Attack → animation plays,
damage shows, HP drops → the enemy retaliates → kill it → `Defeat` animation, victory, XP
distributed through `AwardBattleXP` (raw sum; with `USA` and three heroes, a Gimard's 55 EXP
pays 14 each), back to the map, pawn gone.

And the failures, each an automation test where it can be one (no map):

| Case | Expected |
|---|---|
| The target dies before the action | Retarget to another living combatant on that side (check the original's rule in the RE first); none left → the action is dropped, nothing consumed |
| The attacker dies before its turn | Its command is dropped; a reserved item is released |
| Two equal initiative keys | A fixed order (ties by `SlotIndex`), the same every run |
| The last enemy dies in the middle of a combo | The rest of the chain does not resolve; victory once the action ends |
| A clip is missing | The default frames; the fight goes on, a log line says which |
| The fight ends during a transition (fade, a second `EndBattle`) | Ends once; rewards once |
| `Aborted` | The party exactly as before the fight; no item consumed, no reward |
| Easy (Japan rules) | `diff.set 3`: the same fight installs the raw stats and pays the full EXP |
| Determinism (no map) | The same seed, the same commands and the same inputs at the same frames produce exactly the same event list — the test that guarantees PvP later |
| A command for a slot the issuer does not own | Rejected; nothing changes |

---

## Phase 6 — Directional Arts and the new AP

**Goal:** the heart of Legaia, with the AP inversion.

**Comes before Phase 5** (decided 2026-11-04, [Phases](#phases)): the recognition, learning and
AP rules are written and tested on their own, and the chain plays in the Art tester; what needs a
fight (the window inside a turn, damage, AP on the combatant) closes in Phase 5. The acceptance
below is split that way.

### Two meters, not one

| Meter | Scope | Role |
|---|---|---|
| `CommandGauge` | Per turn, seeded from `AGL` | Caps how many directional commands fit in a turn. This is what makes high AGL mean long combos — the original's feel |
| `AP` | Persistent, `0..100`, already in `FJRPGPartyMember` | **Regular Arts add. Hyper / Super / Miracle spend.** This is the improvement |

The original spent AP on every command, which is the complaint this remake fixes. Here the
player builds AP by performing Arts and burns it on the big ones.

### `UArtsSubsystem`

```cpp
static EJRPGArtButton ButtonForPad(FName Character, EPadButton Pad);
// Noa is left-handed: her Arms is the right button (resolved here, never stored)

// The Arts the buttons perform, overlapping as in the original; the unknown ones it meets
// come back flagged, and LearnArt adds them
TArray<FJRPGRecognizedArt> RecognizeArtSequence(FName Character, const TArray<EJRPGArtButton>& Input) const;
TArray<FName> GetKnownArts(FName Character) const;
bool HasLearnedArt(FName Character, FName ArtKey) const;   // BlueprintPure: dialogue / triggers branch on it
bool LearnArt(FName Character, FName ArtKey);              // true = newly learned (on use, NPC, trigger, book)
void TeachArtToCharacter(const FString& CharacterName, FName ArtKey);   // preserved; calls LearnArt
```

### How each kind of Art is unlocked (decided 2026-11-04)

The original's rules (RE `docs/subsystems/battle-action-queue.md` › `FUN_801EFBFC`,
`docs/formats/art-data.md` › "Super Arts", `docs/subsystems/inventory.md` › "The Hyper-Art
books"), kept as they are:

| Kind | Unlocked by | In the original |
|---|---|---|
| **Regular** | Performing it: an unknown Art whose buttons the player enters is learned on that use (and plays as that Art, with the "learned" banner once). Or taught at any moment | `FUN_801EFBFC` inserts any id above the per-hero innate cap when the learn gate is open (otherwise a 1/512 roll) |
| **Hyper** | **Only** taught: the Hyper-Art books, or any NPC / trigger. Entering its buttons without knowing it does nothing special (plain swings / the regular Arts inside it) | The Hyper ids sit under the innate cap, so learn-on-use never takes them; the books (`0x8F..0x97`, Fire / Wind / Thunder Book I–III, one line per hero) insert them |
| **Super** | Not learned: it fires when **every Art in its chain is already known** before the turn. If the input unlocks one of them, that one is learned and plays, but the Super **does not complete** that turn; the next turn it does | The Super find/replace runs on the finished queue and needs every Art in it to be non-NEW: a newly learned Art is staged as `0x1A`, not `0x19`, so the find pattern does not match |
| **Miracle** | Gated by the Ra-Seru, not by a learned list | `ctx[+0x25F + slot]` is raised from the hero's Ra-Seru equipment byte at battle load |

So the flow on commit: recognize (overlap rule) → each recognized Art is checked against the
learned list *as it was when the turn started* → Regulars not known are learned now (and count
as new for the rest of this turn) → a Super is applied only over Arts that were known before the
turn → specials checked against AP (open question 6).

- **Teaching at any moment.** `LearnArt` is BlueprintCallable and works out of battle: an NPC,
  a trigger, a cutscene or a book teaches an Art that the player never discovered. It returns
  whether the Art is new, so the caller can show the notice once.
- **`HasLearnedArt`** is the check for dialogue and triggers ("teach Vahn Hyper Elbow" only when
  he does not have it; another line when he does).
- **The 1/512 roll.** The original's learn gate (`actor[+0x266]`) has no known meaning and reads
  open, so performing an unknown Regular always teaches it. The roll stays as a tunable
  (`LearnOnUseChance`, default 100%) in case a stricter mode wants it, drawn from the fight's
  `Rng`.

Recognition follows the original's tokenizer (`FUN_801EED1C`; RE `crates/art/src/tokenize.rs`,
`docs/subsystems/battle-action-queue.md` › "Normalize arrows into art constants"), **not** a
consuming longest-match:

- **Arts overlap.** The original walks the input tail-first and restarts right after each
  match, so one button can belong to two Arts: Tri-Somersault is seven buttons because
  Somersault, Cyclone and Somersault share their ends. A greedy match that *consumes* the
  input would see Somersault and then lose the Arts that start on its last button. `ARTS.md`
  already says it for the Supers ("one's last button is the next one's first").
- **Over every Art of the character, known or not**, matched on `Command` (`DT_Arts`; there is
  no `Directions` column — Noa's mirroring is resolved when the button is read). An Art the
  hero did not know is **learned by performing it** — the original's learn-on-use
  (`FUN_801EFBFC`: it inserts the Art in the hero's learned list and stages it with `0x1A`
  instead of `0x19`, so the "learned" banner shows once). Only Regular Arts are learned this way
  ([How each kind of Art is unlocked](#how-each-kind-of-art-is-unlocked-decided-2026-11-04)).
- **The learned list is saved.** It is per hero and goes into the save (a `TArray<FName>` of
  `DT_Arts` keys in `FJRPGPartyMember`, written by `UArtsSubsystem` only; `SaveVersion` goes up).
  `LearnArt` / `TeachArtToCharacter` (books, NPCs, triggers) add to the same list.
- A button that starts no Art is a plain swing (the connector).

Validation for the generator: recompute each Super's whole input from its `Sequence` with the
overlap rule and compare it with its `Command` (the RE derives it the same way,
`derive_super_input`).

### The input window

Choosing Arts opens the window, and the window is **local**: each button debits `CommandCost`
from `CommandGauge` and pushes a byte, with no round trip to the rules. `CommitArtsChain` closes
it and sends the whole chain as **one** command (`FJRPGBattleCommand` with `ArtBytes`, through
`SubmitCommand`); the rules recognize it again from the bytes (never trust the payload) and turn
it into the turn's queue (`UJRPGArtSequenceLibrary`). Before Phase 5, the Art tester plays the
same path without a fight: a gauge of its own, commit, recognition, queue.

When AP moves, which the inversion makes a real decision (the original paid AP per Art at
recognition time, `+0x170`). **Proposed, to confirm with the user** (open question 6):

| Moment | What happens |
|---|---|
| **Commit** (the chain is recognized) | Each special (Hyper / Super / Miracle) is **checked** against the AP the hero has at commit, plus the `APGain` of the regular Arts that come before it in the same chain. One that does not fit is downgraded to its buttons (plain swings / the regular Arts inside it). Nothing is debited yet |
| **Each Art, when its action reaches it** | A regular Art **credits** `APGain` when its first hit resolves (an Art cut short by the enemy's death still credits); a special **debits** `APCost` when it starts |
| **The action is cancelled** (the hero dies, the fight ends) | What was not reached is neither credited nor debited |

So AP gained early in a chain **can** pay for a special at its end — that is the point of the
inversion (build, then burn), and it is what makes "Commit" check against the running total.
Whether to keep it is open question 6; if not, the check at commit uses the AP the hero has.

Hyper / Super / Miracle appear **unavailable** (greyed out) when AP does not cover
`APCost` — the feedback has to be visible before the player builds the sequence. With the
running total, the list updates as the chain is typed (a special lights up once the regular
Arts before it pay for it).

**Acceptance, in Phase 6** (Art tester + automation tests, no fight): enter `Arms, Ra-Seru,
High` as Vahn → "Hyper Elbow" is recognized and the right animation plays; Noa's mirrored input
gives her Arts; the overlap case (Tri-Somersault from its seven buttons) recognizes every Art in
it; an unknown Regular is learned once and survives a save / load; an unknown Hyper's buttons
teach nothing, and `LearnArt` (the book / NPC path) teaches it, with `HasLearnedArt` flipping to
true; Tri-Somersault entered while Cyclone is unknown learns Cyclone and plays the three Arts
without the Super, and the same input on the next turn fires the Super; each Super's `Command` matches
the one recomputed from its `Sequence`; the AP check at commit downgrades a special that does not
fit and lets one paid by the regular Arts before it through (if open question 6 keeps that).

**Acceptance, closed in Phase 5** (in a fight): damage uses the Art's power per hit and AP
**rises**; fill AP and fire a Super Art → AP **drops** and damage is much larger; with
insufficient AP, the Super shows as blocked.

---

## Phase 7 — Remaining commands and presentation

**Goal:** every command works, and the battle stops looking like a prototype.

Two kinds of work, kept apart: the **rules** go through the combat core like Phase 5's
(commands in, resolution, events out, automation tests without a map, the 5.0 contracts); the
**presentation** only consumes events.

### Rules

- **Item** — 5.0 › Items: reserved at command time, consumed when its action resolves, the
  effect applied to the combatant through `ApplyBattleItemEffect` (still a stub today).
- **Escape** — the roll (party SPD vs enemies; the RE has the original's), `EndBattle(Escaped)`
  and what it keeps (5.0 › End of the fight). Not in scripted fights.
- **Guard** — what it changes in the damage (the kernel already reads `bGuarding`) and how long.
- **Rewards beyond EXP / Gold** — the drop roll (at most one item per fight, the original's
  `0x8004F3D8`) and the steal; `DropItem` / `StealItem` are already in `DT_Enemies`.

### Presentation

- **HUD in `shell.html`.** A new `battle` screen: party HP/MP/AP strip, enemy row, the AP
  bar with its cost markers, damage log. Add `Battle` to `EJRPGUIState` **at the end of the
  enum** (values are serialized in Blueprints). New `UWebUIBridge` callbacks:
  `OnBattleCommand`, `OnBattleArtsButton`, `OnBattleArtsCommit`, `OnBattleTargetChanged`.
  C++→JS always goes through `UWebUISubsystem`, never `ExecuteJS` from `UBattleSubsystem`.
- **Attack camera.** `FocusOnActor` on turn start, `PlayCameraShake` on `Strike`, `Orbit`
  on victory.
- **Sound.** Impacts, Art voice lines and battle BGM extracted via the RE project's `vab` /
  `xa` crates, wired into `UAudioSubsystem`.
- **The victory screen** — EXP, Gold and the item, from the events the rules emitted.

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

## Version rules — USA or Japan

**Decided 2026-11-04.** The monster records are byte-identical on every disc; the executables
differ (RE `docs/subsystems/battle.md` › "Battle-load stat boost" and "No boost on the PAL
executables", `docs/subsystems/battle-formulas.md` › "Regional difference"):

| | USA (SCUS_942.54) | Japan (SCPS_100.59) and PAL (SCES_019.44/45/46) |
|---|---|---|
| Enemy stats when the fight loads | Boosted: scripted fight ATK ×5/4, UDF/LDF ×2, INT ×9/8; random encounter UDF/LDF ×7/4, INT ×5/4 | The record |
| EXP | Sum × 3/4, split among the living rounding up | The sum, split the same way |
| Gold | Σ `gold >> 1`, Golden Book +¼, halved again | Σ `gold >> 1`, Golden Book +¼ |

So the framework keeps the record as data and the rules as code:

- **`DT_Enemies` is the raw record** (generator, 2026-11-04; rule 53 in
  [ARCHITECTURE.md](ARCHITECTURE.md)).
- **`EJRPGRuleset { USA, Japan }`, picked by the difficulty** (decided 2026-11-04):
  `FDifficultyScaling::Ruleset`, read with `UCoreSubsystem::GetRuleset`. A new difficulty,
  **Easy, is the Japanese original** (rules `Japan`, every multiplier 1.0); **Normal is the
  American original** (`USA`, 1.0); Hard and Juggernaut are `USA` with their multipliers on top.
  Easy is the last value of `EJRPGDifficulty` (rule 28), so "harder than" goes through
  `JRPGDifficulty::Rank` (Terra's `MinDifficulty` through `GrowthRank`, where Easy counts as
  Normal). The player picks it on the Options screen, listed Easy → Juggernaut.
- **`UJRPGBattleRules`** does the arithmetic (`InstallEnemyStats`, `VictoryEXPPool`,
  `VictoryEXPShare`, `VictoryGold`), integer and truncating; `AwardBattleXP` already uses it.
  Checked against the RE's captures in [Guides/ENEMIES.md](Guides/ENEMIES.md#version-rules-usa--japan).

Like the difficulty, it is not in the save yet. Other regional differences the RE documents (the
Seru-magic debuff lockout on bosses is a product of the USA boost alone) follow from the same
switch when Phase 8 gets there.

---

## PvP, online and co-op — door open (decided 2026-11-04)

**Decided 2026-11-04 (the user):** the fight has to work **locally** and leave the **base ready
for PvP later**. So the combat core keeps the door open — Phase 5 is written as
[Phase 5 with the door open](#phase-5-with-the-door-open--what-changes-in-the-plan), and
[What the core cannot do](#what-the-core-cannot-do) is binding for Phases 5–8 (and for Phase 6,
which comes first). The modes themselves are still **not planned**: no replication, lobbies, EOS
or second-player input is built in Phases 5–8. (Raised 2026-10-31.)

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

### Why it was decided before Phase 5

A turn-based fight is the easiest genre to network: only **commands** travel ("Vahn: Arms,
High, Low → slot 2"), never positions — plus, with the parry, the defender's timed press. The
host runs the fight's timeline and sends each hit's events as they happen; the clients only play
animations. That works only if the core is written as
**commands and inputs in → resolution on the logic timeline → events out**, with the
presentation consuming the events. Writing
it that way costs almost nothing in Phase 5 (it is the same code, organised differently, and
it also makes the core testable without opening a map). Converting a core whose damage comes
out of animation notifies means rewriting the resolution.

### What the core cannot do

| Do not | Because | Do instead |
|---|---|---|
| Apply damage, HP, deaths or AP in an AnimNotify or timer callback | Host and clients do not run their animations in sync (frame rate, hitches, montage blends) | `ResolveHit(N)` on the logic timeline emits `FJRPGBattleEvent`s; `Strike` and `JRPGAnimNotify_ChainCut` only *show* them |
| Roll with `FMath::Rand` / `FMath::RandRange` or any global stream | Each machine rolls something different | One `FRandomStream` inside the battle state, seeded from `BeginBattle`'s `Seed`; only the resolution consumes it. Cosmetic randomness (which flinch clip, particles) uses a separate stream |
| Keep presentation in the combatant (`EJRPGActionPhase Phase`, `float PhaseTimer` in `FJRPGCombatant`) | Ties the rules to frame time, and floats diverge between machines | `FJRPGCombatant` holds rules only, all integer; phase and timers live in the pawn / presentation — already the plan either way (Phase 5's sketch) |
| Let the UI bridge mutate the battle directly (`SubmitCommand` straight from `UWebUIBridge`) | Online, the command has to travel to the host first; local co-op needs to know who sent it | Every command is a value struct (`FJRPGBattleCommand`: issuing player, slot, command, target, Art bytes) through one entry point that checks the issuer owns the slot and validates the rest (rule 22: never trust the payload) |
| Send an Arts chain button by button to the rules | Each button would be a round trip | The input window stays local; `CommitArtsChain` sends the whole chain as one command |
| Read `UPartySubsystem` / `UInventorySubsystem` / `UStatusSubsystem` during resolution | In PvP the opponent's party is not in your GameInstance | Snapshot everything into the combatants at `BeginBattle`; resolution reads only the battle state; write back (HP, XP, items used) at `EndBattle` |
| Use `GetFirstPlayerController()` in battle code (`AJRPGBattleArena`, `UJRPGBattleSceneSubsystem` do it today) | On a listen server it is always the host's controller | Pass the player explicitly; which player owns which slot lives in the battle state |
| Let positions feed the rules (traces, distance after root motion, "staying in the arena") | Actor positions differ per machine | Targeting, range and hits are decided by slot; spacing and arena clamping stay presentation |
| Advance the logic by the frame's `DeltaTime`, or let time dilation / a menu pause stop it | Variable steps land a hit on a different frame per machine; the host cannot be paused for everyone | A fixed-step integer frame counter owned by the battle; online it never pauses (single player may) |
| Run enemy AI outside the resolution | Each machine would pick different targets | AI is a step of the resolution, using the battle stream, on the host only |
| Judge a parry on the host's clock | The defender sees the attack late by the network delay; a press on time on their screen would arrive late | The defender's machine judges the press against its own playback and sends "parried hit N"; the host holds hit N until that input or a deadline (see "The transport") |

Phase 5 does **not** add replication, lobbies or EOS — it only keeps the core from closing the
door.

### Phase 5 with the door open — what changes in the plan

The rules themselves stay: the damage formula, the turn order, the dumb AI and "Staying in the
arena" are the same. What changes is how Phase 5 is organised, item by item. On screen, in
single player, the result is identical. The Phase 5 sketches were rewritten this way on
2026-11-04; this list keeps what changed from the closed-door version, and why.

**The round.** Already `Commands → Execution` either way (5.0 › The round, the original's
model). With the door open, Commands are also **hidden from the other side**:

```
Commands (everyone chooses; hidden from the other side) → Execution (the logic timeline, hit by hit; parries arrive during it)
```

In PvP both sides choose at the same time without seeing each other.

**`BattleTypes.h`**

| Door closed (the old sketch) | Door open (the plan) |
|---|---|
| `EJRPGBattleSide { Party, Enemy }` | Stays, but the rules ask "same team?": add `int32 Team` and `int32 OwnerPlayer` (-1 = AI). Victory is "a whole team is down", not "the enemies are dead" |
| `EJRPGActionPhase` | Already out of the combatant, in `AJRPGBattlePawn` (decided either way, 2026-11-04) |
| Stats read through `GetEffectiveStat` at the moment of the hit | A **snapshot** at `BeginBattle`: base ATK + Ra-Seru bonus, and each equipment piece's ATK kept separate. This also retires the "ATK double-counted" risk |
| `FJRPGBattleEvent { FName EventType; …; FText Text }` | `EventType` becomes an enum; `FText` goes (the UI builds the text); add `ActionIndex` / `HitIndex` so a notify knows which event to show |
| — | **`FJRPGBattleCommand`** `{ PlayerIndex, Slot, Command, TargetSlot, ArtBytes, ItemKey }` |
| — | **`FJRPGBattleInput`** `{ PlayerIndex, Slot, Kind (Parry…), HitIndex, Frame }` — what arrives during execution |
| — | **`FJRPGBattleState`** `{ Combatants, FRandomStream Rng, Round, TurnOrder, Frame }` — the whole fight in one struct, the logic timeline's counter included |

**`UBattleSubsystem`**

| Door closed (the old sketch) | Door open (the plan) |
|---|---|
| `BeginBattle(Setup)` | Stays, with `Seed` (5.0), and `TArray<FName> Party` (read from the local `UPartySubsystem`) becomes teams carrying their snapshots — the opponent's party arrives as data, not from your save |
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
| A version's factor applied twice, or baked into a table | **High**, invisible — the first `DT_Enemies` held EXP × 3/4 and `AwardBattleXP` cut it again (≈56% of the EXP) | The table is the raw record; every factor lives once, in `UJRPGBattleRules` (rule 53). Tests pin the RE's captures (Gaza, Gobu Gobu, Gimard, Zeto) |
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
6. **AP in a chain** (Phase 6 › The input window, proposed): can AP gained by the regular Arts
   early in a chain pay for a special at its end (the proposal says yes); when exactly a regular
   Art credits (its first hit, its end) and a special debits (its start).
7. ~~Learning Arts by performing them~~ — settled 2026-11-04, the original's rules: Regulars on
   use (and they play as the Art), Hypers only taught (books, NPCs, triggers), a Super only over
   Arts known before the turn, Miracle by the Ra-Seru; `HasLearnedArt` for dialogue
   ([How each kind of Art is unlocked](#how-each-kind-of-art-is-unlocked-decided-2026-11-04)).
8. ~~The version switch~~ — settled 2026-11-04: the difficulty picks it (Easy = Japan,
   Normal = USA; Version rules). Whether the difficulty goes into the save is a separate,
   older question.

---

## Next concrete step

**Phase 6 (next):** `UArtsSubsystem` — `ButtonForPad` (Noa mirrored), recognition with the
original's overlap, learn-on-use and the learned list in the save, the AP check at commit — as
pure functions with automation tests, then the input window and the chain in the Art tester.
Open question 6 (AP in a chain) is to settle with the user at the start; how each kind of Art
is unlocked is settled.

**Phase 5 (after 6), door open** ([decided 2026-11-04](#pvp-online-and-co-op--door-open-decided-2026-11-04)):
first the contracts (5.0 — the round, the logic timeline and its fallback, items,
end of the fight, who gets the EXP), then basic combat on the Muscle Dome — combatant stats
through the version rules, the original's turn order, the integer damage kernel, the action
state machine, a dumb enemy AI, victory / defeat. In parallel, the party's battle clips
(attack, hit, KO) that it needs.
