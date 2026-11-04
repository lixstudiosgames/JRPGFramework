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

The HUD is Phase 7. In PIE, with the console (`JRPG.Battle.Start` and `.End` are in
[BATTLE_STAGE.md](BATTLE_STAGE.md)):

| Command | What it does |
|---|---|
| `JRPG.Battle.Start gimard 2` | A fight against two Gimards where the player stands |
| `JRPG.Battle.Attack [enemy]` | The command of whoever is being asked (the on-screen prompt says who): one Arms swing. `enemy` is a number, **0 = the first enemy**; without it, the living enemy of lowest slot |
| `JRPG.Battle.Arts <buttons> [enemy]` | The Art chain: `JRPG.Battle.Arts High Low High 0`, `JRPG.Battle.Arts "A R A"` (letters), or arrows. The hero's handedness applies (Noa's Arms is the right arrow). The gauge limits how many buttons fit |
| `JRPG.Battle.Auto` | Everyone who has not commanded attacks the lowest living enemy: walks the round without typing one by one |
| `JRPG.Battle.End [victory\|defeat\|escape\|abort]` | Ends the fight from outside. `abort` leaves the party exactly as it was |
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
| `bScriptedFight` | The original's scripted-fight flag (`ctx+0x287`): the enemies take the **boss** stat profile, the fight cannot be escaped, no formation advantage. A fight from `DT_Bosses` copies `bScriptedFight` of the row; a random encounter leaves it false |
| `bNoRewards` | A fight that pays nothing (the original's special battles) |
| `FormationAdvantage` | 0 none, 1 back attack (the party loses its round-1 initiative keys), 2 pre-emptive (the enemies do). The roll is Phase 7; Phase 5 honours what arrives |

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
- **Retarget**: a hero whose target fell hits the **lowest living enemy slot**; a monster draws another
  living hero; none left → the action is dropped and nothing is spent.
- **The enemy AI** is the original's physical half: uniform random living hero, swings drawn from
  its `Swings` while the pool pays. The magic roll and the per-monster scripts are Phase 8 (a monster
  with magic always takes the physical branch).
- Commands are checked at the door (`SubmitCommand`): the issuer must own the slot, the target must be
  a living foe, Art bytes must be buttons that fit the gauge. `Item`, `Magic`, `Escape` and `Guard` are
  refused until Phases 7 / 8.

---

## 7. The logic timeline

An action's frames are built **when it starts**, from data, as an `FJRPGActionTimeline`; every hit is
resolved at **its own frame** (`ResolveHit`), never by an animation notify. One logic frame is **1/60 s**
(the original's tick). A clip of rate *R* at game speed *S* (8 normal, 4 in an Art, 2 after the
special starter) advances *S·R/32* clip frames per logic frame.

- **Monsters**: the `DT_EnemyActions` row. A hit beat *b* fires at clip frame *b − 1*; the power byte is
  the row's byte at the hit's index. With a next swing queued and `Gate` 0, the next swing starts at beat + 3 and the
  hits that would fall after it are lost, as in the original.
- **The party**: the clip's length and its `UJRPGAnimNotify_Hit` markers, read **as data** when the
  action starts. **The markers are not on the party's clips yet**, so for now the fallback by kind is
  the normal path: a plain swing is one hit; an Art is *N* hits (`HitPower`) spaced along the clip. The
  frames are `UBattleSubsystem::TimelineDefaults` (editable) and the use is logged once per kind of step
  ("golpe simples sem marcador de golpe no clip: quadros padrão"). Placing the markers
  ([ARTS.md](ARTS.md)) is how a clip gets its real frames.
- **Pause**: the counter does not advance while the game is paused; the fight waits at the command
  prompt for as long as the player thinks.

A hero's hit bytes: Arms, Ra-Seru and High swings `0x18` (power 20, UDF), Low `0x1D` (power 20, LDF); an
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
5. The reaction: a hit that kills — or, on someone with a get-up clip, one that takes more than ¼ of max HP in one action or
   leaves it under ¼ — knocks down; the others flinch and open the window.

The Evil Fly example from the RE (`battle-formulas.md`) is a test (`JRPG.Battle.EvilFly`): 275 / 283 / 536 — the
first hit is 1 more than the RE's 274 because the RE adds the distance term.

---

## 9. Arts in the fight

The input window stays local and sends the chain as bytes; the rules recognise it again from the
buttons (never the payload) with `UJRPGArtsRules::BuildChain` over the hero's snapshot and the fight's Rng.

- A **loose Regular** Art credits its `APGain` at its **first hit**; a Hyper / Super / Miracle **debits**
  its `APCost` when it starts; the Arts inside a fired Super move nothing. What the action does not reach is
  neither credited nor debited (an Art cut short by the last enemy's death still credits).
- An Art the hero did not know is **learned on the combatant** when the action reaches it
  (`ArtRecognized` + `ArtLearned` events) and written to the save through `UArtsSubsystem::LearnArt` at the end.
- Not enough AP for a Super → it falls back to the Arts it is made of, which add AP (`JRPG.Battle.SuperInFight`).

---

## 10. What to check after building

Nothing here has been built or run by the author. In the editor:

1. **Re-import** `DT_Enemies` (new column `TargetClass`), `DT_Bosses` (new `bScriptedFight`) and import the three
   new tables `DT_EnemyActions`, `DT_RaSeruTiers`, `DT_WeaponCosts` (Row Structures `JRPGEnemyActionData`,
   `JRPGRaSeruTierData`, `JRPGWeaponCostData`, at `/Game/Data/`). The importer may complain about the `Anim`
   paths of monsters not imported yet; those rows are still used for their frames.
2. Run the automation tests: Session Frontend → Automation → filter `JRPG.Battle` (and `JRPG.Arts`).
   `JRPG.Battle.SuperInFight` needs `DT_Arts` imported. They run without a map.
3. In a map with an arena: `JRPG.Battle.Start gimard 2`, then `JRPG.Battle.Auto` until the round plays; look
   at the log lines "Battle: …" and the numbers over the heads. `JRPG.Battle.Arts High Low High 0` should
   learn the Art once.
4. Check the pawns walk to the enemy and stop at `ReachDistance` (adjust on `AJRPGBattlePawn`), and that
   the clips play (a monster plays its row's `Anim`; a hero looks for `AS_<Name>_Battle_<Clip>` next to its
   mesh).

Not in Phase 5: Item, Escape, Spirit, formation advantage, drop / steal (Phase 7); magic and the monsters' scripts
(Phase 8); the parry (its window and effect are open question 5; the input is accepted, stamped and kept, and has
no effect yet); level scaling of the enemy stats (no curve is defined: the row is used as is).

---

## 11. Events and delegates

For a UI (Phase 7) or Blueprint: `UBattleSubsystem::OnBattleEvent` (one `FJRPGBattleEvent` at a time:
`RoundStart`, `ActionStart`, `Damage`, `Miss`, `Block`, `Death`, `APChanged`, `ArtRecognized`, `ArtLearned`,
`ActionEnd`, with the action and hit index and the logic frame), `OnSlotChanged` (whose command is being entered /
who is acting), `OnLogicFrame`, `GetEventLog()`, `GetState()`. The UI builds the text; the events carry
no `FText`. `FindTimeline(ActionIndex)` gives the frames of any action of the fight.

## 12. The tests

`JRPG.Battle.*` (all without a map): `PowerBytes`, `Element`, `EquipmentFold`, `Melee`, `EvilFly`, `Block`, `Initiative`,
`EnemyQueue`, `Time`, `Rand` — the formulas against the RE's numbers; `Round`, `CommandOwner`, `Retarget`, `AttackerDies`,
`Ties`, `KernelWiring`, `Combo`, `Fallback`, `ArtsInFight`, `SuperInFight`, `BlockInFight`, `EndOnce`, `VersionRules` — the
state machine and the failure table of the roadmap; `Determinism` — the same seed, commands and inputs give exactly the
same events.
