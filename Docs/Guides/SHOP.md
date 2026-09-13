# Shop System

The shop uses Gold from `UCoreSubsystem`, items from `UInventorySubsystem` and conditions
from `UWorldStateSubsystem`. Shops come from the **DT_Shops** DataTable (generated from
`shops.toml`).

## 1. Generate and import the shop DataTable

```
Docs/Data/gamedata/shops.toml
        ↓  python Extras/generate_shops_csv.py
Docs/Data/DT_Shops.csv   (32 shops)
        ↓  import in the editor (Data Table, Row Struct = FShopData)
Content/Data/DT_Shops.uasset   (in the GAME project — /Game/Data/DT_Shops)
```

`ShopSubsystem` finds the table automatically at `/Game/Data/DT_Shops` (falling back to
`/JRPGFramework/Data/DT_Shops`, or set it explicitly with `SetShopDataTable`).

## 2. Opening a shop from an NPC Blueprint

- Simple shop: Interact → `WebUISubsystem → OpenShop("rim_elm_variety_shop")`.
  The ShopID is the **row name** in DT_Shops (column 1 of the CSV).
- Shop with variants (e.g. Biron before/after mist): Branch on
  `ShopSubsystem → IsShopAvailable("biron_monastery_morlang_after")` →
  true: `OpenShop("...after")` / false: `OpenShop("...before")`.
  (`OpenShop` also refuses unavailable shops on its own, with a log warning.)

## 3. Rules

- **Buying:** price is `BuyPrice` from DT_Items. The UI has a quantity selector (1..99;
  ◀▶ = ±1, ▲▼ = ±10). Without enough gold the transaction is refused, and the price turns
  red in the list when not even one unit is affordable.
- **Selling (Sell tab):** price is `BuyPrice / 2`. Items with `BuyPrice = 0`, **Key items**
  and **Art Books** never appear in the sell tab.
- **Featured items (★):** only appear if the player holds the gate item — by default
  **`platinum_card`**, configurable through `ShopSubsystem → FeaturedGateItemID`. Without
  the card the item is **invisible**, not shown as locked. Grant it with
  `InventorySubsystem → AddItem(platinum_card)`.
- **Shop condition (`RequiredFlagID` + `bRequiredFlagValue`):** the shop only opens when
  the WorldState event flag holds the expected value. The converter generates
  `mist_cleared` (false = before, true = after) — **swap it for whatever flag you want** by
  editing the DataTable row. In game, change phase with
  `WorldStateSubsystem → SetEventFlag(mist_cleared, true)`.

## 4. UI controls

Buy/Sell → item list (name ★, owned count, price) with the description below → quantity →
confirm. The header's gold updates immediately after each transaction. Escape/B backs out
one level at a time (quantity → list → tab → close). Mouse works too: hover selects, click
confirms, the quantity arrows are clickable.

## 5. C++ / Blueprint API (`ShopSubsystem`, category `JRPG|Shop`)

| Function | Purpose |
|---|---|
| `IsShopAvailable(ShopID)` | Check the shop's condition (to pick a variant in BP) |
| `GetShopStock(ShopID, OutName, OutTown, OutStock)` | Resolved stock, with gates applied |
| `BuyItem(ShopID, ItemID, Qty)` | Buy from code — validates everything, refunds on failure |
| `SellItem(ItemID, Qty)` / `CanSellItem` / `GetSellPrice` | Sell from code |
| `GetSellableInventory()` | Sellable inventory with prices |
| `FeaturedGateItemID` | Item that unlocks featured entries (default `platinum_card`) |

## 6. Troubleshooting

- **Shop does not open:** check the log — wrong ShopID (verify the row name), shop
  unavailable (flag condition), or DT_Shops not imported at `/Game/Data/DT_Shops`.
- **Item missing from a shop:** it is featured and the player lacks the platinum_card, or
  the key does not exist in DT_Items (warning in the log).
- **Transaction refused:** not enough gold (buy), or the item is not sellable (sell).
