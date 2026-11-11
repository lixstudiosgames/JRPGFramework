# Battle timing: how the original paces a fight

How long each part of a battle takes in *Legend of Legaia*, what a fight waits for before it moves on, and how that compares with
our logic timeline. Sections 1 to 8 and 10 are research; section 9 compares it with our code and records the nine changes made to
follow it (implemented, **not built or run**: they need a PIE re-test). It settles
[`BATTLE_PHASE5_BUGS.md`](../BATTLE_PHASE5_BUGS.md) bugs 17, 18 and 19.

Everything was derived from the [`legend-of-legaia-re`][re] reverse-engineering project (the US disc). Only numbers and
descriptions are written down here; no disc data is copied (rule 48). The party clip figures in section 10 were read from the
user's own extraction with the RE tools; the monster figures come from `Docs/Data/DT_EnemyActions.csv`, which is already in the
repository.

Sources (all in the RE clone, `docs/`): `subsystems/battle-action.md`, `battle-action-helpers.md`, `battle-action-exit-gates.md`,
`battle-actor-rendering.md`, `battle-stage-camera.md`, `battle-command-flow.md`, `battle-round-loop.md`, `battle-formulas.md`,
`formats/monster-animation.md`, `formats/battle-data-pack.md`, `formats/anm.md`, `reference/re-settled-threads/battle.md`; and
the Rust ports `crates/engine-battle/src/victory.rs`, `crates/engine-battle-vm/src/battle_formulas/tint_sm.rs`.

[re]: https://github.com/andrewaltimit/legend-of-legaia-re

**Confidence tags used throughout**

| Tag | Meaning |
|---|---|
| **CONFIRMED** | stated by the RE docs from disassembly or a capture, or read directly from the disc data |
| **INFERRED** | follows from confirmed formulas, but the product itself was never measured |
| **NOT FOUND** | the RE does not say; listed in section 11 |

**Unit.** Everything is in **vsyncs**, i.e. our *logic frames* (1/60 s). A clip of N keyframes at rate R lasts about
`4 * N / R` vsyncs (section 1). 60 vsyncs = 1 s.

---

## 0. Summary table

| What | Value | Unit | Source | Tag |
|---|---|---|---|---|
| Battle tick | 60 (NTSC vsync) | Hz | `FUN_80016B6C` / `VSync(step)` | CONFIRMED |
| Action SM pass | once every `step` vsyncs, `step` = 1..4, usually 2 (about 3 passes in 5 read 2) | vsyncs | `battle-stage-camera.md` "The frame step" | CONFIRMED |
| Every SM timer | drains by `step` per pass, so it is counted in vsyncs whatever the step is | vsyncs | `DAT_1F800393` | CONFIRMED |
| Clip cursor | 12.4 fixed point; `+= (step * speed * rate) >> 1` per pass, speed = 8 normally | 1/16 keyframe | `FUN_80047430` | CONFIRMED |
| Clip length | `4 * N / R` (or `4 * (N-1) / R` to the cursor cap) | vsyncs | same | CONFIRMED |
| Hit beat B fires at | keyframe `B - 1`, i.e. `4 * (B-1) / R` | vsyncs from clip start | `FUN_801EC3E4` `0x801EC45C..0x801EC484` | CONFIRMED |
| Next swing of a string starts at | keyframe `B + 3` (gate `+0x76 == 0`), i.e. `4 * (B+3) / R` | vsyncs from clip start | `FUN_80047430` `0x800478EC..0x80047948` | CONFIRMED |
| Party approach (walk) speed | Vahn 42, Noa 44, Gala 28, Terra 48 | PSX units per vsync | walk entry `+0x0C` | CONFIRMED |
| Walk loop | Vahn 24, Noa 24, Gala 32, Terra 18 | vsyncs per cycle | walk entry | CONFIRMED |
| Last clip of the action | plays to its natural end before the action may finish | - | state `0x20` | CONFIRMED |
| Target reaction hold | until the target is back on idle (or kneeling on clip 8 for a party member, or faded out for a monster) | - | state `0x20` `0x801E54FC..0x801E5580` | CONFIRMED |
| Done band countdown | **60** (`0x3C`); **150** (`0x96`) with the level-up banner | vsyncs | state `0x50` / `0x51` | CONFIRMED |
| HP readout settle (party targets only) | drains in 20..30 passes (600 HP in 20, 9999 in 30) | passes | `FUN_80047430`, `FUN_801E7250` | CONFIRMED |
| End-of-action gate to next dispatch | `0x5A -> 0x0A -> 0x0C -> 0x14`: about 3 passes | about 6 vsyncs | state table | INFERRED (pass count confirmed, step typical) |
| Monster defeat fade | lanes `0x100 -> 0` at 8 per pass per `dt` | about 32 vsyncs | `tint_sm.rs`, seat word `0x10040100` | INFERRED |
| Hero death chain | Knockdown (4) -> entry 7 -> entry 8, entry 8 **loops** | see section 10 | `FUN_8004AD80` tag ladder | CONFIRMED |
| Round-open name labels | `0x5A` (90), or `0x78` (120) after an advantage roll | vsyncs | command flow `0x0A` / `0x0B` | CONFIRMED |
| Battle entry camera sweep | counter `0x80..0xC0` | about 64 vsyncs | `FUN_80046A20` | CONFIRMED |
| Victory: signal -> results frame | 80 (CD streaming bound) | vsyncs | measured once, `rim_elm_gimard_victory` | CONFIRMED (one capture) |
| Victory: results hold | `0x100` = 256 | vsyncs | `FUN_8004E568` | CONFIRMED |
| Victory: exit fade then exit | about 66 (v336 -> v402) | vsyncs | same capture | CONFIRMED |
| Victory: signal -> battle exit | 402 | vsyncs | same capture | CONFIRMED (one capture) |
| Defeat: signal -> exit | about 322 (256 hold + about 66 fade), no CD wait | vsyncs | port reading of the same sequencer | INFERRED |
| Game over | silent return to the title screen (CARD continue), no GAME OVER art | - | `battle-round-loop.md` | CONFIRMED |
| Juggle (flinch) window | `4 * beat / rate` at speed 8 | vsyncs | `actor[+0x1F7]` | CONFIRMED |

---

## 1. The clock

**What the battle ticks on.** The vsync, at 60 Hz. Nothing in the battle is denominated in a 30 Hz "frame": the frame driver
`FUN_80016B6C` measures how many vsyncs the last frame cost, stores that as the **frame step** `DAT_1F800393` (1..4, floor 1 in
battle) and calls `VSync(step)`. The action state machine `FUN_801E295C` therefore runs **once per `step` vsyncs** (typically
every 2, so about 30 Hz), and every timer it keeps drains by `step` per pass. The wall-clock length of any timer is therefore the
same at any step; only the granularity changes. (`battle-stage-camera.md`, "The frame step"; `battle-action.md`, "Notes for the
engine port".) **CONFIRMED.**

**Clip time.** A clip is a stream of keyframes (`frame_count`, `rate` in the action entry head `+0x78`, 1 or 2 on the disc). The
anim tick advances a 12.4 fixed-point cursor, 16 units per keyframe:

```
cursor += (step * actor[+0x21D] * entry[+0x78]) >> 1       // actor[+0x21D] = 8 in normal play
        = 4 * rate  per vsync                              // 16 units = 1 keyframe
```

So a clip plays at `rate / 4` keyframes per vsync: **rate 1 = 4 vsyncs per keyframe (15 keyframes/s), rate 2 = 2 vsyncs per
keyframe (30/s)**. A clip of N keyframes lasts `4 * N / R` vsyncs (the RE also stamps a cursor cap of `(N-1) * 16`, so the
natural end is `4 * (N-1) / R`; the two differ by at most 4 vsyncs). `actor[+0x21D]` is the speed scale: 8 normally, **4 or 2 for
every actor** while an Art plays (section 7), 0 is a freeze. The same formula is `UBattleFormulas::ClipFramesToSteps`
(`ceil(N * 32 / (speed * rate))`). **CONFIRMED.**

**How our logic frames map.** One logic frame is one vsync (`UBattleSubsystem::LogicStep` = 1/60 s). That is the right unit and
should not change. What we do not copy is the SM's 2-vsync pass quantisation: it only decides on which vsync a state change is
noticed (up to `step - 1` vsyncs late) and the original's own numbers are all in vsyncs, so a fixed 1/60 step is a faithful
superset.

---

## 2. Action life cycle

States of `FUN_801E295C` (`battle-action.md`, State table). Addresses are in the battle overlay (PROT 0898).

| # | State | What happens | How long | Tag |
|---|---|---|---|---|
| 1 | `0x00` begin, `0x0A` pre-action wait, `0x0C` seed | per-action reset; `0x0A` polls `FUN_8003F2B8(1)`, a CD-drive seek poll (not a gameplay gate); `0x0C` builds the queue, recentres the formation (`FUN_801EFE44`: the attack's target goes to the stage origin) | one pass each | CONFIRMED |
| 2 | `0x14` face target | turn to the target, range check `FUN_8004E2F0`; in range goes straight to `0x1E`; a party attacker stages the walk entry (`+0x1DA = 1`) and goes to `0x19`; a monster with a tag-`0x20` entry goes `0x15 -> 0x16 -> 0x17 -> 0x18` | one pass | CONFIRMED |
| 3 | `0x19` short step (all party attackers, and the monsters without a tag-`0x20` entry: all but six) | holds **until the range law reads in range**; no timeout, no movement code in the state | distance-bound | CONFIRMED |
| 4 | the approach itself | not the SM: it is the **walk clip's root motion**, `facing x entry[+0x0C] x step x 8 >> 15` per pass = `speed` units per vsync, applied only while the range poll fails (`FUN_80047430` `0x80047D20..0x80047E18`) | `(distance - reach) / speed` vsyncs | CONFIRMED |
| 5 | `0x1E` strike loop | stages **one queue byte per swing**; the next byte is read only when the previous swing's latch (`+0x1DC` bit 1) is clear, so swings pace one per clip | see below | CONFIRMED |
| 6 | `0x1F` recovery wait | waits for the last staged swing to commit | until the commit | CONFIRMED |
| 7 | `0x20` return | waits for the attacker's **last clip to end** (`actor[+0x1D9] == 0`), **then the target's reaction hold** (section 3) | clip + reaction | CONFIRMED |
| 8 | `0x50` cleanup | Final Heal revive, re-order the round, seed the countdown `ctx[+0x6D8] = 0x3C` (`0x96` with the level-up banner) | one pass | CONFIRMED |
| 9 | `0x51` fade-down | counts the 60 down by `step` per pass, but **only on passes where the HP readout of a party target has settled** (`FUN_801E7250`); a monster target never holds it | 60 vsyncs | CONFIRMED |
| 10 | `0x5A` end-of-action gate | clears per-actor flags, scans for a wiped side (raises the battle-end signal), else advances the turn cursor | one pass | CONFIRMED |
| 11 | `0x0A` / `0xFF` | next actor, or the round boundary | - | CONFIRMED |

**Swings inside a string.** Each swing is its own clip. While the previous swing is in flight (`+0x1DC` bit 1 set) the loop does
nothing. A swing entry whose gate byte `+0x76` is 0 (every default hero swing and almost every monster attack) lets the
next staged swing **commit mid-clip once the cursor frame is past the entry's first event beat by more than two**: keyframe
`B + 3`, i.e. `4 * (B+3) / R` vsyncs after the clip started. Hits that would fall after that point are lost. The *last* swing
plays to its natural end. The first swing of an action, staged over the idle, waits for idle frame 3 (the idle entry has no
beat, so `0 + 3`). **CONFIRMED** (`battle-actor-rendering.md` "When a staged clip commits"; `formats/monster-animation.md`).

Concrete numbers for the party's default swings (read from the disc): beat 7 (Vahn arms, Noa arms), clip 25 keyframes at rate
2 (23 for the kicks). So the hit lands at keyframe 6 = **12 vsyncs** in, the next swing may start at keyframe 10 = **20
vsyncs** in, and a lone swing ends at about **48..50 vsyncs**.

**Reach snap.** Every time the acting actor commits a clip while the SM is in `0x1E`, the commit nudges it forward (or back)
by `trig >> 10` (4 units at full scale) until the range law reads "just in range", so every swing of a string starts at the
edge of reach whatever the previous knockback did (`battle-action.md`, "Where an action leaves its combatants"). Our
`FollowTarget` is the equivalent. **CONFIRMED.**

**Nobody walks home.** There is no return leg; an action leaves its combatants where they stopped. **CONFIRMED** (bug 6).

**Hand-off to the next actor.** The gap between one actor's action ending and the next one starting is **not fixed**; it is the
sum of waits, in order:

1. the attacker's last clip finishing (state `0x20`, first hold);
2. the **target's reaction** finishing (state `0x20`, second hold; section 3);
3. the Done band countdown, a fixed **60 vsyncs** (150 with the level-up banner), which also waits for a party target's HP
   readout to finish draining (normally already done by then);
4. about 3 SM passes (`0x5A`, `0x0A`, `0x0C`) before the next actor's `0x14`.

Then the next actor's own approach starts. There is no other fixed delay and no camera wait. So the *minimum* gap is about 60 + 6
= **66 vsyncs (1.1 s)**, and it grows with the target's reaction (a Gimard knocked down adds about 160 vsyncs). **CONFIRMED** for
1 to 3, **INFERRED** for the 6 in 4 (three passes at the usual step of 2).

---

## 3. Waiting on reactions

**Does the next action wait for Knockdown -> Down -> GetUp?** **Yes.** State `0x20` does not hand over to the Done band until the
target's reaction is over. The hold (`battle-action-helpers.md`, "The state-`0x20` reaction hold", `0x801E54FC..0x801E5580`)
reads the **target** `s8` (the actor at the attacker's `+0x1DD`) and keeps the band while **all three** tests are true:

| Test | Releases when |
|---|---|
| target's committed clip `+0x1D9 != 0` | the target is back on idle (clip 0). A flinch, a block, a knockdown **and its get-up**, all count: the commit chain only returns to 0 at the end of the last one |
| not "party target on clip 8" | a downed party member reaches its kneeling clip 8 (chain 4 -> 7 -> 8) |
| the target's render node is still drawn | a dead monster's defeat fade has walked it to black |

Bypass: a scripted lone monster whose fade raised the `ctx[+0x288]` latch. The test is skipped for a group target code
(`>= 8`), so an all-target move does not hold on any one body. **CONFIRMED.**

So the original has a real condition equivalent to "the target is back to ready". It is **not** "every combatant idle": only the
action's own target (and the attacker's own clip) is checked. Bystanders are never waited for; they only get hit when they are
a target.

How long each case holds, counted from the *landing* of the last hit (durations in section 10; monster figures are medians
over the 179 monsters in `DT_EnemyActions`, all **INFERRED** from confirmed lengths):

| Target / case | Hold |
|---|---|
| Hero, flinch (tag 2/3) | the flinch clip: Vahn 32, Noa 36, Gala 36 vsyncs |
| Hero, block (tag `0x0B`) | Vahn 32, Noa 28, Gala 28 |
| Hero, knocked down (HP > 0) | Knockdown + Get-up: Vahn 32 + 50 = **82**, Noa 56 + 60 = **116**, Gala 38 + 56 = **94**, Terra 50 + 50 = 100 |
| Hero, killed | Knockdown + entry 7, released on reaching entry 8: Vahn 32 + 40 = **72**, Noa 56 + 88 = **144**, Gala 38 + 56 = **94** |
| Monster, flinch | median 54 (tag 2), 46 (tag 3); range 26..120 |
| Monster, block | median 46; range 26..120 |
| Monster, knocked down | Knockdown + Get-up, median **119** (range 84..240); Gimard 160, Gobu Gobu 120 |
| Monster, killed | Knockdown (median 60, range 22..180) + defeat fade (about 32) |
| Monster with no get-up entry (29 of 179) | never knocked down by a melee hit unless it dies: the melee kernel flinches instead (`actor[+0x1F2] == 0`) |

Knockdown is chosen by the quarter rule on the last connecting hit of the combo (already implemented, bug 7). Later hits of a
combo that land on a target already reacting simply **stage another reaction over it**: there is no reaction guard in the clip
commit (`battle-actor-rendering.md`, "One staged-anim channel"), so each flinch restarts, and the hold is measured from the
last one. **CONFIRMED.**

---

## 4. Death

### Monster

1. The killing hit (the last connecting hit of the action) stages the knockdown (tag 4). It plays to its end.
2. At the next commit the clip ladder sees the previous entry tagged 4 at HP 0: it **re-installs that entry held on its last
   frame**, runs the spoils, sets the render state `+0x21C = 2` (defeat fade) and the root-motion latch. A body in this state
   **holds the knockdown's last frame**; it does not loop and does not play a separate death clip. (`battle-actor-rendering.md`,
   "Monster death.") **CONFIRMED.**
3. The defeat fade (`FUN_80050120` arm 2): each colour lane steps toward 0 by `8 * dt` per pass, and the body draws
   additively (mode `0x81000000`, so it ghosts out). A monster is seated with all three lanes at `0x100`
   (`0x10040100`), which gives `0x100 / 8` = 32 passes of `dt` vsyncs = **about 32 vsyncs**. **INFERRED** (a capture reads lanes
   `0xF0` mid-fade, consistent). While it fades it also **sinks** by `(record[+0x1F] * step) >> 2` units a pass (class 14..48, so
   3..12 units per vsync) and is clipped at the floor, so it goes down through the stage and is gone once wholly below. **CONFIRMED.**
4. The body leaving (node colour 0) is the third reaction-hold test, so the action ends only then. Total: knockdown + about 32.
5. A monster's KO taunt (tag `0x22`) plays instead when an attack downs a *party member* while someone still stands.

### Hero

1. Hit to 0 HP: the melee kernel stages the knockdown (tag 4). At its commit the ladder sees HP 0 on a party seat and stages
   **entry 7** (`+0x1DA = 7`), then entry 7's commit stages **entry 8** (`tag 7 -> +0x1DA = 8`).
2. Entry 8 carries tag 8: its commit sets the root-motion latch (`+0x1DC` bit 3) and **re-commits itself at every natural
   end**. Nothing ever stages idle on a dead hero, so it plays entry 8 **forever, as a loop**, not a held frame
   (`battle-actor-rendering.md`, "The commit's clip-tag ladder": "entry `8` re-commits itself at every natural end with the latch
   raised"; `formats/anm.md`). The speed field of entry 8 is 0 in every file: it is an in-place kneel, not a slide. **CONFIRMED.**
3. This answers **bug 17**: the original's downed hero keeps animating; `Down` must **loop**. Its length on the disc: Vahn 10
   keyframes at rate 1 (a 40-vsync cycle), Noa 15 at rate 1 (60), Gala 7 at rate 1 (28). Terra the wolf has empty streams for
   entries 7 and 8.
4. The next actor does not go until the hero reaches entry 8 (state `0x20`, second test): Vahn 72, Noa 144, Gala 94 vsyncs after
   the hit. The loop then runs for the rest of the battle. **CONFIRMED.**
5. A fallen hero is revived (Final Heal etc.) through the defeat fade's party arm: the body goes black, is put back in its place
   and the state resets (`tint_sm.rs`, `party_fade_done`).

### The end of the fight

The wipe test is **only** at state `0x5A`, after the 60-vsync Done band, never mid-action (bug 16). So the order after a last
kill is: the rest of the swing, the target's knockdown, the defeat fade, 60 vsyncs, then the battle-end signal. **CONFIRMED.**

---

## 5. Round flow

| Phase | What happens | Time | Tag |
|---|---|---|---|
| Battle load and entry sweep | camera rises and pulls in while the counter runs `0x80 -> 0xC0` (steps `0x30` y / `0x40` z per pass); the battle tick `FUN_801D0748` starts after | about 64 vsyncs | CONFIRMED |
| Name labels, flow `0x0A` / `0x0B` | party plates and the formation banner; intro timer `ctx[+0x6D6]` | **90** vsyncs, **120** when `ctx[+0x290] != 0` (an advantage was rolled) | CONFIRMED |
| Back attack | the party loses its first command phase outright (`0xFE`) | - | CONFIRMED |
| Round start `0x14` | one-frame setup (`FUN_801D88CC` sweep), then the `Begin` / `Run` prompt `0x1E` | none | CONFIRMED |
| Command phase | the player's time; the action SM is idle | unbounded | CONFIRMED |
| Hand-over `0x6E -> 0xFE` | the flow SM arms the action SM (`ctx[7] = 0`) | none | CONFIRMED |
| Execution | one action at a time by the highest unspent initiative key (`FUN_801DABA4`), each as in section 2 | sum of actions | CONFIRMED |
| Round end `0xFF` | `ctx[+0x06] = 0x14`, round counter +1, status waker; the next round's prompt opens at once | none | CONFIRMED |

Between rounds there is **no fixed delay**: the round boundary is one pass and the `Begin` prompt appears immediately. The only
fixed delays in the whole flow are the 90/120 name hold, the 60-vsync Done band after each action, and the end-of-battle
sequence. **CONFIRMED.**

**Victory.** After the last action's Done band, `0x5A` raises the battle-end signal; the SM stops and the results sequencer
`FUN_8004E568` runs (`battle-round-loop.md`, "Battle end, retail's way"). Measured once on `rim_elm_gimard_victory` (vsyncs from
the signal):

| t | Event |
|---|---|
| 0 | signal raised, win-pose framing starts |
| 0..80 | CD loads (hero voice clip, level-up bank); a hardware wait, **not game design** |
| 80 | results frame: XP / gold / drops / level-ups applied, result window up, win pose staged |
| 80..336 | hold, `gp+0xA54` counts to `0x100` = **256** |
| 336 | exit fade spawned (black, `0x40` frames) |
| 402 | battle exits (`ctx[+0x6CE] >= 0x43`) |

**CONFIRMED** (one capture; the 80 varies with the CD). The win pose is played by the hero whose action ended the fight (the
state `0x5A` victory arm, `0x801E6688..0x801E676C`: a living acting actor keeps the pose slot, a dead one re-rolls a living
party member, the Songi fights force a slot; the capture had the leader acting last), from a table keyed by the HP quarter tier,
round count and a weak-status mask.

**Defeat.** The same sequencer lands on its last phase at once (no CD loads), shows the loss window, holds `0x100` and fades,
flooring the party's HP at 1 on the fade frame: about 256 + 66 = **322 vsyncs** from the signal. **INFERRED** (not captured). A
scripted loss (story flag 0) returns to the field; an ordinary loss goes to the title screen with the cursor on CONTINUE after a
short menu-overlay load, with **no GAME OVER screen** (the mode-18/19 overlay is unreachable).

---

## 6. Juggle and reaction windows, knockback

**Juggle window.** The byte `actor[+0x1F7]` is re-derived every frame as `(cursor_frame < first event beat of the playing
clip)`. A hit that lands while it is up adds one to the juggle counter; one that lands after resets it to 1. For a flinch the
window is therefore `beat` keyframes: `4 * beat / rate` vsyncs at speed 8 (and `32 * beat / (speed * rate)` in general). Heroes:
Vahn flinch/block beat 3, Noa 2, Gala 4, Terra 8/5, all at rate 1 except Terra's flinch (rate 2): **12 / 8 / 16 vsyncs** for the
three. The flinch also opens the window for the block roll's override. **CONFIRMED**, and already implemented
(`UBattleFormulas::ReactionWindow`, `HeroReactionBeats`).

**Knockback.** Two terms, both from the *reaction clip's own entry*, along the defender's facing (away from the attacker):

- **Root speed** (entry `+0x0C`, negative on reactions): `speed` units per vsync, **continuously, for the whole clip**, with no
  range test (a negative speed goes straight to the step, `0x80047D20..0x80047E18`). Total = `speed * clip vsyncs`.
- **End step** (entry `+0x0E`): at the clip's natural end the live position jumps by this many units. The clip's own keyframes
  carry the matching recoil in model space (the decoder tweens the final interval), so visually it is part of the clip, not a
  second slide. **CONFIRMED** (formula); the totals below are **INFERRED**.

| Case | Root speed | End step | Total slide (units / cm at 0.433) |
|---|---|---|---|
| Vahn flinch (tag 2 / 3) | 0 / -1 | -159 / -115 | 159 / 143 units (69 / 62 cm) |
| Vahn knockdown | -4 | -1 | about 113 units (49 cm) over 28 vsyncs |
| Vahn block | -4 | -1 | about 113 units (49 cm) |
| Noa flinch (2 / 3) | 0 / -1 | -130 / -105 | 130 / 137 units (56 / 59 cm) |
| Noa knockdown | -3 | -1 | about 157 units (68 cm) over 52 vsyncs |
| Gala flinch | -4 | -1 | about 129 units (56 cm) |
| Gala knockdown | **-12** | -1 | about **433 units (187 cm)** over 36 vsyncs |
| Terra knockdown | -2 | -1 | about 97 units |
| Gimard knockdown (bug 14 note) | -8 | - | about 620 units over 78 vsyncs |

So the original's knockdown slides the body **across the whole fall** (28..78 vsyncs), not in a 12-frame glide, and for some
actors it is large. The attacker's reach snap closes the gap again at the next swing. Our current model (`FlinchKnockback` 45,
`KnockdownKnockback` 120, `BlockKnockback` 20 cm over `KnockbackFrames` 12) is a rough substitute; the per-clip speed and end step
are not in our tables yet (section 9, item 8). Caution before using the table as is: if the Unreal clips have the recoil baked
into the bone translations, adding the end step as a second slide would double it. Check on a real clip first.

---

## 7. Returning to idle after an action or an Art

How the original hands a one-shot clip back to the idle loop (relevant to **bug 18**):

1. **Idle is staged id 0 and loops by re-committing.** At every natural end of a clip, `FUN_80047430` calls the commit
   `FUN_8004AD80` with whatever is staged in `actor[+0x1DA]`. If that is the idle id it plays idle; since nothing re-stages
   anything, the same entry commits again at *its* natural end, so it loops seamlessly. **CONFIRMED.**
2. **A swing hands back by itself.** The attacker's committed id `+0x1D9` returns to 0 as soon as its last clip reaches its
   natural end; state `0x20` waits for exactly that (`actor[+0x1D9] != 0` holds the band), and state `0x50` additionally raises
   the exit-to-idle flag (`+0x1DC` bit 2, `0x801E5E9C`) on the attacker, which makes the tick replace whatever is staged with 0
   at the next natural end. The flag is how every reaction gets back to idle too (a hit reaction's commit sets bit 2). Which
   store puts the 0 behind the *last swing* (the queue's `0x00` terminator being the idle id is the natural reading) is
   **INFERRED**; the observable, a return to idle without any forced step, is **CONFIRMED**.
3. **The action waits for that.** State `0x20` does not leave until the attacker's `+0x1D9 == 0`. There is no "force idle" step in
   the SM; the Done band's poses (`FUN_801D5854(actor, 6/7/8)`) are *camera programs*, not clips. **CONFIRMED.**
4. **Arts add slow motion.** A Special starter (`0x1A`) sets every actor's speed scale to 0 (freeze) except the acting one (2); an
   Art constant (`>= 0x1B`) sets all to 4 (2 under the special mode flag). The scales return to 8 on the pass the acting
   actor's last art clip ends (`FUN_801E93C8`, called from the shared tail of states `0x1E`, `0x1F`, `0x20` and `0x50`), so a
   monster an Art just killed plays the rest of its knockdown at **normal** speed. **CONFIRMED.**

What the RE does **not** give is the cause of Noa's freeze: the original has no engine-side crossfade layer to lose. What it does
say is where to look:

- The action must not finish before the Art's last clip has actually ended (a fixed `TotalFrames` that is shorter than the real
  clip at game speed 4 or 2 cuts it; a longer one leaves a held last frame).
- Whatever scaled the other actors' (and Noa's) play rate for the slow motion must be **restored** at the end, exactly as
  `FUN_801E93C8` does. A rate left at 0, or at the starter's value, looks exactly like a frozen pose with the right clip.
- Idle must be requested through the one path that loops by itself (the crossfade instance), after the mesh is back in
  animation-blueprint mode.

These are hypotheses to test, not RE facts (**NOT FOUND**).

---

## 8. Camera

The camera **never gates the action flow**. The SM runs every pass regardless of where the camera is; the camera is recomputed
from the live actors each display frame (an ease-out chase, about a sixth of the remaining gap per pass, still closing after 12
frames). The only camera-linked times are:

| What | Time | Gates? |
|---|---|---|
| Entry sweep | about 64 vsyncs | yes, the first battle tick (start of the name labels) |
| Name labels | 90 / 120 vsyncs | yes, the first prompt (it is a flow state, not the camera) |
| Action framing (case 6 / 8) | follows the action | no |
| Done band framing | the 60-vsync countdown | the countdown is the gate, the camera follows it |
| Summon and capture casts (`0x32..0x38`, `0x6E..0x71`) | the module's own length | yes, but that is Phase 8 |
| Results framing | the 256 hold | part of the victory sequence |

**CONFIRMED** (`battle-stage-camera.md`).

---

## 9. Compared with our implementation

What we did **before the change list below** (from `JRPGBattleResolver.cpp`, `BattleSubsystem.cpp`, `JRPGBattlePawn.cpp`,
`BattleTypes.h`, `FJRPGTimelineDefaults`; the "Ours" column and this list are that old state, the verdict column and the list under
the table say what is done now):

- Logic clock 1/60 s, `StepFrame` per fixed step. An action's `TotalFrames = LeadFrames (30) + sum of steps + TailFrames (24)`;
  the next action is planned the instant `EndAction` runs (`Pump`), at the same frame.
- Hit frames: `ClipFramesToSteps(Beat - 1)`; monster next swing at beat + 3; hero plain swing hit from `DT_HeroSwings`, cut at
  `SwingCutFrame` 20.
- The target's reactions (flinch, knockdown, get-up, dying) are played by the **pawn on wall-clock timers**
  (`ReactionTimer`, clip length / play rate); the resolver does not know how long they last.
- After the fight is decided `FinishDelayFrames` = 90 frames pass before `EndBattle`.
- Hero death: `PlayReaction(RoleDying, false, RoleDown, true)`, i.e. `Down` held on its last frame.
- Return to idle: `FinishAction()` at `Elapsed >= TotalFrames` calls `PlayIdle()`.

Item by item against the original:

| # | Topic | Ours | Original | Verdict |
|---|---|---|---|---|
| 1 | Clock | 60 Hz logic frame | 60 Hz vsyncs, SM every `step` | **matches**; keep |
| 2 | Clip rate math | `ClipFramesToSteps`, `HitStepForBeat` | `4 N / R`, beat at keyframe `B - 1` | **matches** |
| 3 | Approach | fixed `LeadFrames` 30, lerp to the stop point | constant speed (42/44/28/48 units per vsync) until the range law reads in range; for an example 1150-unit gap about 27 vsyncs for Vahn and 41 for Gala | close for Vahn/Noa, **Gala is too fast**; low priority |
| 4 | Next swing of a string | monster: beat + 3 (**matches**); hero plain swing: `SwingCutFrame` 20 for all | keyframe `B + 3` per swing: Vahn arms and kicks 20, Noa arms 20 and kicks 16 / 18, Gala arms 24 and kicks 16 / 24 | **done** (item 5): hero `CutFrame` derived from the beat |
| 5 | Action tail | `TailFrames` = 24 after the last clip | none; the last clip plays out, then target reaction, then **60** | **done** (item 3) |
| 6 | Wait for the target's reaction | none (resolver); pawn plays it on its own timer | the next action waits (state `0x20`) | **done** (items 1, 2; bug 19) |
| 7 | Gap to the next actor | 0 frames | 60 + about 6 vsyncs, plus the reaction hold | **done** (item 2) |
| 8 | Win/wipe check | at `EndAction` | at `0x5A`, after the 60 | **done** (item 2) |
| 9 | `FinishDelayFrames` | 90 after the decision | killed monster's knockdown + fade (about 90) + 60 are already inside the last action; then a 256 hold with the win pose (the results screen) | **done** (item 7; the win pose itself is not played) |
| 10 | Hero death pose | `Dying` then `Down` held | knockdown, entry 7, then entry 8 **looping** | **done** (item 4; bug 17) |
| 11 | Monster death | holds the last Knockdown frame | holds it, **fades additively to black over about 32 vsyncs while sinking**, and the action waits for it | **done** (item 6; the darkening needs a material parameter, the sink and the wait do not) |
| 12 | Knockback | per-hit constant over 12 frames | per-clip speed over the whole clip + end step | approximation; see section 6 (**deferred**, the optional part of item 8) |
| 13 | Idle hand-off | forced `PlayIdle()` at `TotalFrames` | idle is staged id 0; action waits for the last clip's natural end | **done** (item 9; bug 18's cause was not reproduced) |
| 14 | Slow motion | per-step `Speed` for the actor's own clip | every actor's scale 4/2 during an Art, restored at the art clip's end | **partly**: the target's reaction plays and is awaited at the speed in force when it was hit; it does not go back to 8 at the Art clip's end |
| 15 | Juggle windows | `ReactionWindow`, `HeroReactionBeats` | `4 * beat / rate` | **matches** |
| 16 | Name labels / entry sweep / round gap | scene `HoldLogic`; nothing between rounds | 90/120 + about 64 before round 1; nothing between rounds | **matches** in effect |
| 17 | Reactions on wall-clock timers | `GetWorldTimerManager` | vsync-driven | **done** (item 8) |

### The change list, in order (all nine done; not built or run)

1. **Reaction lengths as data on the combatant (bug 19).** Add to `FJRPGCombatant` (`Public/Battle/BattleTypes.h`) a small struct
   of logic-frame lengths: `FlinchFrames`, `BlockFrames`, `KnockdownFrames`, `GetUpFrames`, `DyingFrames` (hero entry 7),
   `DeathFadeFrames` (monster, 32). Fill it in `UBattleSubsystem::MakeEnemyCombatant` from the `DT_EnemyActions` rows of tags
   2/3/4/5/`0x0B` (`ClipFramesToSteps(FrameCount, 8, Rate)`) and in `MakeHeroCombatant` from a new hero table: add
   `DT_HeroReactions` (or columns on `DT_HeroSwings`) to `Extras/generate_hero_battle_csv.py`, using the figures in section 10.
   Logic must stay deterministic and geometry-free, so the numbers come from data, never from the pawn's clip.
2. **A post-action hold in the resolver (bug 19).** In `FJRPGBattleResolver::EndAction`, do not `Pump()` / `CheckFinish()` at
   once. Compute `State.NextActionNotBefore = State.Frame + max(reaction hold of the target) + DoneBandFrames (60) +
   PassGapFrames (6)`, with the reaction hold taken from the *last hit that landed on it*: knockdown + get-up (or flinch, block,
   Dying, knockdown + fade for a death), at the **speed in force when it was hit** (an Art at speed 4 doubles it until the art
   clip ends). In `StepFrame`, when no action is active, only `Pump()` once `State.Frame >= NextActionNotBefore`. Emit `ActionEnd`
   where it is now (the attacker is free), so the pawn can go idle. Run `CheckFinish()` at the end of the hold, not before.
   Add `DoneBandFrames` and `PassGapFrames` to `FJRPGTimelineDefaults`. Update the tests that assume a zero gap
   (`JRPGBattleTests.cpp`, the ones using `D.TailFrames`).
   **Done (1, 2).** `FJRPGBattleReactionLengths` on `FJRPGCombatant::Reactions` (monsters from `DT_EnemyActions` in
   `MakeEnemyCombatant`, heroes from the new `DT_HeroReactions` in `MakeHeroCombatant`; the generator reads each hero's `record[0]`
   and reproduces the table of section 10 exactly). Missing data falls back to `FJRPGTimelineDefaults::Hero*Frames` (Vahn) /
   `Enemy*Frames` (the disc medians). Each hit sets `ReactionHoldUntilFrame` on its target (scaled by the game speed of the hit, in the
   event as `GameSpeed`); `EndAction` sets `FinishCheckFrame` and `NextActionNotBefore` on the state, `StepFrame` runs `CheckFinish()`
   at the first and `Pump()` from the second. One refinement of the text above: the win/wipe test is at the end of the Done band,
   and only the next action waits the extra `PassGapFrames`. Tests: `JRPG.Battle.PostActionHold` (flinch, knockdown + get-up,
   victory and defeat timing, an Art's slow motion).
3. **Drop `TailFrames` to 0 and use the clip's end.** The original has no tail; the last clip's natural end is the end of the
   attacker's part. Set `FJRPGTimelineDefaults::TailFrames` = 0 (and the enemy equivalent), leaving `DoneBandFrames` as the
   fixed pause. The pawn's `Recovery` phase is the rest of the last clip.
   **Done (3).** `TailFrames` defaults to 0 (the property stays, for tuning); the monster timeline shares it, and `ArtTailFrames` is
   the tail of an Art's *clip* when it has no markers, not a pause after the action. The hero's last swing plays `Timing.Frames` (the
   clip) or `SwingFrames` (48; the disc has 50 for arms and 46 for kicks) to its end.
4. **Hero `Down` loops (bug 17).** In `AJRPGBattlePawn::ShowEvent` (Death, hero) the call is `PlayReaction(RoleDying, false,
   RoleDown, true)`; the last argument holds the final frame. Add a loop mode to the follow clip (a `bLoopFollow`, played with
   `PlayClip(Clip, true, Rate)`), keep `bDown` true so `ReturnToIdle` never fires, and let `FindRoleClip` supply the rate. The
   monster stays a hold. `Dying` is the disc's entry 7 (Vahn 40, Noa 88, Gala 56 vsyncs).
   **Done (4).** Not as a flag of `PlayReaction` but as a reaction chain (`AJRPGBattlePawn::StartReaction`, steps with an end mode
   `Idle` / `Hold` / `Loop`): a dying hero plays Knockdown -> Dying -> Down looping.
5. **Hero `CutFrame` from the beat.** In `ComposeHeroTimeline` (`Swing` case), when the swing has `HitBeats`, use
   `Cut = UBattleFormulas::ClipFramesToSteps(Beat + 3, Plan.Speed, Plan.BeatRate)` instead of `Scaled(Defaults.SwingCutFrame, ...)`.
   Keep `SwingCutFrame` as the fallback.
   **Done (5).** Also after the Chain Cut marker (which still wins) and before `SwingCutFrame`. Test: `JRPG.Battle.HeroCut`.
6. **Monster death fade and sink.** In the pawn, after the Knockdown's last frame, fade the body (additive, lanes 0x100 to 0, 32
   vsyncs) while sinking `size_class / 4` units per vsync (the original adds `(record[+0x1F] * dt) >> 2` a pass), then hide it; `DeathFadeFrames` is the resolver's share (item 1).
   **Done (6).** The Death event carries the fade length in `Value`; `BeginDeathFade` / `UpdateDeathFade` sink the pawn and drive
   the scalar material parameter `DeathFadeParameter` (1 -> 0), then hide the actor. A lane colour fade in additive mode is not
   something the plugin can do without editing materials, so the darkening only appears on materials that have that parameter.
7. **`FinishDelayFrames`.** With the hold in place the 90 is redundant for the killed monster. Replace it by the pose hold the
   results screen wants (the original shows the win pose for 256 vsyncs before the fade; the 80-vsync CD wait is not needed). The
   party-wipe equivalent is about 322 vsyncs. Keep it a `UPROPERTY` and make the number explicit.
   **Done (7).** `UBattleSubsystem::VictoryHoldFrames` = 256 and `DefeatHoldFrames` = 322 replace `FinishDelayFrames`. The 322 is the
   original's wipe sequence, including its own fade-out; our scene fade comes after it. The leader's win pose is not played yet. The
   pawns run on a presentation clock (`UBattleSubsystem::GetPresentationFrame`, +1 per logic step while not paused, also during the
   Commands stage and this hold; the value of `OnLogicFrame`), not on the resolver's `State.Frame`.
8. **Reaction timers on logic frames.** Replace `ReactionTimer` (wall clock) in `JRPGBattlePawn.cpp` by a logic-frame deadline
   checked in `UpdatePlayback` / `OnLogicFrame`, so pause, hitches and the resolver's hold agree. Optional: add the per-clip root
   speed and end step (section 6) to the hero table and to `DT_EnemyActions` (the monster entry `+0x0C` / `+0x0E`, not exported yet)
   and replace `KnockbackFrames` = 12 with a slide over the clip's own length.
   **Done (8), except the optional part.** `ReactionTimer` and `GetWorldTimerManager` are gone from the pawn: reaction chains end at
   logic-frame deadlines checked in `UpdatePlayback`. The per-clip root speed / end step and the slide over the clip's length are
   **deferred**: they need new columns in `DT_EnemyActions` (monster entry `+0x0C` / `+0x0E`), the real recoil may already be baked
   into the clips (section 6's warning) and it changes the feel of the fight, so it should be decided on a real clip.
9. **Idle hand-off (bug 18).** In `FinishAction`, instead of forcing idle when `Elapsed >= TotalFrames`, compare with the playing
   clip's real remaining length (log when they differ), restore any slowed play rate, and request idle through the crossfade
   instance. Test with Noa's Arts first. The RE says the original never cuts the last clip: the action ends after it.
   **Done (9).** `FinishAction(LogicFrame)` compares the action's end with the last clip's real end (asset length / play rate in logic
   frames), logs when they differ by more than 2 frames, and lets Idle wait up to `MaxIdleHandoffWait` (30) frames for the clip.
   Slowed play rates cannot be left behind any more: every clip, loop or one-shot, is a request on the one crossfade instance, which
   carries its own rate (see bug 18 for what else changed; the cause of the freeze was never reproduced).

Expected effect on the pacing of a Vahn plain swing against a Gimard (flinch): approach about 30, swing 12 to the hit and 48..50
to the end, the flinch (60) is over before the swing ends, then 60 + 6: about **150 vsyncs (2.5 s)** per action, against about 100
today. Against a knocked-down Gimard (160) it is about **230 vsyncs** (3.8 s).

---

## 10. Clip data (numbers read from the disc)

Party reaction entries of `record[0]` (PROT 0863..0866), keyframes / rate. Length in vsyncs = `4 * frames / rate`. The "beat" is
the entry's first event frame; speed and end step are the signed `+0x0C` / `+0x0E` values.

| Entry (tag) | Vahn | Noa | Gala | Terra |
|---|---|---|---|---|
| idle (0) | 9 / 1 = 36 | 8 / 1 = 32 | 9 / 1 = 36 | 30 / 1 = 120 |
| walk (1) | 12 / 2 = 24, speed 42 | 12 / 2 = 24, speed 44 | 16 / 2 = 32, speed 28 | 9 / 2 = 18, speed 48 |
| flinch (2) | 8 / 1 = 32, beat 3 | 9 / 1 = 36, beat 2 | 9 / 1 = 36, beat 4 | 30 / 2 = 60, beat 8 |
| flinch (3) | 8 / 1 = 32, beat 3 | 9 / 1 = 36, beat 2 | 9 / 1 = 36, beat 4 | 30 / 2 = 60, beat 8 |
| knockdown (4) | 8 / 1 = **32** | 14 / 1 = **56** | 19 / 2 = **38** | 25 / 2 = 50 |
| get-up (5) | 25 / 2 = **50** | 15 / 1 = **60** | 28 / 2 = **56** | 25 / 2 = 50 |
| entry 7 (dying) | 10 / 1 = **40** | 44 / 2 = **88** | 28 / 2 = **56** | empty |
| entry 8 (down, loops) | 10 / 1 = **40** | 15 / 1 = **60** | 7 / 1 = **28** | empty |
| entry 9 | 13 / 1 = 52 (loop window) | 12 / 1 = 48 | 23 / 2 = 46 | 30 / 2 = 60 |
| block (`0x0B`) | 8 / 1 = 32, beat 3 | 7 / 1 = 28, beat 2 | 7 / 1 = 28, beat 4 | 15 / 2 = 30, beat 5 |

Default hero swings (section 2 of the battle file, arms): Vahn 25 keyframes, rate 2, beat 7; Noa 23 / 2 / 7; Gala 18 / 2 / 9;
kicks: Vahn 23 / 2 / 7, Noa 23 / 2 / 5 and 6, Gala 19 / 2 / 5 and 18 / 2 / 9. All gate bytes 0 (so a following swing may commit
at `B + 3`). Entry 9's role (the SM stages literal 9 only in the summon invoke) is unclear; do not call it "defeat".

Monsters (`DT_EnemyActions`, 179 monsters), length in vsyncs: idle median 60 (30..180); walk median 40; flinch tag 2 median 54
(26..120), tag 3 median 46; knockdown median 60 (22..180); get-up median 60 (30..140), present in 150 of 179; block median 46;
knockdown + get-up median 119 (84..240); attack clips 458 rows, median 60 (27..256). Only one monster has no knockdown entry.

---

## 11. Not found / open

- The camera glide lengths per action framing in vsyncs (the chase is described, not timed per case); not needed for gating.
- Monster root speeds (`+0x0C`) and end steps (`+0x0E`) per clip: documented as fields, not exported in `DT_EnemyActions`. They can
  be read from PROT 0867 with the RE tools if the slide is to be exact.
- The defeat sequence's own timeline was never captured (section 5 derives it from the shared constants).
- Whether the monster defeat fade lane really starts at `0x100` for every seat (the seat word is `0x10040100`; one capture reads
  `0xF0` mid-fade).
- `FUN_8003F2B8(1)` in state `0x0A` is a CD poll; its duration on hardware is irrelevant here.
- The cause of Noa's freeze after an Art (bug 18): not derivable from the RE; section 7 lists what to test.
- How hits on a hero who is already on the floor are paced (a dead hero cannot be targeted by melee; the RE shows no case).
- The party-side `entry 9` meaning.
