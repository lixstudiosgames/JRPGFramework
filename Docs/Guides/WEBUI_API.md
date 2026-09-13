# WebUI Shell — API and usage

The HTML UI runs as a **single shell** (`Content/UI/WebUI/shell.html`): one document loaded
**once** per session that stays alive the whole time. The menu, popups and every screen are
hidden sections inside it. Blueprints never build a URL or reload a page — they only call
functions.

## Blueprint functions (`WebUISubsystem`, category `JRPG|WebUI`)

| Function | When to call | What it does |
|---|---|---|
| `InitializeUIShell()` | Map/GameMode **BeginPlay** | Creates the permanent widget and loads the shell (idempotent — calling again is a no-op) |
| `ShowItemPopup(ItemName, ItemQty)` | Chest / item pickup | Shows the animated popup. Does **not** steal game input. The name comes from the DataTable row, the quantity from the chest's variable |
| `OpenMenu()` | Pause button (key or gamepad Start) | Opens the menu with animation, cursor and focus (keyboard + gamepad). Refuses while a dialogue is active |
| `CloseMenu()` | Closing the menu from code | Animates out and returns input to the game (Escape / B already do this themselves) |
| `OpenSaveLoad()` | Interact on the save statue | Opens Save/Load (choose Save or Load → 5×3 grid of 15 slots → detail card). Sends slot metadata to JS and focuses the UI |
| `OpenDialogue(Speaker, Text, Options)` | *Placeholder* | The node exists with its final signature; JS only logs for now |
| `OpenOptions()` | Options menu | Opens Options (Display/Audio/Game/Controller/Keybindings). Can be called from the game, from inside the pause menu, and from the main menu — it returns to the right origin on close. **Controls are still visual only** |
| `OpenMainMenu()` | Title map BeginPlay | Opens the main menu (a transparent section over the 3D scene). Sends version/build and disables "Continue" when no save exists |
| `SetUIState(NewState)` | Whenever the game context changes | Switches the global state (e.g. `DialogueActive` blocks the menu). Does not touch input |
| `GetUIState()` | Query | The current state (`HudOnly`, `MenuOpen`, `DialogueActive`, `OptionsOpen`, `SaveLoadOpen`, `ShopOpen`, `MainMenuOpen`, `DevMenuOpen`, `PartyOpen`, `ItemsOpen`) |
| `ReloadShell()` | Dev only | Forces `shell.html` to reload (after editing the UI) without restarting the editor |
| `ExecuteJS(JSCode)` | Advanced / debug | Arbitrary JS in the shell. Data strings must be escaped with `JRPGWebUI::ToJSStringLiteral` |

**Minimum Blueprint setup:** BeginPlay → `InitializeUIShell` · Chest → `ShowItemPopup` ·
Pause → `OpenMenu`.

## Gamepad

With the menu open: **D-Pad / left stick** navigate (with hold-to-repeat), **A/Cross**
confirms, **B/Circle** closes, **LB/RB** switch tabs (options screen; **Q/E** on keyboard).

The mapping happens in C++ (`SUltralightBrowser::OnKeyDown`) and reaches JS as a semantic
action:
`handleUIInput('up'|'down'|'left'|'right'|'confirm'|'cancel'|'tab_prev'|'tab_next')` —
the keyboard uses the same path. Opening the menu with Start is the game's Input Action
calling `OpenMenu()`.

## States (JS `UIState` + a C++ mirror)

`hud_only` → everything closed, the game has control. `menu_open` / `dialogue_active` /
`saveload_open` / `shop_open` / `main_menu` open from `hud_only`; `options_open` opens from
`hud_only`, `menu_open` **and** `main_menu`; `saveload_open` also opens from `main_menu` (the
title's "Continue", locked to Load mode). The **item popup is not a state** — it is a passive
overlay that can appear in any situation.

**Internal UI transitions** (options closing and handing control back to the pause menu or the
main menu) do not go through `closemenu`: JS notifies C++ with
`onuistatechanged('<state>')`, which only syncs the mirror — focus stays on the UI and input
does not return to the game.

## Main menu actions

`OnMainMenuAction` (a `BlueprintAssignable` delegate on `WebUISubsystem`) hands the project
only what the plugin cannot resolve: **`newgame`** (`StartNewGame()` is already applied and
input already returned — the BP only does the `OpenLevel`) and **`quit`** (fired right before
`QuitGame`). `continue` and `options` are resolved inside the subsystem. Details in
[`MENU_AND_OPTIONS.md`](MENU_AND_OPTIONS.md).

## Render rules that must not be broken

- **`#ul-repaint-anchor`** (1px, alpha 0.02, in `shell.template.html` + `05_shell.css`) keeps
  the page from ever being empty. Without it, hiding the last section leaves Ultralight with
  no draw commands and the final frame is never presented — the UI freezes on screen. The
  `kickUIRepaint()` helper (`00_core.js`) runs after every close as a safety net.
  **Do not remove.**
- **`setInterval` does NOT fire reliably in-game** (it works in a browser and in Node, and
  fails in the game). For visible periodic updates, use a `requestAnimationFrame` loop that
  only touches the DOM when the value changes — the pattern used by `fpsMonitor` and
  `playtimeTicker` (`10_menu.js`). One-shot `setTimeout` works normally.
- **`radial-gradient(ellipse ...)` is NOT reliable**: Ultralight ignores the aspect ratio and
  draws a **circle** — correct in Chrome, a blob in game (which is what happened to the tab
  and main-menu flare). For soft glow use **`box-shadow`** (blur + spread), and for horizontal
  falloff use **`linear-gradient`**. `radial-gradient(circle, …)` on small elements
  (particles) is still fine.
- **Responsive scaling**: layouts are a fixed 1920×1080 canvas scaled by `--scale-factor`,
  recomputed by `updateUIScaleFactor()` (`00_core.js`) on load, on window resize, and when
  opening the menu or the save screen.

## Editing the UI (workflow)

1. Edit the sources in `Content/UI/WebUI/src/` (css/, sections/, js/ — the numeric prefix is
   the concatenation order).
2. `python Extras/build_webui.py` → regenerates `shell.html` (**never** edit the shell
   directly).
3. In PIE: call `ReloadShell()` to see the change without restarting the editor.
4. Quick test outside the game: open `shell.html` in Chrome and use the console
   (`JRPGUI.openMenu()`, `showItemPopup('Potion', 3)`, …). The bridge is offline and sounds
   fall back to HTML5 audio.

## Fonts

Local, no CDN: `Content/UI/WebUI/fonts/` — **Cinzel** (general UI) and **MedievalSharp**
(dialogue/popup), declared via `@font-face` in `00_global.css`.
`python Extras/download_fonts.py` verifies and downloads them (`--check` only verifies).

## Contracts C++ calls in JS (do not rename)

`showItemPopup(name, qty)` · `JRPGUI.openMenu()` / `JRPGUI.closeMenu()` ·
`openDialogue(speaker, text, options)` · `openOptionsScreen('<origin>')` (origin =
`hud`/`menu`/`mainmenu`) · `JRPGMainMenu.open({hasSaves, version, build})` ·
`setBuildInfo(version, build)` · `UIState.set('<state>')` · `resetUIShell()` ·
`handleUIInput('<action>')` · `triggerAnimateOutAndClose()` (Escape) ·
`updateCharacterStatus(charId, level, hpCur, hpMax, mpCur, mpMax, apCur, apMax)` ·
`updateGoldTime(gold, playtimeSeconds)` (CoreSubsystem data; playtime in RAW SECONDS — JS
formats it and keeps the seconds ticking while the menu is open) ·
`JRPGSaveLoad.open(slots[, {mode, returnTo}])` / `JRPGSaveLoad.updateSlots(slots)` /
`JRPGSaveLoad.onSaveResult(i, ok)` — `{mode:'load', returnTo:'mainmenu'}` is the title's
"Continue": it opens straight into the grid in Load mode and returns to the main menu on
cancel.

## JS→C++ (through `window.ue.uebridge`)

`onuiready` (shell boot) · `closemenu` (return input) · `onmenuoptionselected` ·
`playsfx` (`Open`/`Close`/`Next`/`Cancel`/`Select`/`Item`) · `echo` ·
`onsaveslot(i)` · `onloadslot(i)` (JS hides the screen BEFORE calling; C++ restores input and
does the `OpenLevel`) · `onshopbuy` / `onshopsell` ·
`onmainmenuaction('newgame'|'continue'|'options'|'quit')` ·
`onuistatechanged('<state>')` (internal UI transition, does not return input) ·
`openurl(url)` (opens in the SYSTEM browser; http/https only) ·
`onpartytoggle(key, activate)` · `onitemdiscard(id, qty)` · `onsetdifficulty` ·
`ondevcommand(line)`.
