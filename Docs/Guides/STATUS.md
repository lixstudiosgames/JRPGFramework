# Status System

Owns the **rules** that act on a character: equipping, applying conditions, raising the
Ra-Seru, and answering what a stat is worth **after everything is added up**.

Complements the [Party System](PARTY.md) — read both together.

---

## The split: one record, two owners

The original's status screen shows each stat like this:

```
ATK   357 ( 200 )
       │      └── BASE       — what the character has from leveling
       └───────── EFFECTIVE  — base + equipment + accessories
```

That is exactly the split between the two subsystems:

| | Party | Status |
|---|---|---|
| **Owns** | the RECORD | the RULES |
| Roster, level, XP | ✅ | — |
| **Base** stats | ✅ | reads |
| `Equipment` / `Conditions` fields | stores | **writes** |
| `SeruLevel` | stores | **writes** |
| **Effective** stats | — | ✅ computes |
| Equip / unequip | — | ✅ |
| Conditions, elemental affinity | — | ✅ |
| Goes into the save | ✅ (a single array) | nothing |

**Status keeps no record of its own.** It reads and writes the Party's, through
`UPartySubsystem::FindMemberForWrite()`. If each kept its own array, the save would have two
lists to keep in sync — and one day they would diverge.

> Rule of thumb: **ask Status, save Party.** Nothing other than `UStatusSubsystem` should
> write to `Equipment`, `Conditions` or `SeruLevel`.

---

## Equipment

Five slots, one per `EJRPGEquipSlot`. The record's `Equipment` array always has
`EJRPGEquipSlot::MAX` entries; `NAME_None` means empty.

| Slot | Comes from `DT_Items` | Contributes |
|---|---|---|
| `Weapon` | `Category = Weapon` | `AttackBonus` → ATK |
| `Armor` | `Category = Armor`, `ArmorSlot = Armor` | `UDF` / `LDF` |
| `Helmet` | `Category = Armor`, `ArmorSlot = Helmet` | `UDF` / `LDF` |
| `Shoes` | `Category = Armor`, `ArmorSlot = Shoes` | `UDF` / `LDF` |
| `Accessory` | `Category = Accessory` | `EffectClass` / `EffectValue` (percentage) |

### API

| Function | Behavior |
|---|---|
| `GetSlotForItem(ItemID, OutSlot)` | Which slot the item goes into. `false` if not equippable |
| `CanEquip(Key, ItemID)` | Whether this character may use this item |
| `EquipItem(Key, ItemID)` | Removes the item from the inventory, fills the slot, **returns whatever was there to the inventory** |
| `UnequipItem(Key, Slot)` | Returns it to the inventory and empties the slot |
| `GetEquipped(Key, Slot)` | What is in the slot |

`EquipItem` calls `RemoveItem` **before** `AddItem` of the old piece, on purpose: in the
other order, a failing `RemoveItem` would leave the player holding both.

### Who can equip what

**Weapon** — any one of these is enough:

1. the character is the item's `EquipBest`, or
2. they are in the `EquipOthers` list, or
3. the item's `WeaponType` is one of their `WeaponClasses` in `DT_Characters`.

Rule (3) is the safety net: a new weapon nobody remembered to list still works by class. The
comparison **tolerates plurals** — the two tables do not speak the same language
(`WeaponType` is `"Sword"`, `WeaponClasses` came out of the extractor as `"Swords"`), and
normalizing in code was cheaper than rewriting the extraction.

**Armor** — only its `EquipCharacter`. Empty **or the literal `"None"`** means anyone: the
extractor wrote `None` where there was no owner (the `war_god_plate` case).

**Accessory** — anyone in the roster.

**Terra** — has no `WeaponClasses`, and no weapon or armor names her. So she equips neither
weapons nor armor, only accessories. That is correct: in the original she has no equipment
screen at all.

---

## Stats — the `effective ( base )` display

| Function | Returns |
|---|---|
| `GetBaseStat(Key, Stat)` | The number from leveling. The one in parentheses |
| `GetEquipmentBonus(Key, Stat)` | Only what equipment adds (can be negative) |
| `GetEffectiveStat(Key, Stat)` | The total, clamped by `CoreSubsystem`'s cap |

`Stat` is an `FName`: `HP` `MP` `ATK` `UDF` `LDF` `SPD` `AGL` `INT`.
(`HP`/`MP` answer the **maximum**, not the current value — current is a vital, owned by Party.)

The order of the calculation matters:

```
effective = ( base + flat equipment bonus + Ra-Seru bonus )
            × ( 1 + accessory percentages )
            clamped by the cap
```

The percentage comes **last, over the total** — which is what makes a +20% ring worth more
the better your equipment is. A level 99 Vahn with the Ra-Seru kit and the Power Ring:

| | base | effective |
|---|---|---|
| ATK | 200 | **357** |
| UDF | 180 | **411** |
| LDF | 175 | **403** |

The accessory `EffectClass` values that touch a stat are just these nine: `hp_max_pct`,
`mp_max_pct`, `attack_pct`, `udf_pct`, `ldf_pct`, `speed_pct`, `agility_pct`,
`intelligence_pct` and `defense_pct` (which moves **both** defenses). Everything else
(`ap_accrual_pct`, `xp_pct`, `encounter_pct`, `elemental_def`, …) belongs to other systems
and Status ignores it.

The cap is always the last step and comes from `CoreSubsystem::GetStatCap()`. AGL 275 with
+20% does not become 330: it becomes **280**, the cap.

### Permanent upgrades — the Waters

`ApplyPermanentStatUpgrade(Key, EffectClass, Value)` raises the **base**, not the effective
value. `InventorySubsystem` calls it when a `PermanentStat` item is used. The eight in
`DT_Items`:

| Item | EffectClass | Value |
|---|---|---|
| Life Water | `hp_max` | +16 |
| Magic Water | `mp_max` | +8 |
| Power Water | `attack` | +4 |
| Guardian Water | `defense` | +4 (UDF **and** LDF) |
| Swift Water | `speed` | +4 |
| Wisdom Water | `intelligence` | +4 |
| Miracle Water / Honey | `all_stats` | +4 |

`all_stats` means the **five combat stats** (ATK, UDF, LDF, SPD, INT). AGL is deliberately
excluded — it comes from Art blocks, see the AGL cap rule in
[`../Reference/PROGRESSION.md`](../Reference/PROGRESSION.md). HP and MP have their own Waters.

> Both fields were **empty** in `DT_Items` until recently, with `EffectValue = 0` — meaning
> every Water in the game did absolutely nothing. The numbers above came from each row's own
> `EffectDescription`.

---

## Conditions

`ApplyCondition` · `RemoveCondition` · `HasCondition` · `GetConditions` · `ClearConditions`

They are free-form `FName`s — poison, paralysis, petrified. Every change fires
`OnConditionChanged(Key, Condition, bApplied)` and calls Party's `NotifyMemberChanged()` so
the UI redraws.

---

## Ra-Seru

| Function | Behavior |
|---|---|
| `GetSeruLevel(Key)` | 1..9 |
| `RaiseSeruLevel(Key, N)` | Raises it, clamped to `SeruMaxLevel` (9, as in the original) |
| `GetSeruBonus(Key, Stat)` | What that level **grants** in stats |

> **`GetSeruBonus` does not come from the disc.** In the original, Seru level feeds magic
> power (`FUN_801dd864`), not the character's stats. Today's bonus — **+2% INT per level
> above the first, and nothing on the other seven** — is our decision, kept small and in
> **one place** so it is easy to change or remove.

The *level* lives in the Party record (it is character progression, like their level). What
it *grants* is computed by Status. The same split as always.

---

## Elemental affinity

`GetElementMultiplier(Key, Element)` answers how much an element multiplies damage **against**
this character:

| Situation | Multiplier |
|---|---|
| The element is in `AffinityWeak` | **1.04** |
| The element is in `AffinityStrong` | **0.96** |
| Anything else (and `None`) | 1.00 |

These are the original's values (an 8×8 matrix at `0x801F53E8`, stored as integers /100). The
lists come from `DT_Characters`: Vahn strong against Fire / weak to Water, Noa strong against
Wind / weak to Thunder and Earth, Gala strong against Thunder / weak to Wind and Water.

Note the difference is **4%**. In Legaia the element is seasoning, not what decides a fight —
the `Offense − Defense` subtraction is.

---

## Events

| Delegate | Fires when |
|---|---|
| `OnEquipmentChanged(Key)` | Something was equipped or unequipped |
| `OnConditionChanged(Key, Condition, bApplied)` | A condition entered or left |

Every write also calls `UPartySubsystem::NotifyMemberChanged()`, which fires `OnPartyChanged`.
UI already listening to Party needs nothing else.

---

## Still missing

- Equipment screen in the WebUI (the Items screen lists everything but does not equip).
- `GetEffectiveStat` is not yet what the Party screen and the shop display — they draw the
  base. Swapping is one line in each payload, but it changes what the player sees, so it
  waits for the equip screen.
- No condition has an effect in battle yet; today they only enter and leave the list.
- `elemental_def` / `protect_status` / `all_status_def` on accessories are read by nobody.

---

## A data-side loose end

As `DT_Items` stands today, **all three characters equip almost everything**: nearly every
weapon's `EquipOthers` lists the other two, so Vahn can use a claw and Gala can use a sword.
If the intent is the original — Vahn swords/knives, Noa claws, Gala axes/clubs — what needs
changing is the **table**, not the code: empty out `EquipOthers`. The code already respects
whatever the table says.
