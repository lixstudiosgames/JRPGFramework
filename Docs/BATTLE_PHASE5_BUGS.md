# Battle phase 5 — bugs to fix (PR #14)

Bugs found while testing the basic combat, fixed in one batch (nothing built or run: the C++ was reviewed by eye, the editor was
not available — **everything marked "re-test" needs a PIE fight**). Add new ones at the end.
After this batch: **Spirit** (the command-gauge boost; core of the fight, not Phase 7).

RE = the `legend-of-legaia-re` clone (`docs/subsystems/battle-*.md`, `docs/reference/glitches/turn-order-and-arts.md`,
`crates/engine-core/src/world/battle/loop_driver/hits.rs`). Disc numbers below were read from the user's own extraction; only the
few constants the code needs went into the repository.

Status key: **Fixed** / **Partly fixed** / **Open**.

## Found

1. **Knockdown returns straight to Idle.** `AJRPGBattlePawn::PlayReaction(Knockdown, false)` sets a timer to
   `ReturnToIdle`, and there is no GetUp role. It should go Knockdown → (Down) → GetUp → Idle.
   - Heroes: `AS_<Name>_Battle_Knockdown`, `_Down` and `_GetUp` exist (Vahn has all three).
   - Monsters: tag 5 (`GettingUp`) in `DT_EnemyActions`.
   - **Fixed (re-test).** `PlayReaction` now chains a follow-up role: a knockdown that does not kill plays Knockdown → GetUp → Idle
     (`GetUp` is tag 5 for monsters, `AS_<Name>_Battle_GetUp` for heroes; a missing clip is skipped). A hero that dies plays Dying
     and then holds `Down`; a monster holds the last frame of its Knockdown. RE: `battle-actor-rendering.md`, "The commit's clip-tag
     ladder" (tag 4 at HP > 0 stages the get-up; at HP 0 a party seat goes 7 then 8, i.e. Dying then Down). Files:
     `JRPGBattlePawn.cpp/.h`.
2. **The hit comes late after the attack clip (~1 s).** This is probably because the DTs were not imported.
   Without `DT_EnemyActions` a monster uses the fallback hit frame (30), which has nothing to do with the clip.
   - With the table, the timing checks out: Gimard Attack1 (beat 7, rate 2) hits 12 logic frames, or 0.2 s,
     into the clip, which plays at 15 fps × rate 2.
   - **Re-test with the DTs imported.**
   - Heroes still use the default hit frame (`FJRPGTimelineDefaults::SwingHitFrame`, 12), because the party
     clips have no `UJRPGAnimNotify_Hit` markers.
   - **Monsters: open, no code change, as decided** — re-test with `DT_EnemyActions` imported.
   - **Heroes: fixed (re-test; needs the `DT_HeroSwings` import).** The hit beats of the hero swing records are now data: the plain
     swing's hit falls at logic frame `ceil(32 (beat − 1) / (8 × rate))` (the same conversion as the monsters), from the swing record of
     what is equipped (`DT_HeroSwings.HitBeats`/`Rate`; see 13). Disc values: beat 5..9 at rate 2 for the default swings (Vahn 7 in all
     four, Noa 7/7/5/6, Gala 9/9/5/9), up to 11 for clubs/axes and 29 (!) for Vahn's Astral Sword, i.e. 8..56 logic frames. The old
     default 12 is exactly Vahn's default swing. A swing with 2 hits (Vahn's Meta 6, Gala's Ozma 4..7 and Ra-Seru Club, Noa's Terra 8) now lands 2 hits. `UJRPGAnimNotify_Hit` markers on the clip, when they exist, still win over the table; a missing
     table or row keeps `SwingHitFrame`. The monsters' lateness is not affected.
3. **Warning `CoreSubsystem: AddGold ignorado — quantia inválida (0)`** whenever a victory pays 0 gold. Skip
   `AddGold` when the amount is 0.
   - **Fixed.** `UBattleSubsystem::PayRewards` only calls `AddGold` when the amount is above 0.
4. **Instant re-encounter.** In TestMap a new 4-enemy encounter started about 3 s after a Victory. The fix is a
   grace period after a battle, plus handling the field enemy and the encounter zone.
   - **Fixed (re-test; the cause is inferred from the code, not reproduced).** There already was a 3 s grace, but it counted from
     `EndBattle`, ~2 s before the field is shown again, and the zone put the other enemies back at a random point that could be
     next to the player (they would then chase and touch him as soon as the grace ended, which matches "about 3 s"). Now: `UBattleSubsystem::NoteFieldReturned` (called by the scene when the field is shown) starts
     `PostBattleGraceTime` (5 s); `TryStartEncounter` refuses and field enemies do not detect or chase during
     `IsInEncounterGrace()`; an enemy that keeps touching the player when the grace ends triggers then (Think checks the overlap,
     since the overlap event does not repeat); `AJRPGFieldEnemy::ResetToHome` and the zone's respawn pick a floor point **away from
     the player** (`GetRandomFloorPointAway`, farther than `DetectRadius + 200`). Files: `BattleSubsystem`, `JRPGBattleSceneSubsystem`,
     `JRPGFieldEnemy`, `JRPGEncounterZone`.
5. **`JRPG.Battle.End victory` with living enemies pays 0 EXP and 0 gold**, because only defeated enemies count.
   Decide whether the debug end should count every enemy.
   - **Fixed (decided: pay every enemy).** `UBattleSubsystem::EndBattleForDebug(Result, bPayEveryEnemy)`; the console command uses it for
     `victory`. A normal victory still counts only the defeated.
6. **The attacker never walks back to its seat.** The roadmap says "nobody walks back". Does the original return
   to its place? (question for the user)
   - **Fixed (decision: kept, RE confirms).** The original does not walk back: `battle-action.md`, "Where an action leaves its
     combatants": "An action does **not** return its combatants to their authored formation seats ... `tick_battle_locomotion`
     drives exactly one leg - the approach - and no walk-home leg at all", and "there is no recover backstep". Capture evidence: party
     member and monster 300 apart and far off the formation after an exchange. Nothing changed in the logic; the pawn header and the
     roadmap ("Staying in the arena") now cite it. What the original does instead is the attacker re-closing the distance at every
     swing (see 14).
7. **Every later hit of the same combo is also flagged "knocked down"** (Evil Fly: hits 3 and 4). Check against
   the RE whether a combo keeps knocking down a target that is already down.
   - **Fixed.** RE: `hits.rs`, `melee_reaction_entry` (port of `FUN_801EC3E4` `0x801EE380..0x801EE3B4`): the quarter rule
     (`combo total > max HP/4`, or `HP − total < max HP/4`, on someone with a get-up entry) is only evaluated when `kill_check` is on,
     i.e. on the hit that **writes the combo total** (the last one that connects). Hits before it only flinch. The resolver now
     evaluates the quarter rule only on the last connecting hit of the action against that defender
     (`IsLastConnectingHit`: no later hit that the target's height class lets through); a killing hit still knocks down. Test
     `JRPG.Battle.Knockdown`. File: `JRPGBattleResolver.cpp`.
8. **A refused command only shows in the log, so the fight looks stuck.** Example: Gala's chain of 3 costs 90 and
   the gauge has 80 (2 buttons). Show the refusal, and why, on screen next to the prompt.
   - **Fixed.** `UBattleSubsystem::OnCommandRefused(Slot, Why)`; the scene shows "Command refused for Gala (slot 2): a cadeia custa 90 e
     o gauge tem 80" in red (key 7422) and shows the prompt again (key 7421, now 60 s) next to it; a slot change clears it. The
     console commands also put their own failures (unparsed buttons, "not waiting for commands") on screen. The reason text is the
     resolver's (Portuguese, like the other log messages).

9. **Noa's idle freezes after an Art.** When the action ends she stays on a frozen frame instead of looping the
   Idle.
   - **Partly fixed (re-test; root cause not reproduced).** Suspect: a one-shot clip is played with `Mesh->PlayAnimation(Clip, false)`,
     which switches the mesh to single-node mode and drops our crossfade anim instance; the looping Idle that follows then went
     through the single-node path. `PlayClip` with `bLoop` now puts the `UJRPGFieldEnemyAnimInstance` back (`SetAnimInstanceClass`)
     when the current instance is not it, i.e. the same state the fight starts in, which loops by the engine's own tick records. If
     Noa still freezes, the next suspects are her Art clip asset (a last frame held by `RateScale`) and the leader-pose pieces.
   - **Still happens (PIE, 2026-11-05).** Noa stays frozen after an Art. See 18 for the second attempt.
10. **Walking in place.** The approach plays the walk clip even when the target is already within reach, so the pawn
    walks without moving. Skip the Advance phase and the walk clip when `ApproachTo == ApproachFrom`, and check the
    other cases too.
    - **Fixed.** `BeginAction` only walks when there is something to walk: `LeadFrames > 0` **and** the planar distance is above
      `MinWalkDistance` (40 cm). A shorter approach (or none, or an arena clamp that left it in place) goes straight to the spot
      and to Windup; the Advance phase and the walk clip are skipped. The walk is also never re-played for the per-step re-close
      (14), which is a short slide under the swing clip.
11. **Too many blocks (enemies and party) and too many knockdowns.** The formula may be off. Check the
    `RollBlock` inputs (SPD/ATK, power byte, the reaction-window override) and the knockdown rule (¼ of max HP in
    one action, or HP under ¼) against the RE, and compare with a real fight.
    - **Partly fixed.** Checked against the RE (`battle-formulas.md` "Block roll"; `hits.rs` `roll_block`): `RollBlock` already
      matches (SPD + ATK·4/5 with the unfolded ATK, the max, the `BLOCK` scalar by power byte, Spirit and Art ×3/2, the two gates —
      has a block clip and still stands — before any draw; the angle/distance terms are the known departure). The reaction-window
      override also matches the RE (`in_block && reacting` keeps blocking, another reaction cannot block). What was off:
      (a) **knockdowns** — every hit after the ¼ was flagged; fixed with 7 (quarter rule on the last connecting hit only);
      (b) the heroes' reaction windows were all Vahn's (beat 3): the disc has flinch and block beat 3 / 2 / 4 at rate 1 for Vahn /
      Noa / Gala, now `UBattleSubsystem::HeroReactionBeats`. **Still open:** whether the block/knockdown *rates* feel right needs a
      real-fight comparison (the formula gives a ~25% block chance when the two sides are equal and the attacker's swing scalar is
      20, and almost none when the attacker is much stronger; a hero blocking a monster is rare by the numbers). Please re-test and
      say what rate you saw and against whom.
12. **The approach stops too far away.** The attacker should end closer to the target (`ReachDistance` is 220 for
    now). Tune it per size, or take it from the original.
    - **Fixed (re-test; needs the `DT_Enemies` re-import).** From the original: the range law `FUN_8004E2F0`
      (`battle-action-helpers.md`, "battle range / reach metric"): in range when the planar distance between the attacker and the
      target's centre is below `{256, 384, 1024}[class]` PSX units if the size class is under 3, else `class << 4`, where the
      class is the monster record's byte `+0x1F` (the target's for a hero, the attacker's own for a monster); heroes add an offset
      `{+43, 0, −53, −100}` (Vahn, Noa, Gala, Terra; positive shortens). The disc's classes are 14..48, so the thresholds are
      224..768 units = 97..333 cm at 0.433 cm a unit (a Gimard, class 18, is 125 cm; the old fixed 220 was 508 units). New column
      `DT_Enemies.SizeClass` (`FJRPGEnemyData`, `FJRPGCombatant::SizeClass`; the CSV and `generate_enemies_csv.py` /
      `monster_block.py` updated; 0 = unknown falls back to class 22, the most common). `AJRPGBattlePawn::ReachDistance` is now 0 by
      default (the law); > 0 forces a fixed distance. `StopMargin` (8 cm) stops just inside the threshold like the original's last
      walking step.
13. **Low hits land on a flyer (Vera, `TargetClass` 2).** In the original, 5 plain Low swings on Vera all miss.
    Every Low swing misses a flyer, not just the Arts.
    - Our plain swing bytes are wrong: `0x1D` for Low (and `0x18` for High) are ≥ `0x16`, which always connects.
    - The plain Low swing must fall in the class-2 miss range (`0x11..0x15`).
    - Find the real swing power bytes in the RE, for High / Low / Arms / Ra-Seru.
    - Very likely a plain High also misses class 3 (short: frogs, mushrooms). Check it.
    - Fix `UBattleFormulas` and the roadmap ("party swing default power bytes"), plus the `JRPG.Battle.PowerBytes`
      test.
    - Also confirm that the in-editor `DT_Enemies` row has `TargetClass` 2.
    - **Fixed (re-test; needs the `DT_HeroSwings` import).** The byte is not a constant: it is the first byte of the swing record
      that the **equipped item** brings into the battle file (`turn-order-and-arts.md`, "Side Kick's first hit misses floating enemies
      even with War God Boots"; `battle-action-queue.md`: "a swing entry's power byte 0 is equipment-spliced"). New table
      **`DT_HeroSwings`** (`FJRPGHeroSwingData`, `Docs/Data/DT_HeroSwings.csv`, 114 rows), generated from PROT 0863..0865 by
      `Extras/generate_hero_battle_csv.py` (run against your disc; only the CSV is in the repo). One row per hero + item + button;
      `UBattleSubsystem::ApplyHeroSwings` (called by `MakeHeroCombatant`) fills `FJRPGCombatant::SwingBytes` / `SwingProfiles` from the
      equipped weapon (Arms), the Ra-Seru tier `seru_N` (Ra-Seru) and the shoes (Low, High), falling back to the `default` row of the
      button, then to `UBattleFormulas::DefaultSwingPowerByte` (also used when the table is missing). The row's `Cost` also fills the
      button cost; `DT_WeaponCosts` stays and is the fallback for an item with no row. Checked against the RE's Gala table (none / Power
      Shoes / Fighting Boots Low `0x13` High `0x0E`; War God Boots `0x1D` / `0x18`; Ra-Seru Thongs `0x13` / `0x18`) and against the
      other heroes' list (Vahn: Triumph and Ra-Seru Boots Low `0x1D`, Ironman..Hero Boots High `0x0E`; Noa: no free Low at all, High `0x0E`
      with Electric / Tempest / Steel). So a plain Low always misses a flyer with the default boots, and a plain High misses the
      frogs/mushrooms for Noa and Gala. Tests: `JRPG.Battle.PowerBytes`, `HeroSwings`, `ArtsInFight`, `KernelWiring`. Not in the table
      (no `DT_Items` key): Feral (Noa), Mace (Gala) and an unnamed Vahn weapon id 0x1A, which use the default row. Also: the repo
      `DT_Enemies.csv` has `vera*` and `evil_fly` at `TargetClass` 2; the **in-editor** DataTable must be re-imported/checked by the user.
14. **The fight gets crowded.** In the original, each hit the player lands seems to push the target back a little.
    Confirm the knockback per hit in the RE (distance and direction) and implement it, clamped by the arena.
    - **Fixed (approximation; tune in the editor).** RE: `battle-action.md` ("The negative speeds in the player files are the
      reactions' - knockdown, block, some flinches - so a struck actor slides back"): the push is the reaction clip's own root speed
      (entry `+0x0C`, negative, per vsync at game speed 8) plus the end-of-clip step (`+0x0E`), along the defender's facing, i.e.
      directly away from the attacker (it faces the attacker). Measured: hero flinches end 105..159 units back (Vahn −159 / −115, Noa −130 /
      −105 → 45..69 cm; Gala by speed −4); knockdown speed −4..−12 (Vahn −4, Noa −3, Gala −12, Gimard −8); block −4 with a
      −1 step; Gimard's flinch −4. The per-clip numbers are not in our data, so the pawn uses per-hit constants
      (`FlinchKnockback` 45 cm, `KnockdownKnockback` 120 cm, `BlockKnockback` 20 cm, sliding over `KnockbackFrames` = 12
      logic frames, clamped by the arena: whole distance, else half, else none). The scene calls `ReactToHit` on every Damage / Block
      event. To keep the combo in reach the attacker re-closes the distance at the start of every step, as the original's commit
      does ("reach snap", same page), over `FollowFrames` = 6 logic frames. If you want the exact numbers, the per-clip speeds
      and end steps would have to be added to `DT_EnemyActions` and the hero data.
15. **The defender does not turn to the attacker.** On a hit (and a block), the defender must turn to face the
    attacker. This applies to both sides: an enemy hit from behind stays turned away, and a party member stays
    facing wherever it stopped.
    - **Fixed.** `AJRPGBattlePawn::ReactToHit` turns the defender (yaw only) to the attacker on every Damage and Block event, in the
      same call as the knockback. The attacker also re-faces its target at the start of each step.

16. **The target dies mid-chain and the rest hits someone else.**
    - What happened: Noa, aiming at one enemy, killed it with 2 swings. The Art that followed played on the spot
      of the dead enemy, but its damage went to another enemy that was not the target.
    - Logic and picture disagree: the resolver retargets the remaining hits, while the pawn stays on the dead one.
    - Check in the RE what the original does when the target dies mid-chain: cut the rest of the action, or
      retarget and move to the new target.
    - Then make the pawn and the logic agree.
    - **Fixed, the original's way (decision: no cut, no retarget; re-test).** RE: the original does neither. An action's target is
      fixed when it starts (`actor[+0x1DD]`; `FUN_801DB124` only runs at dispatch, off a target that was already dead). Each hit adds to a
      combo accumulator and **HP is written once, on the last connecting hit**, as `max(HP − total, 0)` (`apply_combo_total`,
      `FUN_801EC3E4` `0x801EE9F8..0x801EEA78`); the block roll is skipped once the accumulated total reaches HP; the wipe is only tested at
      the end of the action (state `0x5A`, `battle-action.md`). So a 99-hit combo lands all 99 hits, the excess is lost, nothing goes to
      another enemy, **also when the target is the last enemy** (victory comes when the action ends). Now: every remaining hit resolves on
      the same target; HP is clamped at 0 per hit and `ComboDamage` keeps the unclamped sum; the target only falls (Death,
      `KillCombatant`, the knockdown) on the last connecting hit (`IsLastConnectingHit`), flinching until then; `EndAction` finishes any
      pending fall as a safety net. The cuts (`KillCombatant`, `ResolveHit`, `CutRestOfAction`, `KillTailFrames`) are gone. The retarget
      before an action starts (target already dead) is unchanged. **Combo summary**: the original's cluster reads `N HIT` / `TOTAL sum`
      (`battle-hud.md`: each landed hit adds to `DAT_8007BD14` and restarts the cluster; it closes at the `0x51` teardown; a one-hit action
      shows `DAMAGE` instead). New event `ComboTotal` (`Count` = landed hits, `Value` = the damage of every hit, excess included), emitted
      at the end of the action for each target that took a hit. The scene shows it like the original's cluster, on screen with fixed keys
      (a new line replaces the old one, the next action clears it): **nothing during the combo** (each hit only shows its damage over the
      target, as before), and at the end "Hit 11" over "Damage 1234"; a one-hit action shows only "Damage X" (the original's `DAMAGE`).
      Placeholder styling (yellow / orange `AddOnScreenDebugMessage`, 10 s) to be polished. Blocked and missed hits do not count. Tests: `JRPG.Battle.TargetFalls`, `Combo`, `ArtsInFight`.

### Second test pass (PIE, 2026-11-05) — animation polish

These are visual only. The logic is fine; the animation sequencing needs to be put together properly.

17. **The defeated hero's pose is frozen.** Vahn and Noa, once dead, stay kneeling on a still frame.
    - The `Dying -> Down` chain holds the last frame of `Down` (`PlayReaction(RoleDying, false, RoleDown, true)`).
    - The original's downed hero keeps animating while kneeling. `Down` should loop, not hold.
    - Check the heroes' `_Down` clips, and what the original plays for a fallen party member.
    - **Research done (2026-11-05):** the original's downed hero plays Knockdown, then entry 7, then entry 8, and entry 8 re-commits
      itself at every natural end, so `Down` loops (Vahn 10 keyframes at rate 1 = a 40-frame cycle, Noa 60, Gala 28; entry 7 is
      Vahn 40, Noa 88, Gala 56 frames). See [`Reference/BATTLE_TIMING.md`](Reference/BATTLE_TIMING.md), section 4 and change list
      item 4.
    - **Fixed (re-test).** A hero that dies now plays Knockdown -> Dying -> Down and `Down` **loops** (a looping `PlayClip` on the
      crossfade instance), instead of holding its last frame; `bDown` still keeps it from ever returning to Idle. A missing `Dying`
      or `Down` clip is skipped (it holds whatever played last). The monster still holds the last frame of its Knockdown, then fades
      (see 19). Files: `JRPGBattlePawn.cpp/.h` (`ShowEvent`, `StartReaction`, `AdvanceReaction`). Content note
      (ARCHITECTURE rule 52): the converter only closes the loop of `Idle,Walk,Move` (`ps1_glb_to_fbx.py --loop-clips`), so the
      heroes' `_Down` clips, which now loop, may pop once per cycle; re-export them with `Down` added to `--loop-clips` if so.
18. **Noa still freezes after an Art** (9 is not fixed).
    - After the action ends she stays on a still frame instead of looping Idle.
    - The `SetAnimInstanceClass` fallback in `PlayClip` did not help. Find the real cause: her Art clips, `EndAction`'s return to
      idle, the leader-pose pieces, or a clip that never hands back to the crossfade instance.
    - **Research done (2026-11-05):** the RE does not explain the freeze, but it gives the original's rules: the action never ends
      before the attacker's last clip reaches its natural end, idle is the staged id 0 and loops by re-committing, and the Art's
      slow-motion scale (4 / 2, a freeze for the special starter) is restored the pass the art clip ends. What to test is in
      [`Reference/BATTLE_TIMING.md`](Reference/BATTLE_TIMING.md), section 7 and change list item 9.
    - **Partly fixed (re-test with Noa and an Art; the freeze was never reproduced, so the cause is a best reading of the code).**
      What was wrong in our code, checked against the three hypotheses of the timing doc: (a) *the forced idle cut or outlived the
      clip* - the pawn went idle at the data's `TotalFrames` whatever the clip was doing; (b) *a slowed rate left behind* - the
      Art's slow play rate lived on the mesh (`SetPlayRate`); probably not the cause, since the idle call set the rate again, but
      it is now only ever part of a clip request; (c) *idle not requested through the
      crossfade instance* - every one-shot clip (swing, Art, reaction) went through `Mesh->PlayAnimation`, which swaps the mesh to
      single-node mode and destroys the crossfade instance, and the way back (`SetAnimInstanceClass` in the middle of the fight)
      rebuilt it, a path that the engine refuses (with a log warning only) if it is reached while the animation is updating.
      What changed: **one-shot clips now play on the same `UJRPGFieldEnemyAnimInstance`** (new `bLoop` argument
      of `PlayClip`: a clip that plays once and holds its last frame; asking for a one-shot again restarts it, so a flinch over a
      flinch still restarts) - the mesh never leaves animation-blueprint mode, there is nothing to rebuild on the way back, and the
      play rate is part of each request, so it cannot outlive its clip. The pawn's hand-off (`FinishAction`) now compares the
      action's end with the real end of the last clip it played: if the clip still has up to `MaxIdleHandoffWait` (30) logic frames
      to go, Idle waits for it (the original never cuts the last clip); otherwise it goes at once. **When the two disagree by more
      than 2 frames the log says so**, e.g. `BattlePawn Noa: a ação 5 acabou no quadro 412, mas o clip ... ainda tocaria por 9
      quadro(s)` / `... já tinha acabado e segurava o último quadro` - if Noa still freezes, that line (or its absence) tells which
      asset or timeline is wrong. Files: `JRPGBattlePawn.cpp/.h` (`PlayClip`, `PlayStep`, `FinishAction`, `UpdatePlayback`),
      `JRPGFieldEnemyAnimInstance.cpp/.h` (`PlayClip(..., bLoop)`, one-shot tick records, restart serial).
19. **No time for the fall and the get-up before the next attack.** When a hero or an enemy is knocked down, the next combatant
    attacks right away. It should wait for the knockdown and the GetUp clip to play before the next action starts, like the
    original.
    - The logic timeline does not count the target's Knockdown + GetUp length. Add that hold, in logic frames, at the end of the
      action, or before the next action, when someone is down.
    - Same for a hero who falls and gets up.
    - In general, sequence the reactions properly (fall, down, get-up, idle) so actions don't overlap them.
    - **Research done (2026-11-05):** confirmed, the original waits. State `0x20` holds until the target is back on idle (a hero
      who is killed: until it reaches clip 8; a killed monster: until its defeat fade is over), then a fixed 60-vsync Done band
      (150 with the level-up banner), then about 3 SM passes, then the next actor. Knockdown + get-up lengths: Vahn 82, Noa 116,
      Gala 94 vsyncs; monsters median 119 (84..240). Full numbers and the implementation plan (reaction lengths as data on the
      combatant, a post-action hold in the resolver, `TailFrames` to 0) in
      [`Reference/BATTLE_TIMING.md`](Reference/BATTLE_TIMING.md), sections 3 and 9.
    - **Fixed (re-test; exact numbers need the `DT_HeroReactions` import).** The fight now follows the original's chronology:
      approach -> swings (each hero swing cut at keyframe beat + 3 of its own record) -> the last clip plays to its natural end (no
      `TailFrames`: it is 0) -> **wait for the target's reaction** -> the 60-vsync Done band -> the win/wipe test -> about 6 passes ->
      the next actor. In detail:
      - *Reaction lengths are data on the combatant* (`FJRPGCombatant::Reactions`, `FJRPGBattleReactionLengths`): monsters from
        `DT_EnemyActions` (tags 2 / 3 / 4 / 5 / `0x0B`, `4 * FrameCount / Rate`), heroes from the new `DT_HeroReactions`. Missing
        data falls back to `FJRPGTimelineDefaults` (hero = Vahn's numbers, monster = the disc medians).
      - *The resolver holds* (`EndAction`, `StepFrame`, `Pump`): every hit records when its target's whole reaction ends
        (`FJRPGCombatant::ReactionHoldUntilFrame`: flinch, block, knockdown + get-up, hero knockdown + Dying, monster knockdown + death
        fade), at the speed in force when it hit (an Art at 4 doubles it; `FJRPGBattleEvent::GameSpeed`). `EndAction` emits `ActionEnd`
        (the attacker is free) but only schedules `FinishCheckFrame = max(now, reactions) + DoneBandFrames (60)` and
        `NextActionNotBefore = FinishCheckFrame + PassGapFrames (6)`; the victory / defeat test runs at the first, the next action
        starts at the second. No win/wipe test mid-action or at `EndAction` any more (also bug 16's rule).
      - *End of the fight*: `FinishDelayFrames` (90) is gone; `UBattleSubsystem::VictoryHoldFrames` (256, the results hold) and
        `DefeatHoldFrames` (322) follow the original's sequencer (the 322 is the original's wipe sequence including its own fade-out). The
        win pose of the leader is not played (pawns stay on Idle); our scene's fade-out comes on top of the hold, after it.
      - *Presentation clock*: the pawns do not run on the resolver's `State.Frame` (it only advances in Execution, so reaction chains,
        the death fade, the idle hand-off and the held sounds froze while the players typed commands). `UBattleSubsystem` keeps a
        monotonic `GetPresentationFrame()`: +1 per logic step whenever the game is not paused (Commands stage and the victory / defeat
        hold included), and `GetPresentationOffset()` = presentation frames that elapsed without the resolver advancing. It is the value
        of `OnLogicFrame` (which no longer skips F-1 -> F+1 on the finishing step), it is added to `Event.Frame` before `OnBattleEvent`,
        and the scene adds it to `FJRPGActionTimeline::StartFrame` of the copy it gives the pawn (`ToPresentationTimeline`). The resolver
        and `GetEventLog()` still use the raw rules frames (deterministic, never reading pawn data). Pause and `HoldLogic` freeze everything.
      - *Turn-start condition death*: a kill by a condition at the start of a turn (poison) used to call `CheckFinish()` at once and let
        `Pump` pick the next actor the same frame; it now schedules like `EndAction` (`ScheduleFinishCheck`: reaction hold, Done band,
        finish test, pass gap). Test: `JRPG.Battle.PostActionHold`.
      - *Pawn reactions on logic frames*: the `GetWorldTimerManager` timer is gone; a reaction is a chain (`StartReaction`) whose
        clips end at logic-frame deadlines checked in `UpdatePlayback`, so pause and hitches agree with the resolver. A killed monster
        holds its Knockdown's last frame, then fades and sinks `SizeClass / 4` units per frame for `DeathFadeFrames` and hides
        (`Event.Value` of the Death event carries the fade length). The fade itself is a scalar material parameter
        (`DeathFadeParameter`, default `JRPGDeathFade`, 1 -> 0) that only the materials which have it will show; without it the body
        sinks and disappears.
      - *Hero swings*: `ComposeHeroTimeline` cuts a swing at `ClipFramesToSteps(beat + 3)` of its record's first beat (Vahn arms 20,
        Noa arms 20 and kicks 16, Gala arms 24 and kicks 16..24) when the record is known; the Chain Cut marker still wins, and
        `SwingCutFrame` (20) is only the fallback.
      Files: `BattleTypes.h`, `JRPGBattleResolver.h/.cpp`, `BattleSubsystem.h/.cpp`, `HeroBattleData.h`, `JRPGBattlePawn.h/.cpp`,
      `JRPGBattleSceneSubsystem.cpp`, `Extras/generate_hero_battle_csv.py`, `Docs/Data/DT_HeroReactions.csv`. Tests:
      `JRPG.Battle.PostActionHold`, `HeroCut`, and `Fallback` (no tail). Not done: the per-clip knockback speed / end step (still the
      per-hit constants of 14), and the reaction's slow motion ends with the reaction instead of at the Art's last clip.

20. **The impact sound plays when the swing MISSES.** In PIE, a swing that misses because of the target's height class (tall
    flyers / short targets, `TargetClass` against the power byte) still plays the attacker's impact sound.
    - Cause: the sound is a stock `AnimNotify_PlaySound` on the attack clip, and a notify fires whenever the clip passes it; the
      clip knows nothing about the hit result, which the logic decides at the hit's logic frame.
    - **Fixed (re-test).** `UJRPGFieldEnemyAnimInstance::HandleNotify` now routes the `PlaySound` notifies of an
      `AJRPGBattlePawn` owner to `AJRPGBattlePawn::HandleClipSound`. A sound that falls within the asymmetric window of a hit of the
      step being played (`HitSoundLeadFrames` = 2 logic frames before the hit, `HitSoundLagFrames` = 8 after) is "the impact": it is held until that hit's event for this attacker arrives (the
      scene forwards every Damage / Block / Miss with `NoteHitResult`) - **Damage or Block plays it, Miss drops it** (or plays the
      optional `MissSound` of the pawn); if the notify fires after the event, the recorded result decides at once. Sounds outside
      any hit's window (voices, footsteps, the whoosh at the start), reaction clips and idle play as before, with the notify's own
      volume, pitch, `bFollow` and `AttachName` (the pawn plays them exactly like `UAnimNotify_PlaySound::Notify`). Held sounds are
      dropped if the hit never resolves, when the action ends (a notify that arrives after the end, for a hit that never resolved, is
      discarded) and when the pawn dies; the held-sound deadline counts logic frames, so pause freezes it. Both window values at 0 turn
      the rule off. The action's sound context (step, hit frames, recorded results) outlives `FinishAction` (`bSoundContextActive`)
      until the last clip's tail is over - the idle hand-off frame, the last hit + `HitSoundLagFrames` and the end of the
      `AnimBlendTime` crossfade, whose outgoing clip still fires notifies - so a tail impact sound is still gated by the last hit; it is
      cleared by the next `BeginAction`, the pawn's death and `InitCombatant`. `PlayNotifySound` attaches the sound only when `bFollow`
      is on (UE 5.8; `AttachName` is ignored otherwise). Impact sounds authored later should use `UJRPGAnimNotify_HitSound`, which
      needs no window; on an `AJRPGFieldEnemy` that notify plays its HitSound through `PlayClipSound`.
    - New opt-in notify `UJRPGAnimNotify_HitSound` (`HitSound`, `MissSound`, volume, pitch, follow, attach) for clips authored later:
      place it on the impact frame; it belongs to the nearest hit of the step with no window.
    - Limitation: a visual with its own `AnimClass` (`FJRPGBattleVisual::AnimClass`, not used by the arena today) plays the engine's
      `PlaySound` itself and cannot be gated; only `UJRPGAnimNotify_HitSound` is (it asks the pawn through the mesh's owner).
    - Files: `JRPGBattlePawn.h/.cpp`, `JRPGFieldEnemyAnimInstance.cpp`, `JRPGBattleSceneSubsystem.cpp`, new
      `JRPGAnimNotify_HitSound.h/.cpp`.

## Not bugs (checked)

- Pause stops the logic clock: the frame counter stayed still for 16 s while paused.
- After a Defeat the learned Art is discarded and learned again in the next fight. That is correct, because Defeat
  writes nothing back.
- Monster combos chain several swings by AGL (Gimard: hit + block in the same action).
- Gala has only 2 Arts slots (gauge 80, 30 per button).
- Content warning: `MI_Evil_Fly` is missing the MorphTargets usage flag. Resave the material.

## For the user — re-test and decisions

Re-test in PIE (nothing was run):

- **Import** `DT_Enemies` again (new column `SizeClass`; without it every monster uses class 22 for reach) and check the in-editor
  `vera*` / `evil_fly` rows have `TargetClass` 2. Import `DT_EnemyActions` too (bug 2).
- 1 / 15 / 14: a hit makes the defender turn, slide back and, on a knockdown, play Knockdown → GetUp (heroes: Dying → Down when they
  die). Tune `FlinchKnockback` / `KnockdownKnockback` / `BlockKnockback` / `StopMargin` on the pawn if it feels off.
- 12 / 10: the attacker stops 100..330 cm from the target's centre by its size, no walking in place when close.
- 9: Noa after an Art. 4: win a fight standing next to a field enemy; nothing should start for ~5 s.
- 7 / 11: a long combo on a monster with a get-up clip (only the last hit flags the knockdown), and block/knockdown rates against
  a real fight.
- 13: plain Low on Vera/Evil Fly misses with default boots; High misses frogs/mushrooms with Noa or Gala.
- 16: kill one of several enemies with the first swing of an Art: the Art plays to the end on the body, nothing hits the others, the
  enemy falls on the last hit and "Hit N / Damage X" shows on screen at the end; the same on the last enemy (victory only after the action).

Second pass (animation pacing, nothing was built or run):

- **Import `DT_HeroReactions`** (Row Structure `JRPGHeroReactionData`, `/Game/Data/`, from `Docs/Data/DT_HeroReactions.csv`; 4 rows:
  Vahn, Noa, Gala, Terra, in logic frames at game speed 8, generated from PROT 0863..0866 by `generate_hero_battle_csv.py`). Without it
  the heroes wait with Vahn's numbers (a log line says so); monsters need no new table (`DT_EnemyActions` already has the lengths;
  re-import it only if it was never imported).
- 19 (pacing): one action should now take, for a Vahn plain swing on a Gimard that flinches, about 2.5 s from the action start to the
  next one (approach ~30 + swing ~50, the flinch is shorter than the swing, then 60 + 6); against a knocked-down Gimard about 3.8 s.
  Watch: the next attacker waits until the target is up again; nobody attacks a body that is still falling; after the last enemy
  falls the screen holds ~4 s (256 frames) before the fade, and a wipe holds ~5.4 s (322).
- 17: a dead Vahn / Noa keeps moving (kneeling loop) instead of freezing.
- 18: Noa after every kind of Art. Look at the Output Log for `BattlePawn Noa:` lines (action end vs the real end of the last clip).
- A killed monster: knockdown, then it sinks for ~0.5 s and disappears. The darkening only shows if its material has the scalar
  parameter `JRPGDeathFade` (1 = normal, 0 = black); none do today - add it to the monster materials you want to fade, or change
  `DeathFadeParameter`.
- 20: a Low swing on a flyer (or High on a frog with Noa / Gala): no impact sound on the Miss, the sound still plays on a hit and on a
  block. If a clip's impact sound is not within 2 frames before / 8 frames after the hit frame, raise `HitSoundLeadFrames` /
  `HitSoundLagFrames` (or place a `UJRPGAnimNotify_HitSound` on the impact frame).
- New things to tune (on `AJRPGBattlePawn` unless said): `HitSoundLeadFrames` (2), `HitSoundLagFrames` (8), `MissSound` (none), `MaxIdleHandoffWait` (30),
  `DeathFadeParameter` (`JRPGDeathFade`), `bDeathSink` (on); on `UBattleSubsystem`: `VictoryHoldFrames` (256), `DefeatHoldFrames`
  (322; replace `FinishDelayFrames`); in `FJRPGTimelineDefaults` (`TimelineDefaults`): `TailFrames` (now 0), `DoneBandFrames` (60),
  `PassGapFrames` (6) and the fallback reaction lengths (`Hero*Frames`, `Enemy*Frames`).

Decisions for you:

- **Import `DT_HeroSwings`** (Row Structure `JRPGHeroSwingData`, `/Game/Data/`, from `Docs/Data/DT_HeroSwings.csv`) together with the
  re-imported `DT_Enemies`; without it the plain swings behave as before.
- **Melee pacing:** the heroes' hit frames now follow the disc (8..56 logic frames; Vahn's Astral Sword is slow on purpose, beat 29).
  Say if a swing feels off against its clip; the clips have no hit markers yet.
- **Combo summary** is a placeholder on-screen text (`Hit N` / `Damage X`) at the end of the action; style and position are yours to polish.
