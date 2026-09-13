# Items Screen (inventory UI)

Opens from the **Items** entry in the pause menu and returns to it on close — the same
pattern as Options/Save/Load/Party.

It mirrors the shop on purpose: same category icons, same filter bar, same navigation.
Learn one and you know the other.

---

## Layout

**One large card** (1100px) holding everything:

- **Filters at the top**, inside the card — boxes with icon, name and item count. A category
  with no items **does not become a button**.
- **List on the left**: icon, name, dot leader, quantity.
- **Description on the right**: name, category, what the item improves, the flavor text and
  how many you own.
- **Gold at the top**, outside the card, to the right of the title.

## Navigation

| Key | Action |
|---|---|
| **← →** | Change filter. **No confirmation needed** — same as the shop |
| **↑ ↓** | Move through the list |
| **Del** / Backspace | Discard the item (asks for confirmation) |
| **Esc** | Back. With the confirm open, it closes the confirm first |

Mouse follows the shell rule: hovering only highlights, clicking selects.

## The "Recent" filter

Sorts by what you picked up most recently. **It is not array order** — that records the
*first* acquisition, so picking up ten more healing leaves would not move the stack.

Each slot carries a stamp (`FInventorySlot::AcquiredOrder`) updated on every `AddItem`,
**including when it re-stacks**. `LoadInventoryState` resumes the counter above the highest
saved stamp; otherwise newly collected items would show up as old.

> `SaveVersion` moved to **3** because of that field. A v2 save is treated as empty.

## Discarding

Asks for confirmation in an overlay (Yes/No navigate with **← →**).

**The rule for what can be discarded lives in `UInventorySubsystem::CanDiscardItem`**, not
in the UI:

| Category | Allowed? | Why |
|---|---|---|
| `Key` | **No** | Quest item — would soft-lock the story |
| `ArtBook` | **No** | Would erase an Art permanently |
| Everything else | Yes | — |

The same pair `ShopSubsystem::CanSellItem` already refuses to sell.

The payload carries `candiscard` **answered by C++**, and the item row is marked `BOUND`.
The UI infers nothing from the category — it used to, and the result was that an Art Book
passed the JS check and was only rejected later by C++.

Execution is C++ too: JS sends `bridge.onitemdiscard(id, qty)`, `InventorySubsystem`
validates and removes, and the screen just redraws with whatever inventory comes back.
Rejections arrive at `onDiscardRejected`.

## Language

**All on-screen text is English** — that is the whole shell's default. Code comments and
logs are in Portuguese.

Categories: `All`, `Recent`, `Item`, `Weapon`, `Armor`, `Accessory`, `Art`, `Boost`, `Lure`,
`Key`. Party states: `ACTIVE`, `RESERVE`, `AWAY`.

## What the description shows

Only **what the item improves** — no "best for Vahn", that is the shop's business. It comes
from three places:

1. **Direct healing**: `HealHP`, `HealMP`, `bHealAP`, `bRevive`, `bCureStatus`, `bTargetAll`.
2. **Equipment**: `AttackBonus`, `UDF`, `LDF`.
3. **`EffectClass` + `EffectValue`** on accessories, translated by the `EFEITOS` map in
   `85_items.js`.

**An effect that touches one of the 8 stats uses the ABBREVIATION** — the same one shown on
the Party screen and in the shop boxes. Anything that is not a stat (XP, gold, encounters,
drop rate) stays as text: it has no abbreviation, and inventing one would make the screen
unreadable.

The map covers the `EffectClass` values that exist in `DT_Items`, in four shapes:

| Kind | Example | Rendered as |
|---|---|---|
| Stat, percentage (accessory) | `attack_pct` 20 | **ATK** +20% |
| Stat, permanent (Water) | `attack` 4 | **ATK** +4 permanently |
| Stat, flat value (equipment) | `AttackBonus` 98 | **ATK** +98 |
| Non-stat | `encounter_pct` 50 | Encounters **−50%** |
| Switch | `revive_once` | Revive once **yes** |

Note that `attack` and `attack_pct` are **different** classes: the first is the Water, which
raises the base stat forever; the second is the accessory, a percentage that only applies
while equipped. That is why the Water line carries the *permanently* suffix. See
[`STATUS.md`](STATUS.md).

Abbreviations used: `ATK`, `UDF`, `LDF`, `SPD`, `INT`, `AGL`, `HP`, `MP`, `AP`.
`defense_pct` and `defense` (which touch both defenses) render as **UDF / LDF**.

**An untranslated `EffectClass` is shown raw** rather than disappearing — so a new value in
the CSV is visible immediately, and C++ does not need recompiling to accommodate it (effects
go through raw in the payload; JS does the translating).

### An empty optional field never becomes a line

A sword does not "teach no art" — it simply has no such line. The trap is that
`FName::ToString()` on a `NAME_None` returns the word **`"None"`**, which on the JS side is a
non-empty and therefore *truthy* string: `if (it.art)` passed, and every item's description
showed `Teaches Art: None` and `Summons: None`.

The payload now sends `''` for those fields via `JRPGWebUI::OptionalNameToJS()` — use that
helper (not `ToJSStringLiteral(Name.ToString())`) for every `FName` that may be empty. JS
still keeps a `temValor()` guard treating `''` and `"None"` as absent.

Watch out for **duplicate lines** too: `it.summon` printed a `Summons` line, and the
`summon_seru` `EffectClass` printed an identical one right below. When an `EffectClass`
already renders a field, its standalone line should not appear.

---

## Testing in the browser

```js
JRPGDemo.items()        // sample inventory with 9 items and 4820 gold
JRPGDemo.items(0)       // the same, broke
```

Covers the cases that matter: consumable, weapon, armor, percentage accessory, reduction
accessory, art book and quest item.
