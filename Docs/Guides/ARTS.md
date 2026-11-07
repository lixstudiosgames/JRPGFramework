# Arts — `DT_Arts`

Every Tactical Art the heroes know, one row each, plus the pieces the long ones are made of.
The table is data; recognising the buttons, learning, the Supers and the AP are
[§ 4](#4-recognition-learning-and-ap) (Phase 6 of [BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md)), and
dealing the hits is Phase 5.

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
| `Command` | The buttons, in order: `Arms`, `RaSeru`, `High`, `Low`. The player's scheme — Noa's mirroring (her Arms is the right button) is resolved at input time, not stored. Every Art, Super and Miracle is recognised from this column alone |
| `OriginalAP` | The original's AP cost, kept as a balance reference |
| `APGain` | Regular: AP the Art **adds** (seeded with `OriginalAP / 2`) |
| `APCost` | Hyper / Super / Miracle: AP the Art **spends** (seeded with `OriginalAP`) |
| `CommandCost` | Weight against the turn's command gauge (seeded with the button count) |
| `Anim` | The clip: `/Game/Legaia/Battle/Party/<Hero>/AS_<Hero>_Battle_<Art>` |
| `HitPower` | One multiplier per hit, in order. The disc uses 12, 18, 20, 22, 28 |
| `HitTarget` | One per hit, same order: `Upper` (vs UDF), `Lower` (vs LDF), `UpperMissesShort`, `LowerMissesFloating` |
| `Sequence` | A combined Art: what plays, in order (below). Empty = `ArtStarter` + this row's `Anim` (a Regular) |
| `ActionConstant` | The Art's record on the disc (`0x1B..0x32`, stored in decimal). `0` = a new Art with no record. Also the recognition order ([§ 4](#4-recognition-learning-and-ap)) |
| `Unlock` | How the Art is learned: `ByKind` (the default — the original's rule for the `Kind`), `OnUse`, `Taught`, `ChainKnown`, `RaSeru` ([§ 4](#how-each-kind-is-learned)) |
| `SpeedScale` | The game's speed scale when this Art is a step of a turn (`8` / `4` / `2`). `0` = the original's rule (below) |

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

The Super's **own Arts** — what "every Art known before the turn" checks, and what adds no AP while
it fires — are what its `Command` tokenizes to, **not** its `Sequence` ([§ 4](#4-recognition-learning-and-ap)).
Power Punch with two more Power Punches at the end, as a Super: `Command`
`(Arms,Arms,Low,Arms,Arms,Low,Arms,Arms,Low)`, `APCost` `60`, `Sequence`
`(ArtStarter,vahn_power_punch,SwingD,ArtStarter,vahn_power_punch,SwingD,SpecialStarter,vahn_power_punch)`
— three Power Punches are its Arts, it fires once Power Punch is known, and it plays in the tester
(`Input`) without a line of code. A Super whose `Command` holds no Art would fire knowing nothing:
give it `Unlock` = `Taught` (the validator warns). To make a step stronger than the Art it copies,
add a `Part` with the same `Anim` and new hits; to make it faster or slower, `SpeedScale` on the
step's row.

**A new clip** — any `UAnimSequence` on the hero's battle skeleton (`SK_<Hero>_Battle`)
works in `Anim`. Clips made in Blender or Unreal come in like the disc ones; the skeleton is a
`root` plus one `part_NN` bone per rigid piece, so the animation is keyed per piece
([PS1_ASSETS.md](PS1_ASSETS.md)).

Hits of a new Art land where its clip says — a **`UJRPGAnimNotify_Hit`** ("Art Hit") on the clip
marks each hit's frame, one per hit in `HitPower` order (track **Hits**), not the table. The damage is not computed by the notify: the fight reads those frames from
the clip as data and resolves each hit when its own timeline reaches that frame; the notify only
shows it ([BATTLE_ROADMAP.md › Phase 5 › State machine](../BATTLE_ROADMAP.md#state-machine)).

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
| `Input` + **Play Input** | The buttons as the player types them, and **the rules** decide the turn — the same path the fight takes: the gauge, the commit (the chain goes through the bytes), recognition, the Super, the AP. `Arms, Ra-Seru, Arms`, `A R A`, or the pad arrows `← → ←` / `Left Right Left` (by the hero's hand: for Noa `→` is Arms). **`Recognition`** shows, while you type, every Art, the Super and what is inside it, the AP at each step and what fell (`needs 60 AP, had 50`, `locked`) |
| `InputAP` / `bCarryAP` | The AP at the start of the turn; with `bCarryAP` it becomes the turn's end AP after each Play Input, so the next one goes on from there |
| `KnownArts` / `bKnowAllArts` / `bLearnOnPlay` | The tester's own learned list (not the save). Play Input adds the Regulars learned on use, so typing Tri-Somersault with Cyclone unknown learns Cyclone, and the same input again fires the Super. `bKnowAllArts` treats every Art as known |
| `bHasRaSeru` / `InputGauge` | No Ra-Seru = no Hyper and no Miracle (the original's rule). `InputGauge` is the turn's gauge, as the fight seeds it from AGL (30 per button); `0` = no limit |
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
Art commit sets **4**; the `SpecialStarter` is the dash at **2** (its `0x1A` arm freezes everyone else at 0) and turns special
mode on, and every Art after it runs at
**2** until the action ends. So a Miracle's or a Super finisher's clips run at half an ordinary
Art's speed. A step whose row has `SpeedScale` other than `0` runs at that scale instead. Each
step shows its scale (`x8` / `x4` / `x2`) next to its name.

**Root motion in the original** (not reproduced yet): each clip entry carries a root speed
(`+0x0C`, applied every tick while it plays) and an end step (`+0x0E`, the distance the actor
moves along its facing at the clip's end, with the last frame blended into the next clip's first).
Slash Kick: speed −7, step 534 units. The tester lines the clips up by the body instead.

---

## 4. Recognition, learning and AP

The rules are pure functions in **`UJRPGArtsRules`** (`Public/Arts/JRPGArtsRules.h`): buttons in,
the turn decided out (`FJRPGArtChain`). They read only `DT_Arts` and a snapshot of the hero
(`FJRPGArtsContext`: the Arts known when the turn started, the AP, the Ra-Seru) — no subsystem, no
float, no global RNG. The fight (Phase 5) calls `BuildChain` with its own `Rng`; out of the fight,
**`UArtsSubsystem`** takes the snapshot from the roster and the Art Tester from its own fields.

### The buttons

`ButtonForPad(Character, Pad)`: Vahn and Gala have Arms on the left, Noa (left-handed,
`DT_Characters.bLeftHanded`) on the right; up is High, down is Low. The rows store the player's
scheme, so the same row works whatever the hand.

**The input window** is local: `OpenInputWindow(Character, Gauge)` → `PushInputButton` /
`PopInputButton` → `EncodeArtBytes`. Each button costs its own price from the turn's gauge (the
original: pool = AGL, 30 a button — 42 / 54 for the weapon hand off its class, still to come; RE
`arts-command-gauge.md`), at most 16 buttons. The chain leaves the window as **bytes** (one per
button, the `FJRPGBattleCommand::ArtBytes` of Phase 5), and the rules recognise it again from them
(`DecodeArtBytes` refuses anything that is not a button).

### Recognition — the original's tokenizer

Not a longest match that consumes the input: the original's walk (`FUN_801EED1C`; RE
`crates/art/src/tokenize.rs`) goes from the **end** of the input to the start, and for each start
tries every Art of the hero in the disc's grid order. A matched Art turns only its **last** button
into the starter; the buttons before it stay (they play as plain swings, and can still be part of
another Art). So Arts overlap:

| Input | Arts |
|---|---|
| `↑↓↑` (Somersault) | `↑`, `↓` as swings, then Somersault |
| `↑↓↑↑↑↓↑` | Somersault (1–3), Cyclone (2–5), Somersault (5–7) — Tri-Somersault's chain |
| `↑↓↑ ↓↑↑↑ ↑↓↑` (the three typed apart) | four Arts, not the Tri-Somersault chain |

- **Order.** Disc Arts by `ActionConstant` (the grid: the longer Arts have the lower numbers), then
  new Arts (`ActionConstant` 0), the longer first, then the table order.
- **A Hyper takes its whole span** (the original's special arm writes the `0x1A` at its start): no
  other Art is found inside it afterwards, and it plays with no swings before it.
- **Super and Miracle** are matched on their `Command` and take precedence over the Arts inside
  their span. The Arts inside are the Super's (they move no AP while it fires); the buttons before
  its first Art play as swings (the original's "find" starts at that Art's starter); a Miracle
  replaces its whole span. Two specials that share buttons are both checked, in play order: only
  one that **fires** takes the buttons from the other — one that falls for AP becomes its Arts, and
  the next is still checked (two overlapping Tri-Somersaults at 50 AP: the first falls, its Arts
  bring the AP to 71, the second fires).
- A button that ends no Art is a plain swing.

### How each kind is learned

| `Unlock` | Rule | `ByKind` gives it to |
|---|---|---|
| `OnUse` | An unknown Art whose buttons the player enters is learned on that use — and plays as that Art, with the "learned" notice once (on its first appearance in the turn) | Regular |
| `Taught` | Only `LearnArt` (a book, an NPC, a trigger, a cutscene). Entering its buttons without knowing it does nothing special — swings, or the Regulars inside it. A `ByKind` Hyper also needs the Ra-Seru (the original's marker gates the special arm) | Hyper |
| `ChainKnown` | Fires when **every Art its `Command` tokenizes to was known before the turn**. If the input teaches one of them, that one is learned and plays, and the Super completes on the next turn | Super |
| `RaSeru` | Fires when the hero has a Ra-Seru (`DT_Characters.RaSeru`) | Miracle |

`LearnOnUseChance` (on `UArtsSubsystem` and in the context, default 100%) is the original's learn
gate, which reads open: below 100 it rolls once per Art from the `Rng` passed in.

**The learned list** is `FJRPGPartyMember::LearnedArts` (`DT_Arts` keys, in the save since
`SaveVersion` 6). Only `UArtsSubsystem` writes it: `LearnArt(Character, ArtKey)` returns true only
when the Art is new (the caller shows the notice once), refuses another hero's Art or a `Part`, and
works out of battle. `HasLearnedArt` is the check for dialogue and triggers. The Art Books call
`TeachArtToCharacter` → `LearnArt`, so `DT_Items.TeachesArt` holds a `DT_Arts` key
(`vahn_tornado_flame_hyper`).

### The new AP

Regular Arts **add** `APGain`, Hyper / Super / Miracle **spend** `APCost`. At commit the chain is
walked **left to right** with a running AP that starts at the hero's:

- a loose Regular adds its `APGain` (capped at `MaxAP`, 100), wherever it is;
- each special is checked **on its own**, against the AP at **that point** of the chain — what the
  Arts before it added, minus what the specials before it spent. One that fits is kept and
  subtracts its `APCost`; one that does not is **downgraded to its buttons** (a Super to its Arts,
  which then add AP like any loose Art; a Hyper to swings / the Regulars inside it) and the walk
  goes on. An Art after a special never pays for it;
- the Arts inside a fired Super add nothing.

| Hero's AP | Input | Result |
|---|---|---|
| 50 | `↑↓↑↑↑↓↑` (Tri-Somersault, 60) | 50 < 60: downgraded; Somersault, Cyclone, Somersault add 9 + 12 + 9 (80) |
| 50 | Hurricane (+12), then `↑↓↑↑↑↓↑` | 62 ≥ 60 when the Super comes: it fires; AP ends at 2 |
| 50 | `↑↓↑↑↑↓↑`, then Cross-Kick (+12) | downgraded; its three Arts add, then Cross-Kick adds 12 (92) |
| 15 | Tornado Flame (30), then a +15 Art | 15 < 30: Tornado Flame becomes three swings; the Art after it does not pay for it (30) |

Nothing is debited or credited at commit: each entry carries `APBefore` / `APAfter`, and the
meter moves when the action reaches each Art (Phase 5). `GetSpecialsAvailability` answers, for the
menu, which specials would fire if typed next (unlocked, and paid by the AP at the end of what is
already typed) — the greyed-out ones.

### Out of the fight — `UArtsSubsystem`

| | |
|---|---|
| `PreviewArtsChain` / `RecognizeArtSequence` | The turn the buttons would give now; nothing is saved |
| `CommitArtsChain(Character, ArtBytes, bValid)` | Recognises from the bytes, learns the new Regulars, returns the turn |
| `GetKnownArts` / `HasLearnedArt` / `LearnArt` / `ForgetArt` | The list |
| `OnArtLearned(Character, ArtKey)` | Fired once per new Art |

Console (PIE): `JRPG.Arts.Try Vahn Arms Ra-Seru Arms` (or `Vahn ← → ←`) prints the turn,
`JRPG.Arts.Commit` does it for real, `JRPG.Arts.Learn Vahn vahn_fire_blow`, `JRPG.Arts.Known Vahn`.

### The validator

`UJRPGArtsValidator` holds what a row must satisfy for an Art to be data only. Errors: two Arts of
a hero with the same `Command`; a `Command` longer than 16 buttons; a Regular / Hyper that is never recognised (typing its `Command`
gives something else); a `Sequence` step that does not exist, belongs to another hero or loops back
to the row; a `Part` with a `Command`, or an Art without one; a special without `APCost`, or a
Regular with one; `HitPower` / `HitTarget` of different lengths; an `Anim` that does not load or is
not on the hero's battle skeleton; a disc Super whose `Command` does not tokenize to the original's
chain. Warnings: a clip whose hit notifies do not match `HitPower`; a `ChainKnown` Super whose
`Command` holds no Art; an `Unlock` that means nothing for the `Kind`.

It runs in three places: the **automation tests** (`JRPG.Arts.*`, Session Frontend › Automation —
the validator, recognition, the four AP examples, the disc Supers, a new Super as one row, learning
and the save, determinism); **Data Validation** on the asset (right click `DT_Arts` › Validate);
and **`Extras/arts_rules.py`** — the same rules in Python without the asset checks — which
`generate_arts_csv.py` runs after generating and LegaiaStudio shows in its findings on every save.
`python Extras/arts_rules.py Vahn High Low High` prints what the buttons recognise.

> **The disc corrects the TOML once.** Vahn's Hyper Elbow is `← → ←` (Arms, Ra-Seru, **Arms**) in
> the game's own arts-name table, not the walkthrough's Arms, Ra-Seru, High (RE
> `docs/formats/art-data.md` › "Command-glyph string"). With the walkthrough's buttons Fire Tackle's
> `← → ← ← ↓ → ↑` does not tokenize to Hyper Elbow, Power Punch, Charging Scorch — the validator
> caught it. `generate_arts_csv.py` applies the fix (`COMMAND_FIXES`).

---

## 5. Regenerating

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
  `HitPower`, `HitTarget`, `Sequence`, `Unlock`, `SpeedScale`) is kept — `--reset` rebuilds them
  from the original;
- the previous CSV is backed up to `Docs/Data/_backup/` first;
- at the end it runs the validator (`Extras/arts_rules.py`) and lists what it finds. Without
  `--extracted` / `--tools` it cannot rebuild the Arts the disc splits into several records
  (Hurricane Kick's second segment would go): the validator says so — regenerate with the disc.

`CommandCost` is not what the gauge charges (that is per button, above); it stays as a balance
reference.

The table today: **81 rows** — 33 Regular, 9 Hyper, 15 Super, 3 Miracle and 21 Parts (the
15 Super finishers and the Miracle / Hurricane Kick / Jurassic Blow segments). Every `Anim`
points at a clip already in the project
([PS1_ASSETS.md › The party's clips and sounds](PS1_ASSETS.md#the-partys-clips-and-sounds)).

---

## 6. Not in the table yet

- **Root motion** (`+0x0C` / `+0x0E` above).
- **When each hit lands.** The disc's hit timing lives in the clip's effect script, which is
  not pinned per hit. The marker exists (`UJRPGAnimNotify_Hit`, one per hit) and the validator
  checks its count against `HitPower`, but the clips do not carry it yet: placing it on the party's
  Art clips is the content step Phase 5 needs. The fight's logic timeline reads those frames as
  data and resolves each hit at its frame; playback uses the notify to show it.
- **The weapon hand's gauge cost** (42 / 54 off its class) — the window takes per-button costs, the
  fight fills them from the weapon in Phase 5.
- **The voice and fanfare an Art picks when it runs** (the CD-XA layer).
- **Status the Art inflicts** (Tough Love's, for instance) — comes with Phase 8's conditions.
