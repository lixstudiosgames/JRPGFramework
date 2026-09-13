# Audio System (BGM + Volumes)

`UAudioSubsystem` handles **BGM that persists across maps** and **volume settings across 4
channels** (Master, Music, SFX, Ambient), saved to disk in the **"JRPGSettings"** slot —
separate from the game's 15 save slots.

## 1. BGM in Blueprints

**Level BP (BeginPlay):**
```
Branch: AudioSubsystem → IsBGMPlaying
  false → AudioSubsystem → PlayBGM(MapBGM, bPersistAcrossMaps, FadeInTime)
  true  → do nothing (persistent BGM from the previous map keeps playing)
```

**Teleport (before OpenLevel):**
```
Branch: IsBGMPlaying AND IsBGMPersistent
  true  → do nothing (the track carries into the new map)
  false → StopBGM(FadeOutTime)   ← optional: non-persistent BGM dies on its own during
                                    a level change; a manual StopBGM is for fading out
                                    or for teleporting within the SAME map
```

### PlayBGM rules

- **Same track already playing** → ignored (it does not restart).
- **A different track playing** → automatic crossfade when `FadeInTime > 0` (old one fades
  out, new one fades in); `FadeInTime = 0` is a hard cut.
- `bPersistAcrossMaps = true` → the sound survives `OpenLevel` (native `CreateSound2D`
  persistence); `false` → the engine destroys it on the map change.
- `StopBGM(FadeOutTime)` — stops with an optional fade.
- `GetCurrentBGM()` — the current track, or nullptr.

### The old component is destroyed (it does not leak)

The BGM component is created with **`bAutoDestroy = false`** — that is exactly what lets it
survive `OpenLevel`. The cost is that **`Stop()` does not destroy it**: it stays alive,
silent, forever.

So every track change and every `StopBGM` goes through `RetireBGMComponent()`, which turns
`bAutoDestroy` back on **before** the fade — the component destroys itself when the fade
finishes, without cutting the fade short — or destroys it immediately on a hard cut. Before
this, every track change left a `UAudioComponent` behind.

If you touch this code: **never** call `Stop()`/`FadeOut()` directly on `BGMComponent` and
drop the reference. Go through `RetireBGMComponent()`.

### Loading cuts the music

`SaveSubsystem::LoadGame` calls `StopBGM(0)` before `OpenLevel`. Cross-map persistence is
right for a door and wrong for a load: the track from wherever you were would keep playing
in the place you loaded into. Hard cut rather than fade — the map is disappearing anyway,
and fading a persistent component is precisely the case that needs the
`RetireBGMComponent` handling above.

## 2. Volumes — 4 channels (0..1, default 1.0)

| Setter | Getter | EFFECTIVE getter (Master × channel) |
|---|---|---|
| `SetMasterVolume` | `GetMasterVolume` | — |
| `SetMusicVolume` | `GetMusicVolume` | **`GetEffectiveMusicVolume`** |
| `SetSFXVolume` | `GetSFXVolume` | **`GetEffectiveSFXVolume`** |
| `SetAmbientVolume` | `GetAmbientVolume` | **`GetEffectiveAmbientVolume`** |

- **In your own Blueprint PlaySound nodes:** wire the EFFECTIVE getter into the
  **Volume Multiplier** pin (`GetEffectiveSFXVolume` for effects,
  `GetEffectiveAmbientVolume` for ambience).
- **UI sounds** (menu, shop, popups — played from C++) already respect Master × SFX
  automatically; nothing to wire.
- **While BGM plays:** changing Master or Music adjusts the volume LIVE.
- **Persistence:** every change writes itself to
  `<Project>/Saved/SaveGames/JRPGSettings.sav` and reloads on startup. Options sliders only
  need to call the setters.

## 3. Troubleshooting

- **Music restarts when re-entering a map:** the Level BP must check `IsBGMPlaying` before
  `PlayBGM` (and `PlayBGM` already ignores a request for the SAME track).
- **Music stopped on a map change but should not have:** check `bPersistAcrossMaps = true`
  in the `PlayBGM` call.
- **Volume does not affect a sound:** the PlaySound Volume Multiplier pin must be wired to
  the effective getter — BP sounds are not automatic, only UI sounds are.
- **Reset the settings:** delete `JRPGSettings.sav` from `Saved/SaveGames/`.
