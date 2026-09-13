# Dev Menu

A development screen for poking at game state without playing through it: grant gold, add
items, switch difficulty, inspect progression, toggle flags and open any shell screen.

**It does not ship.** The bodies of `OpenDevMenu` and `RunDevCommand` are compiled out in
**Shipping** builds (`#if UE_BUILD_SHIPPING`), so the Blueprint call can stay where it is —
in a cooked build it simply does nothing. You do not have to remember to remove it.

---

## Wiring it to your character

1. In the character (or PlayerController) Blueprint, create an **Input Action** on a free
   key — F1 works well.
2. Connect it to `WebUISubsystem → Open Dev Menu`.

That is all. The screen opens with UI focus and a visible cursor, and gives input back to the
game when it closes.

> `OpenDevMenu` only opens from the **HudOnly** state. If a menu, shop or save screen is
> already open it ignores the call and logs a warning — close that screen first.

**Esc** closes. **← →** switch tabs from the keyboard (the dev menu is mouse-first, but the
basics work on a controller).

---

## Tabs

| Tab | What you can do |
|---|---|
| **State** | Snapshot of what is loaded: gold, playtime, map, difficulty with its active multipliers, which DataTables were imported, and `bIsInField` |
| **Progression** | Change difficulty; see a character's 8 stats at a level; query one stat with the next level's gain; compare XP across the roster |
| **Items & Gold** | Add/remove gold; add and remove items by row name |
| **World & Save** | Toggle `bIsInField` (this is what unlocks the SAVE tab); toggle event flags; save and load slots |
| **Screens** | Open the pause menu, save, load, options, main menu, shop and the item popup |

Opening a game screen **closes the dev menu** — those screens require the `HudOnly` state.

### The first test after compiling

Opening the dev menu runs the `info` command automatically. It is the smoke test: it shows
the **path** of every DataTable `CoreSubsystem` managed to resolve.

```
tables:
  DT_StatGrowth   /Game/Data/DT_StatGrowth.DT_StatGrowth
  DT_GrowthCurve  /Game/Data/DT_GrowthCurve.DT_GrowthCurve
  DT_Difficulty   missing (using the multipliers built into C++)
```

A missing `DT_Difficulty` is not an error — C++ carries the same multipliers. A missing
`DT_StatGrowth` or `DT_GrowthCurve`, however, means **nobody gains stats**. Import the CSV
from `Docs/Data/` into `/Game/Data/` under the same name and the subsystem picks it up on the
next play.

Seeing the path also catches the annoying case: you imported the table into a different
folder and the subsystem picked up an older one somewhere else.

---

## Command line

At the bottom there is a field for typing commands directly, with history on ↑. Every button
on the screen just assembles one of these lines — there is nothing a button does that the
line cannot.

| Command | Example |
|---|---|
| `info` | `info` |
| `gold.add <n>` | `gold.add -500` |
| `item.add <id> <qty>` | `item.add healing_leaf 5` |
| `item.remove <id> <qty>` | `item.remove healing_leaf 1` |
| `diff.set <0\|1\|2>` | `diff.set 2` |
| `prog.sheet <char> <level>` | `prog.sheet Terra 50` |
| `prog.stat <char> <stat> <level>` | `prog.stat Noa ATK 40` |
| `prog.xp <level>` | `prog.xp 38` |
| `world.field <0\|1>` | `world.field 0` |
| `world.flag <id> <0\|1>` | `world.flag met_gala 1` |
| `save.save <slot>` / `save.load <slot>` | `save.load 3` |
| `screen.<name>` | `screen.shop rim_elm_general` |
| `party` / `party.recruit` / `party.active` / `party.available` | See [`PARTY.md`](PARTY.md) |
| `party.levelup <key> [n]` | `party.levelup Noa 5` — **actually levels up**, applying growth at each level, and prints the before/after of all 8 stats |
| `party.setlevel <key> <n>` | `party.setlevel Gala 30` — simulates level by level; going down replays from 1 |
| `party.stat <key> <stat> [value]` | `party.stat Vahn ATK 400`. HP and MP write the **maximum** |
| `party.unrecruit <key>` | Deletes the roster record |

Parsing is space-separated, so arguments cannot contain spaces — the screen's fields
substitute `_` automatically. Item and shop row names are visible in LegaiaStudio.

`RunDevCommand` is `BlueprintCallable`, so you can fire a command from a Blueprint or bind it
to a console command without opening the screen.

---

## Testing in the BROWSER (no Unreal)

**The dev menu does not work outside the game.** Its buttons only assemble a command line and
send it to `bridge.ondevcommand`, which is C++ — with no bridge, nothing happens (the output
says *"no bridge — running outside the game"*).

To exercise the screens in Chrome, open `Content/UI/WebUI/shell.html` and use `JRPGDemo` from
the console:

```js
JRPGDemo.party()      // Party screen with all 4 (Vahn, Noa dead, Gala reserve, Terra away)
JRPGDemo.party(1)     // Vahn only
JRPGDemo.party(2, 1)  // 2 characters, formation capped at 1
JRPGDemo.menu(3)      // pause menu with 3 status cards
JRPGDemo.menu(1)      // menu with a single card
JRPGDemo.dev()        // opens the dev menu (layout works; buttons stay mute)

JRPGDemo.fichas       // the sample records, editable before opening a screen
```

`JRPGDemo.party()` calls `UIState.set('menu_open')` first — without it `canOpen` refuses and
the screen does not open.

It lives in `src/js/90_devmenu.js`, so it goes away with the dev menu.

## Adding a new command

The bridge has **one** function (`ondevcommand`) receiving the whole line. A new command
touches neither `WebUIBridge` nor JSC:

1. In `WebUISubsystem.cpp`, inside `RunDevCommand`: one more
   `else if (Verb == TEXT("my.command"))`, filling `Out` with whatever you want to see.
2. A new button in `src/sections/90_devmenu.html` with `data-cmd="my.command"` (fixed) or
   `data-tpl="my.command {level}"` (reading a field from the screen).
3. `python Extras/build_webui.py`.

The fields `data-tpl` understands are in the `FIELDS` map in `src/js/90_devmenu.js`.

---

## Removing it entirely

It already disappears in Shipping. To remove the code too:

1. Delete `src/css/90_devmenu.css`, `src/sections/90_devmenu.html` and
   `src/js/90_devmenu.js`, then run `build_webui.py`.
2. Remove `OpenDevMenu`/`RunDevCommand` from `WebUISubsystem.h/.cpp`, `OnDevCommand` from
   `WebUIBridge.h/.cpp`, and the `AttachFn("ondevcommand", …)` call.
3. In `05_ui_state.js` and `99_boot.js`, drop the references to `dev` / `dev_open` /
   `JRPGDev`.

**Leave `DevMenuOpen` at the end of `EJRPGUIState`** — the value is serialized in Blueprints,
and removing it from the middle of the enum shuffles the others.
