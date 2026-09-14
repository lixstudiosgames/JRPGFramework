# Setup

Steps to build the plugin from a clean clone.

The repository **does not include the Ultralight SDK**. Its license (`Ultralight Free License
Agreement v2`, Article 2.1) is `non-transferable` and `non-sublicensable`, and Article 4.3
only permits distributing the SDK bundled inside a compiled product, to end users — not in a
source repository. The SDK is free and takes two minutes to download.

---

## 1. Prerequisites

| Item | Version |
|---|---|
| Unreal Engine | **5.8.1** |
| Visual Studio | **VS 18** with MSVC **14.51** or newer |
| Python | 3.10+ (stdlib only, no `pip install`) |

> **Watch the compiler:** the MSVC 14.36 that ships with Visual Studio 2022 is **below UE
> 5.8's minimum** and the build fails. Check under *Visual Studio Installer → Modify →
> Individual components* that toolset 14.51 is installed.

---

## 2. Download the Ultralight SDK

1. Go to **https://ultralig.ht** → Downloads and get the **Windows x64** SDK.
2. This project uses **1.4.0.1b4b800** (check `VERSION.txt` in the package). Versions outside
   1.4.x may change the `IGPUDriver` interface and break the D3D11 driver.
3. Extract into `Content/ThirdParty/Ultralight/` so the tree looks like this:

```
Content/ThirdParty/Ultralight/
├── bin/           Ultralight.dll, UltralightCore.dll, WebCore.dll, AppCore.dll
├── lib/           Ultralight.lib, UltralightCore.lib, WebCore.lib, AppCore.lib
├── include/       SDK headers
├── resources/     cacert.pem, icudt67l.dat
├── shaders/       hlsl/bin/*.h  (bytecode the driver includes directly)
├── license/       EULA.txt, LICENSE.txt, NOTICES.md
└── VERSION.txt
```

`samples/`, `inspector/` and `tools/` come in the package but the build does not use them —
delete them to save 31 MB if you like.

### What the build consumes from each folder

| Folder | Used by |
|---|---|
| `include/` | `PublicIncludePaths` in `JRPGFramework.Build.cs` |
| `lib/` | `PublicAdditionalLibraries` — the 4 `.lib` files |
| `bin/` | `RuntimeDependencies` + `PublicDelayLoadDLLs` — the 4 `.dll` files |
| `resources/` | `RuntimeDependencies` — `cacert.pem` and `icudt67l.dat` |
| `shaders/hlsl/bin/` | `#include`d directly at lines 3-6 of `Private/UI/UltralightGPUDriverD3D11.cpp` |

Without `shaders/hlsl/bin/*.h` the build breaks at **compile** time, not at link time — the
most confusing failure to diagnose if the extraction comes out incomplete.

---

## 3. Generate the WebUI

The UI is a single HTML SPA **generated** from `Content/UI/WebUI/src/`. `shell.html` is never
edited by hand.

```bash
python Extras/build_webui.py
```

If the local fonts (Cinzel, MedievalSharp) are missing from `Content/UI/WebUI/fonts/`:

```bash
python Extras/download_fonts.py
```

---

## 4. Build the plugin

```bash
python Extras/build_plugin.py
```

The script runs `RunUAT BuildPlugin` with the `-Rocket` flag (without it UE 5.8's UBT crashes
with `ArgumentNullException`), isolates the source into a sibling working folder, and copies
the result into the destination project's `Plugins/` folder.

Two paths at the top of the script come from the original machine and will likely need
adjusting in your clone:

```python
ENGINE_DIR          = r"D:\JOGOS\UE_5.8"
PROJECT_PLUGINS_DIR = r"D:\JOGOS\PROJETOS UNREAL\5.8\Legaia\Plugins\JRPGFramework"
```

Details and the equivalent manual command are in
[`Docs/Guides/BUILDING.md`](Docs/Guides/BUILDING.md).

---

## 5. Wire it into a project

1. Copy the packaged plugin into your UE 5.8 project's `Plugins/JRPGFramework`.
2. Make the project's GameInstance inherit from `UJRPGGameInstance` — the 12 subsystems
   register themselves.
3. Import the CSVs from `Docs/Data/` as DataTables under `/Game/Data/`, keeping the file name
   (`DT_Items`, `DT_Shops`, `DT_Characters`, `DT_StatGrowth`, `DT_GrowthCurve`,
   `DT_Difficulty`, `DT_LevelCurve`, and `DT_CameraPresets` → import as `DT_Camera`).
   `UCoreSubsystem::ResolveDataTable` looks in `/Game/Data/` and falls back to
   `/JRPGFramework/Data/`.
4. On the map's `BeginPlay`: `WebUISubsystem → InitializeUIShell`.

The full walkthrough is in [`Docs/STRUCTURE.md`](Docs/STRUCTURE.md), under *Minimum Blueprint
wiring in the game project*.

---

## 6. Required credits

Article 4.4 of the Ultralight license requires the notice in the SDK's `license/NOTICES.md` to
appear in the credits section of any product using the library. That applies to the game's
final build, not to the plugin itself.

---

## Where to go next

| Document | Content |
|---|---|
| [`Docs/README.md`](Docs/README.md) | Documentation index |
| [`Docs/ARCHITECTURE.md`](Docs/ARCHITECTURE.md) | How the plugin fits together, and the rules that must not be broken |
| [`Docs/STRUCTURE.md`](Docs/STRUCTURE.md) | File-by-file tree: what exists, what is a stub, what is planned |
| [`Docs/BATTLE_ROADMAP.md`](Docs/BATTLE_ROADMAP.md) | The largest unimplemented system, in 8 phases |
| [`Docs/Guides/`](Docs/Guides/) | How to use each system |
