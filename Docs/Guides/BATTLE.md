# Basic combat

A fight that is played: the round, the damage hit by hit, the Arts with their AP, the enemy's
turn, victory and the rewards. Phase 5 of [BATTLE_ROADMAP.md](../BATTLE_ROADMAP.md), written on
top of the stage that [BATTLE_STAGE.md](BATTLE_STAGE.md) assembles. **Status: written, to build and
test** — see [What to check](#10-what-to-check-after-building).

The rules are the original's (the RE project's documents are the specification; this is not a
port): formulas, order of operations and truncation are kept unless
[Deliberate departures](../BATTLE_ROADMAP.md#deliberate-departures-from-the-original) says
otherwise. Everything in the core is **integer**, draws from **one `FRandomStream`**, and the
fight is **commands and inputs in → resolution on a logic timeline → events out**, so the same
seed, the same commands and the same inputs at the same frames always give the same events (the
door to PvP stays open: [What the core cannot do](../BATTLE_ROADMAP.md#what-the-core-cannot-do)).

---

## 1. Where things live

| Piece | What it is |
|---|---|
| `UBattleFormulas` (`Battle/BattleFormulas.h`) | The contas as pure static functions: power bytes, miss by height, element matrix, equipment fold, block, the melee kernel, initiative, the enemy's swing queue, time. Takes a rand source, never a subsystem |
| `FJRPGBattleResolver` (`Battle/JRPGBattleResolver.h`) | The state machine, **no UObject**: `Begin`, `SubmitCommand`, `SubmitInput`, `StepFrame`. The tests drive it with hand-made combatants |
| `UBattleSubsystem` | The snapshot at `BeginBattle`, the fixed-step clock, the events, the write-back and the rewards at `EndBattle` |
| `BattleTypes.h` | `FJRPGCombatant` (rules only, all integer), `FJRPGBattleCommand`, `FJRPGBattleInput`, `FJRPGBattleEvent`, `FJRPGBattleState`, `FJRPGActionTimeline` |
| `AJRPGBattlePawn` / `UJRPGBattleSceneSubsystem` | The presentation: plays the clips in step with the timeline, shows the events |

---

## 2. Playing it without a HUD

The real HUD is Phase 7, but the command phase now has a **placeholder menu** that needs no console
([13. Battle UI](#13-battle-ui)). The console below is its fallback and still works while the menu is up
(only the on-screen debug prompt is hidden then). In PIE, with the console (`JRPG.Battle.Start` and `.End`
are in [BATTLE_STAGE.md](BATTLE_STAGE.md)):

| Command | What it does |
|---|---|
| `JRPG.Battle.Start gimard 2 [level] [roll\|none\|back\|first]` | A fight against two Gimards where the player stands. The 4th argument forces the formation advantage (the dev menu's Battle tab has the same choice); without it the fight rolls |
| `JRPG.Battle.Attack [enemy]` | The command of whoever is being asked (the on-screen prompt says who): one Arms swing. `enemy` is a number, **0 = the first enemy**; without it, the living enemy of lowest slot |
| `JRPG.Battle.Arts <buttons> [enemy]` | The Art chain: `JRPG.Battle.Arts High Low High 0`, `JRPG.Battle.Arts "A R A"` (letters), or arrows. The hero's handedness applies (Noa's Arms is the right arrow). The gauge limits how many buttons fit |
| `JRPG.Battle.Auto` | Everyone who has not commanded attacks the lowest living enemy: walks the round without typing one by one |
| `JRPG.Battle.End [victory\|defeat\|escape\|abort]` | Ends the fight from outside. `abort` leaves the party exactly as it was. `victory` is a test shortcut: it pays the EXP and Gold of **every** enemy of the fight, living ones too (a real victory only counts the defeated) |
| `JRPG.Battle.PrintEvents 0\|1` | Each event on screen as it happens (1, the default) or only in the log |

Every event is also in the log (`LogTemp`, "Battle: …"), readable:

```
Battle: [f0 a-1] ===== round 1 =====
Battle: [f0 a0] Vahn#0 acts on Gimard#3
Battle: [f42 a0] Vahn#0 hits Gimard#3 for 275
Battle: [f82 a0] Vahn#0 ends
```

`f` is the logic frame, `a` the action number. The prompt for the next command stays on screen.

---

## 3. Starting a fight from code

`UBattleSubsystem::BeginBattle(FJRPGBattleSetup)`; the setup now carries:

| Field | Meaning |
|---|---|
| `Party`, `Enemies`, `Location`, `FacingYaw` | As in Phase 4 |
| `Seed` | The fight's `FRandomStream` seed. **0 draws one** (outside the rules, like the encounter roll) and stores it in `GetBattleSetup()`, so a fight can be repeated |
| `bScriptedFight` | The original's scripted-fight flag (`ctx+0x287`): the enemies take the **boss** stat profile, the fight cannot be escaped, no formation advantage. **Who sets it**: the code that builds the fight copies it with `UJRPGEncounterLibrary::ResolveScriptedFight(Enemies)` (`AJRPGFieldEnemy` and `AJRPGBattleWaves` already do). No enemy is `bIsBoss` in `DT_Enemies` → false (a random encounter). Otherwise it finds the `DT_Bosses` row whose `Enemies` are exactly the fight's (any order) and uses that row's `bScriptedFight` (Lapis is false). A boss with no row → true, with a warning in the log. A fight you build by hand (the debug `JRPG.Battle.Start` too) leaves the field false unless you call it |
| `bNoRewards` | A fight that pays nothing (the original's special battles) |
| `FormationAdvantage` | **-1 (the default) rolls** at the start of the fight: the original's `FUN_80051D84` from the fight's `Rng`, before round 1's initiative (`UBattleFormulas::RollFormationAdvantage`: each side's mean SPD blurred by a spread, then a 1-in-16 gate for the side that won; the accessory passives are not in yet). 0 none, 1 back attack, 2 pre-emptive force it. **Back attack**: the party enters no command in round 1 (only the monsters act) and starts turned away; **pre-emptive**: the monsters lose round 1 and start turned away. Each turned combatant faces again when it acts or is hit. A scripted fight never has one; the Muscle Dome waves set 0 |
| `bForceBackAttack` | The roll gives a back attack whatever the scores (the original forces it for the bees `0x3D..0x3F` on maps `0x0C` / `0x15`; monster `0xA7`, Lapis, forces it by itself through `DT_Enemies.MonsterArchiveID`). Only with `FormationAdvantage = -1` |

`EndBattle(Result)` is idempotent: a second call does nothing and pays nothing. `Aborted` and
`Defeat` write nothing; `Victory` and `Escaped` write HP, MP, AP and the learned Arts back
(an escape lifts the fallen to 1 HP); `Victory` pays EXP and Gold **once** to the heroes of the
fight who are alive.

---

## 4. Data the fight reads

Imported as DataTables under `/Game/Data/` (the CSVs are in `Docs/Data/`, how they are generated
is in [ENEMIES.md](ENEMIES.md)):

| Table | Row struct | Used for |
|---|---|---|
| `DT_Enemies` | `FJRPGEnemyData` | The raw record, `TargetClass`, `Element`; rewards |
| `DT_EnemyActions` | `FJRPGEnemyActionData` | **New.** Each monster clip: hit frames, power bytes, AGL cost, rate. Without it the monsters hit with a default swing (logged) |
| `DT_Arts` | `FJRPGArtData` | The Arts (recognition, hits, AP) |
| `DT_RaSeruTiers` | `FJRPGRaSeruTierData` | **New.** The Ra-Seru's ATK per tier. Without it the piece is 0 ATK (logged) |
| `DT_WeaponCosts` | `FJRPGWeaponCostData` | **New.** The Arms button's cost per weapon. Without it every button costs 30 (logged) |
| `DT_HeroSwings` | `FJRPGHeroSwingData` | **New.** Each hero's plain swing per button and equipped item: power byte(s), hit beat(s), rate, cost. Without it the plain swings use the default byte and `SwingHitFrame` (logged) |
| `DT_HeroReactions` | `FJRPGHeroReactionData` | **New.** How long each hero's reactions last in logic frames (flinch, block, knockdown, get-up, dying), read from the battle files. The fight waits for them before the next action. Without it the heroes use Vahn's numbers (logged) |
| `DT_Bosses` | `FJRPGBossData` | **New column** `bScriptedFight`. Re-import to get it |
| `DT_Characters` | `FCharacterData` | Element (`RaSeruElement`) and handedness |

---

## 5. The snapshot

`BeginBattle` copies everything into `FJRPGCombatant` and the resolution never looks at the
Party, Status or Inventory subsystems again.

| | Heroes | Enemies |
|---|---|---|
| HP / MP / AP | the roster's current | `InstallEnemyStats` (+ difficulty) |
| **ATK** | the **base** stat, with nothing equipped (the equipment is folded **per hit**; reading `GetEffectiveStat("ATK")` would inflate every hit) | `InstallEnemyStats` + `ScaleEnemyATK` |
| UDF LDF SPD INT AGL | `GetEffectiveStat` (equipment and Ra-Seru already in, as the original's loader does) | `InstallEnemyStats` + `ScaleEnemyDEF` on UDF / LDF |
| `PieceATK` | weapon, Ra-Seru tier, shoes, body, head — each piece's `AttackBonus` (Ra-Seru: `DT_RaSeruTiers[SeruLevel]`) | — |
| Element | `RaSeruElement` (Vahn fire, Noa wind, Gala thunder) | `DT_Enemies.Element` |
| Command gauge | AGL; cost per button 30, the Arms button by weapon | AGL; spent by the swings it queues |
| Reactions | flinch 3/1 (12 frames), block the same; they have a get-up | the monster's flinch / block / get-up rows |

---

## 6. The round

```
Commands  (every player-owned hero enters one command; hidden from the other side)
   ↓ the last one in
Execution (the highest unspent initiative key acts, again and again, until all keys are spent)
   ↓
the next round
```

- **Initiative** (once per round): `key = SPD + rand % (SPD/2 + 1) + 1`; a hero adds what is
  missing of HP `>> 4` below ¼, `>> 5` below ½, `>> 6` otherwise; a monster `>> 10`. Ties are drawn from the
  fight's Rng as the original does. A key of 0 means "has acted / fallen".
- **AGL pool**: a hero's input window is paid from it (each button its cost); a monster's AI queues
  swings while the pool pays their `AGLCost` (up to 15, or 16 draws that did not fit). It is
  restored every round.
- **A round with no action**: a round whose every turn was lost to a condition (sleep, stun…) just passes. A round in
  which nobody could plan an action (a target or a chain missing — a data problem) logs a warning each time; only
  after 64 such rounds in a row, with a clear error in the log, is the fight `Aborted` (which writes nothing back), as
  a safety net because the resolver does not advance frames while it loops. 1024 rounds of conditions only: the same net.
- **Retarget**: a hero whose target fell hits the **lowest living enemy slot**; a monster draws another
  living hero; none left → the action is dropped and nothing is spent.
- **The enemy AI** is the original's physical half: uniform random living hero, swings drawn from
  its `Swings` while the pool pays. The magic roll and the per-monster scripts are Phase 8 (a monster
  with magic always takes the physical branch).
- Commands are checked at the door (`SubmitCommand`): the issuer must own the slot, the target must be
  a living foe (not for Spirit), Art bytes must be buttons that fit the gauge. `Item`, `Magic` and `Escape` are
  refused until Phases 7 / 8.
- **Spirit** (`EJRPGBattleCommandKind::Guard`, the original's action category 4; RE `battle-action.md` states
  `0x46..0x48`, `battle-formulas.md` "Defense Value" and "Per-round AGL restore"):
  - **Defence for the round.** From the commit to the next round start the hero is in the Spirit stance
    (`FJRPGCombatant::bGuarding`), before and after their own turn: physical hits face their defence x3 and their
    side of the block roll is x3/2 (`UBattleFormulas`, already there). Another command for the same slot undoes it.
  - **The turn** is one step in place (`EJRPGActionStepKind::Spirit`, the clip `AS_<Hero>_Battle_Spirit`): no target, no
    walk, no hit. Length: the clip's, or `FJRPGTimelineDefaults::SpiritFrames` (60) without it. Then the usual Done band.
  - **AP:** the turn adds `FJRPGTimelineDefaults::SpiritAPGain` (32, the original's Done-band pay for a Spirit turn), capped
    at `MaxAP`, at the end of the clip. 0 turns it off.
  - **Gauge next round:** the next round opens with `CommandGauge = min(Max * 7/5 + 8, 288)` (the menu's gauge shows it);
    the round after that is back to `Max` unless the hero uses Spirit again.
  - No effect is spawned yet (the aura comes with the effects pass). Test: `JRPG.Battle.Spirit`.

---

## 7. The logic timeline

An action's frames are built **when it starts**, from data, as an `FJRPGActionTimeline`; every hit is
resolved at **its own frame** (`ResolveHit`), never by an animation notify. One logic frame is **1/60 s**
(the original's tick). A clip of rate *R* at game speed *S* (8 normal, 4 in an Art, 2 after the
special starter) advances *S·R/32* clip frames per logic frame.

- **Monsters**: the `DT_EnemyActions` row. A hit beat *b* fires at clip frame *b − 1*; the power byte is
  the row's byte at the hit's index. With a next swing queued and `Gate` 0, the next swing starts at beat + 3 and the
  hits that would fall after it are lost, as in the original. **A beat past the clip never fires**: the original
  only fires the beats its cursor reaches (clip frame ≤ `FrameCount` − 1), so a beat with *b* − 1 ≥ `FrameCount` is
  dropped and the clip is **not** stretched to it. A row with no hit frame at all keeps the default swing; a row
  whose beats are *all* past the clip is an action that plays its clip and deals no damage (logged once) — still
  eligible for the AI and paying its AGL. The disc has four such rows (`koru_05_Action_0x10` and `koru_06_Action_0x15`
  at beat 200, `rogue_05_Attack` at 255, and the flinch `cort_juggernaut_02_Damaged` at beat 10 in 7 frames); the first
  three are bosses whose damage is script / magic (Phase 8). `generate_enemy_actions_csv.py` lists them; the CSV keeps
  the disc's values. A flinch / block window is capped at its clip's length (`FJRPGBattleReactionClip::FrameCount`).
  A monster with no swing row hits with the default swing (`ActionIndex` −1: the pawn plays the first attack of its
  clip set, not clip 0).
- **The party**: the clip's length and its `UJRPGAnimNotify_Hit` markers, read **as data** when the
  action starts. **The markers are not on the party's clips yet**, so the frames come from data in this order: the clip's
  markers if present; for a **plain swing**, the hit beat(s) of the equipped item's swing record (`DT_HeroSwings`: beat *B* at
  rate *R* falls at logic frame `ceil(32 (B − 1) / (8 R))`, the same conversion as the monsters' `DT_EnemyActions`; a record
  can have 2 hits); else the fallback by kind: a plain swing is one hit; an Art is *N* hits (`HitPower`) spaced along the clip. The
  frames are `UBattleSubsystem::TimelineDefaults` (editable) and the use is logged once per kind of step
  ("golpe simples sem marcador de golpe no clip: quadros padrão"). Placing the markers
  ([ARTS.md](ARTS.md)) is how a clip gets its real frames.
- **Pause**: the counter does not advance while the game is paused. `UBattleSubsystem` ties its tick to the game
  world (`GetTickableGameObjectWorld`, via the `UGameInstance`) — an `FTickableGameObject` with no world is ticked by
  the engine's world-less call, which ignores the pause — and `Tick` also returns early if there is no world or it is
  paused. The fight waits at the command prompt for as long as the player thinks.
- **While the scene enters** (`HoldLogic`): a command is still accepted and kept, but the round does not pass from
  Commands to Execution and no frame advances; when the scene is up the execution starts and the first `ActionStart`
  reaches the pawns. If an action had already started with the scene out (all-AI party), the scene starts it on the pawn.
- **The target falls mid-combo**: nothing is cut and nothing is retargeted. The original fixes the target when the action
  starts (`actor[+0x1DD]`; the re-roll `FUN_801DB124` only runs at dispatch, off a target that was already dead), adds the
  combo up and writes `max(HP − total, 0)` once, on the last hit that connects, and only tests the wipe at the end of the
  action. So every hit resolves on the same target, HP stops at 0, the excess is lost (never sent to another enemy), the
  target falls (Death, knockdown) on the last connecting hit and, if it was the last enemy, victory comes when the action
  ends. The scene shows the original's combo cluster on screen (fixed keys, so each line replaces the last): there is no counter
  during the combo (each hit only shows its damage over the target); at the end of the action the `ComboTotal` event gives "Hit N" over "Damage X", X being the
  damage of all the hits, the discarded excess included; a one-hit action shows only "Damage X". The next action clears it.
  The per-hit numbers over the target stay.
- **After an action** (the original's pacing, `Reference/BATTLE_TIMING.md`): the attacker's last clip plays to its natural end
  (`TailFrames` is 0), then the fight waits for the **target's reaction** (state `0x20`) - every hit records when its target is back
  on Idle (`FJRPGCombatant::ReactionHoldUntilFrame`: flinch, block, knockdown + get-up, a dying hero's Knockdown + Dying, a monster's
  knockdown + death fade; the lengths are data - `DT_EnemyActions` for monsters, `DT_HeroReactions` for heroes, the defaults in
  `FJRPGTimelineDefaults` otherwise - measured at the game speed of the hit) - then the Done band (`DoneBandFrames`, 60), then
  the **victory / defeat test** (never earlier, not at `ActionEnd`), then `PassGapFrames` (6) before the next actor starts. A fight
  that was decided holds `VictoryHoldFrames` (256) / `DefeatHoldFrames` (322) before `EndBattle`; a kill by a condition at the start of a turn waits the same way. A hero swing is cut for the next
  swing of the chain at the keyframe beat + 3 of its own record (Vahn arms 20 logic frames, kicks 16..20), not at a fixed 20.
- **The pawn's reactions** (presentation only, driven by the events, timed in **logic frames** of the presentation clock, `UBattleSubsystem::GetPresentationFrame`, which keeps ticking while the
  players type commands and during the victory / defeat hold, and stops with the pause - pause and hitches agree with the rules): a flinch plays and returns to Idle; a knockdown plays Knockdown then GetUp then Idle; a hero that falls plays Knockdown,
  Dying and then **loops** Down (the original's downed hero keeps animating); a monster holds the last frame of its Knockdown, then
  sinks and fades out (`DeathFadeParameter`, a scalar material parameter, is optional) and is hidden. Every clip - loop or
  one-shot - plays on the same `UJRPGFieldEnemyAnimInstance`. The defender **turns to face the attacker** on a hit and on a block and **slides back** a little
  (`FlinchKnockback` / `KnockdownKnockback` / `BlockKnockback` on `AJRPGBattlePawn`, clamped by the arena); the attacker
  closes the distance again at the start of each step (the original's "reach snap"), so a combo stays in reach. Nobody walks
  back to a seat after the turn (RE: `battle-action.md`, "Where an action leaves its combatants").
- **Reach** (where the attacker stops): the original's range law `FUN_8004E2F0`, not a fixed distance. The threshold is
  `{256, 384, 1024}[size]` PSX units for a size class under 3, else `size << 4`; the size class is the monster's record
  byte `+0x1F` (`DT_Enemies.SizeClass`, 14..48 on the disc), the target's for a hero and the attacker's own for a monster;
  a hero adds his own offset (`+43` Vahn, `0` Noa, `-53` Gala, `-100` Terra; positive shortens). 1 PSX unit = 0.433 cm. A
  row with `SizeClass` 0 (made by hand) uses `DefaultSizeClass` (22). `ReachDistance` > 0 forces a fixed distance. An approach
  shorter than `MinWalkDistance` does not play the walk clip.

A hero's plain-swing bytes are the first byte of the swing record that his **equipment** brings in the battle file
(the footwear gives the Low and the High kick, the weapon and the Ra-Seru the two arms). With the default equipment
(`UBattleFormulas::DefaultSwingPowerByte`, read from PROT 0863..0865): Arms `0x18` (power 20, UDF); Ra-Seru `0x18`, Noa's
Terra `0x1D` (LDF); **High `0x18` for Vahn but `0x0E` for Noa and Gala** (restricted: it misses class 3, short); **Low
`0x13` for all three** (restricted: it misses class 2, floating). `0x1D` / `0x18` are the unrestricted pair some boots
bring (Gala's War God Boots, Vahn's Triumph and Ra-Seru Boots). `DT_HeroSwings` now brings the equipment's record: `UBattleSubsystem::ApplyHeroSwings`
fills `FJRPGCombatant::SwingBytes` / `SwingProfiles` (index = button) from the row of the equipped item (weapon → Arms, Ra-Seru
tier `seru_N` → Ra-Seru, shoes → Low and High), falling back to the `default` row of that button and then to the default byte.
Without the table every shoe uses the default. An
Art's `HitPower` and `HitTarget` become `Upper 0x16+i`, `Lower 0x1B+i`, `UpperMissesShort 0x0C+i`,
`LowerMissesFloating 0x11+i`. `0x16` and above reach every enemy; below it, an enemy of
`TargetClass` 2 (floating) is missed by `0x11+` and one of class 3 (short) by the rest.

---

## 8. One hit

`ResolveHit` runs the original's melee kernel (`UBattleFormulas::ResolveMelee`):

1. **Miss by height** (hero attackers only) → a `Miss` event, nothing else.
2. **Juggle**: a hit that lands while the defender's reaction window is still open adds 1 to its counter;
   outside it, back to 1. The window is the flinch / block clip's beat over its rate, in frames.
3. **Block** (the defender has the block clip and is still standing): the two draws, `a = SPD + ATK·4/5`
   against `d = SPD + ATK·4/5`; in a block window it keeps blocking, in a flinch window it cannot. A block
   deals nothing (a `Block` event) and resets the juggle.
4. **Damage**: base offense = ATK + the equipment fold of the hit (Arms ½ weapon, Ra-Seru ½ tier, High / Low ½ shoes, an
   Art ½ of the five pieces); attack roll × power ≫ 4 + HP ≫ 8 + juggle; an Art × 13/10; the element matrix in integer
   percent (an Art takes it twice); the defense roll; the underdog rewrite and the chip floor (every hit that lands does at
   least 3); capped at defense + 9999. **The angle and distance terms are 0** (positions do not feed the rules).
5. The reaction: a hit that kills — or, on someone with a get-up clip, the **last hit of the action that connects with the
   target** if the combo took more than ¼ of max HP or left it under ¼ — knocks down; the others flinch and open the window.
   The original sums the combo and decides the knockdown once, on the hit that writes the total (`FUN_801EC3E4`), so the hits
   before it only flinch even when the running total is already past ¼ (`JRPG.Battle.Knockdown`).

The Evil Fly example from the RE (`battle-formulas.md`) is a test (`JRPG.Battle.EvilFly`): 275 / 283 / 536 — the
first hit is 1 more than the RE's 274 because the RE adds the distance term.

---

## 9. Arts in the fight

The input window stays local and sends the chain as bytes; the rules recognise it again from the
buttons (never the payload) with `UJRPGArtsRules::BuildChain` over the hero's snapshot and the fight's Rng.

- A **loose Regular** Art credits its `APGain` at its **first hit**; a Hyper / Super / Miracle **debits**
  its `APCost` when it starts; the Arts inside a fired Super move nothing. What the action does not reach is
  neither credited nor debited (an Art goes on after its target falls and credits as usual).
- An Art the hero did not know is **learned on the combatant** when the action reaches it
  (`ArtRecognized` + `ArtLearned` events) and written to the save through `UArtsSubsystem::LearnArt` at the end.
- Not enough AP for a Super → it falls back to the Arts it is made of, which add AP (`JRPG.Battle.SuperInFight`).

---

## 10. What to check after building

Nothing here has been built or run by the author. In the editor:

1. **Re-import** `DT_Enemies` (new columns `TargetClass` and `SizeClass`), `DT_Bosses` (new `bScriptedFight`) and import the three
   new tables `DT_EnemyActions`, `DT_RaSeruTiers`, `DT_WeaponCosts`, `DT_HeroSwings`, `DT_HeroReactions` (Row Structures
   `JRPGEnemyActionData`, `JRPGRaSeruTierData`, `JRPGWeaponCostData`, `JRPGHeroSwingData`, `JRPGHeroReactionData`, at `/Game/Data/`). The importer may complain about the `Anim`
   paths of monsters not imported yet; those rows are still used for their frames.
2. Run the automation tests: Session Frontend → Automation → filter `JRPG.Battle` (and `JRPG.Arts`).
   `JRPG.Battle.SuperInFight` needs `DT_Arts` imported. They run without a map.
3. In a map with an arena: `JRPG.Battle.Start gimard 2`, then `JRPG.Battle.Auto` until the round plays; look
   at the log lines "Battle: …" and the numbers over the heads. `JRPG.Battle.Arts High Low High 0` should
   learn the Art once.
4. Check the pawns walk to the enemy and stop at its reach (the size-class law above; `ReachDistance`,
   `StopMargin` and the knockback values are on `AJRPGBattlePawn`), that a defender turns and slides back on each hit, that
   a knocked-down fighter gets up, and that the clips play (a monster plays its row's `Anim`; a hero looks for `AS_<Name>_Battle_<Clip>` next to its
   mesh).

Not in Phase 5: Item, Escape, formation advantage, drop / steal (Phase 7); magic and the monsters' scripts
(Phase 8); the parry (its window and effect are open question 5; the input is accepted, stamped and kept, and has
no effect yet); level scaling of the enemy stats (no curve is defined: the row is used as is).

---

## 11. Events and delegates

For a UI (Phase 7) or Blueprint: `UBattleSubsystem::OnBattleEvent` (one `FJRPGBattleEvent` at a time:
`RoundStart`, `ActionStart`, `Damage`, `Miss`, `Block`, `Death`, `APChanged`, `ArtRecognized`, `ArtLearned`,
`ComboTotal`, `ActionEnd`, with the action and hit index and the logic frame), `OnSlotChanged` (whose command is being entered /
who is acting), `OnLogicFrame` (the presentation clock: +1 per logic step, monotonic; the `Event.Frame` of `OnBattleEvent` is on the same
clock, while `GetEventLog()` and the timelines keep the rules' own frames, `GetPresentationOffset()` converts), `GetEventLog()`, `GetState()`. The UI builds the text; the events carry
no `FText`. `FindTimeline(ActionIndex)` gives the frames of any action of the fight.

## 12. The tests

`JRPG.Battle.*` (all without a map): `PowerBytes`, `Element`, `EquipmentFold`, `Melee`, `EvilFly`, `Block`, `Initiative`,
`EnemyQueue`, `Time`, `Rand` — the formulas against the RE's numbers; `Round`, `CommandOwner`, `Retarget`, `AttackerDies`,
`Ties`, `KernelWiring`, `Combo`, `Knockdown`, `TargetFalls`, `Fallback`, `BeatBeyondClip`, `ReactionClip`, `HoldExecution`, `ArtsInFight`, `SuperInFight`, `BlockInFight`, `EndOnce`, `VersionRules`, `PostActionHold` (the wait for the target's reaction, the Done band and the
late win/wipe test), `HeroCut` (a hero swing is cut at beat + 3) — the
state machine and the failure table of the roadmap; `Determinism` — the same seed, commands and inputs give exactly the
same events.


---

## 13. Battle UI

A **placeholder** command menu, so a fight can be played and tested without typing console commands. It
is a screen of the WebUI shell (`battle_open`) driven by a pure C++ state machine; the look is rough on
purpose (the original's screens were the reference for the *flow*, not for the visuals). Written, to
build and test.

Every screen is a four-way d-pad (left / up / right / down around a centre icon), driven by the
keyboard (arrows / WASD, Enter, Esc or Backspace), the gamepad (d-pad / stick, A, B) and the mouse (a
click is the same press as the key). A breadcrumb at the top-left says where you are
(`Begin › Vahn › Attack`).

### 13.1 When it shows

It opens when the scene is up (`OnSceneAssembled`) **and** the round waits for a command of this
player's hero (`OnSlotChanged`: `Commands`, `ActiveSlot >= 0`). It closes when the round starts playing
(the stage becomes `Execution`) and when the fight ends; the next round's command phase brings it back
on the Start screen. A party with no hero of this player (all AI) never sees it. If the WebUI is busy
(the pause menu is open) it does not open and the console keeps working.

### 13.2 The flow

| Screen | d-pad | Notes |
|---|---|---|
| **Start** | **Left** Begin · **Up** Tactics · **Right** Escape | The party panel (name, LV, HP, MP) at the bottom. The Up label is one constant, `JRPGBattleMenuText::TacticsLabel` ("Tactics" or "Quick Battle": undecided) |
| **Escape** | **Left** Confirm · **Right** Cancel | Confirm ends the fight as `Escaped` (13.5). Drawn with an X in a scripted fight |
| **Tactics** | rows, not the d-pad | One row per hero of the fight: Aggressive / Standard / Defensive (left / right), up / down changes hero, Enter or the Confirm button applies and turns the auto on, Esc goes back without changing anything |
| **Command** (a hero) | **Left** Attack · Up Item · Right Ra-Seru (Meta / Terra / Ozma) · Down Spirit | Item and Ra-Seru are drawn with an **X** and are not selectable in this phase. **Spirit** commits the hero's Spirit at once (no target) and moves to the next hero, or to the Final screen after the last. HP / MP / AP at the bottom. Esc goes back to the previous hero (from the first hero: to the Start screen) |
| **Attack** | the four buttons: **Up** High · **Down** Low · **Left / Right** Arms and Ra-Seru *by the hero's hand* | The art block shows the buttons entered, the slots that still fit and the gauge left. Enter confirms, Esc removes the last button (empty: back). A refusal is written on the screen |
| **Target** | left / right (or a click) | The enemy's name; the same species gets A / B ("Frog A", "Frog B"), fixed for the whole fight |
| **Final** | **Left** Begin · **Right** Reselect | One row per hero: `Vahn | Attack | Frog A`. Reselect drops every choice and goes back to the first hero. Esc undoes only the last hero's choice |

The Arms button follows the core's rule (`UJRPGArtsRules::ButtonForPad`): Vahn's and Gala's is on the
left, **Noa's (left-handed) on the right**, with Ra-Seru on the other side. High is always up and Low
always down.

### 13.3 How it maps onto the core

- **A buffer, then the core.** The heroes' choices live in `FJRPGBattleCommandPlan` (one
  `FJRPGBattleCommand` per hero, in party order, plus a cursor). Nothing reaches `UBattleSubsystem`
  until the **Begin of the Final screen**, which sends them **one by one, in order** through
  `SubmitCommand` (the last one starts the execution). The core needed no change: it already accepts a
  slot's command at any time while the stage is `Commands`, so collecting first costs nothing, and
  Reselect only has to clear the buffer.
- **Rule 22.** Every command carries `PlayerIndex = LocalPlayerIndex` (the player that sent it) and the
  core checks that this player owns the slot. Only heroes owned by that player enter the buffer. The
  indices that come from the JS (`pick`) are validated in the menu.
- **Attack vs Arts.** A single Arms button is `Attack`; any other chain is `Arts` with the bytes
  (`UJRPGArtsRules::EncodeArtBytes`). The gauge is checked while the buttons are entered with the rule
  the core applies at the door (each button's `ButtonCost` against `CommandGauge`), so the Begin is not
  refused. If the core refuses anyway, the menu starts over for whoever is still missing, with the
  core's reason (`OnCommandRefused`) on the screen and the auto off.
- **The target is a slot**, never a position, as in the core.
- **HP / MP / AP and the gauge** come from the round's combatant snapshot (`FJRPGCombatant`), so the
  panel always shows the fight's numbers, not the roster's.

### 13.4 Auto-battle and Tactics

Confirming the Tactics screen stores one tactic per hero **for the whole fight** and turns the auto on:
the round plays by itself and so does every following round.

| Tactic | What the hero does |
|---|---|
| **Standard** | One Arms swing at the living enemy of lowest slot (what `JRPG.Battle.Auto` does) |
| **Aggressive** | The longest chain that fits the gauge: the longest *Regular* Art the hero knows, then the cheapest button as many times as still fit (up to 16 buttons) |
| **Defensive** | Spirit every round (defence x3 for the round, extended gauge, AP) |

With the auto on, each command phase shows the Start screen with an **AUTO-BATTLE starting** banner for
`AutoStartDelay` seconds (1.2 s), then the round goes out by the tactics. **Cancel** (Esc / B / the
banner's button) in that window turns the auto off and leaves you on the Start screen in manual mode;
the tactics are kept. If the auto cannot plan the round (no living target) it turns itself off.

`TODO` (Aggressive): Hyper / Super / Miracle (they spend AP) join the choice once the auto checks the AP
of the chain.

### 13.5 Escape

`Escape` → Confirm calls `EndBattle(EJRPGBattleResult::Escaped)` (HP / MP / AP written back, the fallen
lifted to 1 HP, no rewards). A scripted fight (`bScriptedFight`) cannot be escaped: the button is
disabled and the controller refuses it again on its side. **`TODO(Phase 7)`**: the flee roll (see
[BATTLE_ROADMAP.md › Phase 7](../BATTLE_ROADMAP.md#phase-7--remaining-commands-and-presentation)); today
the escape always works.

### 13.6 Where it lives

| Piece | What it is |
|---|---|
| `FJRPGBattleMenu` (`Battle/JRPGBattleMenu.h`) | The state machine, **no UObject, no WebUI**: `Press(key)` / `Pick(a, b)` in, a request out (`Commit`, `Escape`, `AutoRound`, `AutoOff`) and `BuildViewJS()`. `FJRPGBattleCommandPlan` (the buffer) and `JRPGBattleTactics` (tactic → command) are in the same header |
| `UJRPGBattleUISubsystem` (`Battle/JRPGBattleUISubsystem.h`) | World subsystem (nothing to place): shows / hides the menu from the fight's delegates, builds the round snapshot, sends the buffer to `SubmitCommand`, runs the auto timer and the escape. `bUIEnabled`, `LocalPlayerIndex` and `AutoStartDelay` are properties on it (defaults in the header) |
| `UWebUISubsystem::OpenBattleUI` / `UpdateBattleUI` / `CloseBattleUI` | The only door to the screen; `bridge.onbattleinput` → `OnBattleUIInput`. Contract in [WEBUI_API.md](WEBUI_API.md#battle-command-menu) |
| `Content/UI/WebUI/src/*/9?_battle*` | The section, the CSS and the JS. `95_battle_dpad.*` is the d-pad, a **separate piece** to replace with the 3D buttons later |

The on-screen debug prompt and the red refusal line of `UJRPGBattleSceneSubsystem` are hidden while the
menu is up (`SetDebugPromptSuppressed`); both stay in the log, and `JRPG.Battle.Attack / Arts / Auto`
keep working (if one is used with the menu up, the menu restarts on the Start screen with whoever is
still missing).

### 13.7 What to check in PIE

1. `JRPG.Battle.Start gimard 2` (or a field enemy): after the fade-in the Start screen appears with the
   three heroes at the bottom. The mouse cursor is visible; arrows / the pad take over and hide it.
2. Begin → Vahn's menu: Item and Ra-Seru (Meta) have an X and do nothing; Spirit commits at once (Final shows
   `Vahn | Spirit`). Attack → the four
   buttons; check the **left-handed Noa** has Arms on the right. Enter more buttons than the gauge pays:
   the refusal appears and the button dims. Esc removes the last one.
3. Target: left / right and a click; "Gimard A / Gimard B" with two. Next hero; Esc on a hero's first
   screen goes back.
4. Final: the list is right; **Reselect** returns to Vahn with nothing kept; **Begin** plays the round
   and the menu goes away, then comes back for the next round.
5. Tactics: Vahn Aggressive, the others Standard / Defensive, Confirm: the round plays and the next one
   shows the banner; Cancel on it returns to manual.
6. Escape → Confirm ends the fight and the field comes back; in a scripted fight (a boss) the button
   has an X.
7. Resolutions: the layout is the usual 1920 × 1080 canvas × `--scale-factor`; try 16:9 and ultrawide.
8. In a browser (`Content/UI/WebUI/shell.html`), `JRPGBattle.preview('attack')` (and the other screen
   names) draws a screen with fake data.

### 13.8 The tests

`JRPG.Battle.UI.*`, all without a map or WebUI (`Private/Tests/JRPGBattleUITests.cpp`): `Plan` (the
buffer: order, back, Reselect, a Commit that stops at a refusal), `Flow` (the whole menu from Start to
Begin, with the left-handed Noa), `Gauge` (the input window), `Escape` (including the scripted fight),
`Tactics` (tactic → command), `Auto` (the tactics and the auto persisting across rounds, Cancel),
`Names` (A / B) and `Resolver` (what the Begin sends is accepted by the real `FJRPGBattleResolver`, in
order).
