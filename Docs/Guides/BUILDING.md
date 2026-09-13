# Building and Packaging

First-time setup (prerequisites, Ultralight SDK, WebUI generation) is in
[`SETUP.md`](../../SETUP.md) at the repository root. This page covers the packaging step
itself.

## The normal path

```bash
python Extras/build_plugin.py
```

The script:

1. Isolates the source into a working folder next to the plugin (`CODE/5.8/Build/TempSource`),
   so `Binaries/` and `Intermediate/` from your dev tree do not poison the package.
2. Runs `RunUAT BuildPlugin` with the required flags.
3. Cleans the generated `HostProject` out of the result.
4. Copies the packaged plugin into the target project's `Plugins/JRPGFramework`.

Two paths at the top of the script come from the original machine and will likely need
adjusting in your clone:

```python
ENGINE_DIR          = r"D:\JOGOS\UE_5.8"
PROJECT_PLUGINS_DIR = r"D:\JOGOS\PROJETOS UNREAL\5.8\Legaia\Plugins\JRPGFramework"
```

## Known engine bug: the `-Rocket` flag

`BuildPlugin` on UE 5.8 crashes with `ArgumentNullException` without `-Rocket`. The flag
makes UBT skip the engine-module scan that triggers it. `build_plugin.py` already passes it;
if you invoke UAT by hand, you must too.

## Manual equivalent

```powershell
& "D:\JOGOS\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" `
  BuildPlugin `
  -Plugin="<path>\JRPGFramework\JRPGFramework.uplugin" `
  -Package="<path>\JRPGFramework_build" `
  -Rocket `
  -nocompileuat
```

The output folder is what you drop into any UE 5.8 project's `Plugins` directory.

## Regenerating the WebUI

The UI shell is generated, never hand-edited:

```bash
python Extras/build_webui.py
```

Edit the sources under `Content/UI/WebUI/src/` and re-run it. See
[`WEBUI_API.md`](WEBUI_API.md).

## Things that break the packaged build

- **Missing `RuntimeDependencies`.** UE's staging copies only cooked `.uasset`/`.umap` from
  a plugin's Content folder. Raw files (html/ttf/png) are excluded unless declared in
  `JRPGFramework.Build.cs`. Without that, the packaged build has no `shell.html` and the UI
  is invisible — while the editor works fine, because it reads straight from the project.
- **Ultralight DLLs.** They are delay-loaded and copied by the Build.cs into both the
  plugin's and the host project's `Binaries/Win64`.
