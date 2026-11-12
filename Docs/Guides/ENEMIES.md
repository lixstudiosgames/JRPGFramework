# Enemy data (`DT_Enemies`)

The table every battle and every encounter reads its enemies from. One row per enemy the
original game has — 179 of them — keyed by the `EnemyKey` the encounters use (`gimard`,
`gimard_lv2`). Phase 1 of [BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md).

The battle (Phase 5, [BATTLE.md](BATTLE.md)) and the field encounters (Phase 3) are built on it.

---

## 1. Importing it

`FJRPGEnemyData` inherits `FTableRowBase`, so it shows up as a Row Structure once the plugin
is built.

Import `Docs/Data/DT_Enemies.csv` as a DataTable with Row Structure **`JRPGEnemyData`**,
named **`DT_Enemies`**, at **`/Game/Data/DT_Enemies`**. The CSV is the source of truth; edit it
in LegaiaStudio (*Enemies* in the sidebar) and re-import.

---

## 2. Column reference

### Identity

| Column | Type | Notes |
|---|---|---|
| `Key` | Name | snake_case, equal to the Row Name. Level variants: `gimard_lv2`; story forms: `songi_1st`, `gaza_sim_seru`, `cort_juggernaut` |
| `DisplayName` | Text | The name shown in battle. Level variants share it (`Gimard`) |
| `Location` | String | Where it appears in the original — a reference for building encounters |
| `bIsBoss` | Bool | 26 bosses |
| `Element` | `EJRPGElement` | `lightning` and `thunder` in the source are both `Thunder`. The generator checks it against the record's `+0x1D` byte |
| `TargetClass` | Int | The record's `+0x1E` byte: which of the heroes' hits reach the monster. `0` every hit; `2` **floating** (Evil Fly, bees, ghosts - 49 monsters): the low hits ("Lower, misses floating") miss it; `3` **short** (frogs - 6): the high hits ("Upper, misses short") miss it; `1` appears once. Hits with a power byte of `0x16` or more reach anyone. 123 monsters are `0`. See `UBattleFormulas::LimbMisses` |
| `SizeClass` | Int | The record's `+0x1F` byte, the size class that decides how far a strike reaches (the original's range law `FUN_8004E2F0`: threshold `{256, 384, 1024}[class]` PSX units under 3, else `class << 4`; 14..48 on the disc, 22 for 35 monsters). `0` = unknown (a row made by hand): the pawn falls back to `DefaultSizeClass` (22). See [BATTLE.md](BATTLE.md) |

### Stats — the raw record

The **raw record on the disc**, not what the player fights. The record is byte-identical on
every disc (USA, Japan, Europe); what changes is the executable, and each version does
something different with it. The table keeps the one thing they share; the version's rules are
code — see [Version rules](#version-rules-usa--japan) below. Difficulty
(`UCoreSubsystem::ScaleEnemyHP/ATK/DEF`) and the encounter's level go on top of what the
version installs.

| Column | Notes |
|---|---|
| `BaseHP`, `BaseMP` | |
| `BaseAGL` | The action gauge: every action spends it, it refills each round |
| `BaseATK` | |
| `BaseUDF`, `BaseLDF` | Upper / lower defense — which one applies depends on the attack |
| `BaseSPD` | Initiative: who acts first in the round |
| `BaseINT` | Magic damage and magic defense; also seeds hit/evade |

Same names as `FCharacterData`. (A field named plain `INT` would clash with the Windows
`INT` typedef.)

### Rewards

| Column | Notes |
|---|---|
| `EXP`, `Gold` | The raw record. What a fight pays depends on the version (below) |
| `DropItem` / `DropChance` | Key in `DT_Items` + chance in %. Empty = no drop |
| `StealItem` / `StealChance` | What the Evil God Icon can take. `StealChance` 0 = the default steal rule |
| `CaptureSeru` / `CatchRate` | The Seru a killing blow can absorb (`Gimard`) and the chance in %. Empty = not capturable. Magic Boost / Ivory Book add on top |

### Model (Phase 2)

| Column | Notes |
|---|---|
| `MonsterArchiveID` | The monster's slot in the disc's monster archive (PROT 0867, 1–194) — where its mesh and clips are. `asset monster-archive ... --id <this> --glb` exports it. `-1` = none |
| `Mesh` | Soft `USkeletalMesh` — filled by `Extras/generate_enemy_anims_csv.py` from the monster batch ([PS1_ASSETS.md](PS1_ASSETS.md#all-the-monsters-at-once)) |
| `Anims` | `FJRPGMonsterAnimSet`: `Idle`, `Walk`, `Flinch1`, `Flinch2`, `Knockdown`, `GetUp`, `Ready`, `Recover`, `Defeat`, `Block` and `Attacks` (array). In the CSV: `(Idle="/Game/...",Walk="/Game/...")`, `()` when empty. Each role is the clip of the disc action with that id (0 Idle, 1 Walk, 2/3 Flinch1/2, 4 Knockdown, 5 GetUp, 7/8/9 Ready/Recover/Defeat, 0x0B Block); `Attacks` are the castable actions (0x0C..0x1F with an AGL cost — the ones the game's AI rolls), in file order |

### Version rules (USA / Japan)

`EJRPGRuleset` picks which executable's rules a fight follows, and **the difficulty picks it**
(`FDifficultyScaling::Ruleset`, read with `UCoreSubsystem::GetRuleset`): **Easy = `Japan`**;
Normal, Hard and Juggernaut = `USA`. `UJRPGBattleRules` (`Public/Battle/JRPGBattleRules.h`) does
the arithmetic, integer and truncating like the PS1. Source: RE `docs/subsystems/battle.md`
› "Battle-load stat boost" / "No boost on the PAL executables", and
`docs/subsystems/battle-formulas.md` › "Victory spoils" / "Regional difference".

| | `USA` (SCUS_942.54) | `Japan` (SCPS_100.59, and the PAL SCES_019.44/45/46) |
|---|---|---|
| Stats installed (`InstallEnemyStats`) | Boosted. Scripted fight: ATK ×5/4, UDF/LDF ×2, INT ×9/8. Random encounter: ATK ×1, UDF/LDF ×7/4, INT ×5/4 | The record as is |
| EXP (`VictoryEXPPool`, `VictoryEXPShare`) | Sum × 3/4, split among the living rounding up | The whole sum, split the same way |
| Gold (`VictoryGold`) | Σ `gold >> 1`, Golden Book +¼, then halved again | Σ `gold >> 1`, Golden Book +¼ |

HP, MP, AGL and SPD never change. "Scripted" is the original's formation flag, not `bIsBoss`:
Tetsu's training spar and Lapis are scripted fights that install the random-encounter profile.

Checked against the RE's captures:

| Enemy (raw) | USA | Japan |
|---|---|---|
| Gaza (Sim-Seru), scripted — ATK/UDF/LDF/INT `288/222/200/220` | `360/444/400/247` | `288/222/200/220` |
| Gobu Gobu, random — `17/15/14/10` | `17/25/24/12` | `17/15/14/10` |
| Gimard — 60 Gold | 15 Gold | 30 Gold |
| Zeto — 8000 Gold / 9000 EXP, party of three | 2000 Gold + 2250 EXP each | 4000 Gold + 3000 EXP each |

The difficulty's multipliers (`DT_Difficulty`) apply after the version's rules. Easy and Normal
have them all at 1.0, so **Easy is the Japanese game and Normal the American one**; Hard and
Juggernaut are the American rules made harder. In the dev menu, `diff.set 3` is Easy and
`diff.set 0` Normal; `info` shows the rules in use.

---

## 3. Where the numbers come from, and regenerating

```bash
asset monster-archive extracted/PROT/0867_battle_data.BIN > monster_list.txt   # RE tools
python Extras/generate_enemies_csv.py --monster-list monster_list.txt \
    [--archive extracted/PROT/0867_battle_data.BIN --tool asset.exe] [--dry-run]
```

`--archive` and `--tool` (the monster archive and the RE's `asset.exe`) go together and fill `TargetClass` and `SizeClass`:
the generator dumps each monster's decoded block (`asset monster-archive ... --id N --dump-block`, into a
temporary folder - nothing from the disc is kept) and reads the record's `+0x1E` and `+0x1F` bytes, and it also checks the
TOML's `Element` against `+0x1D` (today: no difference; the two disc-only rows, Comm and Evil Bat, take the
record's). Without them the `TargetClass` and `SizeClass` already in the CSV are kept.

Python 3.11+ (it reads the TOML with `tomllib`). Two sources:

| Source | Gives |
|---|---|
| `Docs/Data/gamedata/enemies.toml` (from legend-of-legaia-re) | Name, location, element, drop / steal keys, boss flag; its stats are only used to check the join |
| The monster archive listing (from your disc) | `MonsterArchiveID`, `DropChance`, `CaptureSeru` / `CatchRate`, and the raw record that goes into the table: stats, `EXP`, `Gold` |

The generator joins them **by name** (the disc writes `Gimard $2` where the TOML says
`Gimard Lv2`). Names with several forms — Songi, Tetsu, Gaza, Cort — are told apart by HP,
which is identical in both. The TOML's numbers are the American scripted-fight view of every
enemy (the record × HP, SPD, AGL ×1 · UDF, LDF ×2 · ATK ×5/4 · INT ×9/8 · EXP ×3/4 · Gold
×5/16, the last one with the Golden Book), so the generator converts the disc record the same
way and reports anything more than 1 apart — that is what proves the join picked the right
record. Today: 177/177 matched, no stat off. The table then gets the raw record, not the TOML.

Until 2026-11-04 the table held the TOML's numbers. That baked one version's rules into the
data — and the scripted-fight profile into random encounters, and the Golden Book into every
Gold — so it could not serve the Japanese rules, and could not be turned back into the record
exactly (the shifts truncate per enemy).

What the report also tells you:

- **Only on the disc** — Comm (78) and Evil Bat (140) have no TOML entry. Their row is the
  disc record; `Element` and `Location` are left empty for you.
- **No disc record** — a TOML enemy the join cannot find keeps the TOML's numbers, which are
  *not* raw; it is listed so you can set `MonsterArchiveID` and regenerate. Today: none.
- **Identical records** — Cort 180 = 183–186 and Evil Bat 140 = 176–178 are byte-identical on
  the disc; the lowest id is used.
- **Ambiguous / not found** — would be listed for you to resolve; set `MonsterArchiveID` by
  hand and it survives the next run.

**Regenerating never loses manual work.** `Mesh`, `Anims`, a `MonsterArchiveID` set by hand,
and rows created in LegaiaStudio are kept; everything else is rewritten from the sources. The
previous CSV is copied to `Docs/Data/_backup/` first. Values hand-edited in other columns
*are* overwritten — that is what `--dry-run` and the backup are for.

---

## 4. Boss fights — `DT_Bosses`

A boss's **stats** live in `DT_Enemies` (`bIsBoss`). `DT_Bosses` is the layer of the **fight**:
who enters, the named special attacks, the fight's reward and the recommended level. Row
struct **`FJRPGBossData`** (`Public/Battle/BossFightData.h`); import `Docs/Data/DT_Bosses.csv`
into `/Game/Data/` with it.

| Column | Notes |
|---|---|
| `Key` | Row name: `caruban`, `songi_1st`, `gaza_sim_seru`, `caruban_expert` |
| `DisplayName` / `Location` | Shown name and where it happens |
| `FightCode` | The reference walkthrough's code (`B0001`..`B0014`, `s0005` for Lapis); empty for the Muscle Dome |
| `Enemies` | `DT_Enemies` rows of who fights, one per combatant (`Vigurox2` = `(viguro,viguro)`) |
| `bScriptedFight` | Bool, default `True`. The original's scripted-fight flag (`ctx+0x287`), which belongs to the **formation**, not to the boss: such a fight installs the boss stat profile ([Version rules](#version-rules-usa--japan)), cannot be escaped and rolls no formation advantage. `False` only for **Lapis**. Tetsu's training spar is the other story fight with the flag clear, but it is not in `bosses.toml`, so it has no row: whoever starts it sets `FJRPGBattleSetup::bScriptedFight = false` by hand. Whoever starts a fight from a row copies this into the setup |
| `HP` / `MP` | The walkthrough's numbers, for checking — the fight uses `DT_Enemies` |
| `SpecialAttacks` / `SpecialAttackMP` | The named specials and their MP cost, same order; the plain attack is not listed |
| `EXP` / `Gold` / `RewardItem` | The walkthrough's reward, for checking: the **American payout**, EXP **per member** (Zeto: 2250 / 2000). The fight pays from `DT_Enemies` through the version rules. `RewardItem` is a `DT_Items` key |
| `RecommendedLevel` | `"6-7"`, `"27"` |
| `Tournament` | `Expert` / `Master` for the Muscle Dome fights; empty in the story |
| `Notes` | Special rules (Koru's four-turn kill, Cort's shield, Jette's clones) |
| `Music` | The fight's track, a `DT_BattleMusic` row (`Boss1`, `Songi`, `Cort2`...); empty = the arena's. From a disc census of the scripted-battle records ([AUDIO.md › Battle music](AUDIO.md#battle-music)); kept on regeneration if set by hand |
| `bKeepCurrentMusic` | The fight leaves the music alone (Lu Delilas) |

**30 rows:** 22 story fights (the Delilas trio is three rows, one per sibling — each has its own
reward), Lapis, and 7 Muscle Dome tournament fights.

```bash
python Extras/generate_bosses_csv.py
```

From `Docs/Data/gamedata/bosses.toml` (legend-of-legaia-re). `Enemies` is found by name
(`"Songi (1st)"` → `songi_1st`, the `(Expert)` / `(Master)` suffix goes to `Tournament`, `x2` repeats
the row); anything that does not match is reported. Regenerating keeps an `Enemies` or a `Music` fixed by hand
and fights created in LegaiaStudio; the old CSV is backed up first.


---

## 5. Enemy actions - `DT_EnemyActions`

What a fight needs from each monster clip, and that is not a notify: **one row per action entry of the
monster archive** (1753 rows, 9 to 12 per monster). It is the disc's own action entry - the fight builds
the enemy's logic timeline from it (hit frames, power per hit, rate), the AI queues swings with its AGL
cost, and the pawn plays `Anim`. Row struct **`FJRPGEnemyActionData`**
(`Public/Battle/EnemyActionData.h`); import `Docs/Data/DT_EnemyActions.csv` into `/Game/Data/` with it.

| Column | Notes |
|---|---|
| row name | `<EnemyKey>_<NN>_<Clip>`, NN = the entry's index in the monster's file: `gimard_06_Attack1` |
| `Enemy` | The `DT_Enemies` row |
| `ArchiveID`, `ActionIndex` | The monster's slot in PROT 0867 and the entry's index |
| `Tag` | The role: 0 idle, 1 walk, 2/3 flinch, 4 knockdown, 5 get-up, `11` (0x0B) **block**, `12..31` (0x0C..0x1F) the swings and specials (`13/14/15` the attacks), `32..34` approach, `35` (0x23) the cast clip |
| `Clip`, `Anim` | The clip's name and its `UAnimSequence` (`AS_<Monster>_<Clip>`, the same names the model batch gives; empty if the monster is not imported yet) |
| `Power` | The four power bytes `+0x00..+0x03`, one per hit (the first is also the tag). A byte gives the hit's multiplier (12/18/20/22/28) and the defence it faces: `Power = [12,18,20,22,28][(b - 0x0C) % 5]`, UDF when `(b - 0x0C) % 10 < 5` |
| `HitFrames` | The hit beats `+0x10..+0x13`, ascending, `()` when the clip has none. On an attack they are its hits; on a flinch the first one is the **juggle window** |
| `AGLCost` | `+0x74`: what the AI pays from the monster's AGL to queue the swing. `255` = not an attack |
| `Gate` | `+0x76`: `0` lets the next swing of the turn start in the middle of this clip |
| `Rate` | `+0x78`, 1 or 2: the clip plays at `15 x Rate` frames/s at the game's normal speed |
| `StatusSelector` | `+0x7A`: the status/impact enum (Phase 8) |
| `FrameCount` | The clip's frames |

Gimard (id 10) - block at beat 5, swings `0x0D` at beat 7 (AGL 28) and `0x0F` at beat 8 (AGL 32), flinch
window 9, all at rate 2:

```
gimard_05_Block,   Tag 11, Power (11,0,0,0), HitFrames (5), AGLCost 0
gimard_06_Attack1, Tag 13, Power (13,0,0,0), HitFrames (7), AGLCost 28
gimard_07_Attack2, Tag 15, Power (15,0,0,0), HitFrames (8), AGLCost 32
```

```bash
asset monster-archive extracted/PROT/0867_battle_data.BIN > monster_list.txt   # RE tools, once
python Extras/generate_enemy_actions_csv.py \
    --archive extracted/PROT/0867_battle_data.BIN --tool <asset.exe> \
    [--manifest exports/monsters/all/monsters.json] [--dry-run]
```

It reads the decoded block of every monster in `DT_Enemies` (`Extras/monster_block.py`; two seconds in
all) and writes the CSV. The optional manifest (the model batch's `monsters.json`) gives each clip its
name and `Anim` path; without it the clips are `Action_0x<tag>` and `Anim` stays empty. Regenerating
keeps an `Anim` set by hand and the rows of a monster that is not in `DT_Enemies`; the old CSV goes to
`Docs/Data/_backup/`. Import as a DataTable with Row Structure **`JRPGEnemyActionData`**, named
**`DT_EnemyActions`**, at **`/Game/Data/DT_EnemyActions`**.

Two notes: a monster with no swing at all (`cort`: scripted by the original's per-monster `switch`, Phase 8)
makes the fight fall back to a default swing and say so in the log; and the 7 duplicate records the disc
holds (140 = 176-178, 180 = 183-186) are not in `DT_Enemies`, so they are not here either.

---

## 6. What the fight reads about the heroes

Two small tables, generated from the disc by `Extras/generate_hero_battle_csv.py`:

```bash
python Extras/generate_hero_battle_csv.py --extracted <LegaiaRE/extracted> --tool <asset.exe> [--dry-run]
```

| Table | Row struct | What |
|---|---|---|
| `DT_RaSeruTiers` | `FJRPGRaSeruTierData` | One row per hero (`Vahn`, `Noa`, `Gala`): `TierATK`, the ATK of the Ra-Seru at each tier - Meta `10 20 30 40 50 60 70 85 100`, Terra `16 26 36 46 56 66 81 96`, Ozma `24 36 48 60 72 89 106`. The tier is the hero's `RaSeruLevel` (past the last tier the last one counts; 0 = no Ra-Seru yet, 0 ATK). In the original the Ra-Seru is an item of its own (`Ra-Seru Meta $1..$9` in the SCUS equipment table); here it is one of the five pieces the damage folds. Source: `asset item-tables SCUS_942.54 --equipment-only` |
| `DT_WeaponCosts` | `FJRPGWeaponCostData` | `<Hero>_<Weapon>` -> `ArmsCost`: what the hero's **Arms button** costs on the command gauge with that weapon - `30` the favoured class, `42` one class away, `54` far off (Vahn's Astral Sword; Noa with a club or an axe). Only weapons the hero can equip and whose cost is not 30 have a row (18); everything else, and the other three buttons, cost `UJRPGArtsRules::DefaultButtonCost` (30). Source: the weapon section of the battle files PROT 863 / 864 / 865 (`[+0x04] + 0x74`), `asset battle-data-pack`; the values are the table in the RE's `arts-command-gauge.md` |
| `DT_HeroSwings` | `FJRPGHeroSwingData` | One row per **hero + item + button** (114 rows), `<Hero>_<Item>_<Button>` (`Gala_war_god_boots_Low`). Columns: `Character`, `Item` (the `DT_Items` key; `default` = the section default, nothing equipped; `seru_N` = Ra-Seru tier N, since the Ra-Seru is the hero's `RaSeruLevel` and not an item), `Button` (`Arms` / `RaSeru` / `High` / `Low`), `PowerBytes` (one per hit, 1 or 2), `HitBeats` (the clip frame of each hit, the same count), `Rate` (1 or 2) and `Cost` (the button's gauge cost). The weapon drives Arms (Noa's right arm), the Ra-Seru tier the Ra-Seru button (Noa's Terra is her left arm), the shoes the two kicks. Read from the same swing records as the costs (record `+0x00..+0x03`, `+0x10..+0x13`, `+0x78`, `+0x74`). Items the table does not know (Feral, Mace and a Vahn id with no name) fall back to `default` |

| `DT_HeroReactions` | `FJRPGHeroReactionData` | One row per hero (`Vahn`, `Noa`, `Gala`, `Terra`). Columns: `FlinchHighFrames`, `FlinchLowFrames`, `BlockFrames`, `KnockdownFrames`, `GetUpFrames`, `DyingFrames` (the entry 7 of a hero who dies) and `DownLoopFrames` (the looping entry 8, informative) - the length of each reaction clip of the battle file in logic frames (`4 * keyframes / rate`; Vahn knockdown 32 + get-up 50, Noa 56 + 60, Gala 38 + 56). The fight waits for them after a hit before the next action starts. Read from the `record[0]` reaction entries (tags 2 / 3 / 4 / 5 / 7 / 8 / `0x0B`) by `generate_hero_battle_csv.py` (needs the RE's `lzs-decode`). Terra's entries 7 and 8 are empty on the disc (0) |

Import each as a DataTable with its Row Structure, named like the file, at `/Game/Data/`. Without
`DT_RaSeruTiers` the Ra-Seru piece is 0 ATK; without `DT_WeaponCosts` every button costs 30 - both log a
warning when the fight starts, and the fight goes on. Without `DT_HeroSwings` the plain swings use the default byte of the
hero (`UBattleFormulas::DefaultSwingPowerByte`) and `FJRPGTimelineDefaults::SwingHitFrame` (one line in the log). When both
tables exist, a row's `Cost` wins over `DT_WeaponCosts` (an item with no row keeps the `DT_WeaponCosts` cost); `DT_WeaponCosts` stays as it is.
