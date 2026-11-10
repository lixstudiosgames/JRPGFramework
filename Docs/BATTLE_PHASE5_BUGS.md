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

### Third test pass (PIE, 2026-11-05)

Bug 20 (miss sound) confirmed fixed in PIE. Worked on in parts.

21. **The body slides back to its old spot when a reaction ends.** After a flinch (and after Knockdown → GetUp) the defender slides
    back correctly, but when the clip hands over to idle the body glides forward again; enemies too.
    - Cause: the reaction clips end with the body displaced from the idle pose (Gimard `Damaged`: every part ~30..45 PSX units,
      13..19 cm, off its start; `Knocked Down` / `Getting Up` cancel each other but neither matches idle exactly). The 0.2 s crossfade
      to idle then pulls the body back. The original moves the actor by the clip's end step (`+0x0E`) instead.
    - **Fixed (part 1, re-test).** `AJRPGBattlePawn::SettleIntoIdle`: on every hand-off to idle (end of a reaction, end of an action)
      the offset of the pose centroid (all bones but the root, component space) from the measured idle pose becomes actor location
      (clamped by the arena) and idle starts with no blend. The idle pose is measured once per pawn after idle has played alone for a
      few frames. Tunables: `bApplyEndStep`, `EndStepMinDistance` (3 cm).
22. **The approach is a lerp over a fixed 30 frames.** A far target is reached almost instantly ("teleport"); a close one walks
    slowly with a run cycle ("skating").
    - **Fixed (part 1, re-test).** The pawn walks at a constant speed, the disc's walk speed for heroes (Vahn 42, Noa 44, Gala 28,
      Terra 48 PSX units per vsync) and `EnemyWalkSpeedUnits` (36, not exported from the disc) for monsters. A walk longer than the
      rules' `LeadFrames` shifts the action's steps and the scene calls `UBattleSubsystem::StallResolver` for the difference: the
      resolver stands still while the presentation clock runs, so the rules and their RNG are unchanged. A short walk arrives early
      and waits on idle (Windup) for the first swing.
23. **Arts: the enemy's hit reactions come late** compared with the Art's animation (plain swings are fine).
    - Cause: no Art clip has `UJRPGAnimNotify_Hit` markers (the log says "sem marcadores de golpe no clip" for every Art), so the
      resolver spaced the hits by `ArtLeadFrames` / `ArtFramesPerHit`, unrelated to the clip (Hyper Elbow: hit at 50% of the clip
      instead of 35%).
    - **Fixed (part 2, re-test; needs the `DT_ArtHits` import after the build).** The Art record in the hero's battle file is a clip
      entry with a 0x24-byte header (`record0[+0x58] + 4 + (constant - 0x10) * 0xD0`): power bytes at `+0x24`, hit beats at `+0x34`
      (one per power byte; the 0x4C impact cues at `+0x78` fall on the same frames), rate at `+0x9C`. New table **`DT_ArtHits`**
      (`FJRPGArtHitData`, 63 rows, generated by `Extras/generate_hero_battle_csv.py`; every hit count matches `DT_Arts`). The
      subsystem's `ClipTiming` uses it when the clip has no markers: hit at `ClipFramesToSteps(beat - 1, speed, rate)`, the same
      conversion as the plain swings. The Miracle Arts (constant 0x1B) have no clip record there and keep the default spacing.
24. **The fight gets crowded**: a boss pushed to the arena's end by knockbacks, with the three heroes piled next to it. Check what
    the RE does.
    - RE (`battle-action.md`, "Where an action leaves its combatants"; `battle-action-helpers.md`; `battle.md` `FUN_80050BB8`):
      four things keep the formation apart, and we had none of them.
      1. **Action-seed recentre** `FUN_801EFE44`: at the start of every action the whole formation is translated so the Attack's
         target sits on the stage origin (any other action: the centre of everyone's box), after squashing an axis wider than
         `0x800` units down to `0x800`. This is why the original never piles up against the stage edge.
      2. **Round-start** span-normalise + recentre `FUN_801DB318` over everyone.
      3. **Make-way** (state `0x14` + SM tail): while a melee attacker walks in, every bystander steps sideways off the attack line,
         to the side it is already on, about `size_class * 8` units.
      4. **Separation** `FUN_80051078` / `FUN_80050BB8`, every frame: two actors closer than `(r1 + r2) / 6` (radius `0x280` hero,
         `0x400` Terra, `size_class << 5` monster) are nudged apart 4 units per ordered pair.
    - **Fixed (part 3, re-test).** All four, on the presentation side (the rules stay geometry-free): `UJRPGBattleSceneSubsystem`
      `PlanFormationShift` (on `ActionStart`, focus = the target of a melee action; on `RoundStart`, everyone), `MakeWay` (when the
      attacker walks), `StepFormation` (the recentre glide and the separation, every frame before the pawns). The recentre is
      instant in the original with the camera following; ours glides over `RecentreFrames` because the battle camera is fixed. Every
      move goes through `AJRPGBattlePawn::TranslateBy`, which also shifts the walk and slide anchors, so it composes with whatever the
      pawn is doing. Tunables on `AJRPGBattleArena` (Formation): `bRecentreFormation`, `RecentreFrames`, `FormationSpanLimitUnits`
      (2048), `bMakeWay`, `bSeparation`, `SeparationStepUnits` (4).
    - **Recentre is instant, like the original (decision, 2026-11-05).** The original moves everyone in one frame and adds the same
      offset to the camera focus ("focus pair"), so it is never seen (Cort capture: everyone moved ~813 units in one frame).
      `RecentreFrames` = 0 (default) does the same; > 0 glides instead. With today's fixed camera the jump **is visible**; the action
      cameras still to come must follow `UJRPGBattleSceneSubsystem::OnFormationShifted(Translation, Frames)` (the formation centre's
      world offset; Frames 0 = instant) to hide it.
    - **Arrival shove** (state `0x16`, `0x801E33EC..0x801E3490`): when the walk to the target closes, the target is pushed along
      the attacker's facing out to the edge of the reach (the original loops while the range law reads "in range"). Only after a
      walk: an attacker already in range skips state `0x16`. `AJRPGBattlePawn::ArrivalShove` / `ShoveBy`, `bApplyArrivalShove`.
25. **Dev menu**: a tab to unlock every Art, and a Battle tab to trigger a fight with chosen enemies and count.
    - **Done (part 4, re-test).** New **Battle** tab (`Content/UI/WebUI/src/sections/90_devmenu.html`, `js/90_devmenu.js`; `shell.html`
      regenerated with `Extras/build_webui.py`) and four dev verbs in `UWebUISubsystem::RunDevCommand`: `battle.start <key[,key...]>
      [count] [level]` (one key repeats `count` times, several keys one each; starts where the player stands and closes the dev menu),
      `battle.end [victory|defeat|escape|abort]` (the debug end; victory pays every enemy), `battle.enemies [filter]` (lists the
      `DT_Enemies` keys) and `arts.learnall [Vahn|Noa|Gala|all]` (every `DT_Arts` row but the Parts, through `UArtsSubsystem::LearnArt`).

#### PIE pass 4 (2026-11-05)

- Art hit timing (23) confirmed right in PIE.
- **Slash Kick: Vahn ends a step forward and the knocked-down enemy gets up inside him.** Cause: the end step of 21 was also applied to
  the attacker at the end of its action, so a lunge drawn in the Art clip became position. The original only moves an attacker by a
  positive root speed while the target is **out** of range, so it never advances into the target. **Fixed:** the end step now applies
  to reactions only (`ReturnToIdle(true)`); the attacker goes back to idle with the normal blend.
- **The recentre with today's fixed camera looks broken.** `bRecentreFormation` now defaults to **off**; turn it on when the action
  cameras follow `OnFormationShifted`. RE check: the Muscle Dome runs ordinary battles on the shared round driver ("nothing in a leg's
  damage, input or ending is dome-specific", `minigame-muscle-dome.md`); its ring is a backdrop that the recentre never moves, so the
  repositioning there is the same.
- **A body "blinks" for an instant when it is repositioned.** Likely cause: the end step moved the actor and asked for idle in the
  same frame, but the pose only changed on the next animation tick, so one frame drew the reaction's last (displaced) frame at the new
  spot. **Fixed (re-test):** `SettleIntoIdle` ticks the animation and refreshes the bones right away.
- Knockdown checked again: matches the RE (`melee_reaction_entry`: `quarter < accum || hp - accum < quarter` on the hit that writes
  the combo total). A low-HP monster (Gimard, 99) goes down on any action over ~25 damage, as in the original.

#### PIE pass 5 (2026-11-05)

27. **Vahn snaps back after Slash Kick; a boss (Viguro) pops from GetUp to idle; enemy attack clips are cut by idle.**
    - Slash Kick: since pass 4 the attacker got no end step, so the lunge drawn in the clip blended back. Now the attacker gets the end
      step too, but **only up to the edge of its target's reach** (or as close as it already was): the original's positive root speed
      only moves while the target is out of range. `SettleIntoIdle(KeepReachOf)`, `ReturnToIdle(bAfterAction)`.
    - Viguro: it has a GetUp clip; the pop was the end step's hard cut to idle (no blend), which shows on a pose far from idle. Now the
      end step slides the actor over exactly the idle crossfade (`AnimBlendTime`): the outgoing clip's offset and the slide cancel
      out (both linear), so the body stays put and the pose blends. This also replaces the forced pose refresh of pass 4 (blink).
    - Enemy attacks: the idle hand-off cut a last clip that still had more than `MaxIdleHandoffWait` (30) frames to go. The original
      never cuts the last clip; the default is now 600 (only a guard against a broken clip).
    - **Fixed (part 1 of pass 5, re-test).**
28. **HP after a fight**: the party gains HP when a fight ends, and a hero who died comes back with 40 HP. In the original a fallen
    hero comes back with 1 HP and nothing is restored.
    - RE: every battle exit floors a downed member at 1 HP and writes the live HP / MP to the record (`battle_exit_party_reset`;
      `battle-round-loop.md`: the wipe arm floors every seat at 1 on the fade frame, which is why a scripted loss returns to the field
      standing); a level-up raises the maxima only, the currents "stand where the fight left them" (`level-up.md`, Noa L2 -> L3:
      164/182 HP becomes 164/221).
    - Causes: (a) `UPartySubsystem::ApplyLevelUp` added the max HP / MP gain to the current values (the gain after a win); (b)
      `UBattleSubsystem::WriteBack` left a hero who died in a won fight at 0 HP, and wrote nothing at all on a Defeat (the 40 HP was the
      pre-fight value); (c) MP spent in the fight was never written back (only MP gains were).
    - **Fixed (part 2 of pass 5, re-test).** Level-up keeps the current HP / MP. `WriteBack` runs on Victory, Escaped **and Defeat**
      (Aborted writes nothing): HP floored at 1, MP up or down (new `UPartySubsystem::SpendMP`); AP and the Arts learned in the fight
      are kept on Victory / Escaped only.

#### PIE pass 6 (2026-11-05) — open, not looked at yet

29. **Some hits do not land on the enemy** (no damage / no reaction where the swing should connect). **Fixed** (user, 2026-11-10:
    no longer happens in play). Suspects at the time:
    the hit timing from `DT_ArtHits` / the approach wait, the arrival shove or separation moving the target out of the swing, or the
    reach clamp of the end step. Note which hero, which Art or swing, and which enemy.
30. **Slash Kick still slides at the very end.** After the pass-5 change (end step slid over the idle crossfade, clamped to the edge
    of the target's reach), Vahn still glides at the end of the Art. Probably the reach clamp: the clip's lunge goes past the edge, so
    the part beyond it blends back. Not investigated.
31. **Gimard ended up inside Vahn again.** Not investigated. Suspects: the target's knockdown / get-up end step moving it toward the
    attacker, separation too slow to undo it, or the arrival shove.
- **29-31 analysed (2026-11-05), still open:** see [`Reference/BATTLE_RE_SWEEP.md`](Reference/BATTLE_RE_SWEEP.md) section 1. 30
  and 31 come from our movement model: the original measures reach and separation on the **body pair** (position + pose
  centroid), backs the attacker off at speed 4 every tick while it is in range, snaps reach **both ways** at every commit and
  ends a clip with the entry's own `+0x0E` step; we measure feet, only snap forward, and invent the end step from the centroid
  with a reach clamp (the clamped part is Slash Kick's slide). 29 is most likely a Miss / Block read as "nothing", or an Art
  whose Unreal clip length differs from the disc's `4 * N / R` (hit frames come from `DT_ArtHits`, clip length from the asset).
  Fix list: section 5.8 of that file.

35. **Bystanders step aside even when the attacker walks far from them.** When a hero or monster walks to its target, the others
    slide sideways although the walk passes nowhere near them. RE (`battle-action-helpers.md` "The make-way step"): the original
    does exactly that, every bystander steps `class * 8` units off the attack line, near or far, with no distance test.
    - **Fixed (step 1a, re-test), our improvement:** `UJRPGBattleSceneSubsystem::MakeWay` moves only a bystander whose body is in
      the attacker's lane (ahead of the attacker, before the point where it stops plus the lane width, and closer to the line than
      the lane's half width) and only as far as it takes to clear the lane (at most the original's `class * 8`). Half width =
      `(attacker radius + bystander radius) / 6` (the separation distance) x `MakeWayLaneScale` (1.25). New on
      `AJRPGBattleArena`: `bMakeWayOnlyInLane` (on; off = the original's behaviour), `MakeWayLaneScale`.

#### Step 1 of the RE sweep: movement (`Reference/BATTLE_RE_SWEEP.md` 10.3), in parts

- **1a. Body location (done, not built).** `AJRPGBattlePawn::GetBodyLocation` / `GetSettledBodyLocation`: the actor plus the
  current pose centroid (the original's body pair `+0x3C` / `+0x40`), switchable with `bUseBodyPair`. Now measured on the body:
  the target side of the reach law (`ComputeStopPoint`, so the walk and `FollowTarget`), the arrival shove, the attacker's aim at
  each step, the defender's turn on a hit (from its body to the attacker), both sides of the separation, and the make-way lane
  (bug 35). `ReachFor` is public now (the scene reads it). What to look at in PIE: attackers stop at the reach of the body they
  see (a monster that leans forward is hit where it is), bodies that a clip carries into each other get pushed apart, and the
  make-way only moves who is in the way. If something looks worse, compare with `bUseBodyPair` off.
- **1b. Spacing during the strikes (done, not built).** `AJRPGBattlePawn::UpdateStrikeSpacing`, every logic frame of an action
  after the walk: (1) the attacker **faces the target's body every frame** (`bFaceTargetEveryFrame`; the original's 0x1E / 0x1F
  stores); (2) the **in-range back-off**: while the target's body is closer than the reach threshold, the attacker steps back
  `InRangeBackoffUnits` (4) PS1 units a frame, scaled by the step's game speed, never past the edge, not during a slide. And
  `FollowTarget` is now the **two-way reach snap**: at each step it goes to the reach threshold of the target's body, forward from
  too far **or back from too close** (`ComputeSnapPoint`; a band of `StopMargin` inside the edge counts as "at the edge", like
  the original's one step either side), over `FollowFrames` (6; 0 = the original's one-frame cut). What to look at in PIE: no
  attacker ends a swing or an Art inside its target; combos stay at the edge; the attacker turns with a target whose knockdown
  carries it sideways. Tunables on the pawn: `InRangeBackoffUnits`, `bFaceTargetEveryFrame`, `FollowFrames`.
- **1c. Clip motion from the disc (done, not built) - closes bug 30.** Every action entry carries a root speed (`+0x0C`, PS1
  units per vsync along the facing) and an end step (`+0x0E`, how far the clip leaves the body from where it began). The
  generators now export them: `generate_enemy_actions_csv.py` (via `monster_block.py`) -> `RootSpeed` / `EndStep` on
  `DT_EnemyActions`; `generate_hero_battle_csv.py` -> the same two on `DT_HeroSwings` and `DT_ArtHits`, and six pairs on
  `DT_HeroReactions` (`FlinchHigh`, `FlinchLow`, `Block`, `Knockdown`, `GetUp`, `Dying` + `RootSpeed` / `EndStep`). Checked against
  `BATTLE_TIMING.md` section 6 (Vahn flinch 0 / -159, knockdown -4 / -1; Gimard knockdown -8, walk 28). Slash Kick is **-7 / +534**:
  it ends 534 units (~231 cm) ahead, which the old reach clamp cut and blended back (bug 30). Hero plain swings are all 4 / 0.
  - Pawn: `BeginClipMotion` on every clip (reaction, step); `UpdateRootMotion` each frame: a negative speed always moves (a
    knockdown slides the whole fall, a block steps back), a positive one only while the target's body is out of reach and only to
    the edge. The end step is walked over the crossfade (`SlideBy`) at the natural end (reaction chain, idle hand-off), pro-rated
    when a clip is cut (a combo's next swing, a hit in mid-flinch), never for a body that died. The reach clamp of
    `SettleIntoIdle` is gone (the in-range back-off of 1b keeps the attacker out of the target). The fixed `FlinchKnockback` /
    `KnockdownKnockback` / `BlockKnockback` and the pose-centroid end step stay only for clips without data (tables imported before
    these columns). Monster walk speed comes from its tag-1 row (`EnemyWalkSpeedUnits` is the fallback). The arrival shove now runs
    only for the six monsters with a tag-`0x20` walk entry. Switch: `bUseClipMotion` on the pawn.
  - **Re-import** in `/Game/Data/`: `DT_EnemyActions`, `DT_HeroReactions`, `DT_HeroSwings`, `DT_ArtHits` (new columns; without
    them the pawn falls back to the old knockback and end step, by design).
  - What to look at in PIE: knockdowns slide back over the whole fall (Gala's ~187 cm, Gimard's ~268 cm), flinches end a little
    back, Slash Kick ends ahead of where it started without gliding back, nobody ends inside a target, monsters walk at their own
    speed.
- **1d. Bug 29 diagnostics (done, not built).** The timing check is a warning in the Output Log, once per clip per session
  (`BattleSubsystem:` lines), in `ClipTiming`: (1) a clip whose `UJRPGAnimNotify_Hit` markers do not match the action's hit count
  (they were ignored silently); (2) an Art whose `DT_ArtHits` hit count differs from `DT_Arts` (its hits fell back to the default
  spacing, away from the clip); (3) an Art clip whose length is not a whole number of disc frames at the row's rate (asset baked at
  another rate: the logic hits land away from the visible impact); (4) an Art hit that falls after the end of the asset. A height
  miss now reads `Miss (height)` over the target. Re-test: play the Arts that "did not land", then read the warnings; a `Miss
  (height)` or `Block` is the rules, not a bug. Bug 29 stays open until a repro is matched to one of these.
- **1e. Arts slow motion for everyone (done, not built).** The original's per-actor rate byte `+0x21D`: an Art clip puts the
  **whole fight** at half speed, a Super / Miracle starter **freezes everyone but the dasher**, and everyone is back at normal
  speed the moment the Art clip ends (so a monster an Art killed finishes its fall at normal speed). The acting pawn reports it
  (`AJRPGBattlePawn::GetWorldSpeedRequest`: 0 during a `SpecialStarter` step, the step's speed during an `Art` clip, 8 otherwise
  and once the clip has ended); the scene hands it to every other pawn each frame (`SetWorldAnimSpeed`), which plays its idle,
  reaction or down loop at that rate (`GlobalAnimRateScale`) and stretches its reaction deadlines and root motion to match
  (`AdvanceWorldTime`). A reaction clip now starts at normal speed and the world speed slows it (instead of the hit's speed baked
  in). Switch: `bSlowWorldDuringArts` on the pawn (off = the old behaviour). The rules' reaction hold is still computed at the
  hit's speed, so after an Art the next action may wait a little longer than the body needs (harmless; `BATTLE_TIMING.md` 9,
  item 14). What to look at in PIE: during an Art the others' idles and the target's reactions run at half speed, everything
  freezes for a Super / Miracle starter dash, and speed snaps back when the Art's clip ends.
- **1f. Crossfade review (done, not built).** Finding: one-shot clips (swings, Arts, reactions) already enter with a hard cut;
  only loops (idle, walk, down) blend over `AnimBlendTime` (0.2 s). The end step of 1c was slid over 0.2 s at every clip change,
  so a hard-cut transition (knockdown -> get-up, a combo's next swing, a hit in mid-flinch) would pop the body back and then
  slide it forward. Now `SlideBy` follows the blend of the clip that enters: a hard cut moves the actor in the same frame the pose
  changes (as the original's commit does), the idle hand-off still slides over the idle's blend. The original has no blend layer
  (only the last keyframe interval is tweened into the next clip's frame 0: 2 to 4 logic frames, ~0.05 s); `AnimBlendTime` keeps
  0.2 for a softer look and its tooltip carries the original's figure. What to look at in PIE: no pop at knockdown -> get-up or
  between combo swings; if idle hand-offs look floaty, try `AnimBlendTime` 0.05..0.1.

- **Codex review of step 1 (2026-11-05): no compile / UHT problem found; six logic findings, all accepted and fixed:**
  (1) a positive root speed added to the reach-snap slide carried the attacker past the edge: positive root motion now waits
  while a slide runs; (2) the last clip's end step (Slash Kick +534) happened after the in-range back-off had stopped, so an
  attacker could stay inside its target: the back-off now keeps running after the action (`bPostActionSpacing`, no re-aim) until
  another action starts (`EndPostActionSpacing` at `ActionStart`) or the pawn is hit; (3) the pro-rated end step of an interrupted
  reaction measured wall-clock time under slow motion: `ClipMotionFraction` now uses the clip's own progress (`MotionProgress`,
  advanced at the world speed for reactions); (4) the idle hand-off slide ignored slow motion and freezes: the slides of a pawn
  that is not acting now run on its local time (`AdvanceWorldTime` pushes `SlideStartFrame`); (5) after `FinishAction` a step
  clip still playing switched to the world speed: step clips keep their own speed to the end (`bMotionOwnSpeed`); (6) a missing
  get-up clip made the knockdown's end step apply twice: the end step is consumed once walked (`ConsumeClipEndStep`).

**Step 1 done (1a-1f, nothing built).** Re-test list: bugs 29, 30, 31, 35 and the PIE notes of 1a-1f above; re-import the four
tables of 1c first.

#### Step 2 of the RE sweep: Spirit (done, not built)

- `EJRPGBattleCommandKind::Guard` is accepted (no target). Rules (`FJRPGBattleResolver`): the stance (`bGuarding`) starts at the
  commit and lasts until the next round start (defence x3, block side x3/2, both already in `UBattleFormulas`); the turn is one
  `EJRPGActionStepKind::Spirit` step with the clip `AS_<Hero>_Battle_Spirit` (no walk, no hit; `SpiritFrames` 60 without the
  clip); it adds `SpiritAPGain` (32) AP at the end of the clip; the next round opens with `min(Max * 7/5 + 8, 288)` gauge. Menu:
  the Down arm (Spirit) commits at once and moves on (Final shows `Vahn | Spirit`); Tactics Defensive = Spirit. Scene: an action
  whose target is the actor itself gets no target (nobody walks or aims). No effect yet (the aura comes with the effects pass).
  Tests: `JRPG.Battle.Spirit` (new), `JRPG.BattleUI` updated. Docs: `Guides/BATTLE.md` section 6.
- What to look at in PIE: Spirit on a hero plays the Spirit clip in place; enemy hits on that hero this round do much less
  damage and are blocked more; the hero's AP goes up by 32; next round the Attack window's gauge is longer (Vahn 160 -> 232);
  the round after, normal again. Tune on `UBattleSubsystem::TimelineDefaults`: `SpiritAPGain`, `SpiritFrames`.

#### PIE pass 7 (2026-11-05) — open, not looked at yet

36. **Long wait before round 2.** Once, the fight took noticeably longer than usual to go from the end of round 1 to round 2's
    command phase; every animation played normally, only the gap was long. **Fixed** (user, 2026-11-10: no longer happens in play).
    Suspects at the time: the rules' reaction hold
    computed at the hit's (slow-motion) speed after an Art (`BATTLE_TIMING.md` 9, item 14: the next action can wait longer than the
    body needs); the idle hand-off wait of the last clip (`MaxIdleHandoffWait`); a monster's death fade holding the round end; the
    approach stall (`StallResolver`) of an action that did not walk. Note which hero / Art ended round 1 and whether a monster died.
- Movement: about 95% right after step 1 (user, 2026-11-05); a few hit / movement glitches remain, to be listed with repros.

#### PIE pass 8 (2026-11-06) — cameras done; fixed, re-test

37. **The per-Art camera cuts too fast; the original has a "slow" on the starter (new Art, Hyper).** RE
    (`battle-action-helpers.md`, "Retail frames stretch while a shout streams"): while the Art's voice is read from the CD (the
    XA shout on a known Art's starter, the fanfare on a new Art / Super / Miracle dash) the PS1's frames stretch and the whole game
    runs about a third slower (Lizard Tail's starter 32 vsyncs instead of 22, the Art 61 instead of 40); camera cuts are in frames,
    so they slow too. **Fixed:** `AJRPGBattleArena::ArtsShoutTimeDilation` (0.67; 1 = off): the scene sets the world's time
    dilation while a hero plays an `ArtStarter`, `SpecialStarter` or `Art` step (rules, animations, camera; voices keep their
    pitch), back to 1 after the step, at battle end and on teardown.
38. **Slime walk sound repeats too fast (overlapping).** The original fires a looping clip's cues every cycle too (each loop is a
    re-commit), but a PS1 voice does not stack; the Slime's walk is 8 keyframes (~0.5 s), shorter than its sound. **Fixed:**
    `AJRPGBattlePawn::bLoopSoundsWaitToFinish` (on): during a looping clip, the same sound from the same pawn waits for the previous
    one to end. One-shot clips (swings, reactions) always play.
39. **The action camera sometimes changes side too often.** Case 7's one-way long arc was re-armed at every shot change, so a combo
    that cut between an Art's own camera and case 7 swung the camera round each time. **Fixed:** the long arc is chosen only when
    the strikes begin (case 6 -> 7 / 8); coming back from an Art's camera, or 7 <-> 8, takes the short way. The per-action side
    flip (the style's half turn, the hero's first-swing coin flip) is the original's and stays.
40. **Fast hits: some miss the enemy's flinch (Vahn on Gimard, Noa too).** The low-hit flinch is tag 3, and Gimard (like many
    monsters) only has tag 2: the reaction found no clip and played nothing. RE (`monster-animation.md`, the `+0x1EF` map):
    each light flinch falls back to the other when absent, and a missing knockdown reuses the light flinch. **Fixed:**
    `AJRPGBattlePawn::FindRoleClip` (and `RoleMotion`) do the same fallbacks.

#### PIE pass 9 (2026-11-06) — fixed, re-test

41. **Miracle too slow (a doubled slow); the Art after a Hyper slow too.** The shout slow of 37 was applied to every Art clip, on
    top of the original's own rate law (RE `battle-actor-rendering.md`, the `+0x21D` byte): the dash freezes the fight, and while
    the starter's latch is up every Art plays the whole fight at 1/4 (a Miracle's six Arts, an Art right after a Hyper); a plain
    Art at 1/2. **Fixed:** the shout slow is now a window of `ArtsShoutSeconds` (1 s real time) from a known Art's starter only, the
    CD read of its voice; the dash and the Art clips keep the original's rates, not doubled. Checked against the RE: Hyper / Super /
    Miracle = dash (others frozen) then Arts at 1/4; Regular = starter at full speed then the Art at 1/2. Ours plays the dasher
    itself at 1/2 where the original's 0x1A arm sets 1/4 (`JRPGBattleResolver` "SpecialStarter: 4"). **Changed (user,
    2026-11-06):** the dash now plays at scale 2 like the original (`ComposeHeroTimeline`, `UJRPGArtSequenceLibrary::ApplyGameSpeeds`).
42. **A newly discovered Art had no special starter.** RE (`battle-action-helpers.md`, the Arts shout): an Art learned on this use
    is staged behind `0x1A` (the SpecialStarter: the dash, the fight frozen, the "NEW" banner), not `0x19`. **Fixed:**
    `FJRPGBattleResolver::AppendArtSteps` uses the SpecialStarter clip and kind for an entry with `bNewlyLearned`.
43. **Taking a hit gave no AP.** RE (`battle-formulas.md`, "Spirit-gauge fill on damage taken", `FUN_801EC3E4`): every hit that
    connects on a hero adds `max(1, damage * 100 / MaxHP)` AP (+1/4 with AP Boost, +1/10 with passive `0x100`), capped. **Fixed:**
    `FJRPGBattleResolver::ResolveHit` adds it and emits `APChanged` for the hero (the passives are a TODO until accessories carry
    them). `JRPG.Battle.Spirit` now expects the AP of the hits taken on top of the 32.

- **User re-test (2026-11-06):** 34b works; 40 works (it was the "movement" glitch: the missing flinch); no movement bug since.
  The rest of pass 8 / 9 is waiting for the next build.
44. **Noa sprinted much too fast on her approach run, once.** Not reproduced. Watch: note the Art / command, the target and whether
    the formation recentre or the make-way ran at the same moment (a slide added to the walk would look like a sprint).

#### Pass 10 (2026-11-06) — after the sounds step (4)

45. **Vahn shakes a little after a flinch** (clearer with motion blur). Cause found: at the flinch's end the end step (Vahn high flinch
    `-159` units) slides during the blend to the idle; the slide moved once per logic frame (60 Hz, coarser in the Arts slow) while the
    pose blend moves every rendered frame, so the body stepped against a smooth blend. **Fixed:** `AJRPGBattlePawn::Tick` moves the
    slide between logic frames on world time (the blend's clock).
46. **Battle music: another part of the track seems to play underneath.** Not found: one BGM component plays at a time (log), the
    cue is `Concatenator(Intro, Loop)` (one child at a time in the engine source), and the render's reverb is the original's Studio C
    (short). **To check:** play `BGM_2026_Battle1_Intro` and `_Loop` alone in the Content Browser: if one of them has it, it is the
    render (the RE engine's sequencer); if not, it is the cue.
47. **Hyper: the voice comes after the animation.** Cause: the XA channels open with ~0.15 s of silence (the CD wait) and the fanfares
    up to 0.31 s, and `FJRPGBattleVoices` added its own 0.15 s on top. **Fixed:** `ps1_battle_voices.py` trims the leading silence (10 ms
    kept), `VoiceLatencySeconds` is 0.05 (tune it on the arena).
48. **A dying monster fades out with no sound.** The RE knows no sound of its own there (the knockdown clip's cry and thud are the
    death sound, and they are notifies). Our addition: `AJRPGBattlePawn::DeathFadeSound` (optional, empty). The fade itself now shows:
    `M_PS1Monster` had no `JRPGDeathFade`, so the body only sank; the material now dithers its opacity mask by it.
49. **Combatants near the camera block the shot.** **Done:** the original's near-camera ghost (`FUN_8004DC68`) in
    `UJRPGBattleSceneSubsystem::UpdateCameraGhost`: during an action, a body (not the actor nor its target) within `dist / 4` of the
    point `dist * 25 / 128` in front of the focus fades to `CameraGhostOpacity` (0.3) through `JRPGCameraGhost` on `M_PS1Monster`
    (dithered mask). Arena: `bCameraGhost`, `CameraGhostOpacity`, `CameraGhostPullBack`, `CameraGhostRadius`, `CameraGhostFadeSeconds`.

- **User re-test (2026-11-06, pass 10):** 45 fixed; the known-Art shout, the block grunt, the field music coming back, the death fade
  (48) all work. 46: the "beats underneath" are in Battle theme 1 only, and the original has them too.

#### Pass 11 (2026-11-06)

50. **Battle music from the user's album** (`D:\JOGOS\PROJETO LEGAIA\AUDIO\MUSIC\OG`, fuller than the game's sequencer). **Done:**
    `ps1_battle_music.py --ost <folder>` takes the album WAV of each track (its number is the sound-test index), finds the loop on it
    (the score's loop start / length, refined by the envelope correlation: the album runs up to ~0.4 % off the score's clock) and
    crossfades the Loop's last 50 ms into what precedes its start (a recording does not repeat sample for sample).
    `DT_BattleMusic` points at `BGM_OST_<id>_<Name>_Intro / _Loop`; the rendered ones were deleted. Found on the way: the loop length
    was converted from ms with the 44-sample envelope hop instead of 44.1, so every rendered loop was 0.23 % short (~120 ms on Battle
    theme 1), a jump at each seam. Fixed for both modes.
51. **Hyper voice still a little late.** The step commit was read before the pawn played the logic frame (the voice started one
    logic frame after the clip). **Fixed:** `CommitActingSteps` runs after `UpdatePlayback`; `VoiceLatencySeconds` is 0 by default.
52. **New Art: after the starter the Art plays too fast, and the starter itself was a little slower in the original.** In the original
    the whole fight slows while the CD reads any voice: on a new Art that is the starter's fanfare and the Art's own shout right
    after it (under the latch). **Fixed:** for a newly learned Regular Art both open the shout slow for their read span. Super,
    Miracle and Hyper still do not (bug 41).
53. **The dying monster makes a sound as it fades into the ground in the original; ours is silent** (user correction: not the
    Seru absorb). Cause, read from the RE (no capture): the monster-death arm of the commit (`FUN_8004AD80`, `0x8004B094..0x8004B6A0`)
    re-installs the knockdown entry held on its last frame, and every commit zeroes the cue cursor `+0x1F6` (`0x8004AFD0`), so the
    next frame's cue walk fires the whole knockdown track at once (the cry and the thud) as the fade starts. **Fixed:**
    `AJRPGBattlePawn::bReplayKnockdownSoundsOnDeath` (on) replays the knockdown clip's `PlaySound` notifies at `BeginDeathFade`,
    skipping a sound equal to the one before it (the router's rule). `DeathFadeSound` stays as an optional extra.
54. **The camera ghost should be see-through, not dithered,** and it should also work on the command / Attack / Spirit screens when
    the camera stands beside an enemy. **Fixed:** with `AJRPGBattleArena::CameraGhostMaterial` set (`M_PS1MonsterTranslucent`), the
    ghosting body swaps its materials to translucent copies (texture and emissive carried over, `Opacity` driven) and gets its own
    back after; without it, the dither stays. `CameraGhostMaterial` is a soft reference whose C++ default is the project's
    `M_PS1MonsterTranslucent`, so every arena (placed or the provisional one) has it; a project without that asset gets the dither. `bCameraGhostInMenus` (on by default, ours): the ghost also runs outside actions,
    sparing whoever the shot frames and the cursor's target (`AJRPGBattleCamera::GetShotSlot` / `GetShotTargetSlot`).
55. **Formation recentre on** (`bRecentreFormation` defaults to true): the battle camera follows `OnFormationShifted`. To test.
56. **The monster under the target cursor is not lit.** The original (`FUN_801DA6B4`, target-select cursor tint) stamps the pointed
    monster's colour word `0x20080200` (lanes `0x200`, twice the neutral `0x100`) at full blend `0x1000` while the cursor is up: its
    texture draws twice as bright, a whitish highlight; the others get blend 0 (unchanged); leaving the cursor clears it. **Done:**
    `JRPGHighlight` on `M_PS1Monster` adds `body colour x value` to the emissive; `UJRPGBattleSceneSubsystem::UpdateTargetHighlight`
    sets it on the menu's target-cursor slot (`UJRPGBattleUISubsystem`, screen `Target`) at `AJRPGBattleArena::TargetHighlightStrength`
    (0.8), 0 on everyone else.

- **User re-test (2026-11-06, pass 11):** the album music is right; the see-through ghost works; the recentre is unnoticeable
  (good). 53's knockdown replay is **not** the sound: the original plays a different "sucked into the ground" sound as the body sinks.

#### Pass 12 (2026-11-06)

57. **Death sound: still not the original's.** The RE documents no sound in the defeat fade (the engine port only releases the
    sustained cues there), so it is not identified yet. `bReplayKnockdownSoundsOnDeath` is now off. Every common sound (the
    static table and the battle bank `bse.dat`) was rendered to `LegaiaRE/exports/sounds/Audition/` (`Static_<id>`,
    `BattleBank_cue<id>`) for the user to find it. **Found by ear:** static table row `0x0D` (sound id `0x0E`, battle bank),
    imported as `/Game/Legaia/Battle/Sounds/Common/SW_Battle_Cue0E`; `AJRPGBattlePawn::DeathFadeSound` is now a soft reference
    with that sound as its C++ default, played as the body starts to fade and sink. Who rings it in the original is still unknown.
58. **The ghost shows on Begin / Escape / Reselect.** **Fixed:** outside actions it only runs on the hero's menu shots (Command,
    Attack, Spirit: `Member`) and the target cursor (`TargetEnemy`); the far shot of Begin / Escape / Reselect never ghosts.
59. **The ghost misses a big monster in front of the camera on Attack / Spirit** (screenshot: the command close-up with a monster
    between the camera and Vahn). The original's test (a point on the view axis, `dist / 4`) uses the body centre, far from that
    point while the mesh covers the screen. **Fixed (ours, menus only):** an occlusion test: the body's bounds (largest planar
    extent x `CameraGhostMenuBodyScale`, 0.8) cross the eye-to-focus line between the two, or hold the eye. Actions keep the
    original's rule.
60. **Hyper alone: the Art after the starter runs ahead and the voice lags** (inside a chain it looked right). Reading: in the
    original the whole fight slows while the CD reads a voice, and a Hyper fanfare reads 5 to 7 s; ours opened no slow for it, so
    the dash and the Art ran at full speed under a voice timed for the slowed game. **Fixed:** which voices open the shout slow is
    per kind on the arena: `bShoutSlowKnownArt`, `bShoutSlowNewArt`, `bShoutSlowHyper` (on) and `bShoutSlowSuperMiracle` (off, bug 41).

- **User re-test (2026-11-06, pass 12):** the death sound (`SW_Battle_Cue0E`) is right.

#### Pass 13 (2026-11-06)

61. **The SpecialStarter dash holds longer in the original before the Art starts** (measured side by side from recordings: Tornado
    Flame started a little early in ours). Cause: every action entry has a **loop window** (RE battle-action.md: `+0x84` = loop
    count, `[+0x85, +0x86]` = the frames; the anim tick `FUN_80047430` rewinds the cursor from `+0x86` to `+0x85` that many times).
    Vahn's SpecialStarter repeats frames 13..14 **5 times** (the crouch hold), Noa's 7..8 twice, Gala's none; also Burning Flare
    (5), Super Tempest and Neo Static Raising (2), UseItem (10), the win poses (255 = until the clip changes). Our clips played once.
    **Fixed:** `UJRPGAnimNotify_LoopWindow` (notify state over the window, `Loops`), exported by `ps1_party_sounds.py` (`loop` in
    the clip list) and placed by `ue_import_monster_sounds.py` (track "Loop"); `UJRPGFieldEnemyAnimInstance` rewinds the window
    (overshoot kept, as the original); the battle `ClipTiming` adds the loops to the step's length (255 not counted). Monster
    entries are not read yet.
62. **The dash camera stood on the other side** (Tornado Flame starter: right of Vahn in the original, left in ours). The per-Art
    camera's dash arm picks its framing by the loop counter (`+0x21B`, `Select`): ours estimated it from the clip's progress, since
    the clip did not loop. **Fixed:** it reads the real loops left and the real cursor (`AJRPGBattlePawn::GetPlayingClipCursor`).
    Note: the arm also has two variants by a per-action coin flip (the `DT_ArtCamera` column), so the side can differ between two
    runs of the original too.
63. **AGL grows too slowly** (Vahn 209 at level 56 set from the dev menu; the max arts blocks should come by ~54-59 without Spirit).
    Cause: AGL divides by `0x18C0` (6336), not `0x24C0` like the other stats (RE level-up.md, `0x801E98C8..0x801E9980`; three retail
    anchors, Vahn 115 / 130 / 145 at levels 6 / 11 / 16, match it), so it grows ~1.5x its span and stops at the 280 cap long before
    99. Also the jitter is added before the `max(1, ...)` floor, not after. **Fixed** in `UCoreSubsystem::GetStatGainRaw` /
    `RollStatGain`: Vahn 262 at 56 (cap at 74), Noa caps at 54, Gala 251 at 56, Terra caps at 69. Stats already stored in a save
    keep their old values until levelled again.

- **User re-test (2026-11-06, pass 13):** with the loop window the dash now holds as long as the original, but the starter and the
  Art after it (Hyper and a new Art) became far too slow. The extra slows of bugs 52 and 60 were the wrong fix for what the loop
  window explains: `bShoutSlowNewArt` and `bShoutSlowHyper` are now **off** by default (only the known-Art shout slow stays on), back
  to the pacing the user had found right, plus the loop window.

- **User re-test (2026-11-06, slows off):** Frost Breath, Tornado Flame and Gala's Hypers are right; Vulture Blade, Hurricane Kick
  and Burning Flare still have the voice a little off.

#### Pass 14 (2026-11-06)

64. **The special latch held for every Art after the SpecialStarter.** The `0x1A` raises `ctx[+0x243]` (its clip's `+0x87`), and
    the Art commit halves the speed while it is set; but every commit rewrites the latch with its own clip's `+0x87`, which is 0
    for an ordinary Art and 2 only for a Super finisher. So only the **first** Art after the starter runs at 2, the next back at 4,
    unless the one before was a finisher. Ours kept 2 for all: Hurricane Kick's second kick (`1E`) ran at half speed. **Fixed** in
    the resolver and `UJRPGArtSequenceLibrary::ApplyGameSpeeds` (`RaisesSpecialLatch`, from `DT_ArtVoices::bRaisesLatch`, Parts as
    the fallback); a swing or an ArtStarter drops the latch too. **Also changes the Miracle:** its inner Arts after the first now
    run at 4, as the RE reads; to check by ear.
65. **`AS_Noa_Battle_HurricaneKick3` was baked at rate 2, the disc says 3** *(wrong, undone in 73)* (1.133 s = 25.5 disc frames at rate 3, 17 at rate 2):
    it played 1.5x too slow. **Fixed:** `UJRPGArtSequenceLibrary::AssetRateCorrection` (the rate at which the asset length is a whole
    number of disc frames) speeds the clip up in the pawn and shortens it in `ClipTiming`. The other Hypers' assets match the disc.
66. **Vulture Blade and Burning Flare: voice still a little off.** Nothing found in the data: their assets match the disc rate,
    Vulture Blade has no loop window, Burning Flare's (5 loops of frames 9..10) is the disc's. To re-test after 64 / 65; if still
    off, say whether the voice is early or late and by how much (a recording is enough), and which voice (the starter's fanfare
    or a shout inside the Art).
67. **During the crouch of a new Art / Hyper, the enemy in front of the lens is not ghosted** (the target never ghosts in the
    original's rule). **Fixed (ours):** while the per-Art camera runs, the menus' occlusion test runs too and spares only the actor:
    the target between the lens and the hero fades as well.

- **User re-test (2026-11-06, pass 14):** 29 was the missing flinch, closed (fixed by 40). 36 and 44 have not happened again: kept
  to watch. Vulture Blade and Burning Flare are right now. Hurricane Kick is still slow (the voice ahead of it). The Miracle's Arts
  after the dash became far too fast for all three heroes (the dash itself is right).

#### Pass 15 (2026-11-06)

68. **Miracle too fast after 64.** The RE reading (the latch drops after the first Art) made every inner Art run at 4. **Reverted:**
    every Art after the SpecialStarter runs at 2 again, as the user had found right; `RaisesSpecialLatch` stays unused.
69. **Hurricane Kick still slow.** *(wrong, undone in 73)* With its second kick already sped up (65) and the latch reverted, the slow part is the first kick
    (`0x1D`, `AS_Noa_Battle_HurricaneKick2`). Its length is a whole number of disc frames at both rate 2 and rate 3, so a bake at
    the wrong rate (like `HurricaneKick3`) cannot be detected from the length. **Data fix:** `noa_hurricane_kick.SpeedScale = 3` in
    `DT_Arts` (2 x 1.5, the same effective speed as the corrected third kick). Tunable in the editor without a build.
70. **Ghost misses some enemies and flickers on and off.** **Fixed:** the occlusion test (the body's bounds on the eye-to-focus line)
    now also runs during actions, beside the original's point test, so a big monster whose centre is far from that point still
    ghosts; a body that enters the ghost stays at least `CameraGhostHoldSeconds` (0.6 s), and the test to leave is 15 % wider.
71. **Tactics: Standard gave one strike per hero; Aggressive should go for Hypers and Supers.** **Done:** Standard fills the gauge with
    known Regular Arts one after another (the longest that fits first), then the cheapest button; Aggressive first takes the Hyper /
    Super of most AP that the hero's AP and gauge pay for, then fills like Standard. Defensive is still Spirit.
72. **Auto-battle: the countdown was too short to cancel, and only clicking the button cancelled it** (the directions did nothing,
    so it looped for ever). **Fixed:** any key in the countdown turns the auto off (back to the manual Start); `AutoStartDelay` is 3 s.

- **User re-test (2026-11-06, pass 15):** Hurricane Kick: the kicks are far too fast now and the voice comes after them.

#### Pass 16 (2026-11-06)

73. **65 and 69 were wrong: they sped up 9 Arts 1.5x, Hurricane Kick among them.** Checked against the RE's `.glb`
    (`legaia-<hero>-battle-arts.glb`): both Hurricane Kick records were baked at rate 3, as the disc says (`0x1D` 19 keys, `0x1E`
    26 keys, 1/22.5 s apart: 0.8 s and 1.111 s). `HurricaneKick3` is 1.133 s in UE only because the import rounds a clip up to whole
    30 fps frames (33.3 -> 34); 17 keyframes at rate 2 was a coincidence. The `AssetRateCorrection` test (the asset length is a
    whole number of disc frames) failed for every clip that the rounding touches and sped up, besides Hurricane Kick 3: Cross-Kick,
    Pyro Pummel, Spin Combo, Cyclone, Acrobatic Blitz, Swan Driver, Triple Lizard (finisher) and Neo Static Raising (finisher).
    Pyro Pummel and Spin Combo are in Vahn's Craze: part of the "Miracle too fast" of pass 14. **Fixed:** `AssetRateCorrection` /
    `ArtDiscRate` removed (pawn and `ClipTiming`); `noa_hurricane_kick.SpeedScale` back to 0.
    - **Speed by the RE, now as ours:** `1A` at 2 (everyone else frozen), `1D` at 2, `1E` at 2. The commit writes the special latch
      (`FUN_8004E13C`) only when the new clip's `+0x87` is **non-zero** (`0x8004BE18..0x8004BE2C`, RE `battle-action.md`), so an
      Art with 0 does not drop it: every Art after the SpecialStarter runs at 2 until `FUN_801E93C8` clears the latch. That is the
      rule 68 put back; 64's reading (the latch drops at the next commit) was wrong.
    - **Still different from the original, by choice:** while a voice streams the PS1's frames stretch and the Arts play about a
      third slower (RE `battle-action-helpers.md`, "Retail frames stretch while a shout streams"); a Hyper's fanfare reads 5 to 7 s.
      `bShoutSlowHyper` is off (pass 13). If Hurricane Kick's voice is still off after this, that is the lever to try first.

- **User re-test (2026-11-06, pass 16):** Hurricane Kick is right now. New: in Noa's Miracle the camera goes very far when the
  last Art (Jurassic Blow) starts.

74. **Miracle: the camera runs far away on the last Art.** Noa's Ark ends with Jurassic Blow `0x2C` / `0x2D`, the same camera arms
    as Hurricane Kick (`0x801D78F0` / `0x801D797C`), which add the accumulator `ctx[+0x87C]` (shifted left on `0x2C`) to the
    distance. The original zeroes the ramp `ctx[+0x26E]`, the accumulator and the latch `ctx[+0x26F]` at **every clip commit** of
    the acting actor (`FUN_8004AD80` `0x8004BF50..0x8004BF78`, RE `battle-action.md`); ours only at the action's start, so after
    4 swings, the dash and 5 Arts the accumulator was huge. In Hurricane Kick only the dash came first, so it went unnoticed.
    **Fixed:** `AJRPGBattleCamera::UpdateActionState` zeroes the three when the acting pawn's playing step changes
    (`ArtCommitStepFrame`). This also changes, slightly, the camera of every Art that comes after other clips (Hurricane Kick's
    kicks, the Supers' finishers): it now starts from 0 at each clip, as the original's.

- **User re-test (2026-11-06, pass 17):** in a Hyper's starter the enemy still shows in front of the lens. Gala, two Arts on Che
  Delilas: he went through him, with a flick back and forth, and his strikes seem to curve (the camera turns with him).

75. **The enemy still covers the hero during the SpecialStarter.** **Done (ours):** `AJRPGBattleArena::bCameraGhostTargetOnStarter`
    (on): while the acting hero plays the SpecialStarter, the action's target is always a ghost, whatever the occlusion test says.
76. **False "baked at another rate" warning** (`vahn_cross_kick`, `ClipTiming`): the same 30 fps rounding as 73. **Fixed:** it only
    warns when no whole number of disc frames fits inside the clip's last sampled frame.
77. **Gala goes through Che Delilas and curves.** Measured on the RE's `163_Che_Delilas.glb`: his flinch (`Damaged`) throws his body
    centroid (the body pair) about 254 units (~110 cm) back and brings it home over the clip; his knockdown about 290. Our rules
    are the RE's: the attacker re-faces the target's **body pair** every frame and the reach snap at every clip commit closes to it
    (both measured to the body pair, `0x801E36F0`, `0x8004BF7C`). So a clip that commits while Che's body is thrown back snaps Gala
    ~1 m forward; when the body comes home it is on top of Gala, and the back-off (4 units a frame) pulls him out slowly: the flick.
    **Fixed (decision, user 2026-11-06: ours):** `AJRPGBattlePawn::bAimRestBodyDuringReaction` (on). While a body plays a reaction,
    whoever attacks it aims and measures the reach at that body in its **idle** pose (`GetAimBodyLocation`, the `NeutralCentroid`
    carried by the actor's current position and facing): the every-frame re-face, the reach snap, the walk's stop point, the lunge's
    range gate, the back-off and the arrival shove. The separation, the defender's own turn and the camera's focus keep the body of
    the frame, as the original's. Off = the original's rule.
78. **Some rounds stand still for no reason after an action** (user, from the session log). Measured in the log: the gap from an
    action's end to the next action's start is 66 logic frames normally, but 346 / 354 after an Art killed a Gimard and 464 / 410
    after a Miracle that knocked Cort down (90..260 after Arts that ended on a flinch). Cause: the resolver's reaction hold counted
    the **whole** reaction at the Art's speed (`Scaled(len, speed)`: Cort's knockdown + get-up 118 x 4 = 472 at speed 2, + the
    60 of the Done band = 532; the log reads 538). In the original everyone goes back to 8 on the pass the acting actor's last
    Art clip ends (`FUN_801E93C8`, RE `arts-command-gauge.md`), so a reaction the Art started finishes at normal speed; our pawn
    already plays it that way, so the fight waited ~5 s with nothing moving. **Fixed:** `ScaledUntil` in the resolver: the
    reaction (flinch, block, knockdown + get-up, the fall) runs at the step's speed only until the end of that step and of the
    slow steps right after it, then at 8 (`ResolveHit`, `KillCombatant`). Test `JRPG.Battle` (the Art-speed hold) updated.

- **User re-test (2026-11-06, pass 18):** the ghost still misses (a big winged monster covers Vahn's command close-up). Asked: AP
  always 100 in the dev menu; a faster camera blend from Begin / Escape to the hero (~0.3 s). Found: after a Hyper the next Arts are
  slow (plain swings are not); Noa's Super Tempest finisher made no flinch on Songi (3rd), nor some hits of Gala's Supers; Vahn's
  Pyro Pummel went through Vera and flicked back; the flyers (Vera, Evil Fly) are lower than in the original and sink into the
  ground when they die.

#### Pass 19 (2026-11-06)

- **Ghost by trace (ours, user's idea):** `AJRPGBattleArena::bCameraGhostTrace` (on). Lines from the camera to five points of who the
  shot must show (the middle, the head, near the feet, both sides; the acting pawn, the action's target, the menu's framed pawn and
  cursor target) are traced against every other pawn's **physics asset bodies** (`AJRPGBattlePawn::BlocksView`; the monsters have
  one, its bodies follow the animation, query only and ignoring every channel). A hit ghosts it. The old tests stay (the heroes have
  no physics asset).
- **Dev: AP always 100:** `battle.ap on|off` (dev menu, Battle tab, "AP"): every living hero's AP back to the maximum every frame
  (`UBattleSubsystem::SetDebugFullAP`, `FJRPGBattleResolver::DebugFillPartyAP`), this fight and the next ones in the session.
- **Camera from the far shot to a menu close-up in 0.3 s (ours):** `AJRPGBattleCamera::MenuEnterBlendSeconds` (0.3; 0 = the
  original's ease-out): from Far to Member / TargetEnemy the camera arrives in that time (cubic ease-out over time, following the
  moving goal), instead of the 6-step ease-out that takes almost a second from far away.
79. **After a Hyper, the next Arts run at 2.** RE (`arts-command-gauge.md`, "Restoring the animation rate"): `FUN_801E93C8` runs on
    every frame of the strike loop while the acting hero's committed id is `< 0x10` (a plain direction swing): everyone back to 8 and
    the special latch `ctx[+0x243]` cleared. The ArtStarter (`0x19`) does not clear it. A regular Art after a Hyper starts with the
    swings of its own buttons, so it runs at 4; only the Miracle chains its Arts with no swing between (all at 2, as before).
    **Fixed:** a Swing step clears the special mode in `ComposeHeroTimeline` and `UJRPGArtSequenceLibrary::ApplyGameSpeeds`.
80. **No flinch on the Super finishers' hits.** RE (`battle-actor-rendering.md`, rate writers): **every commit made while the actor's
    rate is not 8 sets that actor to 4** (`FUN_8004AD80` `0x8004B080..0x8004B090`), the reaction commits included. After the
    SpecialStarter the world runs at 2, so the defender's flinch plays at **4**; ours played it at 2, and each hit restarted it: a
    long flinch (Songi 3rd's is 30 keyframes at rate 1) barely moved. **Fixed:** `AJRPGBattlePawn::ReactionRateFloor`: a reaction
    clip committed with the world slow plays at 4 until the acting actor's next step commit (`GetStepCommitSerial`) or the return
    to 8; the resolver times the hold and the reaction window at 4 too (`ReactionSpeed`).
81. **Flyers too low and inside the ground when they die.** `InitCombatant` put the mesh's **lowest point** on the floor. The
    original draws the model at the actor's position (height 0): a flyer floats because its parts sit above the root (Vera's lowest
    point is 97 cm above it, Evil Fly's 80, Gimard's 3), and its knockdown brings the body down to the root. Ours lowered the flyers
    ~1 m, so the fallen body went that much under the floor. **Fixed:** `AJRPGBattlePawn::bGroundAtMeshRoot` (on): the mesh root
    is the floor; the capsule goes from the floor to the top of the body. Probably also the "Vahn goes through Vera" of this pass
    (her body was at his height); to re-test.
82. **Open: still no flinch on some hits of Gala's Neo Static Raising on Xain** (with 80 built: the log shows the `battle.ap` AP
    refills). Checked and ruled out: the events reach the target every hit, an equal clip asked again restarts from frame 0, Xain
    has its flinch (tag 2, 13 keyframes at rate 1, beat 5), the knockdown only comes on the last hit, the original also commits each
    reaction at once (`+0x1DC |= 5`, `0x801EEC34`). **Diagnostic added:** `AJRPGBattlePawn::bLogReactions` (on) logs every reaction
    that starts (role, clip, rate, frame, world speed, the rate-4 floor, length) and every one with no clip; next test: read which
    hit has no line or a line with an odd speed / length.
    - User re-test (2026-11-06, Xain alive): three hits of Neo Static Raising made no flinch, the last one among them. In the log
      (the diagnostic not built yet) they match the **finisher's three hits** (f1109, f1136, f1205), the ones after the SpecialStarter
      (world frozen, then 2). The earlier case (punches with no reaction after Xain died) is the expected one: a dead body takes no
      reaction, the rest of the chain swings at it.
    - **Diagnostic read (2026-11-06, two runs of the Super):** every finisher hit **starts** its reaction, at the right speed (4:
      asked 2, floor 4) and length; the knockdown even hands over to the get-up on the exact frame (72 x 2). So the logic is right
      and the body just does not move on screen. The bug appeared in pass 18, right after 75 (the target ghosted during the
      SpecialStarter and held 0.6 s after), and the finisher hits fall inside that window. The monster mesh used
      `AlwaysTickPose` (bones refreshed only when the mesh counts as rendered); with the ghost material it most likely did not, so
      the bones froze. **Fixed:** every battle mesh uses `AlwaysTickPoseAndRefreshBones` (`BuildPieces`). The reaction log now also
      prints the mesh's `GlobalAnimRateScale`, whether it was rendered in the last 0.2 s and the ghost opacity, to confirm.
    - **Closed by 83** (the user's recording: after the dash, punches 1 and 2 flinch, the 3rd does not, the 4th knocks down, the
      rest hit nothing): our finisher looped and showed more punches than its hits. The reaction log was right all along; the
      `AlwaysTickPoseAndRefreshBones` change stays (harmless).
83. **Super finishers looped: Neo Static Raising repeated its punches.** Gala's finisher (`0x2F`) carries a loop window in its
    disc entry (keyframes 5..15, count 2), and so does Noa's Super Tempest finisher (`0x30`, 3..17, count 2); ours played both
    (bug 61), so the clip showed more punches than its 3 / 4 hits and the last hit (the knockdown) fell among the repeats. **The
    user checked the original: Neo Static Raising lands only 3 punches, no repeat.** (The RE text says a windowed clip with an id
    `>= 0x2B` re-zeroes its hit index on every rewind; on screen the original does not loop these, so that reading does not hold
    here.) **Fixed:** `ps1_party_sounds.py` no longer exports the loop window of the Super finishers (id `>= 0x2B`); the two assets
    already imported need their `UJRPGAnimNotify_LoopWindow` (track "Loop") removed by hand. The battle `ClipTiming` also delays the
    hits that come after a clip's loop window by the loops (Burning Flare, `0x1C`, holds one keyframe 5 times before its hits
    11..14, which came 5 keyframes early). The other clips with a loop window, all kept: the three SpecialStarters (user-checked),
    Burning Flare (user-checked in pass 14), UseItem (x10) and the win poses (255).
- **Menu camera switches in 0.3 s too (ours):** `MenuEnterBlendSeconds` now also covers a switch between menu close-ups (another
  hero, the target cursor on another enemy, command to target and back), not only the entry from the far shot.

#### Pass 20 (2026-11-07)

- **User re-test (2026-11-07): everything above is closed.** Confirmed fixed: 2 (monsters), 9 / 18 (Noa's idle), 11 (block and
  knockdown rates are right), 30, 31, 36, 44, 79, 80 / 82 / 83 (the two finishers' `LoopWindow` tracks removed by hand), the ghost
  by trace. 46 (music underneath): solved by replacing every BGM with the music CD's tracks (more complete) and filling the sounds.
84. **Two Hypers in a row: the second has no fanfare (and no voice); with three, the second is silent and the third is normal.**
    Cause: `FJRPGBattleVoices::Fanfare` follows the original's dispatcher (`FUN_8004FCC8`), which drops a fanfare while the drive
    still reads the previous voice (`FUN_8003DE7C(1)`). A Hyper fanfare is long (the WAVs run 5.0..6.9 s; read spans 5.3..7.05 s);
    in the original the whole fight slows while the CD reads, so the Hyper outlasts its fanfare, but ours runs without that slow
    (`bShoutSlowHyper` off, pass 13), so the second starter came while the first fanfare was still sounding and was dropped; the
    third came after it ended. **Fixed (decision, user 2026-11-07: no CD drive model):** `FJRPGBattleVoices` no longer has the
    one-channel drive nor its busy time; every voice plays when it is asked for. A new fanfare cuts only the previous fanfare;
    shouts and the block grunt play over whatever sounds and cut nothing (nor are they dropped). The read spans
    (`ShoutSeconds` / `FanfareSeconds`) are still used only for the length of the shout slow. Re-test: two and three Hypers in a
    row, a Hyper right after a known Art's shout, a block during a shout.

#### Backlog (2026-11-05) — noted for later, not started

32. **Full RE sweep of the fight, to make it identical.** Go through the RE inch by inch and list what every piece of the battle
    does: each state of the action SM, every wait / hold / timer, every animation (which clip, when it is committed, how it ends, what
    moves the actor), the camera, the sounds. Compare each with ours and reproduce it. Write the result as a reference doc next to
    `Reference/BATTLE_TIMING.md` (one section per state / mechanism, with the RE source and our file), then fix the differences.
    **Documented (2026-11-05):** [`Reference/BATTLE_RE_SWEEP.md`](Reference/BATTLE_RE_SWEEP.md) (also covers effects, camera, the
    remaining sounds and the HUD; magic / summons / capture out of scope for now). Implementation: its section 10.3, in order.
33. **Arts panel (the Triangle list), UI fixes:**
    - Show it **only on the command-input screen** (the one where Arms / High / Low / Ra-Seru are typed, `attack`), not on the
      hero's main menu nor elsewhere in the fight.
    - **Pages must not wrap:** L1 goes up and R1 goes down and both stop at the first / last page.
    - **Colours like the original:** a Hyper Art's arrows are all orange-ish; the **last arrow** of every Art is orange too; the
      name is coloured by kind: **Miracle red, Hyper orange, Super yellowish** (Regular stays white).
34. **Prefilled chain + Escape:** when the input window opens with the previous round's chain already typed, Escape / Cancel must
    clear **all** of it at once, not one button at a time (one at a time still applies to buttons typed by hand this time).
- **33 and 34 fixed (2026-11-05, re-test).** The Arts panel is in the view only on the Attack screen (`FJRPGBattleMenu::BuildViewJS`);
  L1 / R1 stop at the first / last page (`pageArtList`); each Art carries its `kind`: names Miracle red, Hyper orange, Super
  yellow; the arrows are the user's SVG arrows (`95_battle.html`, purple `#8C88EC` / yellow `#F4B41A` with a dark shadow), purple
  with the **last** one yellow, all yellow on a Hyper; the panel uses the user's gradient (`#1B1E75 -> #121558 -> #0C0E45`) and a
  2 px `#B88A44` border. **Miracle is always first** in the list (then Hyper, Super, Regular). Cancel on a prefilled chain clears
  it at once (`PrefillCount`); buttons typed by hand after it still come off one at a time. Tests in `JRPGBattleUITests.cpp`.
- **34b (2026-11-06, user):** the first direction pressed on an untouched prefilled chain replaces it with the new input instead
  of adding to its end (`PressAttack`); Confirm still repeats it. Test updated.
  `shell.html` rebuilt (`Extras/build_webui.py`).

26. **Too many blocks.** The block roll misses the RE's two approach terms (`hits.rs` `roll_block`, `block.rs` `block_roll`), spent
    by the **first** hit of each action: `attack_ramp` = how far the defender is from facing the attacker (0 face-on .. `0x800` from
    behind), added to the attacker's side (about SPD + ATK·4/5 ≈ 50), and `guard_ramp`, which grows each frame the attacker walks in,
    added to the defender's. ~10° off face-on already adds ~113: **in the original a block is almost only possible face-on**. Ours
    uses 0 for both, so any angle blocks ~25% of the time. The same two terms also enter the damage roll (`physical.rs`:
    `+ (attack_ramp * atk) >> 16` to the attack, `+ (def * guard_ramp) >> 10` to the guard, before Spirit's ×3); `guard_ramp` grows by
    1 per vsync the attacker walks (`BATTLE_PASS_STEP_PER_TICK`); a height-class miss returns before the kernel and spends nothing.
    - **Fixed (decision: the scene measures, the rules take it as an input; PvP-ready).** New `FJRPGApproachInput` (ActionIndex,
      AttackRamp, GuardRamp; integers only). The scene measures it at `ActionStart` right after `BeginAction` (the yaw difference in PS1
      angle units, folded like `0x801E3068..0x801E30C8`; the walk's logic frames) and calls `UBattleSubsystem::SubmitApproach`;
      `FJRPGBattleResolver::SubmitApproach` checks it is the active action and has not received or spent one, **clamps** the values
      (rule 22) and logs it in `FJRPGBattleState::Approaches` for replays. The first hit that goes through the kernel (block or hit)
      spends it (`FJRPGBlockInput` / `FJRPGMeleeInput::AttackRamp` / `GuardRamp`). PvP: the authority measures and sends; with
      `UBattleSubsystem::bWaitForApproachInput` the subsystem holds the resolver before the action's first hit until it arrives
      (`IsWaitingForApproach`), outside the rules' clock, so both machines stay on the same rules frame. No input = 0 (headless tests,
      unchanged). Test `JRPG.Battle.ApproachTerms`.
    - Codex review (2026-11-05), all five accepted and fixed: (1) only the authority's scene measures and submits
      (`UBattleSubsystem::bApproachAuthority`; the other machine applies the network value); (2) a hit on frame 0 resolved inside
      `StartAction` before the scene saw `ActionStart`: with `FJRPGBattleResolver::bRequireApproach` (= `bWaitForApproachInput`)
      `ProcessDue` holds the action's first hit until the approach arrives; (3) the scene's recovery path (an action that started before
      the scene was up) now submits its approach too (`SubmitApproachFor`, shared with `ActionStart`); (4) while the rules wait, the
      acting pawn's steps are delayed frame by frame (`AJRPGBattlePawn::DelayAction`, `UBattleSubsystem::WasHeldForApproach`), and the
      wait starts before the first step, not only before the first hit; (5) with the squash and a gliding recentre, a walking attacker's
      goal moves with its target's share, not its own (`ApplyFormationShift`, `TranslateApproachGoal`).

#### Pass 21 (2026-11-07) — noted for the next batch, not started

85. **The light floor makes the fighters glow on night maps.** The monster and party materials carry a fixed light floor
    (`M_PS1Monster`'s `EmissiveStrength`, 0.2, `Base Color x EmissiveStrength`; `Guides/PS1_ASSETS.md`) so they are not too dark,
    but on night maps they glow too much. To do: measure the light where the fight is (the arena / the map's lighting, or a probe
    at the formation) and lower the floor to match, instead of one constant for every map.
86. **A blocked hit still makes the impact sound.** When a hero or a monster blocks, no hit sound must play (the original's
    block skips the impact cue, `BATTLE_RE_SWEEP.md` 9.2 row 1). Check the `Block` path of the hit-sound gate (bug 20) on both
    sides.
87. **The party enters the fight with the Ra-Seru already equipped.** Maybe because of the test save; the default should be
    without them until the story gives them. Check where the starting equipment comes from (the new game / the dev setup).
88. **Dev menu: an equipment tab** (a feature, not a bug). Equip the Ra-Seru and any weapon / armour / accessory on any hero from
    the dev menu, to test the equipment in a fight (the pieces, the swings of `DT_HeroSwings`, the stats).

#### Status of the open entries (user, 2026-11-10)

The entries above keep the text of the day they were written. What the user confirmed since, playing:

- **Fixed:** 30 (Slash Kick sliding at the very end), 31 (Gimard inside Vahn), 57 (the monster's death sound),
  29 (hits that did not land), 36 (the long wait before round 2), 44 (Noa's sprint), 46 (music underneath), 66 (the
  voice of Vulture Blade and Burning Flare), 82 (no flinch on some hits of Neo Static Raising).
- **Fixed, re-test done:** 37 (the Arts' shout slow), 38 (the Slime's walk sound), 39 (the action camera changing side), 41 (the
  doubled slow on Miracle), 42 (the special starter of a new Art), 43 (AP on damage taken).
- **Still open, for the next fix pass:** 85 (the light floor on night maps), 86 (the impact sound on a blocked hit), 87 (the
  party enters the fight with the Ra-Seru equipped).
- **To do, a feature:** 88 (the dev menu's equipment tab).

## Not bugs (checked)

- Pause stops the logic clock: the frame counter stayed still for 16 s while paused.
- After a Defeat the learned Art is discarded and learned again in the next fight. That is correct: a Defeat writes back only HP
  (floored at 1) and MP, never AP or Arts (see 28).
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
