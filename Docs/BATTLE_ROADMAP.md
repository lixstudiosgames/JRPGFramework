# Battle System Roadmap

**JRPGFramework — Legend of Legaia Remake**
Approved 2026-09-13. **Nothing implemented yet.**

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
| **0** | Extract assets from the disc (Rust + `legaia-extract`) | Not started |
| **1** | Battle data (TOML → CSV → DataTable) | Not started |
| **2** | PS1 3D assets (GLB + skin injection → SkeletalMesh) | Not started |
| **3** | Enemies roaming the map (zone, roaming, trigger) | Not started |
| **4** | Battle stage (spawn, formation, camera) | Not started |
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

1. Install the Rust toolchain (`rustup`, stable MSVC).
2. `cargo build --release` at the root of `legend-of-legaia-re` (31 crates, ~10 min).
3. `./target/release/legaia-extract "<path to Legend of Legaia (USA).bin>" --out extracted`
4. `cargo test --workspace` — must pass before the project is usable as a reference.

**Acceptance:** `asset monster-archive extracted/PROT/0867_*.BIN --id 10 --glb gimard.glb`
produces a file that opens in Blender with a textured mesh and a list of animations.

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
| `magic.toml` | 29 spells (table now, system in Phase 8) |

Two things are not what they look like:

- **`bosses.toml` is not the sibling of `enemies.toml`.** Boss stats already live in
  `enemies.toml` (`boss = true`); this file is the *encounter* layer (`meth_id`,
  `attacks`, `exp_reward`, `gold_reward`, `item_reward`, `recommended_level`).
- **`arts.toml` has no `power` field.** Per-hit damage comes from the **Art Record on the
  disc**: up to 4 *power bytes*, each encoding both the multiplier (12/18/20/22/28) **and**
  the defense target (UDF or LDF), plus `dmg_timing` (which animation frame each hit fires
  on) and `anim_index`.

### New structs

Hard rule: `Extras/LegaiaStudio/studio.py` discovers schemas by regex over the headers.
One `UPROPERTY` per line, no `meta=(...)` with nested parentheses, struct closing with
`\n};` and inheriting `: public FTableRowBase`. Use `Public/Shop/ShopData.h` as the model.

**`Public/Battle/EnemyData.h`**

```cpp
UENUM(BlueprintType) enum class EJRPGCombatantType : uint8 { Character, Seru, Enemy };

USTRUCT(BlueprintType) struct FJRPGEnemyData : public FTableRowBase {
  FName Key; FText DisplayName; EJRPGElement Element; FString Location;
  int32 HP, MP, EXP, Gold, ATK, SPD, UDF, LDF, INT, AGL;
  FName DropItem, StealItem; int32 StealChance; bool bIsBoss;
  int32 MonsterArchiveID = -1;          // PROT 0867 slot (Phase 2)
  TSoftObjectPtr<USkeletalMesh> Mesh;   // filled in Phase 2
  FJRPGMonsterAnimSet Anims;            // nested struct, 10 fixed slots
};
```

`FJRPGMonsterAnimSet` holds one `TSoftObjectPtr<UAnimSequence>` per semantic tag (`Idle`,
`Walk`, `Flinch1`, `Flinch2`, `Knockdown`, `GetUp`, `Ready`, `Recover`, `Defeat`, `Block`) —
a `TMap` does not import from CSV cleanly.

**`Public/Arts/ArtsData.h`**

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

Also: **`Public/Battle/BossFightData.h`** (`FJRPGBossData`) and
**`Public/Battle/MagicData.h`** (`FJRPGSpellData`).

### Converters

Three new scripts in `Extras/`, modeled on `generate_shops_csv.py` (the best template —
it has `strip_comment()` that respects quotes, multi-line array joining, and join
validation): `generate_arts_csv.py`, `generate_enemies_csv.py`, `generate_magic_csv.py`.

Pull the shared parser out into **`Extras/legaia_toml.py`** first, or you end up with five
diverging copies of the same weak parser.

Three concrete traps:

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
row name. Without that, Phase 2's work is lost on every run.

> Known debt: the existing `generate_*.py` scripts have a **wrong** hardcoded path
> (`CODE\JRPGFramework\Docs\Data`, missing the `5.8`). Fix that in the same pass.

**Acceptance:** `DT_Enemies`, `DT_Arts`, `DT_Bosses`, `DT_Magic` imported under
`/Game/Data/`; LegaiaStudio lists the four new structs; a Blueprint calling
`GetRowNames()` returns 177 / 60 / 30 / 29.

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

### `Extras/glb_inject_skin.py`

A stdlib Python post-processor (`struct` + `json`) that opens the `.glb`, adds the
skeleton, and re-emits it. Preferable to patching the Rust: `legend-of-legaia-re` is a
third-party upstream and a fork costs a rebase on every update.

What it does:

1. Object nodes become **pure joints** — they lose `mesh`, keep their rest TRS, and stay
   the animation channels' targets (no `target.node` changes, which is what makes the
   injection cheap).
2. A new node carries the merged mesh with `"skin": 0`, **keeping one primitive per
   object** (do not merge materials: the exporter bakes an atlas per `(cba, tsb)` pair
   with `COLOR_0` carrying the packet modulation).
3. Each primitive gets `JOINTS_0` (`[i,0,0,0]`) and `WEIGHTS_0` (`[1,0,0,0]`).
4. `skins[0]` with `joints` and `inverseBindMatrices`.

**The math you cannot guess at.** Correct rigid skinning needs two things together:
vertices **pre-transformed by the rest TRS** (which the exporter already writes on the
node — it is frame 0 of clip 0) and `IBM_i = inverse(globalRest_i)`. Then
`globalJoint_i * inverse(globalRest_i) * globalRest_i * v_local = globalJoint_i * v_local`.

> Tempting and wrong: leave vertices in local space with `IBM = identity`. The animation
> still resolves, but the bind pose stacks every piece at the origin — invalid reference
> pose, broken preview, useless PhysicsAsset.

### Enemies

`asset monster-archive extracted/PROT/0867_*.BIN --id N --glb out.glb` already exports
geometry + texture + **every** animation. 186 of 194 slots have a mesh. Actions carry
**semantic tags**: `0`=idle, `1`=walk, `2`/`3`=flinch, `4`=knockdown, `5`=get-up,
`7`/`8`/`9`=ready/recover/defeat, `0x0B`=block — exactly the shape of
`FJRPGMonsterAnimSet`.

Naming (Interchange uses the glTF clip name, so the injector renames before re-emitting):
`AS_Gimard_Idle`, `AS_Gimard_Flinch1`, … and `AS_Gimard_Action_0xNN` for untagged slots.

`Extras/import_ps1_assets.py` orchestrates: walk the 194 ids, skip the empty ones, run the
injector, drop the result in `Content/Legaia/PS1/Battle/Monsters/<id>/`.

### Heroes in battle form

The kernels exist (`battle_char_assembly::loadout::build` +
`character_gltf::build_character_glb`) but are only exposed through the RE project's web
viewer. Two ways out:

1. **Use the project site's Arts page**, which runs as WASM against your own disc image and
   has a "download this character with every battle animation" button. Four characters —
   one manual pass, no code.
2. Automate with a new `asset battle-char-glb --cslot N --glb out.glb` subcommand
   (`clip_bank()` in `loadout.rs:641` is private and would need to be made public).

Bone counts: Vahn 15, Noa 16, Gala 15, Terra 17, plus equipment extras (ids 200/201). The
battle mesh is **assembled per equipment loadout**, so export with the intended loadout set.

### Scale

The exporter works in raw PSX units; the project convention is 128 units = 1 m and Unreal
is centimeters, so the factor is `100/128 = 0.78125`. **Treat that as a hypothesis:** import
Vahn, measure the bounding box against ~170 cm, set `Import Uniform Scale` once and record
the number in `Docs/Reference/`.

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

So Phase 2 produces a **separate** asset set under `Content/Legaia/PS1/Battle/` and touches
nothing already in the host. No field rig is reused and no retargeting is needed.

**Acceptance:** Gimard exists as a `USkeletalMesh` with 10+ named `UAnimSequence`s and
plays in the preview without loose pieces; battle-form Vahn exists with the full Arts bank.

> **Validate with ONE enemy before looping over 186.** If Interchange rejects the file, the
> problem is local and cheap. Known fallback: headless Blender (`bpy`) as a GLB→FBX step.

---

## Phase 3 — Enemies roaming the map

**Goal:** visible enemies that wander, chase and disappear. Still **no battle** — contact
just logs.

### `AJRPGEncounterZone` (the spawn box)

An `AActor` with a `UBoxComponent` as its area. Everything `EditAnywhere`, configured per
instance:

```cpp
USTRUCT(BlueprintType) struct FJRPGEncounterEntry {
  FName EnemyKey;                      // resolves in DT_Enemies
  int32 LevelMin = 1, LevelMax = 1;
  int32 Weight = 1;                    // draw weight
  int32 CountMin = 1, CountMax = 1;    // how many of this type join the fight
};

UPROPERTY(EditAnywhere) TArray<FJRPGEncounterEntry> Encounters;
UPROPERTY(EditAnywhere) int32 MaxAlive = 4;              // pawns roaming at once
UPROPERTY(EditAnywhere) float RespawnDelayMin = 30.f;
UPROPERTY(EditAnywhere) float RespawnDelayMax = 90.f;
UPROPERTY(EditAnywhere) int32 EnemiesPerBattleMin = 1;
UPROPERTY(EditAnywhere) int32 EnemiesPerBattleMax = 3;
UPROPERTY(EditAnywhere) USceneComponent* BattleAnchor;   // WHERE the fight happens
UPROPERTY(EditAnywhere) bool bFightWhereTouched = false; // or fight on the spot
UPROPERTY(EditAnywhere) FName RequiredFlagID;            // optional WorldState gate
```

`BattleAnchor` is a `USceneComponent` you drag in the editor — the point the party and
enemies spawn around. `bFightWhereTouched` does the opposite: stage the fight where the
player made contact, using the pawn's location as the anchor.

Responsibilities: weighted draws, keeping `MaxAlive` pawns alive, scheduling respawns with
a randomized delay (so the player cannot clear an area and be left with no fights), and
assembling the combatant list when an encounter fires.

### `AJRPGRoamingEnemy` (one class, swapped per instance)

An `APawn` with a `USkeletalMeshComponent`, a `USphereComponent` (detection radius) and a
touch collider. **One class only**: mesh, animations and stats come from `DT_Enemies` via
`InitFromEnemyData(FName EnemyKey, int32 Level)`, so swapping the enemy is swapping an
`FName`.

States (a plain enum, no Behavior Tree — keeps the plugin free of an AIModule dependency):

- `Wander` — pick a random point inside the zone, walk there, wait, repeat.
- `Chase` — the player entered the detection sphere: pursue. Leaving the radius plus a
  give-up timer returns to `Wander`.
- `Dead` — hidden, collision off; the zone schedules the respawn.

Touch overlap → `OnEncounterTriggered.Broadcast(Zone, this)`.

### The level hook

`FJRPGEncounterEntry::LevelMin/Max` rolls the enemy's level, and the level scales the
`DT_Enemies` stats on top of the existing `UCoreSubsystem::ScaleEnemyHP/ATK/DEF`. The
per-level curve is new and stays configurable — the disc table only supplies base values.

**Acceptance:** drop an `AJRPGEncounterZone` into a host map, configure 3 enemy types, see
4 pawns walking; enter the radius and get chased; touch one and see a log listing the drawn
enemies and the `BattleAnchor` transform; kill one via a debug command and watch it respawn
after the delay.

---

## Phase 4 — The battle stage

**Goal:** contact assembles a fight scene over the current map, then tears it down. Still
**no combat** — enter, position, frame, leave.

Battle **loads no map**. That is not a shortcut: it is how the original works — the fight
happens in the scene where the encounter triggered and only the camera changes. It also
matches the split this project already has (world in Blueprint, systems in C++).

### `ABattleStage` (Blueprint-able, in the plugin)

Listens to `UBattleSubsystem::OnBattleStarted` and owns everything `AActor`-shaped:

- Spawns one `AJRPGBattlePawn` per combatant at the computed formation.
- Registers the pawns with `UCameraSubsystem` and calls
  `FrameGroup(Pawns, 0.75f, 1.15f, ECameraFramingPreset::BattleWide, false)`.
- Disables the field pawn's input and hides roaming enemies that did not join.
- On teardown: destroys the pawns, restores camera and input.

`UBattleSubsystem` **never knows about `AActor`** — it speaks only through delegates, the
same separation the rest of the plugin keeps.

### Flexible formation

No static seat table. The formation is computed from the actual count:

```cpp
// Arc around the BattleAnchor. Party faces the anchor's +X, enemies face -X.
FTransform SeatFor(int32 Index, int32 Count, EJRPGBattleSide Side) const;

UPROPERTY(EditAnywhere) float PartyRadius = 250.f;
UPROPERTY(EditAnywhere) float EnemyRadius = 400.f;
UPROPERTY(EditAnywhere) float ArcDegrees  = 90.f;
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

**Acceptance:** contact removes player control, the camera frames the fight, 3 heroes spawn
on one side and N enemies on the other in their `Idle` poses; a debug command ends it and
restores everything. Set `MaxPartySlots` to 5 and watch the formation adapt.

---

## Phase 5 — Basic combat

**Goal:** a fight that works end to end with plain attacks. No Arts, no magic.

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

FOnBattleStarted OnBattleStarted;   // for ABattleStage
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

- Spell system over the `DT_Magic` table imported in Phase 1.
- **Spell power is missing** — it is not in `magic.toml`. It comes from the disc's
  move-power table (PROT 0898, stride 26), so an extra extraction pass produces
  `DT_MovePower`.
- Seru capture in battle and learning the matching spells.
- **Serus as party support members** — this is where Phase 4's larger party gets real
  content.
- Ra-Seru summons (200–255 MP, gated behind sidequests).

---

## Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Skin injection rejected by Interchange (skewed bind pose, collapsed mesh) | **High** — gates all of Phase 2 | Prove it with **one** enemy before any loop. Fallback: headless Blender as an intermediate converter |
| ATK double-counted (`GetEffectiveStat` already includes equipment) | **High**, because it is invisible — inflates damage uniformly | Explicit test: Vahn's combatant with a sword equipped must read `ATK == base + Seru bonus` |
| PS1 integer arithmetic translated as float | Medium | `int32`/`int64` throughout the damage kernel, no `FMath::` |
| Larger party breaking balance | Medium | `bScaleEnemiesWithPartySize` from Phase 4, multipliers exposed as `UPROPERTY` |
| Regenerating a CSV wiping hand-made mesh/anim mapping | Medium, destructive | Merge by row name in the generator + timestamped `.bak` in `Docs/Data/_backup/` |
| `bosses.toml` inline tables breaking the parser | Low | Dedicated regex; validate 30/30 rows |
| Importing 186 enemies by hand | Low (tedium, not risk) | If it hurts, add a second `JRPGFrameworkEditor` module with a batch `UAssetImportTask` commandlet |

---

## Open questions to settle in practice, not on paper

1. **Does UE 5.8's Interchange honor `inverseBindMatrices` and build `UAnimSequence`s from
   node channels targeting joints?** It is the format's expected behavior but was not
   verified. Answered by importing the first GLB — 20 minutes, and it is Phase 2's first step.
2. **PSX→cm scale.** `100/128` is deduction, not measurement. Import Vahn, measure, fix it.
3. **`EAutomationTestFlags` in 5.8** was renamed recently — check `AutomationTest.h` before
   writing the first automated test.
4. **`UGameInstanceSubsystem` + `FTickableGameObject`** — a known pattern, but confirm
   whether `ETickableTickType::Conditional` is enough or `IsTickableWhenPaused()` is needed.

---

## Next concrete step

**Phase 0, item 1:** install `rustup` and build the RE workspace. Without `extracted/`,
Phases 1 and 2 have no data and no assets to work from — and they are the only two that can
run in parallel.
