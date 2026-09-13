# Party System

Owns the state of the player's characters: who belongs to you, who is fighting, level, XP,
stats, HP/MP.

---

## The core idea: three independent states

| State | Meaning | Who changes it |
|---|---|---|
| **Recruited** | Joined the group at some point. **Irreversible** — from then on the data is the player's forever | `RecruitCharacter` |
| **Available** | Present in the story right now | `SetAvailable` |
| **Active** | In the formation that fights | `SetActive` |

Taking someone out of the formation **changes nothing** in their record. That is what lets
Vahn spend hours away and come back exactly as he was.

```
Roster      everyone ever recruited. Grows and never shrinks. This is what the save holds
Formation   the subset that fights (1 to MaxActiveMembers)
```

---

## API

### Recruitment

| Function | Behavior |
|---|---|
| `RecruitCharacter(Key)` | Creates the record with starting stats from `DT_Characters`. **Idempotent**: calling it again on someone already yours resets nothing, it only returns them to the story |
| `IsRecruited(Key)` | — |
| `SetAvailable(Key, bool)` | Removes from / returns to the story. Leaving preserves the whole record (Terra after Mount Rikuroa) |
| `IsAvailable(Key)` | — |

### Formation

| Function | Behavior |
|---|---|
| `SetActive(Key, bool)` | Adds/removes from the formation. Refuses if unavailable, if the formation is full, or if it is the **last** active member |
| `SetActiveParty(Keys)` | Swaps the whole formation. All of it applies, or nothing changes |
| `GetActiveParty()` / `GetActiveMembers()` | Keys / records of the formation, in order |
| `GetRoster()` / `GetMember(Key, Out)` | Everyone / one |
| `MaxActiveMembers` / `SetMaxActiveMembers(N)` | 3 matches the original. The party is **not fixed** — you can open more slots |

### XP and levels

| Function | Behavior |
|---|---|
| `AwardBattleXP(EnemyXPSum)` | Distributes a battle's XP. Returns how much each member received |
| `GrantXP(Key, N)` | XP straight into one character, applying every level that fits |
| `GetXPToNextLevel(Key)` | — |

### Vitals

`ApplyDamage` · `HealHP` · `RestoreMP` · `SetAP` · `ReviveMember` · `FullRestoreAll`

### Events

`OnPartyChanged` (roster or formation changed) · `OnMemberLevelUp(Key, NewLevel)`

---

## How XP is split

Faithful to the original (`FUN_8004E568`):

```
total    = sum of enemy XP × 3/4
per head = total / (how many are ALIVE in the formation)
```

Three consequences, all verified:

- **Benched members get nothing.** They are not even in the count.
- **The dead get nothing and do not count in the divisor.** With one of three down, the two
  survivors take half each instead of a third.
- **A lone survivor takes the whole total** — 3× what they would take in a formation of
  three. This is why you can focus a character by benching the others.

The difficulty's XP multiplier applies to the total before the division.

Real example (enemies summing 10000):

| Formation | Each receives |
|---|---|
| 3 alive | 2500 |
| 2 alive (one died) | 3750 |
| 1 alive | 7500 |

---

## Why stats are stored

Per-level gain has **jitter** — `RollStatGain` draws inside a range. Two characters at the
same level have different numbers. So the record stores all eight values, exactly as the
original does.

`UCoreSubsystem::GetStatAtLevel` is still useful, but as a **prediction**, not a source of
truth — it returns the deterministic value, without the roll.

**Leveling up does not heal**: only the amount the maximum grew is added to the current
value. Someone wounded stays wounded, as in the source game.

---

## Where Party ends and Status begins

The eight stats this subsystem stores are the **base** — what came from leveling, with
nothing added. The number the player sees in battle is the **effective** one, computed by
[`UStatusSubsystem`](STATUS.md). It is the original's `effective ( base )` display.

| | Party | Status |
|---|---|---|
| **Owns** | the RECORD | the RULES |
| Roster, level, XP, base stats, vitals | ✅ | reads |
| `Equipment` / `Conditions` / `SeruLevel` | stores | **writes** |
| Equipping, conditions, Ra-Seru, affinity, effective stats | — | ✅ |
| Goes into the save | ✅ (a single array) | nothing |

Those three fields live in `FJRPGPartyMember` because **there is one record and one save** —
if each subsystem kept its own array, there would be two lists to keep in sync. But Status is
what writes them, through:

```cpp
FJRPGPartyMember* FindMemberForWrite(FName Character);  // nullptr if not in the roster
void NotifyMemberChanged();                             // fires OnPartyChanged
```

`FindMemberForWrite` is deliberately **not a `UFUNCTION`**: a raw pointer does not cross into
Blueprint, and the record should not be written from outside C++ anyway. Writing directly
bypasses `CanEquip` validation and the delegate the UI listens to.

---

## Existing integration

- **New Game**: `StartNewGame` resets the roster and recruits **only Vahn**. The others join
  when the story calls `RecruitCharacter`.
- **Save**: the whole roster goes into `UJRPGSaveGame::Party`. `PartyDisplay` (what the
  Save/Load screen reads) is a projection of the formation only.
  **`SaveVersion` is at 4** (v2 added the roster, v3 the inventory's `AcquiredOrder`, v4
  `SeruLevel` and the 5 `Equipment` slots). Older saves are treated as empty.
- **Items**: `ApplyConsumableEffect` really applies `HealHP` / `HealMP` / `bHealAP` /
  `bCureStatus` / `bRevive`.
- **DataTable**: `DT_Characters` resolves itself (`/Game/Data/DT_Characters`, falling back to
  the plugin folder). **It must be imported** — without it `RecruitCharacter` fails.

---

## Testing from the dev menu

| Command | Example |
|---|---|
| `party` | Roster with state, stats and XP for each |
| `party.recruit <key>` | `party.recruit Noa` |
| `party.active <key> <0\|1>` | `party.active Gala 0` |
| `party.available <key> <0\|1>` | `party.available Terra 0` |
| `party.max <n>` | `party.max 1` |
| `party.xp <n>` | `party.xp 10000` |
| `party.levelup <key> [n]` | `party.levelup Noa 5` — really levels up, applying growth each level |
| `party.setlevel <key> <n>` | `party.setlevel Gala 30` — simulates level by level |
| `party.stat <key> <stat> [value]` | `party.stat Vahn ATK 400` (omit the value to read) |
| `party.unrecruit <key>` | Deletes the record for good |
| `party.damage <key> <n>` / `party.revive <key>` / `party.heal` | — |

A script that exercises the whole design:

1. `party.recruit Vahn`, `party.recruit Noa`, `party.recruit Gala`
2. `party.xp 10000` → 2500 each
3. `party.damage Gala 99999` → Gala goes down
4. `party.xp 10000` → now **3750** for the two survivors
5. `party.active Noa 0` → Noa is benched
6. `party.xp 10000` → only Vahn gets paid, **7500**
7. `party` → Noa is still at her previous level, untouched

---

## The Party screen

The **Party** entry in the pause menu (between Equip and Status). Opens over the menu and
returns to it on close, the same pattern as Options/Save/Load.

It shows **everyone recruited**, not just the formation — swapping between the two is the
whole point. Order is formation first, then reserve, then those away from the story.

Each card carries a portrait, name, LV, three bars (**HP and MP as current/max**, AP 0..100)
and the six battle stats: ATK, UDF, LDF, SPD, INT, AGL.

| Situation | Appearance |
|---|---|
| In the formation | Gold **ACTIVE** badge, full-color card |
| On the bench | **RESERVE** badge, dimmed card |
| Away from the story | **AWAY** badge, heavily dimmed, cannot be toggled |
| Down | Name and HP in red |

**↑↓** navigate, **Enter** toggles, **Esc** goes back. The mouse follows the shell rule:
hovering only highlights, clicking selects and confirms.

**C++ validates.** JS sends `bridge.onpartytoggle(key, activate)` and `PartySubsystem`
decides; the UI just redraws with whatever state comes back. A refusal (formation full, or
last active member) returns through `onToggleRejected` and plays the cancel sound. The
screen decides nothing on its own.

The counter at the top shows `active / max`.

## The shop

The shop payload sends the **real formation**: if only Vahn is active, one box appears. It
stays on ATK/UDF/LDF, because that is what weapons and armor touch — INT, SPD and AGL are
moved by one or two accessories in the entire game and would be dead columns there. The full
eight appear on the Party screen.

## The menu's status cards

**It is a single card** (`#card-party`) filling the right column, with characters as rows
(`.char-row`) anchored to the top. With one card per character, a formation of 1 or 2 left a
hole in the interface; this way the frame is always the same size and only the content
changes.

C++ sends `JRPGSetParty([...])` with the **formation**, and the rows are built from it: if
only Vahn is active, one row appears. Benched or away characters get no row — the payload
comes from `BuildMenuPartyJS()`, which uses `GetActiveMembers()`.

They are rebuilt at **two** moments:

| When | Why |
|---|---|
| `OpenMenu()` | Opening the menu from scratch |
| `onuistatechanged('menu_open')` | **Returning from any screen** to the menu |

The second exists because changing the formation on the Party screen and going back does not
pass through `OpenMenu` again. Without it, the menu kept showing whoever had just left the
formation. Returning to the menu is the single point every screen passes through.

Each row keeps the id `card-status-<lowercase key>`, so the C++
`updateCharacterStatus(charId, lv, hp, maxhp, ...)` keeps working through the same path.

### Layout

**One large card**, **800px**, horizontally centered and **anchored to the top** — with a
single character it stays up there and grows downward as more join.

At that width a row has three blocks: portrait | (name + bars) | (LV + stats). The name sits
**above** the bars; side by side would not fit in 800px. Stats are in **two columns —
ATK/UDF/LDF on the left, SPD/INT/AGL on the right**.

> `.jrpg-card` alone **draws no border** — the menu cards add their own with
> `border: 2px solid var(--border-gold-dim)`. A new card that needs a frame must do the same,
> or it ends up with only the loose corner pieces.

The entry animation has two stages: the card enters whole (120ms) and the rows cascade inside
it (260ms + 70ms per row).

> **The cascade's `setTimeout` calls MUST be cancellable.** Use `agendarEntrada()` and
> `cancelarEntrada()` — never a bare `setTimeout`. Closing the menu mid-cascade left pending
> timers that re-added `.animate-in` / `.row-in` **after** the close, and the cards came back
> on top of the next screen. It only became visible once the cascade exceeded 350ms (the exit
> animation's duration).

Terra appears in the cards like anyone else. In the original she does not appear in the UI;
here she does — a remake decision.

## Still missing

- **Terra's portrait**: `images/terra.png` does not exist. Her card shows an empty frame
  rather than a broken icon, but the art is missing.
- **Reordering the formation**: today you can activate and remove, not reorder.
- **The Party screen shows base stats**, not effective ones. Swapping is one line in the
  payload (`GetEffectiveStat` instead of the record field), but it changes what the player
  sees — it waits for the equip screen to exist.
