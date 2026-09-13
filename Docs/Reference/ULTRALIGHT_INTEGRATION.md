# Ultralight Integration

How the HTML UI actually renders. This is the most complex part of the plugin — roughly 44%
of its C++ — and the part most likely to surprise a contributor.

For setting the SDK up in a fresh clone, see [`SETUP.md`](../../SETUP.md). For the UI's public
API and the JS contracts, see [`../Guides/WEBUI_API.md`](../Guides/WEBUI_API.md).

---

## The shape of it

The UI is a single HTML document (`shell.html`) rendered by **Ultralight 1.4 in GPU mode**
through a **custom D3D11 `IGPUDriver`**, on a **dedicated thread**, handed to Unreal as a
shared texture.

```
Game thread            UL thread                       UE render thread
───────────            ─────────                       ────────────────
ExecuteJS(...)  ──►  command queue
                       Renderer->Update()
                       Renderer->Render()
                         └─ FUltralightGPUDriverD3D11
                              draws into a D3D11 texture
                              created SHARED + KEYEDMUTEX
                       ReleaseSync(1)  ──────────────►  AcquireSync(1, 0)
                                                         blit into a UTexture2D
                                                         (UltralightBlitShader.usf)
                                                       ReleaseSync(0)
```

| Piece | File | Role |
|---|---|---|
| `UWebUISubsystem` | `Public/UI/WebUISubsystem.h` | Owns the shell; the whole Blueprint-facing API |
| `UJRPGWebBrowser` | `Public/UI/JRPGWebBrowser.h` | UMG `UWidget` wrapping `SUltralightBrowser` |
| `UWebContainerWidget` | `Public/UI/WebContainerWidget.h` | `UUserWidget` that hosts the browser |
| `UWebUIBridge` | `Public/UI/WebUIBridge.h` | JS ↔ C++, built on JavaScriptCore |
| `JRPGWebUI::ToJSStringLiteral` | `Public/UI/WebUIScripting.h` | Safe escaping for anything crossing into JS |
| `FUltralightRenderThread` | `Private/UI/UltralightRenderThread.h` | The dedicated thread and its command queue |
| `FUltralightGPUDriverD3D11` | `Private/UI/UltralightGPUDriverD3D11.h` | The custom `IGPUDriver` |
| `UltralightBlitShader.usf` | `Shaders/` | Blits the shared texture into the UE texture |

> **SDK headers never appear under `Public/`.** The GPU driver and the render thread are
> private on purpose, and `UltralightGPUDriverD3D11.h` states the rule at the top of the
> file. Including it from a public header leaks D3D11 and Ultralight types into every
> translation unit that touches the plugin.

---

## The dedicated thread

`FUltralightRenderThread` is an `FRunnable`. Everything that touches a `View` happens there;
the game thread only pushes commands onto a queue.

`EULCommandType` is the whole vocabulary: `MouseDown`, `MouseUp`, `MouseMove`, `KeyDown`,
`KeyUp`, `KeyChar`, `ScrollEvent`, `LoadURL`, `LoadHTML`, `ExecuteJS`, `BindBridge`, `Resize`,
`Shutdown`.

Adding a new interaction means adding a command type, not calling into Ultralight from the
game thread.

---

## The shared texture and its mutex

The driver creates its render target with:

```cpp
Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE
               | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
```

Two threads with two D3D11 devices share one texture, and an `IDXGIKeyedMutex` arbitrates.
The UL thread holds key 0 while drawing and releases to key 1; the UE render thread acquires
key 1, blits, and releases back to 0.

**Two traps are documented in the code and worth repeating:**

- **`SUCCEEDED()` is not enough on `AcquireSync`.** It returns `WAIT_TIMEOUT` (`0x00000102`)
  and `WAIT_ABANDONED` as *success* codes. Treating those as success makes the thread draw
  without owning the mutex and then call `ReleaseSync(1)` on something it never held. Check
  for `S_OK` explicitly.
- **The serial is incremented *after* `ReleaseSync(1)`.** The consumer uses a non-blocking
  acquire (`AcquireSync(1, 0)`), so a congested UE render thread only falls behind — it never
  deadlocks the UI thread.

---

## The bridge

`UWebUIBridge` binds C++ functions into the page's global scope through JavaScriptCore.
JS calls them as `window.ue.uebridge.<name>(...)`.

Two rules:

- **Anything C++ sends into JS as data must go through `JRPGWebUI::ToJSStringLiteral`.** A
  raw `FString` concatenated into `ExecuteJS` is an injection waiting to happen the first time
  an item name contains a quote.
- **An `FName` that may be empty goes through `JRPGWebUI::OptionalNameToJS`.**
  `FName::ToString()` on `NAME_None` returns the literal string `"None"`, which is truthy in
  JS — see the trap described in [`../Guides/ITEMS_SCREEN.md`](../Guides/ITEMS_SCREEN.md).

The current callbacks are listed in [`../Guides/WEBUI_API.md`](../Guides/WEBUI_API.md).

---

## Build wiring

`JRPGFramework.Build.cs` handles all of it:

- `bUseUnity = false` and `PCHUsage = UseExplicitOrSharedPCHs` — the third-party headers do
  not survive unity builds.
- `PublicIncludePaths` ← the SDK's `include/`.
- `PublicAdditionalLibraries` ← `Ultralight.lib`, `UltralightCore.lib`, `WebCore.lib`,
  `AppCore.lib`.
- `PublicDelayLoadDLLs` ← the four DLLs, plus a manual copy into the plugin's and the host
  project's `Binaries/Win64`. Delay loading is what lets the module load before the subsystem
  initializes.
- `PublicSystemLibraries` ← `d3d11.lib`, `dxgi.lib`.
- `RuntimeDependencies` ← `cacert.pem`, `icudt67l.dat`, and wildcards for `shell.html`,
  `fonts/*.ttf`, `images/*.png`.

> **The `RuntimeDependencies` wildcards are not optional.** UE's staging copies only cooked
> `.uasset`/`.umap` out of a plugin's Content folder. Raw html/ttf/png files are excluded
> unless declared, so without them the packaged build has no `shell.html`: Ultralight loads a
> path that does not exist and the UI is invisible. The editor works fine, because it reads
> straight from the project — which is exactly what makes this bug expensive to find.

---

## Rendering rules that bite

These live in [`../Guides/WEBUI_API.md`](../Guides/WEBUI_API.md) as well, because they are
authoring rules rather than architecture — but they are consequences of Ultralight, not of
the design:

- **`#ul-repaint-anchor`** keeps the page from ever being empty. With no draw commands the
  final frame is never presented and the UI freezes on screen.
- **`setInterval` does not fire reliably in-game.** Use `requestAnimationFrame`.
- **`radial-gradient(ellipse …)` ignores the aspect ratio** and draws a circle. Use
  `box-shadow` for glow and `linear-gradient` for horizontal falloff.

---

## License note

The Ultralight SDK is not redistributed with this repository. Its license grants a
development licence that is `non-transferable` and `non-sublicensable`, and only permits
distributing the SDK bundled inside a compiled product, to end users. See
[`SETUP.md`](../../SETUP.md).

Article 4.4 of that license also requires the notice in the SDK's `license/NOTICES.md` to
appear in the credits of any product using the library.
