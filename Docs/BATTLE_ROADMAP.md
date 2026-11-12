# Battle System Roadmap

**JRPGFramework — Legend of Legaia Remake**
Approved 2026-09-13. Phases 0 to 4 are done (2026-10-26 to 2026-10-30): the battle data (`DT_Enemies`,
`DT_Arts`, `DT_Bosses`), every monster and the party's battle clips in Unreal, enemies roaming the
map and a fight assembled over it. Phase 6, the Arts, is done without a fight (2026-11-04:
recognition, learning, the Supers, the new AP, the validator, the Art Tester's `Input`). Phase 5,
combat itself, which closes Phase 6's in-fight part (order swapped 2026-11-04: [Phases](#phases)), is
**played**: written 2026-11-04, built and tested in PIE, merged 2026-11-07 (PR #14;
[Guides/BATTLE.md](Guides/BATTLE.md)). The bugs still open are in
[BATTLE_PHASE5_BUGS.md](BATTLE_PHASE5_BUGS.md) and go in a later fix. Part of Phase 7 came with it
(Spirit, formation advantage, the field-to-battle transition, the battle camera, voices, music, the
command menu). The combat core keeps the door open to PvP / online / co-op
([decided 2026-11-04](#pvp-online-and-co-op--door-open-decided-2026-11-04)).

The battle is the largest system of the plugin. What is left of it: the rest of Phase 7 (items,
the escape roll, drop / steal, the rest of the HUD, VFX) and Phase 8 (magic
and Seru). (Dialogue, Cutscene and Localization are also unwritten - see
[STRUCTURE.md](STRUCTURE.md).) Everything
else — Core, Save/Load, Inventory, World State, Shop, Audio, Camera, Party, Status, WebUI —
is done and is consumed by the battle system rather than rewritten.

---

## Current state

The fight was **assembled but not fought** until Phase 5 was written (2026-11-04: it now is played, see
[Phase 5](#phase-5--basic-combat); the table is what Phase 5 started from):

| Exists | Where |
|---|---|
| `BeginBattle(FJRPGBattleSetup)` / `EndBattle(EJRPGBattleResult)` (Victory, Defeat, Escaped, Aborted), the combatant list (who and where, no stats yet), `OnBattleStarted` / `OnBattleEnded` | `UBattleSubsystem` |
| The arena, the pawns in their seats, the fade, the Muscle Dome waves | Phase 4 — `AJRPGBattleArena`, `UJRPGBattleSceneSubsystem`, `AJRPGBattlePawn`, `AJRPGBattleWaves` |
| The data: enemies (raw disc record), Arts, bosses | Phase 1 — `DT_Enemies`, `DT_Arts`, `DT_Bosses` |
| What each version does with an enemy's record: stat boost, victory EXP and Gold | `UJRPGBattleRules` + `EJRPGRuleset` ([Version rules](#version-rules--usa-or-japan)) |
| A turn's clip queue from `DT_Arts` | `UJRPGArtSequenceLibrary` (the Art tester plays it) |
| The Arts: buttons → the turn (recognition with the original's overlap, Supers, the new AP), learning and the learned list in the save, the validator | Phase 6 — `UJRPGArtsRules`, `UArtsSubsystem`, `UJRPGArtsValidator`, the Art Tester's `Input` ([Guides/ARTS.md › 4](Guides/ARTS.md#4-recognition-learning-and-ap)) |

Still a stub: `ApplyBattleItemEffect` (the inventory routes item use into it; the call arrives and
does nothing; Phase 7). Phase 5 added the turns, the damage and the rewards.

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
| **Enemy count** | Up to 4 fixed seats | Configurable (`MaxEnemySlots`, 5 today); meant to grow with the difficulty and the party size |
| **Encounters** | Random while walking (mist) | Visible enemies roaming the map, with a chase radius and respawn |
| **Where fights happen** | In the field scene; only the camera changes | Same — battle layers on top of the existing maps |
| **Action state machine** | ~50 states (mixes logic, camera and effect VM) | 8 phases; presentation delegated to AnimMontage / Sequencer |
| **Parry** | None — the defender does nothing while it is hit | **Planned, design open** (2026-10-31): a timed button during the enemy's attack. Why it shapes Phase 5: [State machine](#state-machine); what is still to decide: [Open questions](#open-questions-to-settle-in-practice-not-on-paper) |
| **Angle and distance in the damage and the block** | The kernel reads them from the actors' positions: `(Angle * atk) >> 16` to the Offense, `(def * Distance) >> 10` to the Defense, and both in the block roll | Same terms and numbers (bug 26), but they arrive as an **input**: the scene measures them when the action starts and submits them (`SubmitApproach`); the rules clamp and log them and never read a position ([What the core cannot do](#what-the-core-cannot-do)). No input = 0 |
| **Where a Super is matched** | Only at the tail of the queue | Anywhere in the chain, so an Art can follow a Super. With today's gauge no chain is long enough for that to happen; it shows if the Arts blocks grow |
| **HP during a combo** | Written once, on the last connecting hit | Taken off at each hit (stops at 0), so the HUD can show it dropping while the combo lands. The target still only falls on the last connecting hit |
| **Making way for the attacker** | Every bystander steps off the attack line, near or far | Only who stands in the attacker's lane moves, and only as far as it takes to clear it (`bMakeWayOnlyInLane`; off = the original) |
| **Blend between clips** | None (only a 2–4 frame tween into the next clip) | Loops (idle, walk, down) blend in over `AnimBlendTime` (0.2 s), for a softer look; one-shot clips still cut |
| **Camera ghost** | Combatants in front of the lens turn see-through during the actions | Also on the menu shots (command, Attack, Spirit, target cursor) and on the target during the special starter (`bCameraGhostTargetOnStarter`) |
| **The original's bugs** | Initiative keys of `0x8000`+ never win (a mixed-sign compare); with every key at 0 the pick is a random seat; the RNG is shared with presentation | Not reproduced. The rules have their own stream; the presentation uses another |

**Everything not in this table follows the original as the RE documents it** (decided
2026-11-04): formulas, order of operations, truncation, the constants. A rule is changed only on
purpose, with a row here; it can be refined later, in play. When the roadmap is silent or vague
about a rule, the RE's version is the answer, not a guess.

### Classic and Modern combat (planned, not scheduled)

An option in the menu, wanted by the user (2026-11-10): **Classic** plays the fight as the
original does, for who wants it exactly as it was; **Modern** plays it with the remake's changes.
**Quality-of-life additions are the same in both** (they are not rules: the Arts list, the
prefilled chain, auto-battle, the camera ghost and the like).

So from now on a new departure is written as a **switch** with the original's behaviour on the
other side, not as a replacement, the way `bMakeWayOnlyInLane` already is. Which rows of the table
above each mode takes is still to decide; the candidates are the ones that change the rules or
the feel of a fight: the AP model, where a Super is matched, HP during a combo, making way, the
blend between clips, the enemy count. Not written yet: the option itself, where it is stored
(beside the difficulty) and how it reaches the fight (it would travel in the setup, like the
ruleset, so both machines of a PvP match agree).

---

## Infrastructure already available

None of this needs to be written — only called.

| Needed for | Already in |
|---|---|
| Combatant record (HP/MP/AP, 8 stats, 5 equip slots, conditions, RaSeruLevel) | `FJRPGPartyMember` (`Public/Party/JRPGPartyTypes.h`) |
| Damage, heal, restore MP, `SetAP`, revive, full restore | `UPartySubsystem` |
| XP split among survivors, level-up with jitter | `UPartySubsystem::AwardBattleXP` (takes the **raw** EXP sum; applies the version and the difficulty) / `LevelUpMember` |
| Enemy stats installed and victory EXP / Gold, per version (USA / Japan) | `UJRPGBattleRules` (`InstallEnemyStats`, `VictoryEXPPool`, `VictoryEXPShare`, `VictoryGold`) + `UCoreSubsystem::GetRuleset` |
| Effective stat (base + equipment + Ra-Seru) | `UStatusSubsystem::GetEffectiveStat` — the menu number. The fight snapshots UDF / LDF / SPD / INT / AGL from it, but **not ATK** ([Damage formula](#damage-formula)) |
| Ra-Seru bonus (1..9) | `UStatusSubsystem::GetRaSeruBonus` — ours, not the disc's (INT only). The Ra-Seru's ATK in a fight is its tier's equipment row ([Damage formula](#damage-formula)) |
| Elemental affinity multiplier | `UStatusSubsystem::GetElementMultiplier` — float, for the menus. The fight uses the integer 8×8 matrix in `UBattleFormulas` |
| Status conditions | `UStatusSubsystem::ApplyCondition` / `HasCondition` |
| Enemy scaling by difficulty | `UCoreSubsystem::ScaleEnemyHP/ATK/DEF` |
| XP curve and stat growth | `UCoreSubsystem` + `DT_GrowthCurve` / `DT_StatGrowth`. AGL follows the original's own formula (its divisor is `0x18C0`, not the other stats' `0x24C0`, and it stops at 280; fixed 2026-11-06, bug 63) |
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
| **3** | Enemies roaming the map (zone, roaming, trigger) | **Done** (written 2026-10-26, the spline shape and the editor test 2026-10-27) — `AJRPGFieldEnemy`, `AJRPGEncounterZone` (sphere/box/spline), tested in the editor |
| **4** | Battle stage (spawn, formation, camera) | **Done** (2026-10-27) — `AJRPGBattleArena` assembles the fight, `UJRPGBattleSceneSubsystem`, `BeginBattle`/`EndBattle`, `AJRPGBattlePawn`, `AJRPGBattleWaves` + JSON/Studio; tested in the editor and on the Muscle Dome map. Party-size scaling moved to Phase 8 |
| **5** | Basic combat (turns, damage, death, victory) | **Played** — written 2026-11-04, built and tested in PIE, merged 2026-11-07 (PR #14). Also closes Phase 6's in-fight part and brought Spirit forward from Phase 7. Open bugs: [BATTLE_PHASE5_BUGS.md](BATTLE_PHASE5_BUGS.md), for a later fix ([Guides/BATTLE.md](Guides/BATTLE.md)) |
| **6** | Directional Arts + the new AP | **Done** (2026-11-04 without a fight; in a fight with Phase 5) — recognition, learning, Supers, AP, validator, tester `Input`, automation tests |
| **7** | Remaining commands (item, escape, formation advantage, drop / steal) and presentation (HUD, attack camera, VFX, the remaining sounds) | **In progress** — came with Phase 5: Spirit, formation advantage, the field-to-battle transition, the battle camera, the Arts voices, the battle music, the command menu, Escape from the menu (no roll yet). To do: item, the escape roll, drop / steal, the rest of the HUD, VFX, the victory screen |
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
                         Phase 6 (Arts + new AP)       ← done
                                  ▼
                         Phase 5 (basic combat)        ← played (2026-11-07)
                                  ▼
                         Phase 7 (commands + presentation)   ← in progress
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

The batch over every id (export → convert → import) is `Extras/ps1_monsters_batch.py` plus the
editor scripts' `--all` (done 2026-10-30). It gives **179 models**, one per `DT_Enemies` row: of
the 186 slots with a mesh, 7 are repeated blocks (176–178 repeat 140; 183–186 repeat 180, Cort).

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

**Not written yet:** the level is rolled and stored on the combatant, but no curve exists, so it
changes nothing in the fight. It is defined together with the review of what the difficulties and
New Game+ raise, not in a battle phase of its own.

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

The scene is assembled at the arena **under the transition** (a fade to black at first; the
original's screen shatter and its other styles came with Phase 5), so the jump is never seen. The field party is not moved: it is
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
  → screen opens. `EncounterGraceTime` also counts from the end, and a longer `PostBattleGraceTime` (5 s) counts from the
  moment the field is shown again: until it passes no encounter starts and no field enemy chases (bug 4 of the Phase 5 test
  batch: an enemy standing next to the player started a new fight ~3 s after a victory).

Console: `JRPG.Battle.Start <EnemyKey> [Count] [Level]` and `JRPG.Battle.End
[victory|defeat|escape|abort]`.

`UBattleSubsystem` **never knows about `AActor`** — it speaks only through delegates, the
same separation the rest of the plugin keeps.

### Muscle Dome — the test map

`AJRPGBattleWaves`: fights in a row. A list of encounters (one per wave), the party, the
arena; a won wave calls the next after a delay, a lost / escaped / aborted one stops. It runs
on the field GameMode — no separate battle GameMode, pawn or controller — so it tests the same
path a field encounter takes. A wave ends when the fight does (the party wins, falls or escapes);
`JRPG.Battle.End` still ends it from the console.

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

**Goal:** a fight that works end to end — plain attacks, and the Arts Phase 6 left ready (the
input window inside a turn, damage per hit with each Art's power, AP on the combatant). No magic.

> **Written with the door open to PvP / online / co-op (decided 2026-11-04).** It has to work
> locally (single player, and local co-op stays possible) and leave the base ready for PvP
> later, with no network code in Phases 5–8. So the core is
> **commands and inputs in → resolution on the logic timeline → events out**, and
> [Phase 5 with the door open](#phase-5-with-the-door-open--what-changes-in-the-plan) is the
> plan: the sketches below are written that way, and the table in
> [What the core cannot do](#what-the-core-cannot-do) is binding. Comes **after Phase 6**
> ([Phases](#phases)), so the Arts are there to test from the first turn.
>
> **The rules are the original's** (decided 2026-11-04): what is not in
> [Deliberate departures](#deliberate-departures-from-the-original) follows the RE — its formulas,
> constants, order and truncation — and gets refined later, in play, if it needs to be. The
> sections below cite the RE function and doc for each rule; read it before writing the code.

**Played** — written 2026-11-04, then built and tested in PIE pass after pass (2026-11-05 to 2026-11-07) and merged
(PR #14). Every bug found, its fix and what is still open is in [BATTLE_PHASE5_BUGS.md](BATTLE_PHASE5_BUGS.md); the open ones
go in a later fix. Usage in [Guides/BATTLE.md](Guides/BATTLE.md). What the implementation settled that the draft left open:

- **The core is `FJRPGBattleResolver`** (no UObject): `Begin`, `SubmitCommand`, `SubmitInput`, `StepFrame`; `UBattleSubsystem` takes
  the snapshot, runs the clock (a fixed-step accumulator, 1/60 s a frame; the raw `DeltaTime` never reaches the rules) and
  writes back. `UBattleFormulas` holds the contas as pure static functions with a rand source.
- **The logic frame is 1/60 s** (the original's tick). A clip of rate *R* at game speed *S* advances *S·R/32* clip frames per
  logic frame (`UBattleFormulas::ClipFramesToSteps`); a hit beat *b* fires at clip frame *b − 1*; a flinch's juggle window is its
  beat over its rate (the RE's measurements are tests: Gobu Gobu 16, Zeto 32, Caruban 40, Vahn 12, Gilium under an Art 32).
- **The block's second gate** is "the defender still stands" (above). **Inside a reaction window** the roll is overridden as in the
  RE port: a defender in its block window keeps blocking, one in a flinch window cannot block.
- **Knockdown**: a hit that kills, or (on someone with a get-up clip) the **last connecting hit of the action** when the combo took more
  than ¼ of max HP or left it under ¼, knocks down and opens no juggle window; the others flinch (high hit: flinch 1, low: flinch 2)
  and open the window. The original adds the combo up and decides once, on the hit that writes the total (`melee_reaction_entry`'s
  `kill_check`), so a combo does not flag every hit after the ¼ as a knockdown. The pawn plays Knockdown → GetUp → Idle.
- **The target falls mid-chain**: **nothing is cut and nothing is retargeted** (RE: the target is fixed at dispatch, `actor[+0x1DD]`; the
  combo total is added up and written to HP once, on the last connecting hit, floored at 0; the wipe is only tested at the end of the
  action, state `0x5A`). Every hit still resolves on the same target, HP stops at 0 and the excess is discarded (never dealt to anyone
  else). The target only falls (Death, `KillCombatant`, the knockdown) on the last connecting hit; until then it flinches. That holds for
  the last enemy too: a 99-hit Art lands 99 hits and victory comes when the action ends. At the end of the action a `ComboTotal` event
  (`Count` hits, `Value` = the damage of every hit, excess included; sent for 1+ hits) shows the on-screen summary: nothing during
  the combo (each hit only shows its damage over the target), and at the end of the action the cluster reads "Hit N" over "Damage X" (a one-hit action only "Damage X"),
  the original's `HIT` / `TOTAL` cluster (`DAT_8007BD14`, one running sum of the landed hits, shown until the `0x51` teardown).
- **Party swings** come from the swing record of what is equipped (`DT_HeroSwings`, one row per hero + item + button, read from PROT
  0863..0865 by `generate_hero_battle_csv.py`): the power byte(s), the hit beat(s) (the frame of the clip where each hit falls; 1 or 2
  per swing), the rate and the button cost. The plain swing's hit lands at logic frame `ceil(32 (beat − 1) / (8 × rate))`, as the monsters'
  do, instead of the one `SwingHitFrame`. Default section (nothing equipped): Arms `0x18`, Ra-Seru `0x18` (Noa `0x1D`), High `0x18` (Vahn)
  or `0x0E` (Noa, Gala), Low `0x13`; the restricted ones (`0x13` Low, `0x0E` High) miss class 2 / class 3 targets, the boots that bring
  `0x1D` / `0x18` do not. Missing table or row: the default byte and the default frame, as before.
- **Reach** is the original's range law by size class (`DT_Enemies.SizeClass`), measured on the body (actor + pose centroid). The
  defender turns to the attacker and slides back by its reaction clip's own root speed and end step, read from the disc; the attacker
  snaps to the edge of the reach at each step (forward or back) and backs off while it is inside it; nobody walks back to a seat
  (confirmed by the RE, `battle-action.md`). Details in [Staying in the arena](#staying-in-the-arena).
- **Angle and distance** (bug 26): the original's two approach terms are in the damage and in the block roll, spent by the first hit of
  each action. The scene measures them at `ActionStart` (how far the defender is from facing the attacker, 0..`0x800`; the vsyncs the
  attacker walked) and sends them with `SubmitApproach`; the resolver clamps and logs them (`FJRPGBattleState::Approaches`). In the
  original a block is almost only possible face-on, and so it is here.
- **Spirit** (brought forward from Phase 7): the `Guard` command, no target. From the commit to the next round start the hero's
  defence counts ×3 and its side of the block roll ×3/2; the turn is the Spirit clip in place; it adds `SpiritAPGain` (32) AP; the next
  round opens with `min(Max * 7/5 + 8, 288)` gauge.
- **AP from a hit taken** (the original's, `FUN_801EC3E4`): every hit that connects on a hero adds `max(1, damage * 100 / MaxHP)` AP,
  capped (the accessory bonuses come with the accessories).
- **Formation advantage** (brought forward from Phase 7): rolled at the start from the fight's `Rng` (`UBattleFormulas::RollFormationAdvantage`),
  never in a scripted fight; `FJRPGBattleSetup::FormationAdvantage` can force it.
- **A refused command** (`OnCommandRefused`) shows on screen in red next to the prompt, with the reason.
- **The monster's swings** are queued with the original's budget loop (a random candidate; it is queued if the AGL pool pays;
  15 swings or 16 unpaid draws); with a next swing queued and `Gate` 0 the clip is cut at beat + 3 and the later hits are lost. A monster
  with no swing row (the data is missing, or `cort`, whose script is Phase 8) hits with a default swing, logged (`ActionIndex` −1: the pawn
  plays the first attack of its clip set). **A hit beat past the clip never fires** (the original only fires the beats its cursor reaches,
  frame ≤ `FrameCount` − 1): such beats are dropped and the clip is not stretched; a row whose beats are all past the clip plays and deals no
  damage (logged once), still eligible and paying its AGL — `koru_05` / `koru_06` at beat 200 and `rogue_05` at 255, bosses whose damage is
  Phase 8 script. A flinch window is capped at its clip (`cort_juggernaut_02_Damaged`: beat 10, 7 frames).
- **The party's timeline** comes from the disc, like the monsters': the plain swings from `DT_HeroSwings` and the Arts from `DT_ArtHits`
  (hit beats and rate of each record, generated by `generate_hero_battle_csv.py`). `UJRPGAnimNotify_Hit` and Chain Cut markers on a clip,
  when they exist, win over the table: they are the authoring tool for Arts made by hand, not something the disc clips need. With neither
  (a missing row, the Miracle Arts) the per-kind defaults apply (`FJRPGTimelineDefaults`, editable on the subsystem), logged once per kind.
- **`AwardBattleXPToParticipants`** is the "overload with `TArray<FName>`": a differently named plain method, because UHT does not take an
  overloaded `UFUNCTION`.
- **The parry** (`SubmitInput`): the input is validated (owner, alive, a stamp that is not from the future), stamped and kept in the state; it
  changes nothing yet — its window and effect are open question 5.
- **A round with no action** counts only when nobody could plan one for lack of data (64 in a row, warned each time, then `Aborted` with an
  error in the log — a safety net, the resolver loops without advancing frames); a round emptied by conditions (sleep, stun) does not count.
- **The fight waits** while the scene is entering (`HoldLogic`: commands are accepted and kept but the round stays in Commands; releasing it
  starts the execution), and for as long as the player takes to enter the commands. **After every action** it waits like the original
  (`BATTLE_TIMING.md`): the attacker's last clip to its natural end (no tail), then the target's reaction (flinch, block, knockdown +
  get-up, a hero's Knockdown + Dying, a monster's knockdown + death fade; lengths are data on the combatant), then the 60-vsync Done
  band, then the victory / defeat test, then about 6 passes before the next actor (`FJRPGBattleState::FinishCheckFrame` /
  `NextActionNotBefore`; `FJRPGTimelineDefaults::DoneBandFrames`, `PassGapFrames`). The decided fight holds `VictoryHoldFrames` (256) or
  `DefeatHoldFrames` (322) before `EndBattle`.
- **Not done**: Item, the escape roll (Escape from the menu always works today), drop / steal (Phase 7); magic and the scripted bosses
  (Phase 8); the enemies' level scaling (the encounter's level is stored, no curve exists; it goes with the difficulty / New Game+
  review); rule 4 of "Staying in the arena" (the per-Art override); the Rim Elm / Tetsu training row of `DT_Bosses` (the training is
  not in `bosses.toml`).

### 5.0 — Contracts first

The first step of Phase 5, before any turn is played: write down (in this section, then in code
and tests) what each piece promises, so the core does not grow silent assumptions. They hold for
single player and PvP alike.

**Data and the version's rules.** *Done (2026-11-04):* `DT_Enemies` is the raw disc record, and
`UJRPGBattleRules` installs the stats and computes EXP / Gold for the rules picked by
`EJRPGRuleset` ([Version rules](#version-rules--usa-or-japan)). *Phase 5 (written 2026-11-04):*
`BeginBattle` snapshots each enemy through `InstallEnemyStats(row, Setup.bScriptedFight, ruleset)`
and then the difficulty (`ScaleEnemyHP/ATK/DEF`); `FJRPGBattleSetup` has `bScriptedFight`,
`Seed` and `bNoRewards`. The original's flag is per formation, not per
boss, so `DT_Bosses` gets a column for it (Tetsu's training and Lapis are scripted fights with the
random-encounter profile). A scripted fight also cannot be escaped and rolls no formation
advantage (`ctx+0x287`).

**Data the fight needs from the disc** (Phase 5 generator work like Phase 1's — **written 2026-11-04**:
`DT_EnemyActions`, `DT_Enemies.TargetClass` and `.SizeClass`, `DT_Bosses.bScriptedFight`, `DT_RaSeruTiers`, `DT_WeaponCosts`, `DT_HeroSwings`, and later `DT_HeroReactions` (the heroes' reaction lengths, which the fight waits for);
[Guides/ENEMIES.md](Guides/ENEMIES.md)):

| Data | From (RE) | For |
|---|---|---|
| Each monster's **action entries**: per clip the power bytes (`+0x00..+0x03`, one per hit), the hit frames (the event-frame list `+0x10..+0x13`), the AGL cost (`+0x74`), the playback rate (`+0x78`), the status selector (`+0x7A`), the tag (`0x0B` block; `0x0C..0x1F` what the AI can roll, of which `0x0D` / `0x0E` / `0x0F` are the monster's plain attacks and the rest its specials) | `docs/formats/monster-animation.md` › action entry; the monster archive (PROT 867) | The enemy's timeline, its damage, its AI budget, its block. A table (`DT_EnemyActions`, a row per monster clip), **not notifies** |
| Each monster's **target class** (record `+0x1E`: 2 = floating, the "Lower, misses floating" hits miss it; 3 = short, the "Upper, misses short" hits miss it; 0 = every hit reaches) | `battle-formulas.md` › "The limb-vs-height miss" | The miss by height. A column in `DT_Enemies` |
| The **Ra-Seru tiers** as equipment rows (ATK, UDF, LDF, SPD per tier; weapon category with the Ra-Seru bit) | `docs/formats/equipment-table.md` (the ~24 per-tier entries the curated tables collapsed) | The Ra-Seru's ATK fold and its stats at load. The hero's tier is `RaSeruLevel` |
| The party's **per-button cost** (`+0x74` of the weapon section: 30 / 42 / 54) | `arts-command-gauge.md` › "Where the cost comes from" | The input window's cost per button, per weapon |

**The round.** The original's: **every hero's command is entered first, then the round plays
out** in initiative order (enemies included). That is a gameplay decision, not only a network
one — "each fighter chooses when its turn comes" would be a departure and would go in
"Deliberate departures". A round is `Commands → Execution`; `GetActiveSlot` / `OnSlotChanged`
mean "whose command is being entered" during Commands and "who is acting" during Execution.

**The logic timeline** ("State machine" below says why):

| Question | Contract |
|---|---|
| Where an action's frames come from | Built **when the action starts**, before anything plays, into a plain `FJRPGActionTimeline` (total frames, hit frames with their power byte, chain-cut frames). **Always from data, never from a notify firing.** Monsters: their `DT_EnemyActions` row, which is the disc's own action entry (hit frames, power, rate) — no notify at all. Party: the disc's swing and Art records (`DT_HeroSwings`, `DT_ArtHits`) plus the clip's length; `UJRPGAnimNotify_Hit` markers on the asset, read as data, win over the table (the authoring tool for Arts made by hand). Validated there: a hit frame past the clip's end, or fewer hits than `HitPower` says, is logged. The resolution only sees `FJRPGActionTimeline`, so the tests feed it directly, with no asset |
| The pace | Whole frames at the game's pace: 15 × rate frames/s (scale 8). An Art plays at scale 4 and the special starter at 2 (half and a quarter; `SpeedScale` per step overrides it — Phase 6), so its frames count longer ([Guides/PS1_ASSETS.md](Guides/PS1_ASSETS.md)) |
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

| | HP / MP | AP | Items used | Arts learned | EXP / Gold / drop | Then |
|---|---|---|---|---|---|---|
| **Victory** | Kept as they are; the fallen get up with **1 HP** (the original). A level-up raises the maxima only | Kept | Gone | Kept | Paid once | Back to the field |
| **Defeat** | Kept; everyone floored at **1 HP** (the original: a scripted loss returns to the field standing) | Not kept | — | Not kept | Nothing | Game over (Muscle Dome: the waves stop) |
| **Escaped** | Kept; the fallen get up with **1 HP** (the original) | Kept | Gone | Kept | Nothing | Back to the field, grace period |
| **Aborted** (debug, map change) | Untouched: as before the fight | Untouched | Not consumed | Not learned | Nothing | Back to the field |

So HP and MP are written back on Victory, Escaped **and Defeat** (bug 28; nothing is restored after a fight), AP and the
learned Arts only on Victory and Escaped, and `Aborted` throws the snapshot away. An Art
learned during the fight goes into the **combatant's** learned list (the resolution cannot write
the roster: `UArtsSubsystem::CommitArtsChain` is the out-of-battle path) and reaches the save with
the rest of the write-back, through `UArtsSubsystem`.
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

> **This is the plan's sketch, kept for the reasoning.** The header is the source of truth: it has grown since
> (the `ComboTotal` event, `GameSpeed` on the event, the reaction lengths and holds on the combatant, the approach
> inputs and the finish / next-action frames in the state, the `Spirit` step, the transition style and the formation
> advantage in the setup).

```cpp
UENUM(BlueprintType) enum class EJRPGBattleSide       : uint8 { Party, Enemy };          // exists
UENUM(BlueprintType) enum class EJRPGBattleCommandKind: uint8 { None, Attack, Arts, Item, Magic, Escape, Guard };
UENUM(BlueprintType) enum class EJRPGBattleInputKind  : uint8 { Parry };
UENUM(BlueprintType) enum class EJRPGBattleEventType  : uint8 {   // the UI builds the text
    RoundStart, ActionStart, Damage, Miss, Block, Heal, Death, APChanged, ArtRecognized, ArtLearned, ActionEnd
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
  int32 ATK;                  // heroes: the BASE stat, no equipment (the original's loader does not fold it);
                              // enemies: InstallEnemyStats + difficulty
  int32 UDF, LDF, SPD, INT, AGL;   // heroes: GetEffectiveStat (equipment and the Ra-Seru tier folded at load,
                                   // as the original's FUN_80053CB8 does); enemies: InstallEnemyStats + difficulty
  TArray<int32> PieceATK;     // heroes: the ATK of each piece, by role — Weapon, Ra-Seru tier, Shoes, Body, Head —
                              // folded per hit (Damage formula › Equipment fold)
  EJRPGElement Element;       // heroes: Vahn fire, Noa wind, Gala thunder, Terra wind; enemies: DT_Enemies
  int32 TargetClass;          // enemies: record +0x1E (the miss by height); heroes: 0
  int32 SizeClass;            // enemies: record +0x1F (the reach of the range law, FUN_8004E2F0); heroes: 0
  TArray<int32> SwingBytes;   // heroes: the plain swing's power byte per button, from the equipment's swing record (empty = the default)
  TArray<FJRPGBattleSwingProfile> SwingProfiles;   // heroes: per button, the swing record's power bytes, hit beats and rate (DT_HeroSwings)
  int32 AP, MaxAP;            // persistent meter (Arts add, specials spend)
  int32 CommandGauge, MaxCommandGauge;   // the AGL pool: the input budget and the enemy's action budget
  TArray<int32> ButtonCost;   // heroes: per button, from the weapon (30 / 42 / 54)
  TArray<FName> KnownArts;    // heroes: the learned list, grows when an Art is learned in the fight
  bool bHasRaSeru;            // heroes: gates the Hypers and the Miracle (Phase 6)
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

The original's (`FUN_801DA780`, RE `battle-formulas.md` › "Initiative key seeding"), seeded once
per round for every combatant:

```
key = SPD + Rng % (SPD/2 + 1) + 1
hero:     HP < MaxHP/4  -> key += (MaxHP - HP) >> 4
          HP < MaxHP/2  -> key += (MaxHP - HP) >> 5
          otherwise     -> key += (MaxHP - HP) >> 6
monster:                   key += (MaxHP - HP) >> 10
Slow:                      key >>= 1                       (Phase 8, with the conditions)
always-last / always-first accessories: key = 1 / key += 0x1000   (when accessories have abilities)
```

A hero near death gains more from the wounded term than from the whole SPD roll; a wounded monster
gains almost nothing. Hero SPD is the snapshot's (shoes folded). Execution picks the living
combatant with the highest unspent key, again and again, and a pick spends the key (`FUN_801DABA4`);
a dead combatant's key is zeroed before every pick. **Ties** are drawn from the state's `Rng` as the
original does (the tie list starts at seat 0; a seat that raises the maximum goes in twice, and
`Rng % (count + 1)` picks) — deterministic, because the stream is the fight's. The original's
mixed-sign compare (`0x8000`+ never wins) is a bug and is not kept.

**The AGL pool, per round** (`FUN_801D88CC`, before the keys): a combatant whose command was an
attack / Arts, and every monster, gets `CommandGauge = MaxCommandGauge`; one that chose Spirit
gets `min(Max * 7/5 + 8, 288)`; anyone else keeps what is left. A hero whose target died
is re-pointed at the **lowest living enemy slot** (`FUN_801DB8B4`).

**Formation advantage** (back attack / pre-emptive, `FUN_80051D84`) takes round one from one side,
which starts turned away. It is the original's; written with Phase 5
(`UBattleFormulas::RollFormationAdvantage`, `FJRPGBattleSetup::FormationAdvantage`;
[Guides/BATTLE.md](Guides/BATTLE.md)).

### Damage formula

The original's melee kernel (`FUN_801EC3E4`, RE `battle-formulas.md` › "Melee hit"), which every
direction hit, every Art hit and every monster swing goes through, one call per hit. Kept as it
is; the angle / distance terms (`AttackRamp`, `GuardRamp`) arrive as an input from the scene instead of
being read from positions ([Deliberate departures](#deliberate-departures-from-the-original)), and only
the first hit of an action spends them:

**The power byte** of the hit (`b`, one per hit: the Art's `HitPower`, the monster action's
`+0x00..+0x03`; a plain direction hit carries the 20 tier) gives both the scalar and the height:

```
Power   = [12, 18, 20, 22, 28][(b - 0x0C) % 5]
HighHit = (b - 0x0C) % 10 < 5            // high hits roll against UDF, low ones against LDF
```

**Miss by height** (hero attackers only; the monster's `TargetClass`): `b < 0x16` against class 2
misses when `b >= 0x11`; against class 3 when `b < 0x11`. A miss draws nothing and does nothing
(a `Miss` event). The party's **plain swings** go through this too: their byte is the equipment's swing record's first
byte (RE `docs/reference/glitches/turn-order-and-arts.md`): `0x18` / `0x1D` are free (power 20, UDF / LDF), `0x0E` is a
restricted high kick (misses short targets) and `0x13` a restricted low kick (misses floating ones). The default footwear
gives Low `0x13` for all three heroes and High `0x0E` for Noa and Gala, `0x18` for Vahn — so a plain Low swing always
misses a flyer, as in the original (`UBattleFormulas::DefaultSwingPowerByte`). Equipment that brings the free pair
(Gala's War God Boots, Vahn's Triumph and Ra-Seru Boots) is read from `DT_HeroSwings` (`UBattleSubsystem::ApplyHeroSwings` →
`FJRPGCombatant::SwingBytes` / `SwingProfiles`).

**Block** (a defender that has a block clip — tag `0x0B` — and passes the RE's second gate,
`target[+0x0] < defender HP` — settled 2026-11-04 from the RE port (`World::roll_block`): the word is the combo
total already pending on the defender, which the original applies to HP only when the combo lands, so the gate
is "the defender still stands on the accumulated total". The remake takes each hit's damage off HP at once, so
the gate is `defender HP > 0`):

```
a = A.SPD + A.ATK*4/5 + AttackRamp;   d = D.SPD + D.ATK*4/5 + GuardRamp;   a = max(a, d)
a += (Rng % a) * [6,4,4,4,2][(b - 0x0C) % 5] >> 1
d += Rng % d
D chose Spirit -> d = d*3/2;   A's hit is an Art -> a = a*3/2
blocked = a < d          // no damage, juggle = 1 (a Block event)
```

(The ability-bit arms come with the accessories that set them.) Both gates come before the draws,
so a defender without a block clip uses no randomness.

**Equipment fold — Base Offense** (heroes only; a monster's is its ATK). Keyed by the command of
the hit, not by the item:

| Hit | Folds |
|---|---|
| Arms | `Weapon ATK >> 1` |
| Ra-Seru | `Ra-Seru tier ATK >> 1` |
| High, Low | `Shoes ATK >> 1` |
| The starter | nothing |
| An Art's hit | `(sum of the five pieces) >> 1` |

`BaseOffense = ATK + fold`. Noa is left-handed, but the fold follows the role (her Arms button
folds her claws, her Ra-Seru button folds Terra). The Ra-Seru's ATK is the tier's equipment row
(5.0 › Data the fight needs from the disc), not `GetRaSeruBonus`.

**Offense, Defense, damage** (`atk` = BaseOffense):

```
Offense  = (atk + Rng % (atk/8 + 1)) * Power >> 4
         + A.HP >> 8                          // the attacker's current HP
         + (Juggle * atk) >> 6
         + (AttackRamp * atk) >> 16           // the angle: up to atk/32 more from behind
Art:       Offense = Offense * 13/10, then * Elem / 100
always:    Offense = Offense * Elem / 100     // so an Art takes the element twice
Venom / Toxic on the attacker: * 9/10 / * 7/10   (Phase 8)

def      = HighHit ? D.UDF : D.LDF
Defense  = def + Rng % (def/8 + 1)
         + (def * GuardRamp) >> 10            // the distance the attacker walked
Spirit:    Defense = Defense * 3
Venom / Toxic on the defender: * 9/10 / * 7/10   (Phase 8)

if not (Offense > Defense + Offense*Power/64 + Offense*Juggle/64 + Juggle):     // the underdog
    Offense = Defense + (Offense*3/4 + Rng % (Offense/4 + 1)) * Power / 64
                      + Offense*Juggle/64 + Juggle
    Art:   Offense = Offense * 11/10, element pass(es) again
    chip:  a swing within Defense + Juggle + 3 -> Defense + Rng%3 + 3 + Juggle
           an Art  within Defense + Juggle + 5 -> Defense + Rng%4 + 5 + Juggle
hero defender hit by a monster: the elemental-guard ladder (accessories, when they exist)
Offense  = min(Offense, Defense + 9999)
Damage   = Offense - Defense
```

There is no floor at 1: an attacker weaker than the defence still lands a hit that scales with
its roll, and the chip keeps it at 3 or more (5 for an Art). Only with very small numbers can a
hit that wins outright do 1 or 2. A zero-damage enemy hit is always a block.

**Element** — the original's 8×8 matrix (`0x801F53E8`, row = attacker), integer percent:

| Pairing | % |
|---|---|
| Same element (thunder against thunder included) | 96 |
| Opposed: earth-wind, water-fire, light-dark | 104 |
| Thunder attacking earth, water, fire or wind | 102 |
| Thunder attacking dark | 98 |
| Anything else (thunder against light too), and neutral | 100 |

Two things matter more than the rest:

- **Truncated integer arithmetic everywhere.** `int32`/`int64` and explicit `>>`, in the
  original's order; never `float`, never `FMath::RoundToInt`.
- **ATK without equipment.** `GetEffectiveStat("ATK")` already folds equipment in (it is
  the menu number). Combat needs the **base** ATK and folds one piece (or all five, for an Art)
  per hit, at half. Confusing the two inflates all damage invisibly.

All of it lives in `UBattleFormulas : public UBlueprintFunctionLibrary` as pure static functions
with the constants exposed — refining later should not mean recompiling the state machine. They
take the `FRandomStream&` and the combatant snapshot, never a subsystem. The RNG draws go in the
original's order (attack roll, defence roll, then the rewrite and chip draws only when they fire);
matching its exact stream is not a goal (it was shared with presentation).

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
Each clip's length, rate and hit frames are **data**, the same on every machine: for a monster,
its action entry from the disc (`DT_EnemyActions`, the original's own event-frame list); for a
hero, the disc's swing and Art records (`DT_HeroSwings`, `DT_ArtHits`), or the `UJRPGAnimNotify_Hit`
times on a hand-made `UAnimSequence`, read as data when the action starts
(never as a notify that fires). When the counter reaches hit N's
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
   carry it through the enemy. The remake keeps that — forward movement stops at reach distance,
   which is the law's own: the threshold of the target's size class (`{256, 384, 1024}[class]` PSX units under 3, else
   `class << 4`; the hero adds his offset `+43 / 0 / -53 / -100`), 0.433 cm a unit. At the start of every step the attacker
   closes the distance again (the commit's "reach snap"), so a defender that slid back keeps being reached.
   RE confirmation of the rest of this section: "an action does not return its combatants to their authored formation
   seats ... there is no walk-home leg ... no recover backstep" (`battle-action.md`, "Where an action leaves its combatants").
2. **The arena bounds clamp everyone.** Knockback, the arrival shove and the attacker's own root
   movement are all clamped to the arena's area (`AJRPGBattleArena`). Knockback is what pushes the defender back on each hit:
   the reaction clip's own root speed (`+0x0C`) and end step (`+0x0E`), as in the original, exported from the disc into
   `DT_EnemyActions`, `DT_HeroReactions`, `DT_HeroSwings` and `DT_ArtHits` (`bUseClipMotion`). The fixed distances on the pawn
   (`FlinchKnockback` 45 cm / `KnockdownKnockback` 120 cm / `BlockKnockback` 20 cm) are only the fallback for a clip with no
   data. A target pinned against the edge stops the attacker by rule 1, so the whole fight stays inside.
3. **Fighters keep their distance** (done, bug 24; the original's four mechanisms, all on the presentation side). The
   **separation** nudges apart any two bodies that are too close, every frame; bystanders **make way** for a walking attacker
   (only who is in its lane: [Deliberate departures](#deliberate-departures-from-the-original)); the formation is **recentred**
   at each action and at the round start, the battle camera following it (`bRecentreFormation`); the **arrival shove** pushes
   the target to the edge of the reach when a walk closes. Tunables on `AJRPGBattleArena` (Formation).
4. **Per-Art override** (not done). A new big Art can be marked to play in place (no root movement) or to
   cap its travel; the camera frames the action, wherever it is.

Root movement itself is the disc's: per clip, a root speed (`+0x0C`) and an end step (`+0x0E`)
(see [Guides/ARTS.md](Guides/ARTS.md)). The Art tester lines clips up by the body instead and has
**Reset Position**.

### Enemy AI

The original's generic core (`FUN_801E9FD4` + the target resolver `FUN_801E7320`, RE
`battle-round-loop.md` › "Monster AI"), physical half:

- **Target:** a uniform random living hero (`Rng % party count`, drawn again past the fallen).
  Not weighted by HP — that is the RE port's non-faithful "smart monsters" toggle; it can come
  later as an option.
- **Action:** the monster's swing entries (tags `0x0C..0x1F` with a real AGL cost, from
  `DT_EnemyActions`: the plain attacks `0x0D..0x0F` and the specials around them), queued while its AGL pool pays their `+0x74` cost — which one each time,
  as the RE's `pick_monster_action` does (read it before writing). Each swing hits with its own
  power byte at its own hit frames.
- **Phase 8:** the magic roll (`Rng % (1 + live magic count)` over the record's three spell ids,
  0 = physical) and the per-monster `switch` (bosses' scripted casts, cooldowns, the round
  counter). Until then a monster with magic always takes the physical branch.

It is a step of the resolution: it runs when the enemy's action starts, draws from the fight's
`Rng`, and finds targets by **team**, not by `Side`.

**Acceptance:** touch a Gimard → turn order computed → choose Attack → animation plays,
damage shows, HP drops → the enemy retaliates → kill it → `Defeat` animation, victory, XP
distributed through `AwardBattleXP` (raw sum; with `USA` and three heroes, a Gimard's 55 EXP
pays 14 each), back to the map, pawn gone.

And the failures, each an automation test where it can be one (no map):

| Case | Expected |
|---|---|
| The target dies before the action | A hero re-points at the lowest living enemy slot (`FUN_801DB8B4`); a monster draws another living hero; none left → the action is dropped, nothing consumed |
| The attacker dies before its turn | Its command is dropped; a reserved item is released |
| Two equal initiative keys | The tie drawn from the fight's `Rng` (the original's rule); the same seed gives the same order every run |
| A hero's ATK in the snapshot | The base stat, with a sword equipped too; a High hit folds half the shoes' ATK, an Art half of all five pieces |
| The damage kernel (no map) | The RE's worked example (Vahn vs Evil Fly, `battle-formulas.md`) and its regression cases give the same numbers with the same draws |
| The last enemy dies in the middle of a combo | The whole chain still resolves (HP stops at 0, the excess is lost; the original tests the wipe only at the end of the action); the enemy falls on the last hit and victory comes once the action ends |
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

**Written 2026-11-04** — usage in [Guides/ARTS.md › 4](Guides/ARTS.md#4-recognition-learning-and-ap).
The rules are `UJRPGArtsRules` (pure functions: buttons + a snapshot of the hero → `FJRPGArtChain`),
`UArtsSubsystem` takes the snapshot from the roster and writes the learned list,
`UJRPGArtsValidator` checks `DT_Arts`, the Art Tester has `Input`, and `JRPG.Arts.*` are the
automation tests. What the work settled that the sketch below did not say:

- **The disc corrects one Command.** Vahn's Hyper Elbow is `← → ←` (Arms, Ra-Seru, **Arms**) in
  the game's arts-name table, not the walkthrough's Arms, Ra-Seru, High that `arts.toml` copied (RE
  `docs/formats/art-data.md` › "Command-glyph string"). With the walkthrough's buttons Fire
  Tackle's input does not tokenize to its original chain — the validator found it. The generator
  applies the fix; the acceptance below reads `Arms, Ra-Seru, Arms`.
- **A Hyper takes its whole span** (the original's special arm writes `0x1A` at the *start* of the
  match and the clips over it), and a `ByKind` Hyper also needs the Ra-Seru — the same marker
  (`ctx[+0x25F]`) gates the Hypers and the Miracle in the original.
- **Where a Super plays.** At its first Art's starter: the buttons before it are that Art's
  swings (the original's "find" starts there). A Miracle replaces its whole span. Supers and
  Miracles are matched on their `Command` anywhere in the chain (the original only matches the
  tail of the queue — that would make "a loose Art behind the Super" impossible).
- **The gauge charges per button**, as the original does (pool = AGL, 30 a button, more for the
  weapon hand off its class; RE `arts-command-gauge.md`) — not the per-Art `CommandCost`, which
  stays a balance reference.
- **The hit marker** is `UJRPGAnimNotify_Hit`; the validator checks its count. The disc's Art
  clips do not need it (their hit frames come from `DT_ArtHits`, Phase 5); it is for Arts made by hand.

### Two meters, not one

| Meter | Scope | Role |
|---|---|---|
| `CommandGauge` | Per turn, seeded from `AGL` | Caps how many directional commands fit in a turn. This is what makes high AGL mean long combos — the original's feel |
| `AP` | Persistent, `0..100`, already in `FJRPGPartyMember` | **Regular Arts add. Hyper / Super / Miracle spend.** This is the improvement |

The original spent AP on every command, which is the complaint this remake fixes. Here the
player builds AP by performing Arts and burns it on the big ones. Two sources of AP are the
original's and are kept: Spirit (+32) and every hit a hero takes (`max(1, damage * 100 / MaxHP)`).

### `UArtsSubsystem`

```cpp
static EJRPGArtButton ButtonForPad(FName Character, EJRPGPadButton Pad);
// Noa is left-handed: her Arms is the right button (resolved here, never stored)

// The Arts the buttons perform, overlapping as in the original; the unknown ones it meets
// come back flagged, and LearnArt adds them
TArray<FJRPGRecognizedArt> RecognizeArtSequence(FName Character, const TArray<EJRPGArtButton>& Input) const;
FJRPGArtChain PreviewArtsChain(FName Character, const TArray<EJRPGArtButton>& Input) const;   // the whole turn, nothing saved
FJRPGArtChain CommitArtsChain(FName Character, const TArray<uint8>& ArtBytes, bool& bValid);  // re-recognised from the bytes; learns
TArray<FJRPGArtAvailability> GetSpecialsAvailability(FName Character, const TArray<EJRPGArtButton>& TypedSoFar) const;
TArray<FName> GetKnownArts(FName Character) const;
bool HasLearnedArt(FName Character, FName ArtKey) const;   // BlueprintPure: dialogue / triggers branch on it
bool LearnArt(FName Character, FName ArtKey);              // true = newly learned (on use, NPC, trigger, book)
void TeachArtToCharacter(const FString& CharacterName, FName ArtKey);   // preserved; calls LearnArt

// The rules themselves, what the fight calls with its own Rng (UJRPGArtsRules)
static FJRPGArtChain BuildChain(const UDataTable* Arts, const FJRPGArtsContext& Context,
                                const TArray<EJRPGArtButton>& Buttons, FRandomStream& Rng);
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

- **Arts overlap — the shortened input.** This is the original's, and it stays: the end of one
  Art can be the start of the next, so two Arts cost fewer buttons than typed apart (`↑↓↑↓↑` is
  two Somersaults in five buttons, not six). Between Regular Arts the shared part is **1 or 2
  buttons**: no pair in `DT_Arts` shares 3 (checked 2026-11-10), though nothing in the rules
  forbids it for a new Art. A Hyper never shares (it takes its whole span). Where it lives:
  `UJRPGArtsRules::BuildChain`; [Guides/ARTS.md › Recognition](Guides/ARTS.md#recognition--the-originals-tokenizer).
  The original walks the input tail-first and restarts right after each
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

Validation for the generator: tokenize each Super's `Command` with the overlap rule and compare
the Arts it gives with the original's chain (RE `docs/formats/art-data.md` › "Super Arts", the
find patterns of `crates/art/src/super_art.rs`; the RE derives the input the same way,
`derive_super_input`). Not from `Sequence`: it drops the last Art for the finisher.

### The input window

Choosing Arts opens the window, and the window is **local**: each button debits its cost from
`CommandGauge` (the original's per-button price, 30 for most) and pushes a byte, with no round
trip to the rules. Closing it sends the whole chain as **one** command: in a fight,
`FJRPGBattleCommand` with `ArtBytes` through `SubmitCommand`; out of a fight (the Art tester),
`UArtsSubsystem::CommitArtsChain`, which writes the learned Arts straight to the roster and is
never called by the fight. Either way the rules recognize it again from the bytes (never trust the payload) and turn
it into the turn's queue (`UJRPGArtSequenceLibrary`). Before Phase 5, the Art tester plays the
same path without a fight: a gauge of its own, commit, recognition, queue.

When AP moves, which the inversion makes a real decision (the original paid AP per Art at
recognition time, `+0x170`). **Decided 2026-11-04** (open question 6):

- **Each special is checked on its own, in order**, against the AP the hero has **at that point
  of the chain**: the AP at the start of the turn, plus what the regular Arts before it added,
  minus what the specials before it spent. Nothing after it counts — an Art that comes later
  cannot pay for a special that comes earlier.
- **Loose regular Arts add AP wherever they are**, in front of a special or behind it; behind,
  they only help what comes after them.
- **The regular Arts inside a Super's command add nothing while the Super fires.** The Arts its
  buttons are made of (Somersault, Cyclone, Somersault in Tri-Somersault's `↑↓↑↑↑↓↑`) are the
  Super. If the Super does not fit, it is downgraded to those Arts, and they add AP like any
  loose Art.

| Moment | What happens |
|---|---|
| **Commit** (the chain is recognized) | The chain is walked **left to right** with a running AP (start: the hero's AP). A loose regular Art adds its `APGain` (capped at 100); a special that fits the running AP is kept and subtracts its `APCost`; one that does not fit is downgraded to its buttons (plain swings / the regular Arts inside it, which add as loose Arts) and the walk goes on. Nothing is debited or credited yet |
| **Each Art, when its action reaches it** | The meter moves the same way, step by step: a loose regular Art **credits** `APGain` at its first hit (an Art whose target already fell still goes on and credits); a kept special **debits** `APCost` when it starts; the Arts inside a fired Super move nothing. The meter never goes below 0 — the commit already made sure every special fits where it is |
| **The action is cancelled** (the hero dies, the fight ends) | What was not reached is neither credited nor debited |

Examples:

| Hero's AP | Input | Result |
|---|---|---|
| 50 | `↑↓↑↑↑↓↑` (Tri-Somersault, 60) | 50 < 60: downgraded; Somersault, Cyclone, Somersault play and add AP |
| 50 | A loose Art (+12), then `↑↓↑↑↑↓↑` | 62 ≥ 60 when the Super's turn comes: it fires; AP ends at 2 |
| 50 | `↑↓↑↑↑↓↑`, then a loose Art (+12) | 50 < 60 when the Super's turn comes: downgraded, its three Arts add AP; then the loose Art adds 12 |
| 15 | Tornado Flame (30), then a loose Art (+15) | 15 < 30 when Tornado Flame comes: downgraded to its buttons; the Art after it does not pay for it |

Hyper / Super / Miracle appear **unavailable** (greyed out) when the running AP at that point
does not cover `APCost` — the feedback has to be visible before the player builds the sequence.
The list updates as the chain is typed (a special lights up once the Arts before it pay for it).

### Making new Arts — the base

The remake will grow its own Arts: new combinations of the existing ones, existing clips with
new rules, and new clips. A Regular turned into a Super (Power Punch with two more Power Punches
at the end, called a Super) has to be **a row in `DT_Arts` and nothing else** — no code, no
table in C++. What `DT_Arts` already does ([Guides/ARTS.md › Making a new Art](Guides/ARTS.md#2-making-a-new-art)):
any row is an Art, `Sequence` chains other Arts, `Part`s and the basic clips (repeats allowed),
`ActionConstant = 0` is an Art with no disc record, the generator never touches rows it did not
create. What Phase 6 adds so that stays true once the Arts are *played*, not only previewed:

- **Recognition reads only `DT_Arts`.** Every Art, Super and Miracle is matched on its row's
  `Command`; nothing is matched from the original's find / replace tables. The disc's Supers are
  rows like any other (their `Command` is the derived input, checked by the generator). A Super's
  command takes precedence over the regular Arts inside its span, and the span's Arts are "inside
  the Super" for AP.
- **A Super's own Arts are what its `Command` tokenizes to** — the hero's Regular / Hyper Arts
  that the overlap rule finds in the Super's buttons (Power Slash's seven buttons: Charging
  Scorch, Somersault, Slash Kick). **Not** its `Sequence`: that is what *plays*, and the
  original's replacement drops the last Art for the finisher (Power Slash's `Sequence` has no
  Slash Kick; Super Javelin's has only Rushing Gale). The tokenized Arts are what "every Art
  known before the turn" checks and what adds no AP while the Super fires. A Super made of one
  Regular repeated, with its own `Command`, needs the Arts that command contains; one whose
  command contains no Art has no prerequisite (the validator warns — give it `Unlock = Taught`).
- **`Unlock` column** (new, `EJRPGArtUnlock`): `ByKind` (the default: Regular on use, Hyper
  taught, Super by its chain, Miracle by the Ra-Seru — [the table above](#how-each-kind-of-art-is-unlocked-decided-2026-11-04)),
  `OnUse`, `Taught`, `ChainKnown`, `RaSeru`. A new Super that should come from an NPC, or a new
  Regular that should only come from a book, is one cell.
- **Hits and speed come from each step's row.** A step that is another Art plays that Art's clip
  with that Art's `HitPower` / `HitTarget`; to make the Super's copy stronger, add a `Part` with
  the same `Anim` and new hits. The speed scale per step follows the original's rule (x8 / x4 /
  x2 by position) unless the step's row overrides it (`SpeedScale`, new, `0` = the rule).
- **The chain travels as buttons.** `FJRPGBattleCommand::ArtBytes` holds the buttons pressed,
  not Art ids — a new Art needs no number, and the rules recognize the chain again from the
  buttons (online, too).
- **The learned list is keys** (`TArray<FName>`), so a new Art is saved like a disc one.
- **Validation** (`UJRPGArtsValidator`, run by an automation test over `DT_Arts`, by Data
  Validation on the asset and by LegaiaStudio's Arts tab): two Arts of a hero with the same
  `Command`; a Regular / Hyper whose `Command` is never reachable because a longer Art always
  wins; a `Sequence` step that is missing, belongs to another hero or loops back to the row; a
  `Part` with a `Command`; a special without `APCost` or a Regular with one; `HitPower` and
  `HitTarget` of different lengths; an `Anim` not on the hero's battle skeleton; a clip whose hit
  notifies do not match `HitPower`; a `ChainKnown` Super whose `Command` tokenizes to no Art (the
  player could fire it knowing nothing); a disc Super whose tokenized Arts differ from the
  original's chain. Errors stop the test; warnings are listed.
- **Recognition preview in the Art tester**: an `Input` field (buttons, as typed) shows what the
  rules recognize — the Arts, the Super, what is inside it, the running AP at each step and the downgrades —
  and plays it. A new Art is made in LegaiaStudio / the CSV and tried there without a fight.

The `Unlock` and `SpeedScale` columns, the validator and the tester's `Input` are Phase 6 work;
the rest of the list is a rule for how Phase 6 is written.

**Acceptance, in Phase 6** (Art tester + automation tests, no fight): enter `Arms, Ra-Seru,
Arms` as Vahn (the disc's Hyper Elbow, `← → ←`) → "Hyper Elbow" is recognized and the right animation plays; Noa's mirrored input
gives her Arts; the overlap case (Tri-Somersault from its seven buttons) recognizes every Art in
it; an unknown Regular is learned once and survives a save / load; an unknown Hyper's buttons
teach nothing, and `LearnArt` (the book / NPC path) teaches it, with `HasLearnedArt` flipping to
true; Tri-Somersault entered while Cyclone is unknown learns Cyclone and plays the three Arts
without the Super, and the same input on the next turn fires the Super; Power Slash does not
fire while Slash Kick (its last Art, absent from its `Sequence`) is unknown; each Super's
`Command` tokenizes to the original's chain; the AP check at commit gives exactly the four examples
above (a loose Art in front pays for the Super, one behind does not, the Tornado Flame case); a new Super added as one CSV row (Power Punch + two more Power Punches, its own
`Command`) is recognized, plays in the tester and passes the validator, with no code change; the
validator flags a duplicated `Command` and a `Sequence` step that does not exist.
*Each item is a `JRPG.Arts.*` automation test (2026-11-04; run in the editor after the build); the
animations are checked by eye with the Art Tester's `Input`.*

**Acceptance, closed in Phase 5** (in a fight; played with Phase 5: `JRPG.Battle.ArtsInFight` and
`SuperInFight` test it without a map): damage uses the Art's power per hit and AP
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
- **Escape** — *partly done:* the menu's Escape ends the fight as `Escaped` (refused in a scripted
  fight) and **always works**. *To do:* the original's roll (`FUN_801E791C`: party
  `(SPD*3)>>1 + missingHP>>4` against the enemies' `SPD + missingHP>>5`, two draws), as a command
  through the resolver, and the monsters' own flee roll (`FUN_801EC0DC`). What an escape keeps:
  5.0 › End of the fight.
- **Spirit** (the original's guard) — *done with Phase 5:* Defense × 3 in the kernel and the block
  bonus, from the commit to the next round start; the AGL pool `min(Max * 7/5 + 8, 288)` next
  round; +32 AP. The aura comes with the VFX.
- **Formation advantage** — *done with Phase 5:* back attack / pre-emptive (`FUN_80051D84`), the
  roll at the start, the losing side out of round one and turned away; none in scripted fights.
  The accessory passives that change the roll come with the accessories.
- **Rewards beyond EXP / Gold** — the drop roll (at most one item per fight, the original's
  `0x8004F3D8`) and the steal; `DropItem` / `StealItem` are already in `DT_Enemies`.

### Presentation

- **HUD in `shell.html`.** A new `battle` screen: party HP/MP/AP strip, enemy row, the AP
  bar with its cost markers, damage log. Add `Battle` to `EJRPGUIState` **at the end of the
  enum** (values are serialized in Blueprints). New `UWebUIBridge` callbacks:
  `OnBattleCommand`, `OnBattleArtsButton`, `OnBattleArtsCommit`, `OnBattleTargetChanged`.
  C++→JS always goes through `UWebUISubsystem`, never `ExecuteJS` from `UBattleSubsystem`.
  *Status (2026-11-05): a placeholder of this screen's core exists — the command phase only (Start /
  Tactics / Attack inputs / Target / Final, auto-battle tactics), see
  [Guides/BATTLE.md › Battle UI](Guides/BATTLE.md#13-battle-ui). It uses ONE bridge callback,
  `onbattleinput`, instead of the four listed above, and keeps the choices in a buffer sent on Begin. The
  enemy row, the AP bar with cost markers and the damage log below are still to do.*
- **Attack camera.** *Done with Phase 5:* the battle camera follows the original's framing cases
  instead of the first sketch (`FocusOnActor` / `Orbit`): `JRPGBattleCamera.h`, the per-Art shots
  in `DT_ArtCamera`, the near-camera ghost, and it follows the formation recentre. Still to do:
  the camera shake on a hit and the victory shot.
- **The field-to-battle transition.** *Done with Phase 5:* the original's styles (tile shatter,
  spin-up, scatter, curtain, swirl) besides the fade
  ([Guides/BATTLE_STAGE.md](Guides/BATTLE_STAGE.md#the-field-to-battle-transition)).
- **Sound — the rest of it.** Phase 2 brought the monsters' sounds and the party clips' own cues.
  *Done with Phase 5:* the Art voice lines and fanfares (`DT_BattleVoices`), the battle BGM
  (`DT_BattleMusic`), the impact sound gated by the hit's result, the monster's death sound, and
  the sounds of the placeholder command menu. *To do:* no impact sound on a block (bug 86), the
  victory fanfare, and a review of the HUD's sounds (cursor, confirm, the input window's buttons,
  the AP gauge) together with the real HUD, since today's menu is temporary. Driven by the events,
  like the rest of the presentation.
- **VFX.** The battle's visual effects in Niagara: hit sparks per element, the block, the Art and
  Super flashes, the after-image trail, death and the victory. The original's are effect scripts
  in each action entry (`+0x14..+0x53`, RE `docs/formats/monster-animation.md` › "Effect-script
  records"); read them for when and where each one plays, and rebuild the look in Niagara.
  Presentation only: they play off the events (`Damage`, `Block`, `Miss`, `Death`…) when the clip
  gets there, and any randomness in them uses the cosmetic stream, never the fight's `Rng`.
- **The victory screen** — EXP, Gold and the item, from the events the rules emitted.

**Acceptance:** a full fight with every sound and VFX in place, reactive camera, usable items and
working escape, with nothing debug-looking on screen.

---

## Phase 8 — Magic and Seru

**Goal:** close the system.

Last on purpose: it depends on everything above being stable and carries the most new rules.

- `DT_Magic` (`magic.toml`, 29 spells → `FJRPGSpellData`, `generate_magic_csv.py`) and the spell
  system over it.
- **Spell power is missing** — it is not in `magic.toml`. It comes from the disc's
  move-power table (PROT 0898, stride 26), so an extra extraction pass produces
  `DT_MovePower`.
- **The monsters' special attacks.** Many monsters have attacks that are not plain hits: their
  own clip with its own effect (a breath, a status, a spell cast, a boss's scripted move). Today
  the AI can only take the physical branch, so a special with an AGL cost is played as if it were a
  plain swing (its power bytes and hit frames, no effect of its own), and the ones that are not
  swings (the cast clips, tag `0x23`, and what the per-monster scripts stage) are never used at
  all. What it takes: the magic roll and the per-monster `switch` ([Enemy AI](#enemy-ai)), the
  status selector of each action entry (`DT_EnemyActions.StatusSelector`), the entries' effect
  scripts for the VFX, and `SpecialAttacks` / `SpecialAttackMP` of `DT_Bosses`.
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
| Send an Arts chain button by button to the rules | Each button would be a round trip | The input window stays local; closing it sends the whole chain as one `FJRPGBattleCommand` (`ArtBytes`) through `SubmitCommand` |
| Read `UPartySubsystem` / `UInventorySubsystem` / `UStatusSubsystem` during resolution | In PvP the opponent's party is not in your GameInstance | Snapshot everything into the combatants at `BeginBattle`; resolution reads only the battle state; write back (HP, XP, items used) at `EndBattle` |
| Use `GetFirstPlayerController()` in battle code (`AJRPGBattleArena`, `UJRPGBattleSceneSubsystem` do it today) | On a listen server it is always the host's controller | Pass the player explicitly; which player owns which slot lives in the battle state |
| Let the rules read positions (traces, distance after root motion, "staying in the arena") | Actor positions differ per machine | Targeting, range and hits are decided by slot; spacing and arena clamping stay presentation. The one thing the original takes from positions, the angle and the walk of the approach, enters as a clamped **input** (`FJRPGApproachInput`, `SubmitApproach`): only the authority measures it, and it travels with the commands |
| Advance the logic by the frame's `DeltaTime`, or let time dilation / a menu pause stop it | Variable steps land a hit on a different frame per machine; the host cannot be paused for everyone | A fixed-step integer frame counter owned by the battle; online it never pauses (single player may) |
| Run enemy AI outside the resolution | Each machine would pick different targets | AI is a step of the resolution, using the battle stream, on the host only |
| Judge a parry on the host's clock | The defender sees the attack late by the network delay; a press on time on their screen would arrive late | The defender's machine judges the press against its own playback and sends "parried hit N"; the host holds hit N until that input or a deadline (see "The transport") |

Phase 5 does **not** add replication, lobbies or EOS — it only keeps the core from closing the
door.

**Match setup in PvP (noted, not implemented).** `UBattleSubsystem::TimelineDefaults` (editable), the timing tables (`DT_EnemyActions`,
`DT_HeroSwings`, `DT_ArtHits`, `DT_HeroReactions`) and the times read from the party's clips
(length, `UJRPGAnimNotify_Hit` and Chain Cut markers, through `ClipTiming`) feed the logic timeline, so the two machines must agree on them:
they are part of the match's setup, to travel with the seed, the party snapshots and the ruleset. Today each machine reads its own.

### Phase 5 with the door open — what changes in the plan

The rules themselves stay: the damage formula, the turn order, the enemy AI and "Staying in the
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
| Stats read through `GetEffectiveStat` at the moment of the hit | A **snapshot** at `BeginBattle`: base ATK, each piece's ATK kept separate (the Ra-Seru tier one of them), the other stats with equipment folded. This also retires the "ATK double-counted" risk |
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

**Turn order.** Same formula; the `roll` and the tie draw come from the state's `Rng`. No
`TArray::Sort` (not stable): the original's "highest unspent key" pick, with its tie draw, is
already the same on every machine.

**Damage formula.** Same formula (the plan already bans floats, which helps). `UBattleFormulas`
functions take the `FRandomStream&` and the snapshot instead of asking a subsystem. The element
matrix is integer percent (`* 104 / 100`, `* 96 / 100`, …) — a float from
`GetElementMultiplier` can round differently per machine. The angle / distance terms are an input
the authority measures and sends, never read from positions by the rules.

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
| ATK double-counted (`GetEffectiveStat` already includes equipment) | **High**, because it is invisible — inflates damage uniformly | Explicit test: Vahn's combatant with a sword equipped must read `ATK == base`; the fold adds half of one piece per hit (all five for an Art) |
| Hit frames taken from notifies firing | **High** for PvP — playback differs per machine | Monsters' frames come from the disc (`DT_EnemyActions`), the party's too (`DT_HeroSwings`, `DT_ArtHits`; markers on a hand-made clip are read as data when the action starts); the resolution only sees `FJRPGActionTimeline` |
| PS1 integer arithmetic translated as float | Medium | `int32`/`int64` throughout the damage kernel, no `FMath::` |
| A version's factor applied twice, or baked into a table | **High**, invisible — the first `DT_Enemies` held EXP × 3/4 and `AwardBattleXP` cut it again (≈56% of the EXP) | The table is the raw record; every factor lives once, in `UJRPGBattleRules` (rule 53). Tests pin the RE's captures (Gaza, Gobu Gobu, Gimard, Zeto) |
| Larger party breaking balance | Medium | `bScaleEnemiesWithPartySize` (Phase 8, with the Seru members), multipliers exposed as `UPROPERTY` |
| Regenerating a CSV wiping hand-made mesh/anim mapping | Medium, destructive | Merge by row name in the generator + a timestamped copy of the old CSV in `Docs/Data/_backup/` |
| ~~`bosses.toml` inline tables breaking the parser~~ | Retired — `Extras/legaia_toml.py` reads it with `tomllib`; 30/30 rows | |
| ~~Importing 186 enemies by hand~~ | Retired — `Extras/ps1_monsters_batch.py` + the editor scripts' `--all` | |

---

## Open questions to settle in practice, not on paper

1. ~~Does Interchange honor a glTF skin?~~ Moot: the pipeline goes through FBX (Phase 2).
2. ~~PSX→cm scale~~ — settled at **0.433005 cm per unit** (Phase 2 › Scale).
3. ~~`EAutomationTestFlags` in 5.8~~ — settled by Phase 6's `JRPG.Arts.*` tests; Phase 5's
   follow them.
4. **`UGameInstanceSubsystem` + `FTickableGameObject`** — written as `ETickableTickType::Conditional`
   (`IsTickable()` true only during a fight, `IsTickableWhenPaused()` false). Found in review: without
   `GetTickableGameObjectWorld()` the engine ticks it in its world-less call (`UGameEngine::Tick` →
   `TickObjects(nullptr, LEVELTICK_All, false)`), which ignores the pause; it now returns the game instance's world, and
   `Tick` also returns early if the world is null or paused. To be confirmed in play. The original question: a known pattern, but confirm
   whether `ETickableTickType::Conditional` is enough or `IsTickableWhenPaused()` is needed.
5. **The parry** (planned 2026-10-31, design open). To decide: which hits can be parried (every
   enemy hit, or only some attacks), the window in frames and whether it shrinks with
   difficulty, what a parry does (no damage, less damage, a counter, AP), what a miss costs,
   how the window is telegraphed (a flash, a sound, the clip itself), and whether the party's
   Arts can be parried in PvP. The window comes from the attack's hit frame on the logic
   timeline — for monsters the disc's own frames (`DT_EnemyActions`), so they are right by
   construction. How it sits beside the original's automatic block is part of the design. Its
   phase is not set yet (7 or later).
6. ~~AP in a chain~~ — settled 2026-11-04: each special is checked on its own, in order, against
   the AP at that point of the chain (the Arts before it count, the ones after do not); loose
   regular Arts add AP wherever they are; the Arts inside a fired Super add nothing (Phase 6 ›
   The input window). Credit at a regular Art's first hit and debit at a
   special's start stay as written; tune them in play if needed.
7. ~~Learning Arts by performing them~~ — settled 2026-11-04, the original's rules: Regulars on
   use (and they play as the Art), Hypers only taught (books, NPCs, triggers), a Super only over
   Arts known before the turn, Miracle by the Ra-Seru; `HasLearnedArt` for dialogue
   ([How each kind of Art is unlocked](#how-each-kind-of-art-is-unlocked-decided-2026-11-04)).
8. ~~The version switch~~ — settled 2026-11-04: the difficulty picks it (Easy = Japan,
   Normal = USA; Version rules). Whether the difficulty goes into the save is a separate,
   older question.

---

## Next concrete step

**State (2026-11-07):** Phases 0 to 6 are in `dev`. Phase 5 was played and merged with part of
Phase 7 (Spirit, formation advantage, the transition, the camera, voices, music, the command menu).

**Next, in this order:**

1. **A fix pass on Phase 5** — the three bugs still open in
   [BATTLE_PHASE5_BUGS.md](BATTLE_PHASE5_BUGS.md): the light floor on night maps (85), the impact
   sound on a blocked hit (86), the Ra-Seru equipped from the start (87). With it, one feature: the
   dev menu's equipment tab (88).
2. **The rest of Phase 7's rules** — Item (reserve, consume, `ApplyBattleItemEffect`), the escape
   roll, drop / steal.
3. **The rest of Phase 7's presentation** — the HUD (enemy row, AP bar with cost markers, HP
   dropping as the hits land, damage log), VFX, the victory screen, the sounds still missing.
4. **Phase 8** — magic and Seru.

Apart from the phases: the enemies' level curve, with the difficulty / New Game+ review; the
Classic / Modern combat option ([Deliberate departures](#classic-and-modern-combat-planned-not-scheduled));
the parry (open question 5).

**For the record — what Phase 6 delivered (2026-11-04):** `UJRPGArtsRules` (recognition with the
original's overlap, learn-on-use, Supers, the AP walk), `UArtsSubsystem` (the learned list in the
save, `LearnArt` / `HasLearnedArt`, commit from the bytes), `UJRPGArtsValidator` (+ Data
Validation + `Extras/arts_rules.py`), the `Unlock` / `SpeedScale` columns and the Art Tester's
`Input`.

**For the record — the plan Phase 5 was written from**, door open ([decided 2026-11-04](#pvp-online-and-co-op--door-open-decided-2026-11-04)):
first the contracts (5.0 — the round, the logic timeline and its fallback, items,
end of the fight, who gets the EXP) and the disc data the fight still needs (5.0 › `DT_EnemyActions`,
the target class, the Ra-Seru tiers, the per-button cost), then basic combat on the Muscle Dome —
combatant stats through the version rules, the original's turn order and AGL pool, the original's
melee kernel (miss by height, block, fold, underdog, element matrix), the action state machine,
the original's physical enemy AI, victory / defeat. With it, Phase 6's in-fight part: the input
window inside a turn (`CommandGauge` from AGL, the weapon's cost per button), `BuildChain` on the
combatant's snapshot and the fight's `Rng`, damage per hit from each step's `HitPower`, and AP
moving as the action reaches each Art. **Rule for all of it: what this
roadmap does not change on purpose follows the RE** ([Deliberate departures](#deliberate-departures-from-the-original)).
