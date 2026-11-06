# Battle RE sweep: every part of the original's fight, and what the plugin has

Backlog item 32 of [`BATTLE_PHASE5_BUGS.md`](../BATTLE_PHASE5_BUGS.md). It goes through the original's battle piece by piece: every
state, every hold, every animation rule, the movement, the camera, the effects and the sounds. For each one it records the RE
source, what the plugin does today and what is missing. It is **documentation only**; the implementation comes after, in the
order of the last section.

The timing (how long each part takes, what an action waits for) is already in [`BATTLE_TIMING.md`](BATTLE_TIMING.md) and is not
repeated here; this file links to its sections.

Everything comes from the [`legend-of-legaia-re`][re] clone (`docs/subsystems/battle*.md`, `effect-vm.md`, `audio.md`,
`battle-hud.md`, `docs/formats/*`, and the Rust ports under `crates/`). Only numbers and descriptions are written here; no disc
data is copied (rule 48). Confidence tags are the same as in `BATTLE_TIMING.md`: **CONFIRMED** (disassembly, capture or disc),
**INFERRED**, **NOT FOUND**.

[re]: https://github.com/andrewaltimit/legend-of-legaia-re

**Status of this file:** complete (2026-11-05), written in seven parts. Nothing here is implemented yet; section 10.3 is the
order to follow.

### Plugin decisions (user, 2026-11-05)

The RE is the reference for **behaviour**; the remake does not copy everything as is.

| Topic | Decision |
|---|---|
| Magic, summons, Seru capture | out of scope for now |
| `Auto` / `Command` prompt | not ported: the Tactics screen replaces it (3.2) |
| **HUD** | the original's HUD (10.1) is an **example only**; the plugin's HUD is our own, better design |
| **Enemy HP bar** (ours, not in the original) | a small bar **over each enemy's head**: a thin, polished frame with the fill only, **no numbers** |
| **After-image ghosts** | not used. In their place: the user's own **aura effect** (a glow around the character, Solo Leveling style), improved; same moments as the ghosts (8.1) |
| **Counter-attack** | not part of the ordinary fight: in the original it exists only with the **Warrior Icon** accessory (passive `0x0F` Counterattack, `formats/accessory-passive-table.md`). Deferred to the accessory passives |
| Implementation order | movement -> Spirit -> cameras -> remaining sounds -> battle start -> battle end (10.3) |

---

## Contents

1. [Open bugs 29-31, analysed against the RE](#1-open-bugs-29-31-analysed-against-the-re)
2. [Entry: encounter to the first prompt](#2-entry-encounter-to-the-first-prompt)
3. [Command flow](#3-command-flow-ctx0x06-fun_801d0748)
4. [Action state machine](#4-action-state-machine-ctx0x07-fun_801e295c) (attack, Spirit, done band, run, battle end; magic,
   summon and capture out of scope for now)
5. [Movement and spacing](#5-movement-and-spacing)
6. [Actor animation](#6-actor-animation)
7. [Camera](#7-camera)
8. [Effects](#8-effects)
9. [Sounds and music](#9-sounds-and-music)
10. [HUD, end of battle, and the implementation order](#10-hud-end-of-battle-and-the-implementation-order)

---

## 1. Open bugs 29-31, analysed against the RE

None of the three was reproduced here (no editor run); this is a reading of our code against the RE. The fixes are the movement
model of section 5.

### Bug 29: some hits do not land

Our rules are geometry-free: a hit is a logic-frame event planned before the action starts, and it lands (or misses / is blocked)
whatever the bodies look like. In the original a hit is fired **by the clip**: the melee kernel `FUN_801EC3E4` runs when the
playing clip's cursor passes the entry's event beat (`BATTLE_TIMING.md` section 2). So the two can only drift apart in our port,
and "a swing that connects with nothing happening" is one of these, in order of likelihood:

1. **The event is a Miss or a Block and only the text shows it.** A `Lower` hit on a floater, or a `High` (Upper) one on a short
   monster, is a height-class miss (`HitTarget` in `DT_Arts`: e.g. Spin Combo opens with `Lower`, Gala's Side Kick with
   `LowerMissesFloating`); a Miss plays no reaction and, since bug 20, no impact sound. A Block plays the block clip, which on
   some monsters looks almost like idle. Check the on-screen event text / Output Log first.
2. **The clip and the rules disagree on when the impact is.** For an Art without hit markers the rules take the **hit frames**
   from `DT_ArtHits` (`ClipFramesToSteps(Beat - 1, Speed, Rate)`, the disc rate `+0x9C`, 1..7 on the Art records) but the
   **clip length** from the Unreal asset (`SecondsToSteps(PlayLength)`, `BattleSubsystem.cpp` `ClipTiming`), and the pawn plays the
   asset at `GameSpeed / (4 * RateScale)` (`JRPGBattlePawn.cpp` `PlayStep`). If an asset's duration at that rate is not the disc's
   `4 * N / R`, the visual impact and the logic hit fall at different times: the swing connects with no number, and the number
   shows a few frames before or after. To check per Art: asset play length at its rate vs `4 * frames / rate` from the disc
   record (`+0xAC` part / frame count); log both in `ClipTiming` when they differ by more than 2 frames.
3. **The kill in mid-combo.** After the target dies the rest of the Art plays on the body and hits nothing (bug 16, as in the
   original). Not a bug.
4. **Our hits never fall after the next swing's cut** (`End = max(Cut, last hit + 1)`), while the original **loses** a hit that
   would fall after keyframe `B + 3` when the next swing commits (`BATTLE_TIMING.md` section 2). That makes ours land *more*, not
   less; listed only so nobody "fixes" it the wrong way.

What to note on a repro: hero, Art or swing, enemy, and the event line (`Damage` / `Miss` / `Block`) printed at that moment
(`JRPG.Battle.PrintEvents`, on by default).

### Bug 30: Slash Kick still slides at the end

Cause: our **end step** is a heuristic. `AJRPGBattlePawn::SettleIntoIdle` takes the pose centroid of the clip's last frame
minus the idle's, turns it into actor position during the idle crossfade, and for the attacker **clamps** it at the edge of the
target's reach (`KeepReachOf`). Slash Kick's lunge carries the body past that edge; the part that is clamped away is what blends
back, and that is the slide.

The original does none of this (section 5): the end of a clip adds the entry's own end-step value `+0x0E` (a fixed number per
clip, from the disc), and the body is never clamped there. What keeps an attacker out of its target is the **in-range back-off**
during the strike loop and the **two-way reach snap** at every commit, both measured against the target's **body pair**. With
those, the lunge can end inside the reach for a moment and the back-off walks it out at speed 4 while the next swing or the done
band runs; nothing blends back.

### Bug 31: Gimard ended up inside Vahn

Three differences with the original, any of which can put two bodies on top of each other (section 5 has the details):

1. **We measure feet, the original measures bodies.** Range law and separation read each actor's body pair (live position + the
   rotated pose centroid, rewritten every drawn frame). Ours read `GetActorLocation()` / `GetSettledLocation()`. A Gimard
   knockdown / get-up that carries its body forward, or a lunge, overlaps visually while the actor origins are still apart, and
   nothing pushes.
2. **The reach snap only goes forward.** `FollowTarget` / `ComputeStopPoint` move the attacker only when it is *out* of reach; the
   original also steps it **back** when it is in range (`0x8004BF7C..0x8004C118`), and in the strike loop it keeps backing off
   every tick at speed 4 (`0x80047C3C..0x80047D1C`). Ours has nothing that moves an attacker out once it is too close.
3. **The get-up end step is unclamped and invented.** A reaction's `SettleIntoIdle(nullptr)` turns the get-up's last-frame
   centroid into position with no clamp. The original's end step is the entry's `+0x0E` (about -1 on reactions, i.e. nothing);
   a big body like Gimard ends its get-up wherever its pose centroid points, and that can be toward the attacker.

The arrival shove (`ArrivalShove`; in the original only on the walk chain of six monsters, section 4.2) and the separation
threshold `(r1 + r2) / 6` (RE, but on body pairs) do not undo 1-3 in time.

---

## 2. Entry: encounter to the first prompt

Scope note: magic, summons and the Seru capture are out of scope for now (user decision, 2026-11-05); they are only named where
a state leads to them.

### 2.1 Sequence

| # | Step | What happens | Time | Source | Tag |
|---|---|---|---|---|---|
| 1 | Encounter | field region roll on each new 128-unit tile, or script op `3E FF <row>`, or world-map contact; the formation row goes to `0x8007BD0C..0F` | - | `battle-round-loop.md` "Encounters" | CONFIRMED |
| 2 | Formation advantage | `FUN_80051D84` writes `ctx[+0x290]`: `1` back attack (a monster-id range test), `2` pre-emptive (a `rand` roll), else `0` | - | `battle-action-helpers.md` escape roll | CONFIRMED |
| 3 | Transition | overlay PROT 0979 captures the last field frame and plays one of **five styles** over it: scatter particles, scatter with spin-up ring, tile shatter, swirl fan, screen-strip curtain | **132** display frames (2.2 s), **252** for the swirl (4.2 s) | `battle-stage-camera.md` "Field-to-battle intro presentation" | CONFIRMED |
| 4 | Load | scene loader `FUN_800520F0`, actor setup, stage seats `FUN_800513F0` (also latches the scripted-fight flag `ctx[+0x287]`) | CD-bound | `battle.md` | CONFIRMED |
| 5 | Entry sweep | frame driver counter `0x80 -> 0xA1`: camera rises (`TR y += 0x30`) and pulls in (`TR z -= 0x40`) per frame step; `0xA2..0xC0`: framing case 2 | about 64 vsyncs | `battle-stage-camera.md` "The battle-entry sweep" | CONFIRMED |
| 6 | Flow `0x00` | init `FUN_801D84C0` (also stores the victory / defeat window strings) | one pass | `battle-command-flow.md` | CONFIRMED |
| 7 | Flow `0x0A` | party plates + the **enemy-name labels** (one per run of equal monster ids: `Killer Bee * 3`) + the formation banner line (back attack / pre-emptive, with the leader's name); intro timer `ctx[+0x6D6]` | **90** vsyncs, **120** with an advantage | `battle-hud.md` "The battle-intro enemy-name banner", `battle-command-flow.md` "The formation banner" | CONFIRMED |
| 8 | Flow `0x0B` | counts the timer down; back attack -> `0xFE` (the party enters **no** command in round 1), else `0x14` | - | same | CONFIRMED |
| 9 | Flow `0x14` | round start: per-round actor sweep `FUN_801D88CC` (re-picks a dead stored target, resets the step counter), highlight on the Left arm, `ctx[+0x06] = 0x1E`; under the Muscle Dome draws `4 - round` | one pass | `battle-command-flow.md` "The round loop" | CONFIRMED |

The battle tick does not run until the entry sweep ends, so no prompt can open under it (the RE port opens it early and calls that
a divergence).

### 2.2 Ours

| Step | Plugin | Status |
|---|---|---|
| Encounter, advantage | `FJRPGBattleSetup::FormationAdvantage` (0/1/2), passed to the resolver; field enemies start fights (`FIELD_ENEMIES.md`) | **have** (the roll that picks 1/2 is the field's) |
| Transition | `UJRPGBattleSceneSubsystem` fade (`Fade`, `TransitionTime` timer) | **missing**: the five styles. A post-process / UMG capture of the last field frame is the natural port |
| Entry sweep | arena camera blend (`JRPGBattleArena.cpp`) | **partial**: a blend, not the rise-and-pull-in path (section 7) |
| Name labels + hold | `HoldLogic(true)` while the scene assembles | **missing**: the enemy-name labels, the formation banner and the 90 / 120 hold as such |
| Back attack loses round 1 | resolver: `FormationAdvantage == 1` handled in `JRPGBattleResolver.cpp` (initiative) | **check**: the original skips the party's whole command phase in round 1, not just its keys |

---

## 3. Command flow (`ctx[+0x06]`, `FUN_801D0748`)

### 3.1 States

| `ctx[+0x06]` | On screen | Pad | Leaves to |
|---|---|---|---|
| `0x1E` | `Begin` / `Run` round prompt (**every** round opens here) | Left / confirm = Begin, Right = Run | `0x28`, `0x32`, `0x6E` (nobody can act) |
| `0x28` | the four-arm ring for the current member | Up Item, Left Attack, Right Ra-Seru magic, Down Spirit; cancel = previous member (first member: back to `0x1E`; an item the landed member committed is refunded) | `0x3C`, `0x78`/`0x5A`/`0x50`, `0x46`, `0x6E` |
| `0x32` | Run confirm | confirm / cancel | `0xFE` (the run becomes an action, 4.5), `0x1E` |
| `0x78` | `Auto` / `Command` attack-mode prompt (skipped by the Battle Command option: `1` straight to auto-target, `2` straight to entry) | Left Auto, Right Command, cancel to `0x28` | `0x5A`, `0x50` |
| `0x50` | arts command entry (the input window) | directions = buttons | `0x5A`, `0x78`, `0x28` |
| `0x5A` | target cursor | Left / Right step the **angular** ring of live monsters (`FUN_801D8A88` / `FUN_801D8D00`), opening on the member's last target | `0x28` next member, `0x6E` last member |
| `0x3C`, `0x46`, `0x5B..0x5E`, `0x64..0x67` | item / magic windows and their target sub-cursors | - | out of scope (magic) except the item window |
| `0x6E` | commit confirm: `Begin` / `Reselect` chips around the D-pad glyph, plus the **commit log** (one row per member: name, command, target) | Left / confirm = Begin, Right / cancel = Reselect (steps back **one** member, the last) | `0xFE`, `0x28` |
| `0xFE` | round armed: `ctx[+0x06] = 0xFF`, `ctx[+0x07] = 0` | - | action SM |

Every press sounds: `0x21` commit, `0x22` cursor step, `0x23` cancel / refused (section 9). Source: `battle-command-flow.md`
"Flow states", "Command surfaces", "The commit-confirm screen", "Battle target picker". **CONFIRMED.**

### 3.2 Ours

`JRPGBattleMenu` (screens `Start`, `Escape`, `Tactics`, `Command`, `Attack`, `Target`, `Final`) is the same chain: Start = `0x1E`,
Escape = `0x32`, Command = `0x28`, Attack = `0x50`, Target = `0x5A`, Final = `0x6E`. Differences:

| Topic | Original | Ours |
|---|---|---|
| `Auto` / `Command` prompt `0x78` and the Battle Command option | exists | **not ported, by decision** (2026-11-05): our Tactics screen (Up arm of Start) replaces it as the faster way to automate; Attack always opens the command entry |
| Reselect | one member back | drops every choice, back to the first hero (13.2 of `BATTLE.md`) |
| Target order | angular ring by seat, opens on the last target | slot order with A / B names; check what it opens on |
| Commit log | rows visible during the whole command phase | only on the Final screen |
| Menu sounds | `0x21` / `0x22` / `0x23` | check `96_battle.js` / the menu (section 9) |
| Escape | a **roll** at the party's turn in execution (4.5); a failed run spends the action and the round goes on | immediate `EndBattle(Escaped)`, always works (`BATTLE.md` 13.5, `TODO(Phase 7)`) |
| Spirit | Down arm; on the last member goes straight to `0x6E` | drawn with an X (Phase 7); the user wants it next |

---

## 4. Action state machine (`ctx[+0x07]`, `FUN_801E295C`)

Runs once per frame step after the command flow. Category `actor[+0x1DE]`: `0` Tactical Arts (nothing to play), `1` Item, `2`
Magic, `3` Attack, `4` Spirit, `5` Run. The SM stages a clip in `+0x1DA` and waits for the commit in `+0x1D9`; per-actor flags
`+0x1DC` (bit 0 windup done, bit 1 a staged swing in flight, bit 2 exit to idle). Timer `ctx[+0x6D8]`, drained by the frame step.
Source: `battle-action.md` state table and "State detail"; durations in `BATTLE_TIMING.md` section 2.

### 4.1 Shared head

| State | What | Ours |
|---|---|---|
| `0x00` begin | resets counters, seeds the turn cursor from the advantage byte, latches `+0x290 -> +0x291` (read by the escape roll) and clears `+0x290`; faces the actor at its target | resolver `StartAction` / `ActionStart` event; pawn `BeginAction` faces |
| `0x0A` / `0x0B` | CD poll; hold while the side-band applier streams | nothing needed |
| `0x0C` seed | builds the queue (`FUN_801EED1C` party, monster AI / random retarget for `0x380` statuses), rolls the camera variant `ctx[+0xD]`, **recentres the formation** (`FUN_801EFE44`), pose 6 (a camera program, not a clip) unless Run; dispatches on the category | resolver plans the timeline; scene `PlanFormationShift`; no camera variant |

### 4.2 Attack band (`0x14..0x20`)

| State | What | Exit | Ours |
|---|---|---|---|
| `0x14` face target | pose 6, face the target's live pair, write every bystander's make-way heading and seed the make-way countdown (`class`), range check `FUN_8004E2F0`; in range -> `0x1E`; party: stage walk (entry 1) -> `0x19`; monster with a tag-`0x20` entry -> `0x15` | `0x15` / `0x19` / `0x1E` | `BeginAction` + `MakeWay` |
| `0x15` windup | monster only: wait for the pre-approach clip (tag `0x20`) to commit, stage the tag-1 walk | `0x16` | **missing** (6 monsters: Caruban x3, Evil Shadow, Shade, Nightmare) |
| `0x16` advance | monster walk chain: hold until in range (the walk is the clip's root motion), stage the tag-`0x21` close-in, then the **arrival shove**: push the **target** along the attacker's facing by `trig >> 9` per step while still in range | `0x17` | our `ArrivalShove` runs for every attacker; the original only on this chain |
| `0x17` / `0x18` | wait for the close-in clip to commit, last match | `0x1E` | missing with `0x15` |
| `0x19` short step | party and walk-less monsters: hold until in range; no movement code, **no timeout** (the walk's root motion moves it); sets `+0x1DC` bit 0, clears the Attack x2 counter `ctx[+0x16]` | `0x1E` | the approach lerp + `ApproachStallFrames` |
| `0x1E` strike loop | stages one queue byte per swing when bit 1 of `+0x1DC` is clear (one swing per clip; the next may commit at keyframe `B + 3`); **counter-attack** swap on the attacker's first frame; re-faces the attacker onto the target's body pair every pass; the War God Icon second pass | `0x1F` on the `0x00` terminator | steps of the logic timeline; `FollowTarget`; no counter-attack |
| `0x1F` recovery | pose 7 (or 8 if the target's clip matched a counter trigger), re-face every pass, wait for the stage latch | `0x20` | phase `Recovery` |
| `0x20` return | hold for the attacker's last clip to end, then the target's reaction (`BATTLE_TIMING.md` section 3) | `0x50` | resolver `ReactionHoldUntilFrame` + pawn idle hand-off |

**Counter-attack** (`0x801E35F0..0x801E36E0`): armed by the turn picker after a monster's physical pick on a party seat, on a
`rand & 1` coin, if the hero has the Counterattack passive (record `+0xF4` bit `0x8000`), its key is unspent and the monster has no
`0x380` status. At the monster's first strike-loop frame, if the hero chose Attack, the banner "Counterattack successful" holds
`0x78` frames, the monster's swing is dropped and the hero acts instead (its key is spent). The passive comes only from the
**Warrior Icon** accessory, so an ordinary fight never counters. Ours: **deferred** to the accessory passives (decision).

### 4.3 Spirit band (`0x46..0x48`)

| State | What |
|---|---|
| seed (`0x0C`) | category 4 goes straight to `0x46` (never through the item pre-arm `0x3C`); raises the AP bar (record `0x0F`, width = gauge - 6) and the AP plate `0x52` |
| `0x46` entry | camera depth `ctx[+0x6D0] = 0x800` (the **Spirit close-up**), pose 6, `+0x1DC = 2`, stages the Spirit clip (`+0x1E7`, the commit wrote `0x10`), extended gauge target `min(gauge * 7 / 5 + 8, 0x120)`, Spirit target `+0x170 + 0x20` (`+0x28` / `+0x23` with passives `0x200` / `0x100`), hold `0x20` |
| `0x47` sustain | when the clip commits clears the stage; holds `0x20` vsyncs; then grows the gauge element and the bar one frame step at a time (bar to target - 6) |
| `0x48` flush | finishes the ramp; leaves when the clip is over, the hold is spent and the plate sits on its target |
| done band | `+0x224` pays into Spirit: **8 per action, `0x20` for a Spirit turn**, plus passives, capped at 100; the round boundary restores a Spirit-charged actor's gauge to the extended value for its next entry |

The aura is spawned by the Spirit clip's effect script (section 8). Spirit's effect on the fight (the target's defence x3, the defender's
block side x3/2) is in `battle-formulas.md` and already in `BattleFormulas` (`bDefenderSpirit`, `SpiritGaugeCap` 288). Ours:
the turn itself is **missing** (`EJRPGBattleCommandKind::Guard` refused): no band, no gauge extension, no Spirit pay. Source: `battle-action.md` `0x46..0x48`, `battle-round-loop.md` "AP / Spirit gauge".
**CONFIRMED.**

(`0x3C..0x40` is the item / low-class magic apply band: pre-arm, wait for the commit and fire the cast cue `FUN_801F3990`, fire,
`0x20` then apply `FUN_800402F4`, `0x80` post-apply bar ramp. Needed for items in Phase 7; magic out of scope.)

### 4.4 Done band and the gate

| State | What | Ours |
|---|---|---|
| `0x50` cleanup | Final Heal revive; re-order the round if anyone lives; `+0x224` Spirit pay (4.3); exit-to-idle flag on the actor; pose: Run -> orbit yaw, Attack -> pose 8, else 6; countdown **60** (`0x96` = 150 with the level-up banner) | `DoneBandFrames` 60; no Spirit pay, no poses (camera programs) |
| `0x51` fade-down | audio duck restored; waits on the **HP-bar settle** of a party target; under 12 left unloads the HUD elements of the action; raises the capture banner | the 60 only |
| `0x52` | Seru-absorb banner hold `0xB4`, a press shortens it to `0x13` | out of scope (capture) |
| `0x5A` gate | clears per-actor flags; a wiped side raises the battle-end signal (`5` party, `0` monsters); else next actor (`0x0A`) or the round boundary (`0xFF`) | `CheckFinish` after the band; `PassGapFrames` 6 |
| `0xFF` round boundary | `ctx[+0x06] = 0x14` (next round's `Begin` / `Run`), round counter + 1, the per-round status waker `FUN_801F45A4` | resolver round start; check the status waker exists |

### 4.5 Run (`0x64..0x66`, party) and monster flee (`0x68..0x6B`)

- `0x64`: the **escape roll** `FUN_801E791C`, countdown 60, run UI `0x43`, screen shake. Success floors every party HP at 1.
  ```text
  party = Σ party  (SPD * 3) >> 1 + (maxHP - HP) >> 4      (downed members count)
  enemy = Σ enemy   SPD          + (maxHP - HP) >> 5
  p = rand % party; e = rand % enemy; Escape Boost: p += p >> 1
  Great Escape, or a pre-emptive round (latch == 2), or flag 0x100: p = e
  fail iff not flag 0x100 and (p < e or scripted fight)
  ```
- `0x65`: failed -> camera **orbits** (`2 * step` yaw a pass), a press ends the wait; escaped -> the camera eye **backs off**
  `32 * step` a pass. Failed -> `0x50` (the action is spent, the round goes on); escaped -> `0x66`.
- `0x66`: fade to black (kind 2, `0x40` frames), battle-end signal. The sequencer's escape arm shows no results.
- `0x68..0x6B` (category 5 on a monster): a captured / fleeing monster's takedown, three timers, the actor hidden; out of scope.

Ours: `Escape` ends the fight at once from the menu. **Gap:** make Run an action played at the party's turn (the roll, the orbit or
back-off, the failed case), not a menu shortcut.

### 4.6 Battle end (results sequencer `FUN_8004E568`)

The action SM stops on the signal; the sequencer runs every frame until its phase word reaches `0x43`. Timeline in
`BATTLE_TIMING.md` section 5 (victory 402 vsyncs measured, defeat ~322 inferred, escape the fade only).

| Item | Original | Ours |
|---|---|---|
| Win pose | the **leader** (`ctx[+0x13]`, always seat 0) poses; clip from the SCUS table `0x800788A0` by HP-quarter tier, round count, weak-status mask; one of the eight base-archive records `0x11..0x18`, **held** through the 256 hold (loop window `+0x85..+0x86`) | **missing** (`VictoryHoldFrames` 256 holds on idle) |
| Hero voice | the leader's victory clip from `monster.snd` (slot 7) | missing (section 9) |
| Victory camera | load window: stand-off framing case 8 on the leader; from the results frame: case 6 battle-over arm, the per-character win-pose camera script advancing `8` a frame | missing (section 7) |
| Result window | element `0x41`: `288 x 42` framed window sliding from `(16, 236)` to `(16, 160)`, "<leader> / team ... victory" text; level-up window `0x44 + mask` + cue `0x50` | WebUI results (check against this) |
| Loss window | element `0x42`, same box, defeat text; HP floored at 1 on the fade frame; unscripted wipe -> title, cursor on CONTINUE, no GAME OVER art | `Defeat` result; check the routing |
| Exit fade | kind 2 (subtractive) to black, `0x40` frames, then exit at phase `0x43` | scene `Fade` |

---

## 5. Movement and spacing

What moves a battle actor in the original, and only that. Source: `battle-action.md` "Where an action leaves its combatants",
"The strike band re-faces the attacker every pass", "The commit turns the defender"; `battle-action-helpers.md` "The make-way
step", the separation pass `FUN_80050BB8`; `BATTLE_TIMING.md` sections 2 and 6.

### 5.1 Two positions per actor: the live pair and the body pair

| Pair | Offsets | What it is | Who reads it |
|---|---|---|---|
| Live pair | `+0x34` / `+0x38` | the actor's position on the ground (our actor location) | root motion, the back-off, the reach snap, the attacker side of the range law |
| Body pair | `+0x3C` / `+0x40` | live pair + the **pose centroid**: the mean of the playing stream's part translations, rotated by the facing `+0x46`, scaled by the render scale `+0x72`; rewritten by the pose decoder `FUN_8004998C` on every drawn frame (`0x8004A3DC..0x8004A5F8`) | the **target side** of the range law `FUN_8004E2F0`, **both sides** of the separation pass, the strike-loop facing, the defender's turn on a hit |

The average is over the stream's own part count (the byte at `entry + 0x88`), not over attached equipment objects. Measured:
mid-battle body pairs sit within ~110 units of the live pair. **CONFIRMED.**

Ours: `AJRPGBattlePawn::PoseCentroid()` already computes the centroid (mean of the component-space bone locations, all bones
past the root) but only for the end step. Everything else uses the actor location. **Gap:** a `GetBodyLocation()` (actor location
+ rotated centroid, flattened) used by the reach law, separation, `FollowTarget`, the strike facing and `ReactToHit`. Caution:
our mean runs over every bone, including equipment piece bones (the RE notes the same mistake in its own port held Vahn's centroid
~9 units short); restrict it to the body bones.

### 5.2 Root motion: the entry's `+0x0C` speed

Every playing clip moves the live pair along the facing by `facing * entry[+0x0C] * step * actor[+0x21D] >> 15` per pass
(`speed` units per vsync at game speed 8; `FUN_80047430` `0x80047D20..0x80047E18`). **CONFIRMED.**

- **Negative** speed (reactions: knockdown, block, some flinches): applied every pass, no range test. The struck body slides back
  over the whole clip.
- **Positive** speed (the walk, some lunges): applied **only while the range law reads out of range**, so a walk or a lunge stops
  at the edge of reach by itself.
- `+0x1DC` bit 3 blocks both.

Ours: the walk is a constant-speed lerp to a precomputed stop point (`BeginAction`, `ApproachFrames`), which is the positive
case for the walk; reactions use a fixed `FlinchKnockback` 45 / `KnockdownKnockback` 120 / `BlockKnockback` 20 cm over 12 frames
(`ReactToHit`). **Gap:** per-clip speed and end step as data (hero reactions in `DT_HeroReactions`, monsters in `DT_EnemyActions`,
both read from the disc entry `+0x0C` / `+0x0E`), applied over the clip's own length; positive speeds gated by the range law. The
table of `BATTLE_TIMING.md` section 6 has the hero values (Gala's knockdown slides ~187 cm, Gimard's ~268 cm).

### 5.3 End step: the entry's `+0x0E`

At the clip's natural end the live pair jumps by `+0x0E` units along the facing; the clip's last keyframes carry the matching
offset in model space, so the body does not visibly move. On reactions it is about -1 (nothing); on a few flinches it is large
(Vahn -159 / -115). **CONFIRMED** (formula), values in `BATTLE_TIMING.md` section 10.

Ours: `SettleIntoIdle` derives the step from the pose centroid and, for the attacker, clamps it to the reach (bug 30). **Gap:**
read `+0x0E` per clip and apply it as a jump hidden by the idle crossfade (the slide over `AnimBlendTime` is fine), with no reach
clamp; the back-off (5.5) does the rest. Before relying on it, check on a real Unreal clip that the last frame's offset matches
`+0x0E` (the GLB exports may or may not keep the model-space recoil).

### 5.4 Reach snap at every commit, both ways

When the acting actor commits **any** clip while the SM is in the strike loop `0x1E`, the commit tail polls the range law against
its target: out of range, it steps the live pair **forward** by `trig >> 10` (4 units at full scale) toward the target's body pair,
re-facing on every step, until in range; in range, it steps **back** the same way until out of range. Both loops run at least once
and are unbounded, so every swing of a string opens one step either side of the reach edge. A capture shows Vahn snapping 60 units
in one frame. **CONFIRMED** (`0x8004BF7C..0x8004C118`, `autorun_melee_string_trace.lua`).

Ours: `FollowTarget` at every step start: turn, and if out of reach slide to the stop point over `FollowFrames`. **Gap:** the
backward half, the instant (one-frame) placement, and measuring to the body pair.

### 5.5 In-range back-off during the strike loop

Every anim tick, for the acting seat (`ctx[+0x13]`) with category Attack (`+0x1DE == 3`) while the command flow is `0xFF`: if the
range law reads **in range**, the live pair steps back along the heading by `(trig * step * +0x21D) >> 13`, a speed of 4 units per
vsync at game speed 8 (floored before the subtraction, so fractions round toward the target). It holds an attacker whose strikes
carry it into the target at the reach edge for the whole attack (a capture reads Vahn at 361 against a 352 threshold).
**CONFIRMED** (`0x80047C3C..0x80047D1C`).

Ours: nothing. **Gap:** add it to `UpdatePlayback` while the action is active (phases Strike / Chain / Recovery), measured to the
target's body pair. This replaces the reach clamp of `SettleIntoIdle` (bug 30) and most of `ArrivalShove`.

### 5.6 Facing

| When | Store | Source |
|---|---|---|
| Approach states `0x14..0x19` | attacker faces the target's **live** pair | `battle-action.md` strike-band table |
| Strike loop `0x1E`, recovery `0x1F` | attacker re-faces the target's **body** pair on **every pass**, staging or holding (a knockdown that carries the body sideways turns the attacker with it) | `0x801E36F0..0x801E371C`, `0x801E3AD0..0x801E3AFC` |
| A melee hit commits a reaction (flinch, knockdown, block) | the **defender** turns to face the attacker's live pair, from its own body pair; nothing turns it back until its own action | `0x801EEC34..0x801EECBC` |
| Stoned defender | no reaction commit, no turn | same |

Ours: the attacker turns at `BeginAction` and at each `FollowTarget`; the defender turns in `ReactToHit` (from the feet). **Gap:**
the every-pass re-face during strike / recovery, and body pairs.

### 5.7 Formation moves

- **Action seed recentre** (`0x0C`, `FUN_801EFE44`): the attack's target to the stage origin, the rest with it. **Round start**
  (`FUN_801DB318`) recentres the whole formation. Ours: `PlanFormationShift` (off by default with the fixed camera; see bugs 21-26).
- **Make-way step** (`0x14` and the SM tail): bystanders step aside out of a walking attacker's lane, about `class * 8` units over
  about `class` vsyncs. Ours: `MakeWay`, same numbers.
- **Separation** (`FUN_80050BB8`): every pair closer than `(r1 + r2) / 6` (radius `0x280` hero, `0x400` Terra, `class << 5`
  monster) steps apart, run in both orders, **on body pairs**. Ours: `StepFormation`, same threshold, on actor locations (5.1).
- **Nobody walks home.** No return leg after an action. Ours: same.

### 5.8 What to change, in order (for the implementation pass)

1. `GetBodyLocation()` on the pawn (body bones only), used by reach, separation, follow and the reaction turn.
2. In-range back-off while the attacker's action is active.
3. Two-way, instant reach snap at every step commit (replaces the slide of `FollowTarget`).
4. End step from data (`+0x0E`), unclamped; drop the centroid heuristic and the reach clamp of `SettleIntoIdle`.
5. Root speed from data (`+0x0C`) over the clip's length for reactions; positive speeds gated by the range law. Replaces the
   fixed knockback constants.
6. Restrict `ArrivalShove` to what the original does: only the monster walk chain (`0x16`, the six monsters with a tag-`0x20`
   entry); heroes and the other monsters rely on 1-3.

The columns for 4 and 5 need the generator (`Extras/generate_hero_battle_csv.py`) and a monster pass over PROT 0867.

---

## 6. Actor animation

Source: `battle-actor-rendering.md` "Clip selection", "Arts presentation"; `battle-action.md` "Staged-anim playback";
`battle.md` "Per-frame actor maintenance"; `formats/battle-data-pack.md` "Art-animation bank". Clip lengths and the rate law are in
`BATTLE_TIMING.md` sections 1 and 10.

### 6.1 One channel, three bytes

An actor plays exactly one clip. `+0x1DA` is the **staged** id (last writer wins), `+0x1D9` the **committed** (playing) id, `+0x1DB`
the latched id the camera-variant dispatch reads. There is no reaction "mode": a hit reaction is just a staged id, and the next
staged id replaces it. **There is no crossfade**: a commit swaps the clip on the next decoded frame (rigid PS1 parts; the decoder
only tweens inside the clip's last interval). **CONFIRMED.**

| Producer | Stages |
|---|---|
| Action SM, approach (`0x14`) | `1`, the walk |
| Strike loop (`0x1E`) | the queue byte: `0x0C..0x0F` direction swings (equipment-spliced), `0x19` / `0x1A` starters, `>= 0x1B` Art constants |
| Melee hit (`FUN_801EC3E4`) | the defender's reaction: `+0x1EF` / `+0x1F0` (flinch tags 2 / 3, picked by a mod-10 test on the queue byte), `+0x1F1` (knockdown, tag 4), `+0x1F3` (block, tag `0x0B`) |
| Commit ladder | the follow-up (6.3) |
| Spirit / item (`0x3C`, `0x46`) | the ring commit's clip `+0x1E7` (`0x10` Spirit, `9` item) |

The map `+0x1EF..+0x1F3` is filled at monster init, one slot per tag 2 / 3 / 4 / 5 / `0x0B`; a monster without a knockdown uses
its flinch. (The *item / magic* damage path `FUN_800402F4` picks knockdown whenever a get-up exists or the hit is lethal; the
**melee** rule is the quarter test of bug 7 / 11, `melee_reaction_entry`.)

Ours: `AJRPGBattlePawn` plays roles (`RoleIdle`, `RoleWalk`, `RoleFlinch1/2`, `RoleKnockdown`, `RoleGetUp`, `RoleBlock`, ...) through
one crossfade instance, `AnimBlendTime` 0.2 s. **Difference by design:** the crossfade. It hides pops but also shifts what the
body shows by up to 12 logic frames at each change, and it is what the end-step slide (bugs 27 / 30) has to compensate. Keep it as
a tunable; `0` is the faithful value and worth trying once section 5 is in.

### 6.2 When a staged clip commits

| Case | Commits |
|---|---|
| natural end of the playing clip | always (whatever is staged; the same id re-commits from frame 0, which is how loops loop) |
| `+0x1DC` bit 0 set | at once, mid-clip |
| `+0x1DC` bit 1 set | mid-clip once the cursor is past the entry's first event beat by more than 2 keyframes (`B + 3`) |
| entry `+0x76` lock set | never mid-clip; waits for the natural end |

Every commit zeroes the per-clip hit index `+0x1F4`, releases the stage latch (bit 1, so the strike loop may stage the next byte)
and resets the camera ramp / accumulator. The first swing staged over idle waits for idle frame 3. Ours: the logic timeline's step
`StartFrame` / `CutFrame` (`ComposeHeroTimeline`, `BATTLE_TIMING.md` section 2); **matches** in timing, as data rather than as a
clip gate.

**Loops inside a clip.** Entry `+0x84` is a loop **count** (not a rate) and `+0x85..+0x86` a keyframe window: the cursor rewinds
from `+0x86` to `+0x85` that many times; `start == end` parks the clip on a frame. Used by the win pose (held for the 256 hold) and
by authored parks in casts. Ours: **missing**; needed for the win pose (4.6).

### 6.3 The commit's tag ladder (follow-ups)

Run once per commit on the **new** entry's tag (`FUN_8004AD80` `0x8004BE30..0x8004BF4C`):

| Tag committed | Condition | Then |
|---|---|---|
| 4 knockdown | HP > 0 | stage the get-up (`+0x1F2`) behind it |
| 4 knockdown | HP 0, party | stage entry 7 (dying) |
| 5 get-up | - | exit-to-idle at its natural end |
| 7 dying | - | stage entry 8 |
| 8 down | - | latch `+0x1DC` bit 3 (stops root motion); re-commits itself forever (the kneeling loop) |
| 2 flinch | first monster `0xB3` / `0xB5` at HP 0 | treated as tag 4 (two bosses) |
| (monster, previous entry tag 4, HP 0) | - | re-install the knockdown **held on its last frame**, death spoils, defeat fade `+0x21C = 2`, latch; with a Seru staged it gets up instead for the absorb |

Ours: `StartReaction` chains (`Knockdown -> GetUp -> Idle`, hero `Knockdown -> Dying -> Down` looping, monster `Knockdown` hold +
`BeginDeathFade`). **Matches** (bugs 1, 17). Missing: the two boss flinch-to-knockdown cases (`0xB3`, `0xB5`), the Seru get-up
(out of scope).

### 6.4 Speed: the per-actor rate byte `+0x21D` (Arts slow motion)

Clip cursor advance per pass = `step * +0x21D * rate >> 1`; normal 8. It scales **everything animation-driven** of that actor:
swing pacing, root motion, the strike gate.

| Trigger (committed by the acting party member) | Every actor's rate |
|---|---|
| any commit while the rate is not 8 | that actor's rate = 4 |
| `0x1A` Special starter (Super / Miracle) | **all 0 (frozen)**, the acting actor 2 (quarter speed): a freeze-frame with only the dasher moving |
| `>= 0x1B` Art constant | **all 4** (half speed), or all 2 under the special flag `ctx[+0x243]` |
| direction swings `0x0C..0x0F` | no change |
| restore (`FUN_801E93C8`, shared tail of `0x1E` / `0x1F` / `0x20` / `0x50`) | all back to 8 on the pass the acting actor's last Art clip ends, so a monster the Art killed finishes its knockdown at normal speed |

Status: a Slowed actor on idle advances at `>> 2`; a Stone or Numb actor on idle does not advance at all (holds its frame).

The `0x1A` commit also raises the `ARTS!!` banner and queues the per-character Arts shout (sections 9, 10).

Ours: `FJRPGActionStep::GameSpeed` slows the acting hero's clip, and a reaction plays at the speed of the hit that caused it.
**Gaps:** (1) the **bystanders** (other heroes and monsters on idle) are not slowed or frozen; (2) the Special starter's freeze of
everyone but the dasher; (3) the restore to 8 at the Art clip's end for reactions already playing (`BATTLE_TIMING.md` 9, item 14);
(4) Slowed / Stone / Numb idle. A per-pawn `AnimRateScale` set by the scene from the acting step (and applied to the crossfade
instance's play rate and to root motion) covers 1-3.

### 6.5 What else the per-frame maintenance does (`FUN_8004CE2C`)

1. Clears expired status bits.
2. **Clip-tag impact arms** (keyed on the acting clip's `+0x77` identity byte and its cursor): Gala tags `0x16` / `0x17` / `0x18`
   / `0x67` and Vahn `0x18` tint the target; Gala `0x18` also **freezes the target's pose** (rate 0) for a window; Gala `0x67`
   draws a ribbon at the target; Vahn `0x2B` and a monster tag `0x3B` set additive fades; Noa `0x29` / `0x2D` roll confusion-type
   status on every frame of a window (no latch). Table in `battle.md` "The other clip-tag arms".
3. Boss hooks for four formations (`0x8A`, `0xA7`, `0xAA`, `0xB4`): hand-written camera / pose / scale overrides (Jette's spin and
   shrink).
4. Status recolour of the palette: Stone = grey, three other statuses = a blue tint, latched once.

Ours: none of it. 2 is presentation of specific Arts (section 8); 3 is per boss (Phase 8); 4 needs status effects (Phase 7).

### 6.6 Gaps

- Slow motion for everyone, freeze for the Special starter, restore at the Art clip's end (6.4): step 1e of 10.3.
- Crossfade review (6.1): step 1f.
- Loop window `+0x84..+0x86` per clip (for the win pose and parks): two columns in the clip data, honoured by the pawn; step 6.
- Win pose (4.6) on top of the loop window: step 6.
- Status idle rules (Slow / Stone / Numb) and the palette recolour, with Phase 7 statuses.

---

## 7. Camera

Source: `battle-stage-camera.md` "Battle camera" (projection, framing cases, tween, idle orbit, entry sweep, menu framings, case 6,
cases 7 / 8, Done band, per-art camera, the option, screen shake, the 4x scale); `battle-action.md` "`ctx[+0xD]`";
`battle-round-loop.md` "The victory camera"; `formats/battle-attack-camera-table.md`. All **CONFIRMED** unless noted.

The camera **never gates the fight** (`BATTLE_TIMING.md` section 8); it is recomputed from the live actors every frame.

### 7.1 Units: how to read the numbers below

- **Angles**: 4096 = 360° (`0x400` = 90°, `0x100` = 22.5°, `32` = 2.8°). Pitch positive = looking down.
- **Projection**: `screen = H * (R * v + TR) / Ze`, `H = 256` on a 320 x 240 screen: horizontal FOV ≈ **64°**, vertical ≈ **50°**.
  `R = Rx(pitch) * Ry(yaw)`. The camera is **TR** (a view-space translation: `TR.z` is the eye distance in front of the focus,
  `TR.y` lifts the focus below the screen centre) around a **focus** point (look-at).
- **The 4x world scale**: the battle base matrix scales every battle draw by 4, so camera translations are in 4x units while
  actor positions and seats are 1x (the far framing's `TR.z = 7680` is the eye distance to seats at `±800`). For us:
  **cm = TR units / 4 * 0.433**. Example: `TR (0, 1280, 7680)` = focus lifted ~139 cm, eye ~831 cm away.
- `prescale(x)` in the RE tables is that same 4x folding applied to a depth; `ctx[+0x6D0]` (the action depth) is derived from the
  framed monster's size class (`FUN_801F0348`).
- **Durations**: a tween argument `a3` is in display frames, one camera step = 2 frames: `0xC` = 6 steps, `0xE` = 7.

### 7.2 Who owns the camera

1. **Entry sweep** (SCUS frame driver, section 2): from battle init's pose (pitch `0x3C`, yaw 0, `TR (0, 0x500, 0x1C00)`) the
   camera rises and pulls in (`TR.y += 0x30`, `TR.z -= 0x40` per frame step) while the counter runs `0x80..0xA1`, then case 2 until
   `0xC0`.
2. **Command flow** (no action running): case 9 on the round prompt / commit confirm (+ idle orbit), case 0 on the member's ring
   and arts entry, cases 1 / 3 on the target cursor.
3. **Action SM**: case 6 while the actor sets up and approaches, 7 during the strike loop, 7 / 8 in recovery and return, the Done
   band fork, case 9 + orbit during a run. `0x5A` arms nothing (the far framing comes back between actions).
4. **Per-art override** `FUN_801D71B8` after any case, when the option is not Far.
5. **Results sequencer** after the battle-end signal (4.6).

### 7.3 Framing cases (`FUN_801D5854`)

| Case | Shot | Pitch | Yaw | TR | Focus | Tween |
|---|---|---|---|---|---|---|
| `9` | **far framing**, round prompt / confirm / run | `32` | the orbit (passed through) | `(0, 1280, max(span * 3, 0x800))` prescaled | the formation's bounding-box centre, rebuilt every tick (the depth collapses when an attacker walks in) | 7 steps |
| `0` | **member close-up**: ring, arts entry, item / magic windows | `32` | `0x8F0 - facing` | `(-512, height[char], 2457)`; heights Vahn 1152, Noa 960, Gala 1408 | the member | 6 |
| `1` | target cursor on one enemy (also the submenu-exit swing) | `0x100` | `0x800 - bearing(target -> focus)` | `(0, 0x600, prescale(0x800))` | the commanding member | 6 |
| `3` | target cursor on one ally | `32` | `0x900 - ally facing` | `(0, height, 2457)` | the ally | 6 |
| `4` / `5` | whole-side cursor | - | - | - | unchanged | - |
| `2` | menu transition; the entry sweep's close-in | `0` | `0` | `(0, 0x600, 0x700)` | the acting actor (origin in the sweep) | 6 |
| `6` | **action framing**: setup, approach, Spirit, item | `0` | `ctx[+0x6DA] - facing` | `(0, 0x500, ctx[+0x6D0])` | the acting actor's **live** position, focus height on the floor | 6 |
| `7` | **post-strike two-shot** (strike loop) | `0` | as 6, then a one-way unwrap (`yaw - 0x700`, never the short arc back) | as 6; pull-in on Close (`TR.z * 3/5`, `TR.y + 0x40`, pitch levelled) | **midpoint** of attacker and target (body pairs) | 6 |
| `8` | **end of action, on the target** | by target state | 7's base `- 0x100`, unwrap `- 0x600` | by target state (below) | the target's body pair; falls back to the actor when the target node is gone | 6 |

Case 8 on a **dead** target: `TR.y = 0x300`, pitch `0x140`, `TR.z = ctx[+0x6D0]`, yaw ladder zeroed; while the body is still
falling (`+0x36 != 0`) a ramp `r` (`+8 * step` per call, cap `0xC8`, never reset per action) pushes in: `TR.z - 4r`,
`TR.y = 0x300 - r`, `pitch = 0x180 - 3r/2`; it snaps flat the frame the body lands. Case 8 on a **live** target: re-aim by how it
stands (party animating `TR.z 0x600`, `TR.y -4y`; party idle `height - 0x140`; monster `TR.z` by formation, default `7z/10`,
`TR.y -7y/2`; floors `0x280` / `0x300` with a quarter of the shortfall added to pitch). `y` = the target's display height `+0x3E`.

Which state arms 7 or 8: `0x1E` -> 7 every pass; `0x1F` -> 7, or 8 when the target is on its knockdown / get-up; `0x20` -> as
`0x1F`, and 8 when a party seat faces a target in a death clip; Done band -> Run: orbit only, Attack: 8, a dead party target: 8,
else 6 (held ~60 frames, the band's timer).

**Style variant `ctx[+0xD]`** (rolled `rand % 4` at the seed; Attack and Run keep it, Spirit `(rand % 2) * 2`, magic / Tactical
Arts 0; the in-range shortcut keeps bit 0): bit 0 adds a **half turn** to the yaw (film from the other side), bit 1 raises the
pitch by `0x80` and drops `TR.y` to `0x400` (a low angle). So the same action is filmed from four angles.

**Yaw ladder `ctx[+0x6DA]`**: `0` at the round-begin arm, `0x800` at the seed, `0x200` entering an Attack, and at a party
attacker's first swing-clip commit `(rand % 2) * 0x800 + 0x280` (a three-quarter view, one of two sides; `ctx[+0xD] = 0`);
then `+ max(1, 4 * step / 3)` every pass: a slow drift (~1 unit a frame) during every action.

### 7.4 The tween: an ease-out chase, re-armed every frame

Every framing is rebuilt each pass from the live actor records and the tween builder divides the **remaining** gap by `a3` again,
the walker adding `increment * frame_step`. So each pass covers about **1/6 of what remains** (1/7 for case 9): an exponential
ease-out that chases a moving target and never freezes at a state change. Measured step tables match
`ceil((rem + step * inc) / 0xC)` exactly.

### 7.5 Idle orbit

Battle init sets yaw 0 (looking down the seat axis). The yaw then **decrements `2 * step` a frame** (-2 per display frame, about
-120 units/s ≈ -10.5°/s) while the flow byte is on the round prompt / run confirm / commit confirm / armed, or the action SM is in
`0x00` / `0x0B`; also during a run. Case 9 passes the yaw through, so the far framing slowly circles the formation between
commands. (The RE's own port opens on the field camera's azimuth instead of 0, a deliberate choice.)

### 7.6 Per-art attack camera (`FUN_801D71B8`)

After any case, if the Battle Camera option is not Far and the target is one actor (`< 8`) and **alive**, the acting actor's
latched clip id `+0x1DB` in the Art band `0x1A..0x2D` selects a per-character / per-Art arm (13 arm bodies): a fresh pose (pitch 0,
yaw `-facing`, `TR (0, 0x400, 0x400)`, `0x600` high for character 3, look-at the actor) plus offsets from the **20-row table at
`0x801F4E10`** (PROT 0898), stepped by clip-cursor thresholds, re-tweened over **1, 3 or 6 frames** (much snappier than the cases).
Each row has two alternative offsets; a coin flip at each swing commit picks the column (`ctx[+0x26D]`), so the same Art is filmed
two ways. The close-up tightens as the ramp `ctx[+0x26E]` climbs. Plain direction swings (`0x0C..0x0F`) never arm it. A death
re-frame is never overridden. Row map and thresholds: `formats/battle-attack-camera-table.md`.

### 7.7 Battle Camera option (Close / Normal / Far)

Not a distance; it calms the action shots: Far skips the yaw drift and the setup-band orbit, builds no case-8 yaw, and skips the
per-art camera; only Close runs case 7's pull-in and case 8's dead / live re-aims. Ours: no such option (Phase 7, options menu).

### 7.8 Victory, run, Spirit, shake

- **Victory** (4.6): during the load window (~80 vsyncs) case 8's stand-off arm on the leader: `TR (0, 0x400, radius * 5/2)`,
  pitch 0, focus the leader. From the results frame: case 6's **battle-over arm**, from behind the leader (`yaw = 0x800 -
  facing`), height `-5 * display height` floored at `0x280` (shortfall tilts the pitch), moved by a **per-character win-pose
  camera script** keyed on the win pose id (`0x11..0x18`) and an accumulator advancing 8 a frame, so the shot keeps moving
  through the 256 hold. Focus = the leader's body pair (it follows the pose).
- **Run** (`0x65`): failed = orbit (`2 * step` a pass); escaped = the eye backs off `32 * step` a pass, then fade.
- **Spirit** (`0x46`): action depth `ctx[+0x6D0] = 0x800`, a close-up under case 6.
- **Screen shake** in battle: the shake kernel `FUN_801D9D30` is only called by field overlays, and its amplitude has one writer
  (a field-VM opcode), so hits do not shake the screen. `battle-action.md` names a "screen-shake" in Run `0x64`; whether anything
  visible comes of it is **NOT FOUND**. A hit shake in ours would be a choice, not fidelity.

### 7.9 Ours

`AJRPGBattleArena` points one camera at the start of the fight: a hand-placed `BattleCamera`, or `UCameraSubsystem::FrameGroup`
over every combatant with the `BattleWide` preset (`CameraPadding` 1.15, `bCameraBehindParty`), and leaves it there. Formation
recentre is off by default *because* the camera is fixed (bugs 21-26; `OnFormationShifted` exists for future action cameras).

**Done so far (camera pass, 2026-11-05):** `AJRPGBattleCamera` (`Docs/Guides/BATTLE_STAGE.md` "The battle camera"), spawned by
the arena when there is no `BattleCamera` (`bUseBattleCamera`). Part a: the pose model and units of 7.1, the ease-out of 7.4, the
lens, case 9 with the orbit of 7.5, and the focus moving with `OnFormationShifted`. Part b: the menu framings, case 0 on the
Command and Attack screens and case 1 on the Target screen (`FJRPGBattleMenu::GetFocusHeroSlot` / `GetTargetCursorSlot`); case 3
waits for the ally cursor (items, magic). Part c: the action framings 6 / 7 / 8 (pawn phase -> case: `Windup` / `Advance` 6, `Strike` / `Chain` 7,
`Recovery` 7 or 8 on a knocked-down / dead target, the Done band 8 for a hit action else 6, held `DoneHoldSeconds`), the yaw
ladder with its seeds and drift, the style variant, the action depth from the size class (Spirit `0x800`), case 7's one-way
unwrap (chosen when the case is entered), and the Battle Camera option (`EJRPGBattleCameraOption`, Normal by default) with
case 7's pull-in and case 8's dead / live arms on Close. Not carried: the formation-specific case 8 depths (`0xB4` / `0xA2` /
`0xA7` / `0x1F..0x21`), Terra's case-6 override, the Done band's `0x96` seed. Pawn accessors added for it: `GetActiveTimeline`,
`IsDefeated`, `IsOnKnockdown`, `IsFallingDead`, `IsAnimating`, `GetDisplayHeightCm`. Part d: the per-art camera 7.6, all 13 arms with
their cursor cascades, latch resets and ramps (`AJRPGBattleCamera::RunArtArm`, from the RE port `battle_attack_camera`), the
20-row table as `DT_ArtCamera` (`Extras/generate_art_camera_csv.py`, PROT 0898 `+0x265F8`), the clip cursor from
`AJRPGBattlePawn::GetPlayingStep`. Part e: the option 7.7 in the game's Options (Game tab), saved in `JRPGSettings`; the Spirit
close-up of 7.8 was part c. Camera pass done; the victory camera of 7.8 belongs to the battle-end step.

**Gap: the whole battle camera.** Suggested shape (implementation pass): a `UJRPGBattleCameraDirector` owned by the scene subsystem
that, every logic frame, picks the case from the battle stage + the acting pawn's phase + the menu screen, builds the target pose
(pitch, yaw, TR, focus) from the live pawns' **body locations** (5.1) with the unit conversion of 7.1, and chases it with the
ease-out of 7.4. Order: case 9 + orbit -> case 0 / 1 / 3 for the menu -> case 6 / 7 / 8 + style variant + yaw ladder -> Done band ->
victory -> per-art table (needs the 20 rows read from the user's disc by a generator, like `DT_ArtHits`) -> option. The hand-placed
`BattleCamera` stays as an override for special arenas.

---

## 8. Effects

Ours today: **no battle effect at all** (no Niagara, no tint, no trail in `Private/Battle/`); the only body effect is the monster
death fade through the material parameter `JRPGDeathFade`. Every row below is a gap unless it says otherwise.

### 8.1 Effects drawn on the actors' own bodies

Source: `battle-actor-rendering.md` "Tint and blend", "The after-image ghost walk", "Trails and streaks".

| Effect | When | How it looks | Where it comes from | Port idea |
|---|---|---|---|---|
| **Hit tint** | a landed hit (melee or Art) whose acting record has an impact selector `+0x7A` in 1..5; also the clip-tag arms (6.5) | the struck body's own texture pushed toward an element colour, then easing back (`8` lane units a frame) | colour from the 5-word impact table `0x801F53D4`: red, two blues, violet, white; formula `baked + (tint - baked) * blend / 0x1000`, texel still multiplied | a material parameter pair on the battle materials (`HitTintColor`, `HitTintAlpha`), driven by the pawn on the hit event |
| **After-image ghosts** -> **aura** (decision) | ring id `> 0x10`: a party **Special starter dash** and **chained** Arts (not an ordinary Art swing); a monster only on clips whose record has `+0x77 != 0` or the solo byte `+0x87 == 1` | original: two additive copies of the mesh at the poses `step` and `2 * step` frames back (`step = 8 / rate`) | per-character colour: Vahn red `0x60/0x30/0x30`, Noa green, Gala blue, monsters olive `0x50/0x50/0x30` | **not ported**: the user's own aura effect (a glow around the body, Solo Leveling style), improved, switched on at these same moments; the per-character colours above are a good default tint for it |
| **Weapon trail** | while the committed clip's `+0x77` matches: Vahn `0x29`, Noa `0x1E` / `0x2A`, Gala `0x64` (ordinary Arts swings) | an untextured gouraud ribbon over 3 bones of the weapon chain, up to 16 steps back (2 display frames a step), white-to-grey head then the tint fading to black | tints Vahn `0x802040`, Noa `0x80FFC0` / `0x208040`, Gala `0x204080`; bones from object `0x0C` (Vahn), `0x04` / `0x0A` (Noa), `0x06` (Gala) | Niagara ribbon (or `UTrail`-style anim notify state) on the weapon bones, keyed by clip |
| **Move-FX streak / ribbon** | a staged move-FX scene (casts and monster specials; a plain swing never) | a shrinking textured billboard while the streak counter is `>= 0x281` (party), then a jittered vertical ribbon of textured quads | trail texture id from the move-power record `+0x0B`, texpage `0x27`, CLUT `0x7700 + id` | Niagara; mostly magic / monster specials (later) |
| **Monster defeat fade + sink** | the killed monster's knockdown has ended | drawn additive, colour lanes `0x100 -> 0` (8 a pass), sinking `(size class * step) >> 2` a pass and clipped by the floor plane | `+0x21C = 2`, arm 2 of the tint SM | **have** (`BeginDeathFade`); the darkening needs `JRPGDeathFade` on the materials (bug list, pass 2) |
| **Distance fade** | always, when no blend runs | far bodies pushed toward a darker copy of themselves (brighter on 13 outdoor stages) | view depth vs radius / 2 | optional; fog / post-process does the same job |
| **Status colours** | status `0x1` / `0x2` / `0x380` | tint `0xFF2020` / `0xFF0420` / `0xF020F0` at half weight; Stone = grey palette, three others blue | `+0x16E` | with Phase 7 statuses |
| **Near-camera ghost** | a body (not the actor or its target) within `dist / 4` of a point on the view axis, during actions | the body drawn faint additive (`B + F/4`) so it does not block the view; never during a run or a pre-emptive round | `FUN_8004DC68`, every battle frame | camera occlusion fade on the pawn materials, driven by the scene |
| **Target cursor dim** | target selection | the monsters not pointed at are darkened | `+0x21C == 0xC8` | WebUI cursor today; optional dim on the pawns |

### 8.2 Where battle effects come from: the clip's own effect script

**The key fact:** almost every battle effect is authored **per clip**, in the same action entry that carries the clip's sound
track. Every action entry (monster actions in PROT 0867, the party's `record[0]` entries, the equipment swings, the Art-bank
records' embedded entries) has, at `+0x14..+0x53`, up to **eight 8-byte effect records**:

```
+0x00 u8  frame_gate   fire once the clip's frame + 1 reaches it; 0 ends the list
+0x01 u8  effect       & 0x7F == 0x7F: terminator (installs the move-power record; casts only)
                       & 0x80: direct spawn into the 2D effect pool (efect.dat id)
                       else:  a move-VM part prototype, 0x801F6324[effect] (61 entries)
+0x02 i16 off_x  \     actor-local offsets, scaled by the mesh scale (+0x72),
+0x04 i16 off_y   >    rotated by the facing (+0x46); Y is subtracted
+0x06 i16 off_z  /
```

(`formats/monster-animation.md` "Effect-script records"; walker `FUN_801DEA50`.) The walker runs every anim tick for every actor,
and again over a clip committed in that tick, so **frame-0 records fire on the pass the clip commits**. **CONFIRMED.**

What the disc puts there (`monster-animation.md`, `battle-action.md`, `effect-vm.md`):

| Clip | Effect records | Notes |
|---|---|---|
| **hit reactions** (flinch, knockdown, block) | the **impact flash / ray burst** (codes `0` / `9`) at frame 0 | so the hit spark belongs to the **defender's reaction clip**, seated on the pass the blow lands, the same pass the damage number is pushed (`battle_melee_hit_spark`) |
| **walk** clips | per-footfall dust | |
| the **Spirit** clip (`0x10`) | the aura: prototypes `0x07` / `0x08`, small meshes morphed through `vdf.dat` entry 12 into a cone; opens in `0x28` frames (lane `+0x66` a frame), spins `0x222` a frame, additive (`0xC9000000`, far colour black), fades in over `0x20` frames and out over the last `0x0B` | |
| monster casts / specials | per-frame emitter trains; a `0x7F` terminator installs the move-power record | out of scope (magic) |
| swings and Arts | their own records (per clip; not catalogued in the RE text) | read them with the generator below |

Per-code behaviour of the walker:

- **Table arm** (bit `0x80` clear): part prototype `0x801F6324[code]`, spawned through the effect-actor pool `FUN_80050ED4`. Before
  it, codes `< 0x32` with a non-zero byte in `0x801F6418[code]` copy a **CLUT row** (a recolour of the effect texture, not a
  sound). Scale `0x1000`, except code `4` (`0xC00`) and code `6` (`0x2000`), times the actor's mesh scale. Code `0` becomes `9`
  when the actor is on dynamic slot `0x11` (an Art). Codes `0x0A` / `0x2D` / `0x2E` keep a handle that **follows the actor**.
  Codes `4..6` add two more parts from fixed prototypes. A code past the 61 entries reads garbage (code `0x4C` has no visual).
- **Direct arm** (bit `0x80` set): the 2D pool `FUN_801DFDF0(id, position, facing)`. Codes `0x84` / `0x93` clamp the forward
  offset into `[3d/4, d]` of the attacker-to-target distance (the effect lands **on the target**). Codes `0x81..0x83` add a
  **floor ring** part on the actor's body pair at floor height, scale `node[+0x58] << 1`.

Other producers, smaller: the pose decoder's animation effect arm (`FUN_8004998C`), the move-power effect lists (casts and
monster specials, `FUN_801E09F8`), the per-move cue-group list (`FUN_801E22C8`, items / Spirit applier). Spawns are always seated
at an actor plus a rotated offset, never at the world origin. All battle effects die with the battle (the pool is re-seeded at
every mode switch).

### 8.3 The effect assets

| File (extraction) | Content | Tool |
|---|---|---|
| `etim.dat` (PROT 0870) | three 64 x 256 4bpp TIMs, VRAM `(320,0)` / `(384,0)` / `(448,0)`, CLUT rows 474..476: the sprite atlas; the third page also holds the **damage digits** and the **`NEW` / `HYPER` / `MIRACLE` / `SUPER` / `ARTS!!` / `DAMAGE` / `HIT` / `TOTAL`** words | `tim.exe convert` |
| `etmd.dat` (0871) | 30 Legaia TMD effect models (global TMD pool `3..32`; e.g. index 26 = Gimard's Tail Fire) | `tmd.exe dump-obj` (OBJ, untextured; textures via `vram-dump`) |
| `vdf.dat` (0872) | 32 vertex-morph entries (the Spirit aura is entry 12) | RE parser only |
| `efect.dat` (0873) | pack1: effect-id scripts (14-byte records per child); pack0: frame-batch animations (6-byte frames); inline sprite atlas | RE parser only |

(Our extractor names 0870 / 0871 `sound_data` and 0872 / 0873 `befect_data`; the RE's numbering for the four is 870..873.) The 2D
pool (`effect-vm.md`): 32 master slots spawn children on a cadence; 128 child slots each play a frame batch with motion, drawn as
semi-transparent textured billboards (`GP0 0x2E`), blend mode from the prim code. **Effect ids have no names** on the disc; each
has to be identified by eye (the RE's web viewer plays them).

### 8.4 Damage number and the Arts banner (drawn from the effect sheet)

- **Damage number** (`effect-vm.md` "The floating value readout"): digits from the `etim` sheet, 24 x 24 cells in strip order
  `1234567890`, pitch = drawn width + 1. Seated at the struck actor's projected screen position; the run's **centre holds**, the
  cells **grow** to 1:1 and the run **rises to a fixed screen row `y = 32`**. No tint, no fade. Pushed the pass the blow lands.
  Ours: an on-screen debug string above the target (`ShowEvent`). **Gap.**
- **Arts banner** (`battle-hud.md` "Arts announcement banner"): `NEW ARTS!!` / `HYPER ARTS!!` / `MIRACLE ARTS!!` /
  `SUPER ARTS!!`. Raised at the **Special starter's commit** (`0x1A`, party only; Miracle from a per-seat flag, NEW / SUPER from
  the queue builder's marks, else HYPER), with the slow-motion freeze. Two quads (the word + the shared `ARTS!!`) slide from both
  sides toward a seam while their band shrinks; a clock `+8 * step` a frame saturating at `0xF0`; **four layers** at clock offsets
  `0x30 / 0x20 / 0x10 / 0` and brightness `5 / 10 / 20 / 50 %` (only the last opaque) make a ghost trail that drops off as it
  lands. A commit of the same actor while it is live retires it. Fanfare cues in section 9. Ours: **missing** (the "Learned X!" /
  Art name text is a placeholder).
- The combo cluster (`HIT` / `TOTAL`) is HUD: section 10.

### 8.5 Plan for the plugin (implementation pass)

1. **Export the effect records with the sound tracks.** `Extras/ps1_party_sounds.py` and `Extras/ps1_monster_sounds.py` already
   read each entry's sound track at `+0x54`; read `+0x14..+0x53` in the same loop and write an `effects` list per clip
   (`frame`, `code`, `offset`) into `sounds.json`. `Extras/ue_import_monster_sounds.py` then places a
   `UJRPGAnimNotify_BattleEffect` (code + offset) on each clip, beside the sound notifies.
2. **`DT_BattleEffects`**: code -> what to spawn (a Niagara system or a mesh + material, scale, follows-actor, floor ring,
   clamp-to-target). Filled by hand, by watching the codes in the RE viewer. Start with the ones every fight shows: the hit burst
   (`0` / `9`), footfall dust, the Spirit aura, the floor ring.
3. **Textures and models** for those systems from `etim` (`tim.exe`) and `etmd` (`tmd.exe`), outside the repo like every disc
   asset (rule 48), imported under `/Game/Legaia/Battle/Effects/`.
4. **Body effects** of 8.1 as material parameters on the battle materials (hit tint first, then the weapon trail), and the
   user's aura effect where the original draws ghosts.
5. **Damage number** and **Arts banner** in the battle WebUI, from the `etim` sheet, with the motion above.

---

## 9. Sounds and music

Source: `audio.md` ("Per-actor sound effects", "Monster sound bank", "Hero victory voices", "The battle sound set picks the
fight's track", "SFX bank + scheduler", "The battle command surfaces' blips", "Battle arts-voice shout path", "The second shout
trigger", "CD-XA voice-clip dispatchers", "What a normal attack sounds like"); `battle-action.md` "The sound a melee swing makes";
`battle-hud.md` "Arts announcement banner" (fanfare). All **CONFIRMED** unless noted.

### 9.1 How a battle sound is routed

| Id range (cue router `FUN_8004FE5C`, dispatcher `FUN_8004FCC8`) | Plays |
|---|---|
| `< 0x48` / `0x48..0x63` | the **static SFX table** (SCUS `0x8006F198`, 100 rows); the row's category picks the bank: `0` system (PROT 0868), `2` battle (0869), `6` field (0876, silent in battle), `11` reward (0889) |
| `0x64..0xA6` | `bse.dat` rows in VAB slot 7: in a fight the **struck / acting monster's bank** from `monster.snd`; on the win poses the **victory voice** |
| `>= 0xA7` | `bse.dat` rows in the battle bank (0869) |
| party `0xC8..0xFF` | **CD-XA voice**: clip `(x + 0x38 - 0x100) >> 3`, channel `& 7` (Vahn XA1 / XA27, Noa XA3 / XA28, Gala XA5 / XA29) |
| `>= 0x100` from the dispatcher | CD-XA too (the jingle / voice arm), duration from the SCUS table `0x800788B8` |

Only **one CD-XA voice** plays at a time: a new request replaces the one sounding (one drive). The voice trails the clip install by
~150 ms (seek latency) in the RE's model.

### 9.2 Every sound source of a fight

| # | Sound | Trigger | Data | Ours |
|---|---|---|---|---|
| 1 | **Clip cue track** (whoosh, impacts, footsteps, knockdown thud, party grunts and the per-clip voice lines) | each action entry's 8 `(frame, cue)` pairs at `+0x54`, walked every frame from a cursor `+0x1F6`; a repeated cue right after itself does not sound twice | per clip | **have**: `Extras/ps1_monster_sounds.py` / `ps1_party_sounds.py` -> notifies; the impact sound gated by the hit result (bug 20); a **blocked** hit skips one cue (the impact) |
| 2 | **Arts shout** | the anim commit of an Art: when the byte before the queue cursor is the known-art starter `0x19` (or under the solo latch); **not** for an Art learned this turn | `FUN_8004C140(char, action constant)` picks a channel from the Art's candidate pool, **random, never the same twice in a row**; banks XA2 (Vahn) / XA4 (Noa) / XA6 (Gala), 16 channels each | **missing** (these are not on the clip cue track) |
| 3 | **Starter fanfare** | the Special starter's commit (`0x1A`), with the banner | Miracle / Super / NEW: fixed `0x101` / `0x111` / `0x121` per character (XA1 / XA3 / XA5 channel 1; two if both marks); Hyper: by the Art byte `0x1C` / `0x1D` / `0x1E`, a coin flip between two channels, other Arts none (`legaia_art::hyper_fanfare`) | **missing** |
| 4 | **Block grunt** | a melee strike that commits the defender's **block** entry (and the CD voice level is below 2) | XA30 (`clip 0x1D`), channel / duration by the **attacker's** character: Vahn `(0, 0x26)`, Noa `(4, 0x2E)`, Gala `(6, 0x1A)` vsyncs | **missing**. An ordinary landed swing makes no sound here (only the clip cues) |
| 5 | Melee sting `0x10C` (XA27 ch 4 on a party target, ring `0x2A8` in the monster's bank on a monster) | only while an effect handle is live (Cort's Mystic Shield) | - | out of scope |
| 6 | **Menu blips** | every press of the command flow | dispatcher `0x21` commit, `0x22` cursor step, `0x23` cancel / refused (ring ids `0x20` / `0x21` / `0x22`, static table, **system bank 0868**); the item / magic list windows use the list kernel's own (`0x21` step, `0x20` confirm, `0x23` disabled row, `0x37` cancel); the press that exhausts the arts gauge is silent | **partial**: `96_battle.js` plays the generic WebUI `SFX_*` files; replace with the three battle cues |
| 7 | **Victory voice** | the results frame (+80), the leader's win pose | the tail clips of `monster.snd` (Vahn `0xB8..0xBC`, Gala `0xBD..0xC3`, Noa `0xC4..0xCB`), picked by the pose: SCUS `0x80078867 + action + char * 8`, clip = byte - 1; plays as cue `0xA1` on the pose clip's track | **exported** (`SW_<Name>_VictoryVoice<clip>` on the pose clips) but nothing plays the win pose yet (4.6) |
| 8 | **Level-up jingle** | the results frame, when someone levels: level-up window `0x44 + mask` + cue `0x50` | bank PROT 0889 (category 11, "reward"), loaded at the signal | check the WebUI results; **missing** as the original's cue |
| 9 | **Battle music** | the battle intro's completion arm | the field script's sound set `_DAT_8007B880` (op `0x35` sub-op 7): `N >= 0` loads bundle `0x36F + N` (extraction `877 + N`) and starts its score (`0..9` = tracks `2026, 2027, 2028, 2030, 2032, 2031, 2051, 2067, 2061, 2073`); `-1` = no swap, the event's theme (boss) keeps playing; reset to 0 on every scene entry. **Hard cut**, not a crossfade | **check**: battle music is the arena / setup's choice; add a per-encounter "sound set" (or a BGM override) and restore the field track after |
| 10 | Victory music | **NOT FOUND** in the RE text read: the results sequencer loads the level-up bank and the voice, but no separate victory BGM start is described | - | keep ours until found |
| 11 | Monster sounds outside the clips | none found: a monster's voice and impacts are its clip cues in its own bank | - | **have** |
| 12 | Escape, Spirit, item | Spirit / item: the cast audio-cue dispatcher `FUN_801F3990` at `0x3D` (and the item / Spirit voice lines seen in the spar capture); Run: no cue named in `0x64..0x66` | - | with Spirit (Phase 7) |

### 9.3 Plan for the plugin (implementation pass)

1. **Voice banks**: demux XA1..XA6, XA27..XA30 per channel (the user's disc, `xa.exe demux-disc-all`, already used by
   `ps1_party_sounds.py`) into `SW_<Name>_Shout<ch>` / `SW_<Name>_Fanfare<ch>` / `SW_Battle_BlockGrunt<char>` assets.
2. **Shout pools and fanfare pairs**: a small `DT_ArtVoices` (Art key -> candidate channels; Hyper fanfare pair) generated from
   the SCUS tables (`legaia_art::arts_voice`, `hyper_fanfare`) by a generator, like `DT_ArtHits`.
3. A pawn-side **voice channel**: one voice at a time per fight (a new one stops the old), random pick avoiding the last channel.
   Shout on the Art clip's start (only for Arts known before the turn); fanfare with the Special starter; block grunt on a
   `Block` event, keyed by the attacker.
4. **Menu blips**: export cues `0x20` / `0x21` / `0x22` from the system bank and play them from the battle menu (commit / step /
   cancel), silent on the press that empties the gauge.
5. **Battle music** per encounter: a sound-set / BGM field on `FJRPGBattleSetup` (`-1` = keep the current track), hard cut in,
   restore the field track on exit.
6. Level-up cue `0x50` and the victory voice with the win pose (after section 6.6 item 3).

---

## 10. HUD, end of battle, and the implementation order

### 10.1 The battle HUD

Source: `battle-hud.md` ("At a glance", "Screen layout", "The actor-name plaque", "The party status readout", "Action-phase
openers", "Action-phase records", "Message and intro banners", "Arts announcement banner"); `formats/effect.md` (the value-readout
sheet). Screen 320 x 228. **No HP or MP gauge exists anywhere** (party or monster): labels and numbers only.

**Reference only (decision):** the plugin's HUD is our own design. The table says what the original shows and **when**, which is
what to keep (who is acting, the target, the move name, the combo count, the damage, the results); the look, seats and motion are
free. One addition of ours: a small **HP bar over each enemy's head** (thin polished frame, fill only, no numbers).

| Element | Where / when | Look and motion | Ours |
|---|---|---|---|
| **Actor-name plaque** | `(8, 8)`, 20 px tall; names whoever acts (member on its turn, a monster through its attack); a monster's only HUD | gold plate, width = the name (+ element badge 20 + 5); slides in from `y = -24` | missing |
| **Roster panels** | resting surface, `y = 164`, one 102 x 48 marbled plate per member at x `109` (solo), `58` / `160` (pair), `7` / `109` / `211` (trio) | name, `LV` n, `HP cur / max`, `MP cur / max`; no gauge | **have** (`bt-party`, check against the seats) |
| **Active-actor bar** | replaces the panels while one actor holds the screen: `(8, 188)`, 304 x 20, blue; the panels park below the screen (`y = 230`) | name, `HP cur / max`, `MP cur / max` | missing |
| Bar openers | action seed: a bar for the **party target** of the action (so a hero hitting a monster shows **no** readout); item pre-arm: the member; item apply: the member or all three panels | rises from `y = 234` to 192 | missing |
| **Action target plaque** | a party member's action on a monster: right-aligned to x 312, the monster's name + element badge; rises from `y = 236`; slides back out in the `0x51` teardown | blue plate | missing |
| **Move name** | `y = 150`, centred on x 160, plain glyphs, no plate (Art names, monster moves) | - | placeholder text |
| **Combo cluster** | anchor glides `(328, 170) -> (168, 170)` over 16 frames; **every landed hit restarts the slide**; reads `DAMAGE n` for one hit, `n HIT` / `TOTAL n` for more; slides back out (reverse) in the `0x51` teardown when the action landed damage; a counter-attack never opens it; a party-wide cast opens nothing | the `etim` sheet: `DAMAGE` / `HIT` / `TOTAL` labels + 24 x 24 digits | placeholder (`Hit N` / `Damage X` debug text) |
| **Damage number** | per hit, over the struck actor (section 8.4) | grows, rises to `y = 32` | placeholder |
| **Arts banner** | Special starter (section 8.4) | 4-layer slide | missing |
| **AP plate / arts AP bar** | command entry; Spirit turn grows it (4.3) | blue plate; bar = 1 px per AP | **have** in the Attack screen (check the Spirit growth when Spirit lands) |
| **Command chips** | Begin / Run pair centred (160, 92); ring diamond centred (228, 70): Item up (196, 28), Attack left (152, 60), magic right (240, 60), Spirit down (196, 92); D-pad glyph in the centre | blue plates; glide in on open | **have** (`bt-dpad`), our own layout |
| **Target-select plaque** | `y = 162`, centred on x 232 | blue plate, the name | **have** (`bt-targets`), our own layout |
| **Message banner** | top, frame `(8, 4)..(304, 32)`, fixed 280 x 12 content; shares the plaque's seat | blue-marbled class-0 window | missing (messages: Counterattack successful, escaped / couldn't escape, Seru absorb) |
| **Intro enemy names** | flow `0x0A`, pen `y = 48`, one label per group (`Killer Bee * 3`), + the formation line at `(16, 12)` (back attack / pre-emptive) | message-banner frame | missing |
| **Result window** (win) | `288 x 42` framed window sliding from `(16, 236)` to `(16, 160)`; "<leader> ... victory" / team text; spoils (XP, gold, drops); level-up window `0x44 + mask` | class-0 window | **missing** (no results screen in the WebUI) |
| **Loss window** | same box, defeat text | | missing |
| **Status badge / element badge** | 48 x 16 status words on the panels; 20 x 12 element badge in names | | with Phase 7 statuses |

The placeholders that go away with this: `ShowEvent` debug text, the `Hit N` / `Damage X` messages
(`UJRPGBattleSceneSubsystem::HandleBattleEvent`, `ComboHitMessageKey`).

### 10.2 Everything that is missing, in one list

Out of scope or replaced (decisions at the top of the file): magic, summons, Seru capture; the `Auto` / `Command` prompt
(Tactics); the after-image ghosts (the user's aura); the counter-attack (Warrior Icon only, with the accessory passives); the
original's HUD look (ours is our own design).

| Area | Gap | Section |
|---|---|---|
| Movement | body pair, in-range back-off, two-way reach snap, end step and root speed from data, arrival shove only on the six walk chains | 5.8 |
| Bugs | 29 (check `DT_ArtHits` vs clip length; Miss / Block legibility), 30, 31 | 1 |
| Command flow | Reselect one member back; angular target ring opening on the last target; commit log; menu blips | 3.2, 9 |
| Action SM | **Spirit turn** + Spirit pay + extended gauge; **Run as an action** (roll, orbit / back-off, failed run spends the turn); monster walk chain (`0x15..0x18`); back attack loses the party's whole round 1 | 4 |
| Animation | slow motion for everyone, Special starter freeze, restore at the Art clip's end; loop window per clip; win pose; crossfade review; status idle rules | 6.6 |
| Camera | the whole director: far framing + orbit, member close-up, cursor framings, action / two-shot / target shots with the style variant and yaw ladder, Done band, victory, per-art table, option | 7.9 |
| Effects | per-clip effect records -> notifies -> `DT_BattleEffects`; hit burst, dust, Spirit aura, floor ring; hit tint, the user's aura (in place of the ghosts), weapon trail; damage number, Arts banner | 8.5 |
| Sounds | Arts shout pool, starter fanfare, block grunt, one voice channel; menu blips; battle music per encounter; level-up cue; victory voice | 9.3 |
| HUD (our design) | who acts, the target, the move name, the combo count, damage numbers, messages, result / loss screens; **enemy HP bars over the heads** | 10.1 |
| Entry / exit | enemy names + count with the camera on the enemies, formation banner, hold, entry sweep, transition styles; victory sequence (pose, camera, windows, voice) | 2.2, 4.6 |

### 10.3 Implementation order (user's, 2026-11-05)

Each step is split into parts of the usual size: implemented by Claude, pausing after each part; built and tested in PIE by the
user. Data generators are listed where a step needs disc numbers (outside the repo, rule 48).

1. **Movement: everything right and smooth.** The goal is that every swing, Art, reaction and walk looks right before anything
   else.
   - a. Body location on the pawn (5.1) and its use by reach, separation, follow and the reaction turn.
   - b. In-range back-off and the two-way, instant reach snap (5.4, 5.5).
   - c. End step and root speed from the disc data (5.2, 5.3), replacing the centroid heuristic, the reach clamp and the fixed
     knockback; arrival shove only on the six walk chains (4.2). *Generator:* `+0x0C` / `+0x0E` per clip for heroes
     (`generate_hero_battle_csv.py`) and monsters (`generate_enemy_actions_csv.py`).
   - d. Bug 29: the clip-length vs `DT_ArtHits` log, and a clear Miss / Block.
   - e. Slow motion for everyone during an Art, the Special starter freeze, the restore at the Art clip's end (6.4).
   - f. Crossfade review once a-e are in (6.1).
   - Re-test bugs 29-31 and the PIE list of `BATTLE_PHASE5_BUGS.md`.
2. **Spirit** (4.3): the command (Down arm), the band `0x46..0x48`, the extended gauge `min(gauge * 7/5 + 8, 288)` and the Spirit
   target `+32`, the Spirit pay of the Done band (8 per action, 32 for a Spirit turn), its clip, the close-up depth `0x800`;
   defence x3 / block x3/2 are already in `BattleFormulas`. Tactics' Defensive uses it.
3. **Cameras** (7.9): the director: far framing + orbit, member close-up, cursor framings, action / two-shot / target shots with
   the style variant and yaw ladder, the death re-frame, Done band; then the per-art table (*generator:* the 20 rows at
   `0x801F4E10`). Turn the formation recentre on with it.
4. **Remaining sounds** (9.3): menu blips; one voice channel with the Arts shouts, starter fanfares and the block grunt; battle
   music per encounter. *Generator:* `DT_ArtVoices` from the SCUS tables; XA banks demuxed.
5. **Battle start** (2.1): the camera **on the enemies** with their **names and count** (`Killer Bee * 3`) and the formation line
   (back attack / pre-emptive), the 90 / 120 hold, then the entry sweep to the first prompt; back attack loses the party's round
   1. Transition styles optional.
6. **Battle end** (4.6): the loop window per clip (6.2), the leader's **win pose** held through the 256 hold, the victory camera
   (7.8), the victory voice and level-up cue (9.2), the result / loss screens (our design).
7. After that, order to decide:
   - our HUD (10.1) with the **enemy HP bars**, replacing the debug text;
   - effects (8.5): the per-clip effect notifies and `DT_BattleEffects`, hit burst, dust, Spirit aura, hit tint, **the user's
     aura** at the Special starter dash and chained Arts, weapon trail, damage numbers, Arts banner;
   - Run as an action (4.5);
   - command-flow details (3.2): Reselect one member back, target ring, commit log.
8. Later, with their phases: accessory passives (counter-attack with the Warrior Icon), statuses (idle rules, palette recolour,
   badges), boss hooks, the Battle Camera option, magic / summons / capture.
