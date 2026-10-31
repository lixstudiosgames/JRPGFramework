# Enemy data (`DT_Enemies`)

The table every battle and every encounter reads its enemies from. One row per enemy the
original game has — 179 of them — keyed by the `EnemyKey` the encounters use (`gimard`,
`gimard_lv2`). Phase 1 of [BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md).

No system reads it yet: it is the data the battle (Phase 5) and the field encounters
(Phase 3) are built on.

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
| `Element` | `EJRPGElement` | `lightning` and `thunder` in the source are both `Thunder` |

### Battle stats

The **in-battle** values — what the player actually fights — not the raw record on the disc.
The original's loader boosts the record when a fight loads; the table already holds the
result. Difficulty (`UCoreSubsystem::ScaleEnemyHP/ATK/DEF`) and the encounter's level go on
top of these.

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
| `EXP`, `Gold` | Already the scaled reward |
| `DropItem` / `DropChance` | Key in `DT_Items` + chance in %. Empty = no drop |
| `StealItem` / `StealChance` | What the Evil God Icon can take. `StealChance` 0 = the default steal rule |
| `CaptureSeru` / `CatchRate` | The Seru a killing blow can absorb (`Gimard`) and the chance in %. Empty = not capturable. Magic Boost / Ivory Book add on top |

### Model (Phase 2)

| Column | Notes |
|---|---|
| `MonsterArchiveID` | The monster's slot in the disc's monster archive (PROT 0867, 1–194) — where its mesh and clips are. `asset monster-archive ... --id <this> --glb` exports it. `-1` = none |
| `Mesh` | Soft `USkeletalMesh` — filled by `Extras/generate_enemy_anims_csv.py` from the monster batch ([PS1_ASSETS.md](PS1_ASSETS.md#all-the-monsters-at-once)) |
| `Anims` | `FJRPGMonsterAnimSet`: `Idle`, `Walk`, `Flinch1`, `Flinch2`, `Knockdown`, `GetUp`, `Ready`, `Recover`, `Defeat`, `Block` and `Attacks` (array). In the CSV: `(Idle="/Game/...",Walk="/Game/...")`, `()` when empty. Each role is the clip of the disc action with that id (0 Idle, 1 Walk, 2/3 Flinch1/2, 4 Knockdown, 5 GetUp, 7/8/9 Ready/Recover/Defeat, 0x0B Block); `Attacks` are the castable actions (0x0C..0x1F with an AGL cost — the ones the game's AI rolls), in file order |

---

## 3. Where the numbers come from, and regenerating

```bash
asset monster-archive extracted/PROT/0867_battle_data.BIN > monster_list.txt   # RE tools
python Extras/generate_enemies_csv.py --monster-list monster_list.txt [--dry-run]
```

Python 3.11+ (it reads the TOML with `tomllib`). Two sources:

| Source | Gives |
|---|---|
| `Docs/Data/gamedata/enemies.toml` (from legend-of-legaia-re) | Name, location, element, drop / steal keys, boss flag, the in-battle stats |
| The monster archive listing (from your disc) | `MonsterArchiveID`, `DropChance`, `CaptureSeru` / `CatchRate`, and the raw stats used to cross-check |

The generator joins them **by name** (the disc writes `Gimard $2` where the TOML says
`Gimard Lv2`). Names with several forms — Songi, Tetsu, Gaza, Cort — are told apart by HP,
which is identical in both. It then checks every stat of the TOML against the disc record
converted by the loader's factors (HP, SPD, AGL ×1 · UDF, LDF ×2 · ATK ×5/4 · INT ×9/8 ·
EXP ×3/4 · Gold ×5/16) and reports anything more than 1 apart. Today: 177/177 matched, no
stat off.

What the report also tells you:

- **Only on the disc** — Comm (78) and Evil Bat (140) have no TOML entry. Their row is built
  from the disc with the same factors; `Element` and `Location` are left empty for you.
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
| `HP` / `MP` | The walkthrough's numbers, for checking — the fight uses `DT_Enemies` |
| `SpecialAttacks` / `SpecialAttackMP` | The named specials and their MP cost, same order; the plain attack is not listed |
| `EXP` / `Gold` / `RewardItem` | The fight's reward (`RewardItem` is a `DT_Items` key) |
| `RecommendedLevel` | `"6-7"`, `"27"` |
| `Tournament` | `Expert` / `Master` for the Muscle Dome fights; empty in the story |
| `Notes` | Special rules (Koru's four-turn kill, Cort's shield, Jette's clones) |

**30 rows:** 22 story fights (the Delilas trio is three rows, one per sibling — each has its own
reward), Lapis, and 7 Muscle Dome tournament fights.

```bash
python Extras/generate_bosses_csv.py
```

From `Docs/Data/gamedata/bosses.toml` (legend-of-legaia-re). `Enemies` is found by name
(`"Songi (1st)"` → `songi_1st`, the `(Expert)` / `(Master)` suffix goes to `Tournament`, `x2` repeats
the row); anything that does not match is reported. Regenerating keeps an `Enemies` fixed by hand
and fights created in LegaiaStudio; the old CSV is backed up first.
