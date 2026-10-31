# Arts — `DT_Arts`

Every Tactical Art the heroes know, one row each, plus the pieces the long ones are made of.
The table is data only: recognising the buttons, playing the clips and dealing the hits is
Phase 6 of [BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md).

Row struct: **`FJRPGArtData`** (`Public/Arts/ArtsData.h`). CSV: `Docs/Data/DT_Arts.csv`.
Import it into `/Game/Data/` as a DataTable with that struct.

---

## 1. Columns

| Column | Notes |
|---|---|
| `Key` | Same as the row name: `vahn_hyper_elbow` |
| `Character` | Row name in `DT_Characters` (`Vahn`, `Noa`, `Gala`) |
| `DisplayName` | Shown in battle and in the Arts menu |
| `Kind` | `Regular`, `Hyper`, `Super`, `Miracle`, or `Part` — a piece only played inside a sequence (a Super's finisher, the 2nd segment of Hurricane Kick); it has no `Command` |
| `Command` | The buttons, in order: `Arms`, `RaSeru`, `High`, `Low`. The player's scheme — Noa's mirroring (her Arms is the right button) is resolved at input time, not stored |
| `OriginalAP` | The original's AP cost, kept as a balance reference |
| `APGain` | Regular: AP the Art **adds** (seeded with `OriginalAP / 2`) |
| `APCost` | Hyper / Super / Miracle: AP the Art **spends** (seeded with `OriginalAP`) |
| `CommandCost` | Weight against the turn's command gauge (seeded with the button count) |
| `Anim` | The clip: `/Game/Legaia/Battle/Party/<Hero>/AS_<Hero>_Battle_<Art>` |
| `HitPower` | One multiplier per hit, in order. The disc uses 12, 18, 20, 22, 28 |
| `HitTarget` | One per hit, same order: `Upper` (vs UDF), `Lower` (vs LDF), `UpperMissesShort`, `LowerMissesFloating` |
| `Sequence` | A combined Art: what plays, in order (below). Empty = `ArtStarter` + this row's `Anim` (a Regular) |
| `ActionConstant` | The Art's record on the disc (`0x1B..0x32`, stored in decimal). `0` = a new Art with no record |

### `Sequence`

Each step is either:

- the **key of another Art of the same hero** (`vahn_somersault`) — its `Anim` and hits; the
  row's own key means its own `Anim`;
- a **basic clip** of the hero: `SwingL`, `SwingR`, `SwingD`, `SwingU` (the plain swings, by
  pad direction), `ArtStarter`, `SpecialStarter`.

This is how the original builds its long Arts, and the generator writes them from the disc:

| Art | `Sequence` |
|---|---|
| Super — Tri-Somersault | `ArtStarter, vahn_somersault, SwingU, ArtStarter, vahn_cyclone, SwingD, SpecialStarter, vahn_tri_somersault_finisher ×3` |
| Miracle — Vahn's Craze | `SwingL, SwingR, SwingD, SwingU, SpecialStarter, vahn_spin_combo, vahn_charging_scorch, …, vahn_tornado_flame_miracle` |
| Hyper — Fire Blow | `SpecialStarter, vahn_fire_blow` |
| Hyper in two records — Hurricane Kick | `SpecialStarter, noa_hurricane_kick, noa_hurricane_kick_3` |

The Supers' sequences are the original's queue replacement: the Arts the player performed with
the connector swing between them, then the finisher in place of the last one (RE:
`crates/art/src/super_art.rs`, captured from battle RAM). The Miracles replace the whole queue
(`crates/art/src/miracle.rs`).

A Hyper is staged behind the **`SpecialStarter`** (`0x1A`), not the `ArtStarter` — the RE's
fanfare capture shows Noa's Hurricane Kick as `1A 1D 1E` (her `0x1C` record, "Illusion Kick",
is not part of it).

---

## 2. Making a new Art

A row the generator did not create is **never** touched by it. Add one by hand in the CSV or in
LegaiaStudio (**Arts**):

**A new Art with an existing clip** — e.g. a Regular for Vahn reusing Somersault's clip with
another input:

| Column | Value |
|---|---|
| Key / row name | `vahn_moon_kick` |
| Character / Kind | `Vahn` / `Regular` |
| Command | `(High,High,Low)` |
| APGain / CommandCost | `9` / `3` |
| Anim | `/Game/Legaia/Battle/Party/Vahn/AS_Vahn_Battle_Somersault.AS_Vahn_Battle_Somersault` |
| HitPower / HitTarget | `(20)` / `(Upper)` |
| ActionConstant | `0` |

**A new combined Art (Super-style)** — chain existing Arts and pieces; the finisher can be any
row, including a `Part` you add for it:

| Column | Value |
|---|---|
| Kind | `Super` |
| Command | the whole input |
| APCost | what it spends |
| Sequence | `(ArtStarter,vahn_hyper_elbow,SwingL,ArtStarter,vahn_fire_blow,SpecialStarter,vahn_burning_flare)` |

**A new clip** — any `UAnimSequence` on the hero's battle skeleton (`SK_<Hero>_Battle`)
works in `Anim`. Clips made in Blender or Unreal come in like the disc ones; the skeleton is a
`root` plus one `part_NN` bone per rigid piece, so the animation is keyed per piece
([PS1_ASSETS.md](PS1_ASSETS.md)).

Hits of a new Art land where its clip says — the damage moment goes on the clip as a notify in
Phase 6, not in the table.

---

## 3. Testing Arts — `AJRPGArtTester`

Drag **`JRPGArtTester`** into any map (Place Actors → search "Art Tester"). It shows the hero's
battle model and plays Arts the way they come out in battle. Everything is in the Details panel,
and the buttons work in the editor viewport too (Realtime on), not only in Play.

The rules are not in the tester: **`UJRPGArtSequenceLibrary`** (`Public/Arts/JRPGArtSequence.h`)
builds a turn's clip queue from `DT_Arts` (the leading swings, the starter, the Art or its
`Sequence`), writes each step's speed scale and finds its Chain Cut. The battle uses the same
library; the tester only plays the queue and lines the clips up.


| Field / button | What |
|---|---|
| `Hero` | Vahn, Noa, Gala — filters the lists |
| `Art` + **Play** | Plays one Art as the game stages it: the buttons **before the last** as plain swings, then the last one becomes the starter + the Art (Iron Head: High, Low, Low = `SwingU`, `SwingD`, `ArtStarter`, Ironhead). A Hyper has no swings: only the `SpecialStarter` and the Art (its `Sequence`). A Super plays its `Sequence` with the **first** Art's leading swings before it — its Arts overlap (one's last button is the next one's first) and the buttons in between are the connector swings already in the `Sequence`: Back Punch x3 = `SwingU`, `SwingD`, `ArtStarter`, Ironhead, `SwingU`, `ArtStarter`, Flying Knee Attack, `SwingR`, `SpecialStarter`, finisher ×3 (7 steps for its 7 buttons). A Miracle replaces the whole turn |
| `Combo` + **Play Combo** | A turn put together by hand: Arts of the hero and loose buttons (`Arms`, `Ra-Seru`, `High`, `Low` = a plain swing), played back to back. E.g. `vahn_somersault, Arms, vahn_hyper_elbow` |
| **Play All** | Every Art of the hero (not the Parts), one after the other |
| **Next Art** / **Previous Art** | Walks the hero's list (Parts included) and plays |
| **Stop** | Back to Idle, where it stands |
| `bPlayLeadingSwings` | The swings of the buttons before the last, on Regular Arts and a Super's first Art (on, as the game). Off = starter + Art only |
| `bChainCut` | With another step after it, a clip ends at its **Chain Cut** notify, as in the game: swings follow each other right after the hit, without the recovery. Off = every clip plays whole |
| `bGameSpeed` | The game's speed scale per step (below). Off = each clip's own `RateScale` |
| `bFollowAnimation` / `FollowBone` | Each clip starts where the body (`part_00`) ended the one before: the actor moves so the new clip's first frame lands on the last one, in the same frame (no snap, no motion-blur trail). Only along the hero's facing, as the game moves fighters: a clip that ends to one side (Hurricane Kick) does not drag the actor sideways. Clips that already chain (starter → Art) do not move it; after Slash Kick the Idle stays ahead, and the next Play starts from there |
| **Reset Position** / `bReturnHomeAfterArt` | Back to where the first Play started — by hand, or after every Art |
| `PlayRate`, `PauseBetweenSteps`, `PauseBetweenArts`, `bLoop`, `bPlayOnBeginPlay` | Overall speed (1 = the game's), pauses, repeat, start on Play |
| `Steps`, `NowPlaying` | Read-only: the planned steps and the one playing (also above the model's head) |

Buttons follow the hero's hand: Noa is left-handed, so her Arms is the right swing
(`LeftHandedHeroes` under Setup). The sounds and voices are the clips' own notifies.

**Chain Cut** (RE: `FUN_80047430`, `monster-animation.md` › Event-frame list): with another
action queued, the game commits the next clip as soon as the frame passes the clip's first beat
(`+0x10`) + 2 — unless the clip's lock (`+0x76`) is set. The party's swings have beat 7 (Noa 5–7)
and no lock, so they chain at frame 10; every Art has the lock and plays whole. The import puts a
`UJRPGAnimNotify_ChainCut` there (track **Chain**); the battle will use the same marker.

**The game's speed in a turn** (RE: `FUN_8004AD80`, `ctx[+0x243]`; `battle-action-queue.md`):
swings and the `ArtStarter` run at scale **8** — also the connector swings inside a Super; every
Art commit sets **4**; the `SpecialStarter` turns special mode on, and every Art after it runs at
**2** until the action ends. So a Miracle's or a Super finisher's clips run at half an ordinary
Art's speed. Each step shows its scale (`x8` / `x4` / `x2`) next to its name.

**Root motion in the original** (not reproduced yet): each clip entry carries a root speed
(`+0x0C`, applied every tick while it plays) and an end step (`+0x0E`, the distance the actor
moves along its facing at the clip's end, with the last frame blended into the next clip's first).
Slash Kick: speed −7, step 534 units. The tester lines the clips up by the body instead.

---

## 4. Regenerating

```bash
python Extras/generate_arts_csv.py --extracted <LegaiaRE/extracted> --tools <RE tools folder>
```

| Source | What |
|---|---|
| `Docs/Data/gamedata/arts.toml` (legend-of-legaia-re) | The 60 Arts: hero, name, kind, original AP, buttons, Action Constant |
| The disc — the Arts bank of each player file (PROT 0863..0865, `record[0] +0x58`, `0xD0` records indexed by `ActionConstant - 0x10`) | Each hit's power: the bytes at `+0x24` (RE: `docs/formats/art-data.md`). `0x16..0x1A` = UDF ×12/18/20/22/28, `0x1B..0x1F` = LDF, `0x0C..0x15` = the alternative range |
| `SUPERS` / `MIRACLES` in the script | The Super and Miracle sequences (RE capture, see above) |

Without `--extracted` / `--tools` the hits of new rows stay empty.

**Regenerating never loses manual work:**

- rows it does not create (new Arts) are kept as they are;
- on its own rows, any balance already edited (`APGain`, `APCost`, `CommandCost`, `Anim`,
  `HitPower`, `HitTarget`, `Sequence`) is kept — `--reset` rebuilds them from the original;
- the previous CSV is backed up to `Docs/Data/_backup/` first.

The table today: **81 rows** — 33 Regular, 9 Hyper, 15 Super, 3 Miracle and 21 Parts (the
15 Super finishers and the Miracle / Hurricane Kick / Jurassic Blow segments). Every `Anim`
points at a clip already in the project
([PS1_ASSETS.md › The party's clips and sounds](PS1_ASSETS.md#the-partys-clips-and-sounds)).

---

## 5. Not in the table yet

- **Root motion** (`+0x0C` / `+0x0E` above).
- **When each hit lands.** The disc's hit timing lives in the clip's effect script, which is
  not pinned per hit; Phase 6 places the damage on the clip as a notify.
- **The voice and fanfare an Art picks when it runs** (the CD-XA layer).
- **Status the Art inflicts** (Tough Love's, for instance) — comes with Phase 8's conditions.
