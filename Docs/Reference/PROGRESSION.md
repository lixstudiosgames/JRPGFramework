# Progression: XP, stats and difficulty

Everything here was derived from the [`legend-of-legaia-re`][re] reverse-engineering project
(the original US disc) and validated against values the game shows on screen.

Sources: `docs/subsystems/level-up.md` and `docs/formats/new-game-table.md`.

[re]: https://github.com/andrewaltimit/legend-of-legaia-re

---

## 1. Starting stats (level 1)

A static table in `SCUS_942.54`, base `0x80078C4C`, stride 26 bytes (8 × `u16` + a 10-byte
name). HP and MP are also the starting maximums.

| Slot | Character | HP | MP | AGL | ATK | UDF | LDF | SPD | INT |
|---|---|---|---|---|---|---|---|---|---|
| 0 | Vahn  | 180 | 20  | 100 | 24 | 16 | 12 | 19 | 9  |
| 1 | Noa   | 150 | 10  | 120 | 21 | 13 | 11 | 30 | 3  |
| 2 | Gala  | 210 | 40  | 80  | 30 | 43 | 30 | 15 | 20 |
| 3 | Terra | 400 | 200 | 200 | 45 | 20 | 17 | 45 | 25 |

All four live in `DT_Characters` (`BaseHP`…`BaseINT`).

**Terra is the wolf who raised Noa** — roster slot 3. She is not a Ra-Seru bearer: Noa's
Ra-Seru is *also* called Terra, named after her. Two different things sharing a name. In the
RE project she appears as "Terra-the-wolf", with her own battle data file (index 3), **no
equipment sections at all** — a wolf equips neither weapon nor armor — and she is skipped by
the facial animator.

---

## 2. XP curve

Implemented in `UCoreSubsystem::GetXPForLevel` — **the formula is the runtime source of
truth**. `DT_LevelCurve` is generated from it and exists for reference and tuning in
LegaiaStudio.

```
delta(n) = n² / 4 + 1                        (table DAT_80076AF4, 98 entries)
sum(L)   = delta(1) + delta(2) + … + delta(L)

threshold(L → L+1) = sum(L) × 9 999 999 / 0x140FE    if L <  17
threshold(L → L+1) = sum(L) × 121                    if L ≥ 17
```

`delta(n)` **is** the disc's table: the 98 entries of `DAT_80076AF4` are exactly that closed
form, so there are no bytes to copy.

The threshold is **accumulated XP**, not what remains — it is the number the Status menu
shows as "Next Level".

**Validation** (three independent points, all matching):

| Level | Formula | Game |
|---|---|---|
| L2 | 121 | 121 (Status menu on a New Game) |
| L37 → L38 | 535,546 | 535,546 (field `+0x4` of a real L37 save) |
| L99 | 9,646,483 | 9,646,483 |

### Per-character correction

Noa levels slightly earlier and Gala slightly later:

```
correction = threshold × 0x14 / divisor
Noa  (slot 1): threshold − correction
Gala (slot 2): threshold + correction
Vahn (slot 0): no correction
```

In the original the `divisor` comes from a disc table (the start of the sine LUT sampled every
`0x28`: 125, 251, 376, …). Here we use the approximation `125 × (level − 1)`, which reproduces
the new-game thresholds:

| | Vahn | Noa | Gala |
|---|---|---|---|
| L2 threshold | 121 | 102 | 140 |

`UCoreSubsystem::GetXPForLevelForSlot(Level, PartySlot)`.

---

## 3. Stat growth per level

**Implemented and validated against the disc.** The parameters were extracted from
`SCUS_942.54` by `Extras/extract_growth_from_disc.py` and live in `DT_StatGrowth.csv`
(32 rows = 4 × 8) and `DT_GrowthCurve.csv` (98 rows).

On every level crossed, each of the 8 stats grows like this:

```
jitter_val = rand() % (2 × jitter + 1)        ; 0 .. 2×jitter
byte       = curve[row][level - 1]
gain       = (max − start) × byte / 9408      ; 9408 = 0x24C0
gain       = gain + jitter_val − jitter       ; recentered on [−jitter, +jitter]
gain       = max(1, gain)                     ; never grows less than 1
stat      += gain
```

Caps: HP ≤ 9999, MP ≤ 999, AGL ≤ 280 (`0x118`), everything else ≤ 999.

The divisor `9408` is the normalizer: each of the three curves sums to exactly `0x24C0`.

A level-up **does not heal**: the routine only writes maximums, stats and the level. Current
HP and MP stay wherever the battle left them.

### Why stats at L99 land below `max`

The division `(max − start) × byte / 9408` is integer (in the original, the magic multiply
`0x6F74AE27 >> 44`), and the sum is applied **level by level** (`record[stat] += gain`). Each
level discards the fraction — about 0.5 points on average — and it accumulates:

| Character | Stat | Computed L99 | `max` | Short by |
|---|---|---|---|---|
| Vahn | HP  | 4939 | 5000 | 61 (1.2%) |
| Vahn | INT | 378  | 450  | 72 (16%)  |
| Noa  | ATK | 373  | 440  | 67 (15%)  |
| Gala | AGL | 231  | 270  | 39 (14%)  |

The gap is proportionally larger for small-span stats (INT, ATK), because the per-level loss
is nearly constant while the span is not.

**This is faithful to the game, not a bug.** There is an alternative model that lands exactly
on `max` — accumulate the curve first and divide once,
`start + span × Σcurve[1..L−1] / 9408` — but it **contradicts the disc**: it gives +38 on
Noa's L2→L3 HP, while the RE project's byte-exact capture pins the core at **37**. The
disassembly (`0x801E9758..0x801E97F8`) accumulates per level.

If you ever want round maximums, the place to change is `UCoreSubsystem::GetStatGainCore` —
but then it is no longer the original.

### Terra has no parameters on the disc

The `DAT_80076918` block holds **3 records**. What follows the third is already another
structure.

The reason is simple: **in the original, Terra does not level up.** She joins strong, stays
the same throughout, and leaves the party when you finish Mount Rikuroa — after which it is
Vahn and Noa. With no level-ups, there is no reason for growth parameters to exist.

To make her genuinely playable, her caps were **designed by us** and live in the `DESIGNED`
dictionary inside `Extras/extract_growth_from_disc.py` — not loose in the CSV, so re-running
the extractor rebuilds the whole table instead of erasing them. The script also preserves rows
for any other character you create in LegaiaStudio.

| Stat | start (disc) | max (designed) | jitter | curve |
|---|---|---|---|---|
| HP  | 400 | 4200 | 4 | 0 |
| MP  | 200 |  700 | 1 | 0 |
| AGL | 200 |  290 | 1 | 1 |
| ATK |  45 |  430 | 1 | 0 |
| UDF |  20 |  380 | 1 | 0 |
| LDF |  17 |  360 | 1 | 0 |
| SPD |  45 |  520 | 1 | 1 |
| INT |  25 |  360 | 1 | 0 |

**On Normal she still does not grow** — the `MinDifficulty` column on her rows is `Hard`, and
`GetStatGainCore` returns 0 when the current difficulty is below the minimum. On Normal the
numbers above never come into play: she stays at her starting values all game, exactly like
the source game.

**On Hard and Juggernaut she grows.** It is the only exception to the rule that difficulty
never touches a player character, and it exists precisely because she has no progression in
the original: unlocking her growth on a heavy run is real help without altering Vahn, Noa or
Gala at all.

| Terra at L50 | Normal | Hard / Juggernaut |
|---|---|---|
| HP  | 400 | 2910 |
| ATK |  45 |  277 |
| SPD |  45 |  338 |

`UCoreSubsystem::DoesCharacterGrow(Character)` answers this directly, so the UI can decide
whether to show her numbers.

**Design intent: joins strong, then plateaus.** At L1 she leads 6 of the 8 stats; at L50 only
2 (AGL and SPD — a wolf, fast to the end); and her HP cap is the **lowest in the group**: 4135
at L99 against Noa's 4445 and Gala's 5245.

HP progression, for comparison:

| | L1 | L10 | L25 | L50 | L99 |
|---|---|---|---|---|---|
| Vahn  | 180 | 584 | 1439 | 3371 | 4939 |
| Noa   | 150 | 510 | 1280 | 3024 | 4445 |
| Gala  | 210 | 633 | 1536 | 3579 | 5245 |
| Terra | 400 | 715 | 1387 | 2910 | 4135 |

---

## 4. Groundwork for New Game+

NG+ does not exist yet. What is ready is the base it needs: the level cap is no longer fixed,
and growth works above 99 without a new table.

### Level cap

`GetMaxLevel()` reads the `MaxLevel` property (default **99**, the original's).
`SetMaxLevel(N)` changes it, clamped to `[1, AbsoluteMaxLevel]`.

**`AbsoluteMaxLevel` is 500, and the reason is arithmetic.** Accumulated XP is `int32` and the
curve grows with the cube of the level:

| Level | Accumulated XP |
|---|---|
| 99 | 9,646,483 |
| 200 | 80,083,729 |
| 500 | 1,256,690,754 (58.5% of `int32`) |
| **598** | **exceeds 2,147,483,647 — overflow** |

500 leaves comfortable margin. Going further would require moving XP to `int64`, and then the
practical ceiling moves to millions of levels.

### Growth above level 99

There is no table row above 98 — and none is needed. `GetStatGainCore` does
`FMath::Min(FromLevel, 98)`, repeating the last row.

**This invents no new rule.** The original's three curves are already **constant at 64 from
level 50 onward**: the entire second half of the game grows at a fixed rate. Extrapolating
just continues at the same pace, with no step at the boundary:

```
Vahn HP    L98 +32    L99 +32    L100 +32    L300 +32
Vahn ATK   L98 +3     L99 +3     L100 +3     L300 +3
```

Gain per level above 99 is `(MaxValue − GrowthStart) × 64 / 9408`, roughly `span / 147`.

| | L99 | L150 | L200 |
|---|---|---|---|
| Vahn HP | 4939 | 6571 | 8171 |
| Gala HP | 5245 | 6979 | 8679 |
| Vahn ATK | 441 | 594 | 744 |

### Stat caps

They moved out of a fixed function into the `StatCaps` map, filled in `Initialize` with the
original's values. `GetStatCap(Stat)` reads, `SetStatCap(Stat, N)` writes.

| Stat | Cap | Where it starts to bite |
|---|---|---|
| HP | 9999 | L239 (Gala) to L291 (Noa) |
| **AGL** | **280 — locked** | L115 (Noa) to L148 (Gala) |
| everything else | 999 | — |

> **AGL does not rise, and `SetStatCap` refuses it in code.** 280 is not an arbitrary round
> number: it is the **maximum number of art blocks** the Arts system supports. Raising AGL
> would generate more blocks than the command bar can display, breaking the battle UI with
> it. If that ever changes, the command bar has to change first.

HP is the cap that matters least in practice: it is only reached around L240–290. And note
that above 9999 it becomes **5 digits** — the status and Save/Load cards are laid out for 4.

### What NG+ still needs

- **A cycle counter** (1st, 2nd, 3rd run) in CoreSubsystem, multiplying XP and enemies on top
  of difficulty.
- **What carries over** (items, gold, flags) — a `StartNewGame` decision, which gains a
  parameter.

### Notes for the Party System

Things from the original worth remembering:

- **Terra does not appear in the original's UI.** Where HP and MP would be, the game shows
  `????`, and ATK/UDF/LDF and the rest are not shown at all. Displaying her stats is a remake
  decision, and only makes sense on the difficulties where she grows.
- **She leaves the party at the end of Mount Rikuroa.** After that the group is Vahn and Noa.
  That is an event flag plus party composition, not progression data.

### Where this lives

| Piece | File |
|---|---|
| Disc extraction | `Extras/extract_growth_from_disc.py` |
| Per-stat parameters | `Docs/Data/DT_StatGrowth.csv` (32 rows = 4 × 8) |
| The three curves | `Docs/Data/DT_GrowthCurve.csv` |
| Structs | `Public/Core/ProgressionTypes.h` |
| Runtime | `UCoreSubsystem::GetStatGainCore` / `RollStatGain` / `GetStatAtLevel` |

`GetStatGainCore` is deterministic (no jitter) — it is what the Status menu and predictions
should use. `RollStatGain` is what runs on a real level-up.

`DT_StatGrowth`'s `GrowthStart` **may differ** from the starting stat in `DT_Characters`: the
game retouches Vahn's and Noa's entry template when they join the group. Gala's eight match.

---

## 5. Difficulty

`EJRPGDifficulty`: **Normal**, **Hard**, **Juggernaut**.

**Difficulty touches enemies.** Vahn, Noa and Gala always grow on the original's curve, so
level, stats and XP stay comparable across difficulties — and the Status menu never shows a
number that disagrees with the source game.

**One deliberate exception:** anyone who does not level up in the original may be unlocked to
grow on higher difficulties, through `FStatGrowthRow`'s `MinDifficulty`. Today that applies
only to Terra (`Hard`). It never alters the three characters who have progression in the
original.

| | Normal | Hard | Juggernaut |
|---|---|---|---|
| Enemy HP | 1.0× | **1.8×** | **3.0×** |
| Enemy ATK | 1.0× | **1.2×** | **1.45×** |
| Enemy DEF (UDF/LDF) | 1.0× | **1.25×** | **1.4×** |
| XP awarded | 1.0× | 1.25× | 1.5× |
| Gold awarded | 1.0× | 1.25× | 1.5× |

XP and gold rise slightly on Hard/Juggernaut to compensate for longer fights, without turning
difficulty into a farming shortcut.

### Why the three axes use different multipliers

It is not arbitrary: Legaia's damage formula is **subtractive**
(`Damage = Offense − Defense`, `FUN_801EC3E4`), and because of that the three behave
completely differently.

**Enemy DEF saturates.** When your Offense does not beat the Defense, the game does not floor
damage at 1: it rewrites Offense *on top of* Defense (the "underdog rewrite"), and Defense
**cancels out in the subtraction**. Damage becomes ~29% of your own Offense regardless of how
large the defense is:

| Enemy DEF | Your Art's damage |
|---|---|
| ×1.0 (170) | 466 |
| ×1.5 (255) | 376 |
| ×2.5 (425) | 207 |
| ×5.0 (850) | 207 |
| ×10 (1700) | 207 |

Above ~1.5× the multiplier **does nothing at all** — it only pushes the player into the
underdog regime, where the marginal return on ATK drops from 1:1 to ~0.29:1 and weapon
progression starts paying less. Hence the low value.

**Enemy ATK has no floor** on the player's side: it hits UDF/LDF through the same subtraction,
without the underdog safety net. It is the most dangerous of the three, which is why it gets
the smallest multiplier. At ×2.5 the player takes **5.7×** Normal's damage.

**HP is the honest axis**: linear and predictable. That is where difficulty should weigh.

#### What happened with all three equal

Vahn at L40 from our tables (ATK 221, HP 2519, UDF 195) against a mid-game enemy (HP 1500,
ATK 190, DEF 170), using the RE project's example combo (two hits plus an Art). "Margin" is
how many turns you survive per turn you need to kill:

| | Combo damage | Combos to kill | Damage taken | Hits to die | Margin |
|---|---|---|---|---|---|
| Normal | 855 | 1.8 | 76 | 32.9 | **18.8** |
| ~~Hard 1.5/1.5/1.5~~ | 602 | 3.7 | 185 | 13.6 | 3.7 |
| ~~Juggernaut 2.5/2.5/2.5~~ | 432 | 8.7 | 446 | 5.7 | **0.65** |

A margin below 1.0 means the enemy kills first: the old Juggernaut was mathematically lost,
because fights lasted 4.8× longer **while** the player took 5.7× per hit — the two effects
multiply, giving ~27× harder, not 2.5×.

With the current multipliers:

| | Combo damage | Combos to kill | Damage taken | Hits to die | Margin |
|---|---|---|---|---|---|
| Normal | 855 | 1.8 | 76 | 32.9 | **18.8** (100%) |
| Hard | 719 | 3.8 | 110 | 23.0 | **6.1** (33%) |
| Juggernaut | 620 | 7.3 | 181 | 13.9 | **1.9** (10%) |

The enemy numbers are illustrative — `DT_Enemies` does not exist yet. The *structure* (DEF
saturates, ATK does not) holds for any enemy; the multipliers deserve a second pass once
there is real data. The simulation fixes random rolls at their average and ignores blocking,
Spirit stance, element and status.

#### Why not buff the player's stats instead

Three practical reasons:

1. It breaks the guarantee that justifies the progression system existing — that the Status
   menu shows numbers matching the original.
2. Buffing player ATK is the **least** efficient lever there is, because of the underdog
   rewrite: in the saturated regime marginal return drops to ~0.29:1.
3. Buffing player DEF is mathematically **equivalent** to lowering enemy ATK, only with more
   moving parts and without preserving the original numbers.

When the player needs help, the right place is `MinDifficulty` (that is how Terra grows on
Hard+): it strengthens the group without altering a single number for Vahn, Noa or Gala. Other
levers of the same kind, none of which show in the Status menu: drop rate, MP cost, AP gain
and shop prices.

### Usage

```cpp
UCoreSubsystem* Core = GetGameInstance()->GetSubsystem<UCoreSubsystem>();

Enemy.MaxHP  = Core->ScaleEnemyHP(BaseHP);
Enemy.Attack = Core->ScaleEnemyATK(BaseATK);
Enemy.UDF    = Core->ScaleEnemyDEF(BaseUDF);

const int32 XP   = Core->ScaleXPReward(BaseXP);
const int32 Gold = Core->ScaleGoldReward(BaseGold);
```

All are `BlueprintPure`, so they work directly in enemy and loot Blueprints — **the system is
already ready to receive enemies**.

The multipliers come from `DT_Difficulty` when that DataTable is assigned to
`UCoreSubsystem::DifficultyTable`; without it, the built-in defaults apply (the same numbers).
The game works without creating any asset.

The player chooses on the Options screen, **Game** tab. `SetDifficulty` fires
`OnDifficultyChanged`.
