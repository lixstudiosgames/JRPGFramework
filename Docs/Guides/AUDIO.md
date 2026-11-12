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

## 4. Battle voices and battle music

### Battle voices (`FJRPGBattleVoices`)

The voices of a fight that are **not** on the clips' cue tracks: the Arts **shout**, the
SpecialStarter **fanfare** and the **block grunt**. They are CD-XA in the original and follow its
rules (RE `docs/subsystems/battle-action-helpers.md` › "Battle voice cues"). Data:
`DT_BattleVoices` (one row per hero: the sounds of each channel and how long the drive reads it)
and `DT_ArtVoices` (one row per Art), both generated from the disc by `Extras/ps1_battle_voices.py`
([`PS1_ASSETS.md`](PS1_ASSETS.md#the-battle-voices-arts-shout-starter-fanfare-block-grunt)).
Without the two tables the fight simply has none of these voices.

`UJRPGBattleSceneSubsystem` owns one `FJRPGBattleVoices` per fight and calls it on every step the
acting hero starts (the original's anim commit) and on every `Block` event.

| Voice | When | Which channel |
|---|---|---|
| **Shout** | the `ArtStarter` of a known Art (for the Art right after it), or an Art right after a clip that raises the **latch** (the SpecialStarter, a Super finisher) | random from the Art's list, never the last channel that played (one memory for everyone). The list: `ShoutLatched` under the latch, `ShoutFollowed` when something follows the Art in the turn, `Shout` when the Art ends the turn. A finisher (`bFinisherTable`) with something after it plays the hero's `FinisherCue` sound instead |
| **Fanfare** | the `SpecialStarter` | Super, Miracle or a newly learned Art: `GenericFanfareChannel` (1). A Hyper: a coin flip between the two channels of `Fanfare`. Dropped while the drive is still reading the previous voice |
| **Block grunt** | a hero's strike is blocked | the attacker's `BlockGrunt`; skipped while the drive is busy |

One drive: a new shout or grunt **cuts** the voice that is playing. Each voice starts
`AJRPGBattleArena::VoiceLatencySeconds` (0 by default) after the request, read on the logic frame the clip starts. The XA channels open with the CD
wait as silence (~0.15 s, up to 0.31 s on some fanfares); `ps1_battle_voices.py` trims it (10 ms
kept), so the latency is the only delay (both together left the voice behind the animation). The
**Arts shout slow** (`ArtsShoutTimeDilation`) lasts the read span of the voice that played: the
shout at a known Art's starter. Which voices open it is per kind on the arena (`bShoutSlowKnownArt`
on; `bShoutSlowNewArt`, `bShoutSlowHyper`, `bShoutSlowSuperMiracle` off): the SpecialStarter's loop
window (`UJRPGAnimNotify_LoopWindow`) already gives the dash the original's hold, and a slow on top
made the starter and the Art after it far too slow.
`ArtsShoutSeconds` is only the fallback for a hero with no voice data. Random picks use a stream of their own, never the fight's `Rng`. The voices
play 2D at `GetEffectiveSFXVolume`.

Not here: the voices on the clips' cue tracks (Spirit, the Miracle finisher, the victory voice),
which are `AnimNotify_PlaySound`s from `ps1_party_sounds.py` and do not share the drive.

### Battle music

The tracks live in **`DT_BattleMusic`** (`FJRPGBattleMusicData`, one row per track: `Intro` played
once, then `Loop` looping, with no seam: `UAudioSubsystem::GetIntroLoopSound` builds an in-memory
`SoundCue` `Concatenator(Intro, Loop)`; with no `Intro` the `Loop` plays alone and may be any
sound). Every place that picks music names a **row**:

| Where | Field | Effect |
|---|---|---|
| `FJRPGBattleSetup` | `BattleMusic` | this fight's row, over everything else |
| `FJRPGBattleSetup` | `bKeepCurrentMusic` | the fight leaves the music alone |
| `DT_Bosses` (`FJRPGBossData`) | `Music`, `bKeepCurrentMusic` | the boss fight's row, or keep what plays (found by the fight's enemies, `UJRPGEncounterLibrary::FindBossFight`) |
| `AJRPGBattleArena` | `BattleMusic` (default `Battle1`), `bKeepFieldMusic` | ordinary fights here; `bKeepFieldMusic` keeps the field track (a boss with its own music still swaps) |
| `AJRPGFieldEnemy`, `AJRPGBattleWaves` | `BattleMusic`, `bKeepCurrentMusic` | copied into the setup they build |

Order: setup → boss → arena. When the fight starts, the scene subsystem remembers the current BGM
and stops it (the transition's start sound plays alone); the battle track starts with a **hard cut**,
like the original, `AJRPGBattleArena::BattleMusicLeadSeconds` (1 s) before the screen has finished
opening (the end of the transition's reveal fade, or of the camera fade; at once on a cut). When the scene closes the field track comes back (or the BGM stops if there was
none).

**How the original picks** (RE `docs/subsystems/audio.md`, "The battle sound set picks the
fight's track"): the field script stores a "sound set" `N` (op `0x35` sub-op 7, `-1` = keep the
music) and the battle intro loads battle bundle `0x36F + N`, a copy of one `music_01` track:

| Row | Sound set | Track | Used by |
|---|---|---|---|
| `Battle1` | 0 | 2026 Battle theme 1 | ordinary fights (the default of every scene) |
| `Battle2` | 1 | 2027 Battle theme 2 | ordinary fights where a region's flag (`DAT_8007B64B`) flips the default |
| `Boss1` | 2 | 2028 "Assault" | Caruban, Berserker, Xain, Lapis; Gaza, Gaza (Sim-Seru), Che Delilas (the event starts it, set `-1`) |
| `Songi` | 3 | 2030 | Songi 1st / 2nd / 3rd |
| `Boss2` | 4 | 2032 "Apostles of the Mist" | Zeto, Dohati, Gi Delilas, Zora, Rogue, Jette |
| `Cort1` | 5 | 2031 | Cort |
| `Koru2` | 6 | 2051 "The Hot Heat" | Koru |
| `VanSaryu` | 7 | 2067 "Tense Presence" | Van Saryu (inferred: the only fight not entered by `3E FF`, and no other fight uses set 7) |
| `Cort2` | 8 | 2061 "Apocalypse" | Cort (Juggernaut) |

Set 9 (the Wild Arms battle theme, 2073) is on the disc but no fight uses it. Viguro and the Tetsu
rematch use `Battle1` (their records select no set); Lu Delilas keeps the music (`-1` after the
previous duel). The boss column comes from a disc census of every `3E FF <row>` scripted battle
(`Extras/generate_bosses_csv.py`, `MUSIC`). In the original some scenes store `-1` on entry
(chitei2, deroa, dohaty, garmel on one branch, jouina..jouine, retock, retockin, taiku): their
ordinary fights keep the field music, which is `bKeepFieldMusic` on the arena.

**The tracks in use come from the soundtrack album** (`--ost <folder>`: the WAVs named
`<sound-test index> <title>.wav`, fuller than the game's sequencer). The score still gives the loop:
its start and length, refined on the album's audio (the album runs up to ~0.4 % off the score's
clock), and the Loop's last 50 ms crossfade into what precedes its start, since a recording does not
repeat sample for sample. Assets `BGM_OST_<id>_<Name>_Intro / _Loop`.

Without `--ost`, `Extras/ps1_battle_music.py` renders each track with the RE engine's sequencer + SPU
(`legaia-engine pcm-trace --bgm-id`, on the silent `init_data` scene), splits it at the score's
loop markers (NRPN 99 = 20 / 30, refined on the PCM: the engine runs ~0.5 % off the score's clock)
and writes `DT_BattleMusic.csv`; `Extras/ue_import_battle_music.py` imports the WAVs into
`/Game/Legaia/sounds/OST/Battle/` (the Loop flagged looping).
