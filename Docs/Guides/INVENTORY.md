# Inventory System

How to set up the item table in Unreal and use `UInventorySubsystem` from C++ or Blueprints.

---

## 1. Creating the DataTable

`FItemData` inherits `FTableRowBase`, so it shows up in the editor once the project compiles.

1. In the Content Browser: right-click → **Miscellaneous** → **Data Table**.
2. Pick **ItemData** as the Row Structure.
3. Name it **`DT_Items`** and save it at exactly `/Game/Data/DT_Items`.

Or import `Docs/Data/DT_Items.csv` directly, which is the normal path — the CSV is the
source of truth.

---

## 2. Column reference

One unified table holds every item type.

### Common to all items

| Column | Type | Notes |
|---|---|---|
| `Key` | Name | snake_case id (`healing_leaf`). Must match the Row Name exactly |
| `Name` | Text | Display name |
| `Category` | `EItemCategory` | `Consumable`, `PermanentStat`, `ArtBook`, `Key`, `FishingLure`, `Weapon`, `Armor`, `Accessory` |
| `BuyPrice` | Integer | Shop price. Sell price is `BuyPrice / 2`; `0` means it cannot be bought or sold |
| `EffectDescription` | Text | Description shown in the menu |
| `UseContext` | `EUseContext` | `AnyTime`, `BattleOnly`, `FieldOnly`, `Automatic` (passive/equipment), `QuestOnly` |
| `Icon` | Soft Texture2D | Menu icon |

### `PermanentStat` — the "Waters", which raise a BASE stat permanently

| Column | Notes |
|---|---|
| `EffectClass` | Which stat rises. The seven `UStatusSubsystem` understands: `hp_max`, `mp_max`, `attack`, `defense` (raises UDF **and** LDF), `speed`, `intelligence`, `all_stats` (the five combat stats — AGL is deliberately excluded). **Leave this blank and the item does nothing.** |
| `EffectValue` | How much. Life Water 16, Magic Water 8, everything else 4 |

`hp_max` (Water, absolute) and `hp_max_pct` (accessory, percentage of the total) are
**different** classes and do not mix.

**Waters do not heal.** `HealHP`/`HealMP` are 0 on all eight — the `PermanentStat` branch of
`UseItem` only calls `ApplyPermanentStatUpgrade`, never a heal. (The generator once wrote
`HealHP = 9999` on Life Water because it read the word "maximum" in the description; the UI
promised a full heal the code never performed.)

### `Consumable` — heal/buff effects

| Column | Notes |
|---|---|
| `HealHP` | HP restored (200 for Healing Leaf, 9999 for a full heal) |
| `HealMP` | MP restored |
| `bHealAP` | Fills the AP bar (Fury Boost) |
| `bCureStatus` | Cures status conditions (Antidote, Medicine) |
| `bRevive` | Revives fallen allies (Phoenix) |
| `bTargetAll` | Applies to the whole active party (Healing Bloom) |

### `ArtBook`

| Column | Notes |
|---|---|
| `TeachesArt` | Art id unlocked in `ArtsSubsystem` (`tornado_flame`) |
| `TeachesCharacter` | Who learns it (`"Vahn"`, `"Noa"`, `"Gala"`) |

### `Weapon`

`WeaponType` (`"Sword"`, `"Claw"`, `"Club"`, `"Knife"`), `EquipBest` (the character who gets
full power), `EquipOthers` (array of secondary characters), `AttackBonus` (ATK granted).

### `Armor`

`ArmorSlot` (`Armor` / `Helmet` / `Shoes`), `EquipCharacter` (`"None"` = unrestricted),
`UDF` (Upper Defense Factor), `LDF` (Lower Defense Factor).

### `Accessory`

`EffectClass` (logical effect id for combat formulas: `hp_max_pct`, `encounter_pct`,
`revive_once`, …), `EffectValue`, `StatusType` (immunity granted when `protect_status`),
`ElementType` (defense granted when `elemental_def`), `Summons` (Seru summoned when
`summon_seru`).

### Examples

| Row Name | Category | BuyPrice | UseContext | Extra columns |
|---|---|---|---|---|
| `healing_leaf` | `Consumable` | 100 | `AnyTime` | `HealHP = 200` |
| `life_water` | `PermanentStat` | 5000 | `FieldOnly` | `EffectClass = "hp_max"`, `EffectValue = 16`, `HealHP = 0` |
| `fire_book_i` | `ArtBook` | 0 | `FieldOnly` | `TeachesArt = "tornado_flame"`, `TeachesCharacter = "Vahn"` |
| `short_sword` | `Weapon` | 2700 | `Automatic` | `WeaponType = "Sword"`, `AttackBonus = 24`, `EquipBest = "Vahn"`, `EquipOthers = ["Noa","Gala"]` |
| `cure_amulet` | `Accessory` | 800 | `Automatic` | `EffectClass = "protect_status"`, `StatusType = "venom"` |

---

## 3. Using the subsystem

```cpp
UInventorySubsystem* Inv = GetGameInstance()->GetSubsystem<UInventorySubsystem>();
```

In Blueprint: **Get InventorySubsystem**.

| Function | Behavior |
|---|---|
| `AddItem(ItemID, Quantity)` | Consumables/lures stack to 99 and open a new slot past that. Equipment/key items/books get one slot each |
| `RemoveItem(ItemID, Quantity)` | Safe removal; returns false if the player lacks the quantity |
| `HasItem(ItemID, Quantity)` | Ownership check |
| `GetItemQuantity(ItemID)` | Total units across slots |
| `ResetInventory()` | Clears everything (New Game) |

### Blueprint nodes with an item dropdown

Variants that let you pick the item from a dropdown instead of typing an id:
`AddItemRow`, `RemoveItemRow`, `HasItemRow`, `GetItemQuantityRow`, `UseItemRow`.

On the `Item Row` pin, set **Data Table** to `DT_Items` and then pick the **Row Name** from
the dropdown. Duplicating the node (Ctrl+D) keeps the table selected.

### Save/Load

Call `GetInventorySlots()` and store the returned `FInventorySlot` array; pass it back to
`LoadInventoryState()` on load. `USaveSubsystem` already does this automatically — you only
need it for custom flows.

---

## 4. `UseItem` routing

When the UI or the battle system calls `UseItem(ItemID, TargetCharacter, bInBattle)`, the
item is consumed (if consumable) and the effect is routed:

| Item category | Subsystem | Function |
|---|---|---|
| `Consumable` (out of battle) | `PartySubsystem` | `ApplyConsumableEffect` |
| `Consumable` (in battle) | `BattleSubsystem` | `ApplyBattleItemEffect` |
| `PermanentStat` | `StatusSubsystem` | `ApplyPermanentStatUpgrade` |
| `ArtBook` | `ArtsSubsystem` | `TeachArtToCharacter` |

`Weapon`, `Armor` and `Accessory` never go through `UseItem` — they are `Automatic`.
Equipping is `UStatusSubsystem::EquipItem`, which removes the item from the inventory and
returns whatever was in the slot. See [`STATUS.md`](STATUS.md).

> `BattleSubsystem` and `ArtsSubsystem` are still stubs: the call arrives and logs a TODO.
> See [`../BATTLE_ROADMAP.md`](../BATTLE_ROADMAP.md).

---

## 5. Reacting to item use

`UInventorySubsystem` broadcasts `OnItemUsed(const FItemData&, FName TargetCharacter)`.
Bind it in your GameInstance to drive cosmetics while the hard rules stay in C++:

```cpp
void UJRPGGameInstance::Init()
{
    Super::Init();
    if (UInventorySubsystem* Inv = GetSubsystem<UInventorySubsystem>())
    {
        Inv->OnItemUsed.AddDynamic(this, &UJRPGGameInstance::HandleOnItemUsed);
    }
}

void UJRPGGameInstance::HandleOnItemUsed(const FItemData& ItemData, FName TargetCharacter)
{
    if (ItemData.HealHP > 0)
    {
        PlayCureVFX(TargetCharacter);   // BlueprintImplementableEvent
    }
}
```
