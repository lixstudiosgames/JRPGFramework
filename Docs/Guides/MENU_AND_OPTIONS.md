# Main Menu and Options Screen

Two shell screens:

- **Main Menu** (section `70_mainmenu`) — a **transparent** title screen (the map's 3D scene
  shows through) with New Game / Continue / Options / Quit, copyright + version, and the
  lower third of project-support links (Patreon / YouTube / itch.io).
- **Options** (section `40_options`) — 5 top tabs (Display, Audio, Game, Controller,
  Keybindings), a description panel on the right, and its own particle background.

> ⚠️ **Current scope: visuals only.** Changing resolution, VSync, quality, volume or language
> on screen **does not yet apply anything to the game** — the wiring points are marked with
> `TODO(apply)` in `src/js/40_options.js`. What genuinely works is all the **navigation**:
> opening, switching tabs, moving around, changing the on-screen value, and returning to the
> correct origin screen.

## 1. Blueprint setup (title map)

```
Level BP BeginPlay on the menu map:
  WebUISubsystem → InitializeUIShell
  WebUISubsystem → OpenMainMenu

Also on Begin Play:
  WebUISubsystem → OnMainMenuAction  (Bind Event)
     └─ Switch on String (Action):
          "newgame" → Open Level (the game's starting map)
          "quit"    → (optional: fade / save settings — QuitGame is already done in C++)
```

`OpenMainMenu()` focuses the UI (cursor + keyboard/gamepad), sends version/build, and
automatically disables **Continue** when no save exists.

**Continue** and **Options** are resolved inside the plugin and **never** reach
`OnMainMenuAction` — only `newgame` and `quit` do.

On `newgame` the C++ has already, in order: returned input to the game, set `HudOnly`, and
called `CoreSubsystem → StartNewGame()` (inventory/flags/gold/playtime cleared,
`bIsNewGame = true`). The Blueprint only needs the `Open Level`.

### Version and build in the footer

`GameVersion` (default `"v0.1.0"`) and `GameBuild` (default `"Build 2026.08"`) are
`WebUISubsystem` properties — set them from Blueprint before `OpenMainMenu`
(`Set Game Version` / `Set Game Build`) or keep the defaults.

## 2. Options screen

| Call | Opens from | Closing returns to |
|---|---|---|
| `WebUISubsystem → OpenOptions()` | the game (`HudOnly`) | the game (input returned) |
| **Options** in the pause menu | `MenuOpen` | the pause menu, as it was |
| **Options** in the main menu | `MainMenuOpen` | the main menu |

Nothing to configure: the origin is inferred from the UI's current state and JS reopens the
previous screen by itself.

### Navigation (keyboard and gamepad)

| Action | Keyboard | Gamepad |
|---|---|---|
| Switch tab | **Q / E** | **LB / RB** |
| Move between rows | `▲▼` (or W/S) | D-Pad / stick |
| Change the value (sliders step by 5) | `◀▶` (or A/D) · Enter | D-Pad / A |
| Close (returns to origin) | `Esc` | B |

The `◀ ▶` arrows at the ends of the tab bar are **purely a visual hint** that Q/E and LB/RB
switch tabs — they are not clickable; they pulse on their own and flash on the matching side
with each change.

`tab_prev`/`tab_next` are semantic actions: LB/RB is translated in C++
(`SUltralightBrowser::OnKeyDown`) and Q/E locally in JS — both routes land in the same
`handleUIInput`.

The mouse works everywhere: hover selects a row or tab; clicking the row's `◀ ▶` arrows and
the slider bar changes the value.

### What each tab holds

| Tab | Content |
|---|---|
| **Display** | *Display*: Window Mode, Resolution, VSync, FPS Limit · *Graphics*: Overall Quality, Textures, Shadows, Lighting, Effects, Post-Processing, View Distance, Anti-Aliasing |
| **Audio** | *Volumes*: Master, Music, Sound Effects, Ambient, Interface (sliders) · *Other*: Audio Device |
| **Game** | Language (English / Português (BR)) |
| **Controller** | Placeholder: "Controller diagram coming soon" |
| **Keybindings** | Current keys as badges, read-only ("Rebinding coming soon") |

### How to actually wire it later

- **Volumes** → `AudioSubsystem → SetMasterVolume/SetMusicVolume/SetSFXVolume/SetAmbientVolume`
  (they take 0..1; the slider shows 0..100). They **already persist on their own** into the
  `JRPGSettings` slot (`UJRPGSettingsSave`) — just call them.
- **Display/Graphics** → `UGameUserSettings`; new fields go into `UJRPGSettingsSave`.
- **Interface (UI volume)** and **Language** have no channel/field yet — they need to be
  created alongside.

## 3. Project-support links

The three main-menu cards call `bridge.openurl(url)` → `FPlatformProcess::LaunchURL`, which
opens the **system browser**, outside the game. Only `http://` and `https://` are accepted;
anything else is refused and logged.

The URLs live in `data-url` inside `src/sections/70_mainmenu.html`.

## 4. Editing the visuals

The shell's normal workflow (see [`WEBUI_API.md`](WEBUI_API.md)):

1. Edit `Content/UI/WebUI/src/` — `sections/40_options.html`, `css/40_options.css`,
   `js/40_options.js` (the option rows are **data** in the `TABS` array at the top of the JS)
   and the `70_mainmenu.*` equivalents.
2. `python Extras/build_webui.py`
3. In PIE: `ReloadShell()`.

Quick test without the editor: open `Content/UI/WebUI/shell.html` in Chrome and use the
console —
`JRPGMainMenu.open({hasSaves:true, version:'v0.1.0', build:'Build 2026.08'})`,
`JRPGOptions.open('mainmenu')`,
`JRPGSaveLoad.open([], {mode:'load', returnTo:'mainmenu'})`.
With the bridge offline, clicks that would reach C++ only show up in the log.

## 5. Troubleshooting

- **Main menu does not open:** `OpenMainMenu` only works from `HudOnly` — check that
  `InitializeUIShell` runs first and no other screen was left open.
- **Continue is greyed out:** that is correct when none of the 15 slots hold a save
  (`SaveSubsystem → DoesSlotExist`).
- **Closing options returned input to the game instead of the menu:** the pause menu must
  have been opened through `OpenMenu()` (state `MenuOpen`) — if the state is out of sync, the
  origin falls back to `hud`.
- **The options screen shows no background:** the background is the `#options-bg` layer; it is
  `position: fixed` on purpose, outside the scaled 1920×1080 canvas.
- **Nothing happens when clicking a support card:** check the Unreal log — non-http/https URLs
  are refused by the bridge.
