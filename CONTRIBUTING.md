# Contributing

Thanks for looking. This is a modular JRPG systems plugin for Unreal Engine 5.8, built for a
Legend of Legaia remake. Contributions are welcome — this page covers how to get it building
and how changes get in.

---

## Before you write code

1. **Get it building.** [`SETUP.md`](SETUP.md) has the full path. The one step that trips
   everyone up is the Ultralight SDK — see below.
2. **Read [`Docs/ARCHITECTURE.md`](Docs/ARCHITECTURE.md)**, in particular *Rules that must not
   be broken*. Those 47 entries are decisions and already-fixed bugs; breaking one
   reintroduces a problem someone already paid for. A PR that trips over one will get sent
   back with a pointer to it.
3. **Check [`Docs/STRUCTURE.md`](Docs/STRUCTURE.md)** for what exists (✅), what is a stub
   (🟡) and what is planned (⬜). Nearly everything is done except battle.

### The Ultralight SDK is not in this repository

The UI is HTML rendered through Ultralight 1.4. Its license is a development licence that is
`non-transferable` and `non-sublicensable`, and only permits shipping the SDK inside a
compiled product — not publishing it in a source repository. So it is gitignored and you
download your own, free, from [ultralig.ht](https://ultralig.ht).

Extract it into `Content/ThirdParty/Ultralight/`. Version **1.4.0.1b4b800**; anything outside
1.4.x may change the `IGPUDriver` interface and break the D3D11 driver.

> If the extraction is missing `shaders/hlsl/bin/`, the build fails at **compile** time rather
> than at link time, because the GPU driver `#include`s that bytecode directly. It is the most
> confusing failure in the project — check that folder first.

---

## How changes get in

```
your fork ──PR──► dev ──PR──► main
```

**Fork the repository and open pull requests against `dev`.** Both `dev` and `main` are
protected: nobody pushes to them directly, including the maintainer. `main` only receives code
by merging `dev` once a batch has been tested as a whole.

```bash
# on your fork
git switch dev
git switch -c feature/short-description
# ... work ...
git commit -m "feat: what changed"
git push -u origin feature/short-description
gh pr create --repo lixstudiosgames/JRPGFramework --base dev
```

Every PR is checked out and run in the editor before it is merged, so expect a round trip if
something does not build.

### What makes a PR easy to accept

- **One topic per PR.** A battle formula and a UI fix are two PRs, even when they are five
  lines each.
- **Say how you tested it.** "Compiles" is not tested. Which map, which screen, what you saw.
- **Don't reformat code you are not changing.** A whitespace pass buried in a logic change
  makes the diff unreviewable.
- **Data changes go through the CSV**, never the generated DataTable — see below.

### Commit messages

Prefix with the kind of change, then say what changed in plain words:

```
feat: AP gauge on the battle HUD
fix: BGM component leaking on every track change
docs: correct the Ultralight SDK version
refactor: extract the damage kernel into UBattleFormulas
```

---

## Conventions that matter

These come out of [`Docs/ARCHITECTURE.md`](Docs/ARCHITECTURE.md); they are the ones a new
contributor hits first.

| Area | Rule |
|---|---|
| **Subsystems** | Everything is a `UGameInstanceSubsystem`. They talk through the GameInstance (`GetSubsystem<>()`), never by direct reference |
| **Type names** | Prefix reflected types with `JRPG`. UHT sees `FPartyMember` and `UPartyMember` as the same name, and `Party/` collides with an engine plugin |
| **Language** | Every player-facing string is in **English**. Code comments and logs are in **Portuguese** |
| **UI** | `shell.html` is **generated**. Edit `Content/UI/WebUI/src/` and run `python Extras/build_webui.py` |
| **Game data** | The **CSV in `Docs/Data/` is the source of truth**, not the TOML and not the imported DataTable. Edit it through `python Extras/LegaiaStudio/studio.py` |
| **Gameplay rules** | Live in C++, never in the UI. The screen asks a subsystem and draws the answer — it never infers |
| **Third-party headers** | Ultralight SDK headers never appear under `Public/` — their macros corrupt UE headers |

### If you add a DataTable struct

`Extras/LegaiaStudio/studio.py` discovers schemas by regex over the C++ headers. Keep one
`UPROPERTY` per line, no `meta=(...)` with nested parentheses, inherit `: public FTableRowBase`
and close the struct with `\n};`. Use `Public/Shop/ShopData.h` as the model.

---

## The battle system

Battle and Arts are the only unimplemented systems — 71 lines of stubs. The plan is in
[`Docs/BATTLE_ROADMAP.md`](Docs/BATTLE_ROADMAP.md): 8 phases, each ending in something you can
run and watch work. **Read it before starting anything battle-related**, because the phases
have real dependencies and the early ones unblock the rest.

Two things worth knowing up front:

- **This is not a port.** The [`legend-of-legaia-re`](https://github.com/andrewaltimit/legend-of-legaia-re)
  reverse-engineering project is used as a *specification*: you read how the original solves
  something, then write it from scratch in C++ with the design changes this remake wants. Data
  (tables validated against the disc) and assets come from it; the logic is new.
- **Some departures are deliberate.** Regular Arts *add* AP while Hyper/Super/Miracle *spend*
  it; party and enemy counts are configurable rather than fixed at 3 and 5. The roadmap lists
  them and why.

Anything extracted from the game disc is Sony-derived and stays local — never commit it.

---

## Questions

Open an issue. If you are unsure whether an idea fits before spending time on it, an issue
first is cheaper than a PR that has to be turned down.
