# JRPGFramework — Plugin Structure

Legend of Legaia Remake | Unreal Engine 5.8 | C++ | LIX Studios

Legend: ✅ implemented · 🟡 stub (file exists, no real state or logic) · no mark = planned (file does **not** exist yet)

```
JRPGFramework/
├── JRPGFramework.uplugin ✅                     ← EngineVersion 5.8.1, single Runtime module
├── SETUP.md ✅                                  ← build from a clean clone (incl. Ultralight SDK)
│
├── Source/JRPGFramework/
│   ├── JRPGFramework.Build.cs ✅                ← links the Ultralight SDK + copies its DLLs
│   │
│   ├── Public/
│   │   ├── JRPGFramework.h ✅
│   │   │
│   │   ├── Core/ ✅
│   │   │   ├── CoreSubsystem.h ✅               ← Gold (Add/Remove/Get + OnGoldChanged), playtime
│   │   │   │                                       (tick-free), current map, player position for
│   │   │   │                                       saves, StartNewGame/bIsNewGame, XP curve, stat
│   │   │   │                                       growth and difficulty
│   │   │   ├── ProgressionTypes.h ✅            ← EJRPGDifficulty + FDifficultyScaling, EJRPGRuleset,
│   │   │   │                                       FLevelCurveRow, FStatGrowthRow, FGrowthCurveRow
│   │   │   ├── JRPGGameInstance.h ✅            ← GameInstance base class (GI_Main inherits it)
│   │   │   ├── JRPGSplineArea.h ✅              ← FJRPGSplineArea: a closed spline seen from above as a
│   │   │   │                                       2D area (contains / distance / random point);
│   │   │   │                                       encounter zone + battle arena
│   │   │   └── JRPGTypes.h ✅                   ← global enums and structs
│   │   │
│   │   ├── Save/ ✅                             ← SAVE/LOAD COMPLETE
│   │   │   ├── SaveSubsystem.h ✅               ← 15 slots; SaveGame gathers everything itself;
│   │   │   │                                       LoadGame opens the saved map and restores in
│   │   │   │                                       PostLoadMapWithWorld
│   │   │   ├── JRPGSaveGame.h ✅                ← version, timestamp, map, position, gold, playtime,
│   │   │   │                                       inventory, flags, chests, trees, party display
│   │   │   └── SaveTypes.h ✅                   ← FSaveSlotMetadata + FSavePartyMemberDisplay
│   │   │
│   │   ├── Inventory/ ✅
│   │   │   ├── InventorySubsystem.h ✅          ← add/remove/query/use + save-ready
│   │   │   └── ItemData.h ✅                    ← FItemData (DT_Items row) + FInventorySlot
│   │   │
│   │   ├── World/ ✅
│   │   │   ├── WorldStateSubsystem.h ✅         ← event flags, chests, Revival Trees, bIsInField
│   │   │   │                                       (gates the SAVE tab) + reset/load
│   │   │   ├── EventFlag.h ✅
│   │   │   ├── RevivalTreeState.h ✅
│   │   │   ├── JRPGMistVolume.h ✅              ← box of living mist injected into the volumetric
│   │   │   │                                       fog (shader in Shaders/JRPGMist.ush)
│   │   │   ├── JRPGMistBlocker.h ✅             ← area the mist no longer enters (event flag / Revival
│   │   │   │                                       Tree stage), with a transition
│   │   │   ├── JRPGMistDisturberComponent.h ✅  ← goes on whatever moves and should stir the mist
│   │   │   └── JRPGMistSubsystem.h ✅           ← picks the shader's 8 slots, writes MPC_JRPGMist,
│   │   │                                           paints the trail texture
│   │   │
│   │   ├── VFX/ ✅                              ← VISUAL EFFECTS AS DATA (built; the Spirit auras use it)
│   │   │   ├── JRPGVFXTypes.h ✅                ← FJRPGVFXLayer (Niagara / Mesh / Light / BodyParam /
│   │   │   │                                       CameraShake), tracks, FJRPGVFXContext, FJRPGVFXRow (DT_VFX)
│   │   │   ├── JRPGVFXRecipe.h ✅               ← UJRPGVFXRecipe: one effect = a timeline of layers
│   │   │   ├── JRPGVFXSubsystem.h ✅            ← plays recipes: spawns, anchors, animates and pools the
│   │   │   │                                       layers; JRPG.VFX.Play / JRPG.VFX.StopAll
│   │   │   └── JRPGAnimNotify_VFX.h 🟡          ← fires a recipe from a clip frame
│   │   │
│   │   ├── Field/ ✅                            ← THE CHARACTER WALKING THE MAP (no battle)
│   │   │   ├── JRPGFieldCharacter.h ✅          ← top-down camera with a stack of camera zones,
│   │   │   │                                       walk/sprint, faces where it walks, mist
│   │   │   │                                       disturber, declares bIsInField
│   │   │   ├── JRPGFieldPlayerController.h ✅   ← Enhanced Input (built-in defaults), movement
│   │   │   │                                       relative to the camera or a fixed yaw, sprint,
│   │   │   │                                       character switch (R3) without a camera cut
│   │   │   ├── JRPGFieldPartyComponent.h ✅     ← the formation walking the map: leash/trail follow,
│   │   │   │                                       gathering beside the stopped leader (navmesh),
│   │   │   │                                       safety teleport, leader switch, cutscene API
│   │   │   ├── JRPGPartyFollowerController.h ✅ ← AI controller the followers are possessed by (MoveTo)
│   │   │   ├── JRPGFieldEnemy.h ✅              ← the visible enemy: wander/chase/return, freezes in
│   │   │   │                                       menus, contact rolls the encounter; model from
│   │   │   │                                       DT_Enemies. See Guides/FIELD_ENEMIES.md
│   │   │   ├── JRPGFieldEnemyController.h ✅    ← AI controller of the field enemies (MoveTo)
│   │   │   ├── JRPGFieldEnemyAnimInstance.h ✅  ← native anim instance, no Anim Blueprint: Idle/Walk/Run
│   │   │   │                                       with crossfade, notifies fire; any skeleton
│   │   │   ├── JRPGEncounterZone.h ✅           ← sphere/box/spline area keeping MinAlive..MaxAlive enemies
│   │   │   │                                       (spacing, clear of objects, editor preview), respawn,
│   │   │   │                                       Required / Disable flags, its BattleArena, random encounters
│   │   │   ├── JRPGRandomEncounterSubsystem.h ✅ ← counts the player's steps in zones with random
│   │   │   │                                       encounters (the original's step counter)
│   │   │   ├── JRPGFieldMapSettings.h ✅        ← one per map: base camera, controls, speeds,
│   │   │   │                                       whether the party shows
│   │   │   ├── JRPGInteractable.h ✅            ← interface: Interact(InteractingActor)
│   │   │   ├── JRPGInteractionComponent.h ✅    ← picks the closest target in front, calls Interact;
│   │   │   │                                       bridge for old Blueprint interfaces
│   │   │   └── JRPGFieldTypes.h ✅              ← FJRPGFieldCameraSettings, EJRPGFieldControlMode
│   │   │
│   │   ├── UI/ ✅                               ← WEBUI VIA ULTRALIGHT 1.4 (GPU mode, D3D11)
│   │   │   ├── WebUISubsystem.h ✅              ← owns the shell: OpenDevMenu/RunDevCommand (dev
│   │   │   │                                       only, gone in Shipping), InitializeUIShell,
│   │   │   │                                       ShowItemPopup, OpenMenu/CloseMenu,
│   │   │   │                                       OpenSaveLoad/OpenSaveScreen/OpenLoadScreen,
│   │   │   │                                       CanSaveNow, QuitToMainMenu, OpenShop,
│   │   │   │                                       OpenOptions, OpenMainMenu, SetUICursorVisible,
│   │   │   │                                       SetUIState, ReloadShell, OpenBattleUI /
│   │   │   │                                       UpdateBattleUI / CloseBattleUI (the battle menu)
│   │   │   ├── WebUIBridge.h ✅                 ← JS→C++ bridge (the shell's callbacks + onbattleinput)
│   │   │   ├── WebContainerWidget.h ✅          ← UUserWidget hosting the browser
│   │   │   ├── JRPGWebBrowser.h ✅              ← UMG UWidget wrapping SUltralightBrowser
│   │   │   └── WebUIScripting.h ✅              ← ToJSStringLiteral (safe escaping for ExecuteJS)
│   │   │
│   │   ├── Shop/ ✅                             ← SHOP COMPLETE
│   │   │   ├── ShopSubsystem.h ✅               ← atomic purchase validated against resolved stock,
│   │   │   │                                       selling (BuyPrice/2), featured items gated by
│   │   │   │                                       platinum_card, availability by event flag
│   │   │   └── ShopData.h ✅                    ← FShopData (DT_Shops row) + FShopStockEntry
│   │   │
│   │   ├── Camera/ ✅                           ← CAMERA COMPLETE
│   │   │   ├── CameraSubsystem.h ✅             ← actor focus with presets, automatic restore when
│   │   │   │                                       the UI closes, hand-placed level cameras, target
│   │   │   │                                       registry, shot-reverse-shot conversation,
│   │   │   │                                       FrameGroup/Orbit (battle groundwork), shakes
│   │   │   ├── CameraData.h ✅                  ← 13 framing presets, 8 shakes, FCameraPresetRow
│   │   │   ├── JRPGCameraActor.h ✅             ← managed camera (one per map, transient)
│   │   │   ├── JRPGCameraShakeModifier.h ✅     ← procedural Perlin shake via UCameraModifier
│   │   │   └── JRPGCameraZone.h ✅              ← box trigger: Level Camera or Player Camera;
│   │   │                                           stacks with priority on the field character
│   │   │
│   │   ├── Audio/ ✅                            ← AUDIO COMPLETE
│   │   │   ├── AudioSubsystem.h ✅              ← BGM persisting across maps with crossfade +
│   │   │   │                                       4 volume channels (with effective getters),
│   │   │   │                                       intro + loop music (GetIntroLoopSound)
│   │   │   ├── BattleMusicData.h ✅             ← FJRPGBattleMusicData (DT_BattleMusic: the battle tracks)
│   │   │   └── JRPGSettingsSave.h ✅            ← settings USaveGame (slot "JRPGSettings")
│   │   │
│   │   ├── Party/ ✅
│   │   │   ├── CharacterData.h ✅               ← FCharacterData (DT_Characters row): identity,
│   │   │   │                                       Ra-Seru, affinity, weapon classes, XPCurveSlot,
│   │   │   │                                       8 level-1 stats + EJRPGElement
│   │   │   ├── JRPGPartyTypes.h ✅              ← FJRPGPartyMember: one character's record
│   │   │   │                                       (3 states, level/XP, 8 stats, equipment,
│   │   │   │                                       conditions, RaSeruLevel) + EJRPGEquipSlot (7 slots:
│   │   │   │                                       4 gear + 3 accessories)
│   │   │   └── PartySubsystem.h ✅              ← roster vs formation, recruit/activate/bench,
│   │   │                                           XP split among survivors, level-up, vitals, save
│   │   │
│   │   ├── Status/ ✅
│   │   │   └── StatusSubsystem.h ✅             ← equipment (7 slots), conditions, Ra-Seru,
│   │   │                                           elemental affinity and the EFFECTIVE stat.
│   │   │                                           Owns the RULES: writes into the Party record,
│   │   │                                           holds no state of its own
│   │   │
│   │   ├── Arts/ ✅                             ← Phase 6; played in a fight with Phase 5
│   │   │   ├── ArtsData.h ✅                    ← FJRPGArtData (DT_Arts row: buttons, AP gain/cost,
│   │   │   │                                       clip, per-hit power and target, Sequence for
│   │   │   │                                       combined Arts, Unlock, SpeedScale). See Guides/ARTS.md
│   │   │   ├── JRPGArtsRules.h ✅               ← UJRPGArtsRules: buttons → the turn (FJRPGArtChain) as
│   │   │   │                                       pure functions — the original's overlapping
│   │   │   │                                       tokenizer, learn-on-use, Supers, the new AP walk,
│   │   │   │                                       the input window and its bytes
│   │   │   ├── JRPGArtsValidator.h ✅           ← UJRPGArtsValidator: what a DT_Arts row must satisfy
│   │   │   │                                       (tests, Data Validation; Extras/arts_rules.py mirrors it)
│   │   │   ├── JRPGArtSequence.h ✅             ← UJRPGArtSequenceLibrary: a turn's clip queue as the game
│   │   │   │                                       stages it (leading swings, starter, Art, Sequence),
│   │   │   │                                       its speed scale per step, the Chain Cut. Shared by
│   │   │   │                                       the battle and the tester
│   │   │   ├── JRPGArtTester.h ✅               ← test bench: the hero's battle model playing an Art,
│   │   │   │                                       a hand-made combo, all of them, or typed buttons
│   │   │   │                                       through the rules (Input + Recognition)
│   │   │   └── ArtsSubsystem.h ✅               ← the learned list (FJRPGPartyMember::LearnedArts, in
│   │   │                                           the save), LearnArt / HasLearnedArt, preview and
│   │   │                                           commit out of battle, JRPG.Arts.* console
│   │   │
│   │   └── Battle/ ✅
│   │       ├── BattleSubsystem.h ✅             ← TryStartEncounter, BeginBattle (the snapshot) / EndBattle
│   │       │                                       (write-back, rewards once), the fixed-step clock,
│   │       │                                       SubmitCommand / SubmitInput, OnBattleEvent / OnSlotChanged
│   │       │                                       (Phase 5, played; Guides/BATTLE.md);
│   │       │                                       ApplyBattleItemEffect still TODO (Phase 7)
│   │       ├── JRPGBattleResolver.h ✅          ← FJRPGBattleResolver (no UObject): the round, the logic
│   │       │                                       timeline, ResolveHit, the original's physical AI, Arts + AP
│   │       ├── BattleFormulas.h ✅              ← UBattleFormulas: the melee kernel, block, element matrix,
│   │       │                                       equipment fold, initiative, enemy swing queue (pure, integer)
│   │       ├── BattleTypes.h ✅                 ← side, combatant type, result, FJRPGCombatant (the rules-only
│   │       │                                       snapshot), command / input / event / state / timeline,
│   │       │                                       FJRPGBattleSetup, FJRPGBattleVisual, the hero's
│   │       │                                       equipment pieces (EJRPGBattlePiece, DT_EquipVisuals row)
│   │       ├── EnemyActionData.h ✅             ← FJRPGEnemyActionData (DT_EnemyActions row: a monster clip's
│   │       │                                       hit frames, power bytes, AGL cost, rate)
│   │       ├── HeroBattleData.h ✅              ← FJRPGRaSeruTierData (DT_RaSeruTiers), FJRPGWeaponCostData
│   │       │                                       (DT_WeaponCosts), FJRPGHeroSwingData (DT_HeroSwings),
│   │       │                                       FJRPGHeroReactionData (DT_HeroReactions), FJRPGArtCameraTrackData
│   │       │                                       (DT_ArtCamera), FJRPGBattleVoiceBankData (DT_BattleVoices),
│   │       │                                       FJRPGArtVoiceData (DT_ArtVoices)
│   │       ├── JRPGBattleArena.h ✅             ← where on the map a fight is assembled, and who
│   │       │                                       assembles it: formation preview + ground check,
│   │       │                                       hand-drawn area, FindArenaFor, AssembleBattle /
│   │       │                                       TeardownBattle (BATTLE_ROADMAP Phase 4)
│   │       ├── JRPGBattleCamera.h ✅            ← the battle camera (the original's framing cases, ease-out,
│   │       │                                       orbit), spawned by the arena. Guides/BATTLE_STAGE.md
│   │       ├── JRPGBattleSceneSubsystem.h ✅    ← world subsystem: picks the arena, fade, OnFieldHidden /
│   │       │                                       OnFieldShown, console commands, battle music. Guides/BATTLE_STAGE.md
│   │       ├── JRPGBattleVoices.h ✅            ← the fight's voice channel: Arts shout, starter fanfare,
│   │       │                                       block grunt (Guides/AUDIO.md › Battle voices)
│   │       ├── JRPGBattleMenu.h ✅              ← the battle command menu's state machine (no UObject, no
│   │       │                                       WebUI): FJRPGBattleMenu (Start / Tactics / Command /
│   │       │                                       Attack / Target / Final), FJRPGBattleCommandPlan (the
│   │       │                                       buffer sent on Begin), JRPGBattleTactics (the auto)
│   │       ├── JRPGBattleUISubsystem.h ✅       ← world subsystem: shows the menu in the command phase,
│   │       │                                       sends the buffer to SubmitCommand, auto timer, escape
│   │       │                                       (Guides/BATTLE.md › Battle UI)
│   │       ├── JRPGAnimNotify_ChainCut.h ✅     ← marker on a clip: where the game cuts it when another
│   │       │                                       action is queued (swings chain at frame 10)
│   │       ├── JRPGAnimNotify_LoopWindow.h ✅   ← notify state: the stretch of a clip the original repeats
│   │       │                                       (+0x84 loops of [+0x85, +0x86]: the SpecialStarter's crouch)
│   │       ├── JRPGAnimNotify_Hit.h ✅          ← marker on a clip: one per hit, in HitPower order (the
│   │       │                                       fight reads the frames as data; not on the clips yet)
│   │       ├── JRPGAnimNotify_HitSound.h ✅     ← opt-in marker: the impact sound plays only if the hit
│   │       │                                       connected (HitSound / MissSound); the stock PlaySound near
│   │       │                                       a hit is gated by the pawn too
│   │       ├── JRPGBattlePawn.h ✅              ← one combatant in the scene: battle model in its seat;
│   │       │                                       heroes dressed by equipment pieces (Leader Pose); in a
│   │       │                                       fight it plays the logic timeline and shows the events
│   │       ├── JRPGBattleWaves.h ✅             ← fights in a row, Muscle Dome style, waves from JSON
│   │       ├── JRPGBattleRules.h ✅             ← UJRPGBattleRules: what each version (USA / Japan) does
│   │       │                                       with the raw record — the enemy stat boost, the
│   │       │                                       victory EXP and Gold. Guides/ENEMIES.md
│   │       ├── JRPGEncounterTypes.h ✅          ← FJRPGEncounterEnemy/Setup/Combatant + the roll and
│   │       │                                       DT_Enemies access (UJRPGEncounterLibrary)
│   │       ├── BossFightData.h ✅               ← FJRPGBossData (DT_Bosses row: a boss fight — who enters,
│   │       │                                       named specials, reward, level). See Guides/ENEMIES.md
│   │       └── EnemyData.h ✅                   ← FJRPGEnemyData (DT_Enemies row: the raw disc record,
│   │                                               rewards, capturable Seru, MonsterArchiveID, mesh)
│   │                                               + FJRPGMonsterAnimSet. See Guides/ENEMIES.md
│   │
│   └── Private/
│       ├── JRPGFramework.cpp ✅
│       ├── JRPGFrameworkPCH.h ✅              ← module PCH: reads the engine headers that fire C4996
│       │                                       with the warning off (Guides/BUILDING.md)
│       ├── Core/ ✅        (CoreSubsystem.cpp ✅, JRPGGameInstance.cpp ✅, JRPGSplineArea.cpp ✅)
│       ├── Save/ ✅        (SaveSubsystem.cpp ✅, JRPGSaveGame.cpp ✅)
│       ├── Inventory/ ✅   (InventorySubsystem.cpp ✅)
│       ├── World/ ✅       (WorldStateSubsystem.cpp ✅, JRPGMistVolume.cpp ✅, JRPGMistBlocker.cpp ✅,
│       │                    JRPGMistDisturberComponent.cpp ✅, JRPGMistSubsystem.cpp ✅)
│       ├── VFX/ ✅         (JRPGVFXRecipe.cpp ✅, JRPGVFXSubsystem.cpp ✅, JRPGAnimNotify_VFX.cpp 🟡)
│       ├── Field/ ✅       (JRPGFieldCharacter.cpp ✅, JRPGFieldPlayerController.cpp ✅,
│       │                    JRPGFieldPartyComponent.cpp ✅, JRPGPartyFollowerController.cpp ✅,
│       │                    JRPGFieldMapSettings.cpp ✅, JRPGFieldTypes.cpp ✅,
│       │                    JRPGInteractionComponent.cpp ✅, JRPGFieldEnemy.cpp ✅,
│       │                    JRPGFieldEnemyController.cpp ✅, JRPGFieldEnemyAnimInstance.cpp ✅,
│       │                    JRPGEncounterZone.cpp ✅, JRPGRandomEncounterSubsystem.cpp ✅)
│       ├── Shop/ ✅        (ShopSubsystem.cpp ✅)
│       ├── Audio/ ✅       (AudioSubsystem.cpp ✅, JRPGSettingsSave.cpp ✅)
│       ├── Camera/ ✅      (CameraSubsystem.cpp ✅, CameraData.cpp ✅, JRPGCameraActor.cpp ✅,
│       │                    JRPGCameraShakeModifier.cpp ✅, JRPGCameraZone.cpp ✅)
│       ├── UI/ ✅          (WebUISubsystem.cpp ✅, WebUIBridge.cpp ✅, WebContainerWidget.cpp ✅,
│       │                    JRPGWebBrowser.cpp ✅, WebUIScripting.cpp ✅,
│       │                    UltralightRenderThread.h/.cpp ✅, UltralightGPUDriverD3D11.h/.cpp ✅)
│       │                    ← the dedicated thread and the GPUDriver are PRIVATE on purpose:
│       │                      Ultralight SDK headers never appear under Public/
│       ├── Party/ ✅       (PartySubsystem.cpp ✅)
│       ├── Status/ ✅      (StatusSubsystem.cpp ✅)
│       ├── Arts/ ✅        (ArtsSubsystem.cpp ✅, JRPGArtsRules.cpp ✅, JRPGArtsValidator.cpp ✅,
│       │                    JRPGArtSequence.cpp ✅, JRPGArtTester.cpp ✅)
│       ├── Tests/ ✅       (JRPGArtsTests.cpp ✅ — the JRPG.Arts.* automation tests, no map;
│       │                    JRPGBattleTests.cpp ✅ — the JRPG.Battle.* ones, no map;
│       │                    JRPGBattleUITests.cpp ✅ — JRPG.Battle.UI.*: the command menu)
│       └── Battle/ ✅      (BattleSubsystem.cpp ✅, JRPGBattleResolver.cpp ✅, BattleFormulas.cpp ✅,
│                           JRPGBattleMenu.cpp ✅, JRPGBattleUISubsystem.cpp ✅,
│                           JRPGBattleArena.cpp ✅, JRPGBattleCamera.cpp ✅, JRPGBattleSceneSubsystem.cpp ✅,
│                           JRPGBattlePawn.cpp ✅, JRPGBattleWaves.cpp ✅, JRPGBattleRules.cpp ✅,
│                           JRPGAnimNotify_Hit.cpp ✅, JRPGAnimNotify_HitSound.cpp ✅, JRPGBattleVoices.cpp ✅,
│                           JRPGAnimNotify_LoopWindow.cpp ✅)
│
├── Shaders/ ✅                                  ← mapped to /Plugin/JRPGFramework
│   ├── UltralightBlitShader.usf ✅              ← full-screen blit of the Ultralight texture (JRPGWebBrowser)
│   └── JRPGMist.ush ✅                          ← the living mist (included by M_JRPGMist)
│
├── Content/
│   ├── Editor/ ✅                               ← editor-only icons: T_FieldEnemyIcon, T_EncounterZoneIcon
│   │                                               (from Extras/generate_editor_icons.py)
│   ├── UI/
│   │   ├── WebUI/ ✅                            ← the HTML UI (single SPA shell via Ultralight)
│   │   │   ├── shell.html ✅                    ← GENERATED by Extras/build_webui.py — never edit
│   │   │   ├── src/ ✅                          ← UI SOURCES (edit here, then run the build)
│   │   │   │   ├── shell.template.html ✅       ← skeleton (@@CSS@@/@@SECTIONS@@/@@SCRIPTS@@)
│   │   │   │   │                                   + #ul-repaint-anchor (do not remove)
│   │   │   │   ├── css/ ✅                      ← 00_global (theme + .jrpg-card/.jrpg-tab/
│   │   │   │   │                                   .jrpg-hints + jrpgSelectBreath + kbd-mode),
│   │   │   │   │                                   05_shell, 10_menu, 20_popup, 30_dialogue,
│   │   │   │   │                                   40_options, 50_saveload, 60_shop, 70_mainmenu,
│   │   │   │   │                                   80_party, 85_items, 90_devmenu,
│   │   │   │   │                                   95_battle_dpad (the d-pad, replaceable), 96_battle
│   │   │   │   ├── sections/ ✅                 ← 10_menu, 20_popup, 30_dialogue, 40_options,
│   │   │   │   │                                   50_saveload, 60_shop, 70_mainmenu, 80_party,
│   │   │   │   │                                   85_items, 90_devmenu, 95_battle
│   │   │   │   └── js/ ✅                       ← 00_core (bridge/sound/scaling/kickUIRepaint),
│   │   │   │                                       05_ui_state (state machine + InputMode),
│   │   │   │                                       10_menu, 20_popup, 30_dialogue, 40_options,
│   │   │   │                                       50_saveload, 60_shop, 70_mainmenu, 80_party,
│   │   │   │                                       85_items, 90_devmenu, 95_battle_dpad (the d-pad
│   │   │   │                                       widget), 96_battle (the battle command menu), 99_boot
│   │   │   ├── fonts/ ✅                        ← local Cinzel + MedievalSharp (download_fonts.py)
│   │   │   ├── images/ ✅                       ← 1.webp…15.webp (save slot art) +
│   │   │   │                                       vahn/noa/gala.png (portraits)
│   │   │   └── Concept/ ✅                      ← visual concept HTML (style reference)
│   │   └── SFX/ ✅                              ← SFX_Open/Close/Next/Cancel/Select/Item/Return .wav
│   │
│   └── ThirdParty/Ultralight/                   ← NOT in the repository — see SETUP.md
│
├── Docs/ ✅                                     ← this documentation
│
└── Extras/ ✅                                   ← dev tools (not shipped in the build)
    ├── LegaiaStudio/ ✅                         ← local web panel (studio.py + web/): edits the
    │                                               CSVs record by record, schema read from the C++
    │                                               structs, automatic backup, builds with live log
    ├── build_webui.py ✅                        ← generates shell.html from src/
    ├── build_plugin.py ✅                       ← packages the plugin (isolation + -Rocket flag)
    ├── extract_growth_from_disc.py ✅           ← reads SCUS_942.54 from the disc → DT_StatGrowth +
    │                                               DT_GrowthCurve. Carries Terra's DESIGNED
    │                                               parameters and preserves characters you create
    │                                               in the Studio
    ├── generate_csv.py ✅                       ← item TOMLs → DT_Items.csv
    ├── generate_shops_csv.py ✅                 ← shops.toml → DT_Shops.csv
    ├── generate_characters_csv.py ✅            ← characters.toml → DT_Characters.csv
    ├── generate_enemies_csv.py ✅               ← enemies.toml + the disc's monster archive listing →
    │                                               DT_Enemies.csv. Keeps Mesh/Anims, hand-set ids and
    │                                               rows made in the Studio; backs up first
    ├── generate_bosses_csv.py ✅                ← bosses.toml → DT_Bosses.csv (fighters joined to
    │                                               DT_Enemies by name); keeps hand fixes and new fights
    ├── generate_arts_csv.py ✅                  ← arts.toml + the disc's Arts banks (hit power) +
    │                                               the Super/Miracle sequences → DT_Arts.csv. Keeps
    │                                               new Arts and edited balance; backs up first
    ├── generate_enemy_anims_csv.py ✅           ← the monster batch's monsters.json → Mesh and Anims of
    │                                               every DT_Enemies row (roles by the disc's action ids)
    ├── legaia_toml.py ✅                        ← shared by the generators: tomllib + Unreal CSV helpers
    ├── generate_progression_csv.py ✅           ← formula → DT_LevelCurve.csv + DT_Difficulty.csv
    ├── generate_mist_noise.py ✅                ← tiling noise for the mist → Mist/T_JRPGMistNoise.png
    ├── generate_editor_icons.py ✅              ← red billboard icons of the field enemy and the encounter
    │                                               zone → EditorIcons/*.png (imported in Content/Editor/)
    ├── ps1_glb_to_fbx.py ✅                     ← headless Blender: PS1 battle model .glb (monster or
    │                                               party, from legend-of-legaia-re) → rigged FBX for
    │                                               Unreal; --block adds the clip morphs (slimes) as
    │                                               shape keys + curves. See Guides/PS1_ASSETS.md
    ├── ps1_equip_pieces.py ✅                   ← headless Blender: the party's battle form cut into
    │                                               equipment pieces (one per section and item, same
    │                                               skeleton). See Guides/PS1_ASSETS.md
    ├── generate_equip_visuals_csv.py ✅         ← those pieces → DT_EquipVisuals.csv
    ├── ps1_party_sounds.py ✅                   ← the party's battle clips: real names (the Art, the win
    │                                               pose…), the game's speed (RateScale) and the sounds
    │                                               and voices each one plays. See Guides/PS1_ASSETS.md
    ├── ps1_monsters_batch.py ✅                 ← every monster at once: .glb + block (RE asset tool),
    │                                               FBX (ps1_glb_to_fbx.py, parallel Blenders) and
    │                                               monsters.json. See Guides/PS1_ASSETS.md
    ├── ps1_monster_sounds.py ✅                 ← the monsters' battle sounds from the disc → WAV per
    │                                               monster + common ones, and sounds.json (clip/frame
    │                                               of each, for AnimNotifies). See Guides/PS1_ASSETS.md
    ├── ue_setup_monster.py ✅                   ← runs IN the editor (py): imports the FBX (with morph
    │                                               targets) + texture, renames the clips, PS1 texture
    │                                               settings, MI_<Name> on the mesh; --all imports every
    │                                               batch FBX not in the project yet
    ├── ue_import_monster_sounds.py ✅           ← runs IN the editor (py): imports those WAVs and puts
    │                                               the AnimNotify_PlaySound on the monster's (or hero's)
    │                                               clips; for a hero, renames the clips + sets RateScale
    ├── generate_status_map.py ✅                ← status map in the root README
    └── download_fonts.py ✅                     ← verifies/downloads the WebUI's local fonts
```

> **The `generate_*.py` scripts rewrite the whole CSV** — running one after editing in the
> Studio wipes your edits. They were one-way importers, which is why LegaiaStudio does not
> expose them.

> **In the GAME project** (not the plugin) live the assets: `Content/Data/DT_*.uasset`
> imported from the CSVs above, plus meshes, sounds and textures.

---

## What works today

| System | State | Highlights |
|---|---|---|
| **Core** | ✅ complete | Gold with `OnGoldChanged`, tick-free playtime, map/position for saves, `bIsNewGame` |
| **Progression** | ✅ complete | XP curve with a per-character correction, stat growth for all 4 roster members (3 from the disc + Terra hand-designed), Easy (Japanese rules) / Normal / Hard / Juggernaut difficulty. Runtime state lives in Party |
| **Inventory** | ✅ complete | `DT_Items` (225), stacking up to 99, item use routed to other subsystems |
| **World State** | ✅ complete | Event flags, chests, Revival Trees, `bIsInField`; reset for New Game and atomic load |
| **Save/Load** | ✅ complete | 15 slots, automatic gather/restore, map change + teleport, save versioning |
| **Shop** | ✅ complete | `DT_Shops` (32), atomic buy/sell, featured items gated by platinum_card, flag gating |
| **Audio** | ✅ complete | BGM persisting across maps with crossfade, 4 channels saved in `JRPGSettings` |
| **Mist** | ✅ complete | Living volumetric mist: Ground Mist, Flow Lines, Pulse Rings, Ground + Flow, Air (clouds in a second box) and Chimney Smoke, with presets; one direction or spreading from an origin (an epicenter); follows terrain into holes, parts around the player and movers, trail painted into a texture (player and NPCs), self light for dark places, mist-free areas after story events (Mist Blocker). See [MIST.md](Guides/MIST.md) |
| **Field character** | ✅ basic | `AJRPGFieldCharacter` + `AJRPGFieldPlayerController`: top-down camera with stacking camera zones, walk/sprint relative to the camera or a fixed yaw, interaction through `IJRPGInteractable`, mist trail, input with built-in defaults. The formation walks behind the leader (navmesh leash or trail follow, gathers beside it when stopped) and R3 switches who is controlled. See [PLAYER.md](Guides/PLAYER.md). **Enemies on the field**: wander, chase, start an encounter on contact and respawn, alone or from a sphere/box zone — [FIELD_ENEMIES.md](Guides/FIELD_ENEMIES.md) |
| **Camera** | ✅ complete | 13 presets (+DOF), automatic restore when the UI closes, 8 shakes, manual camera + zone, conversation and FrameGroup/Orbit ready |
| **WebUI** | ✅ complete | SPA shell, 7 screens, gamepad + keyboard + mouse with their own rules, responsive scaling |
| **Party** | ✅ complete (data) | Roster vs formation, 3 states per character, XP split among survivors as in the original, level-up with jitter, whole-roster save. The UI does not follow the formation yet |
| **Status** | ✅ complete (data) | Equipment in 7 slots (4 gear + 3 accessories) with `CanEquip` read from `DT_Items`, conditions, Ra-Seru (1..9), elemental affinity, and the original's `effective ( base )` display. The equip screen is still missing |
| **Arts** | ✅ | Buttons → the turn as the original recognises it (overlapping Arts, Supers, Miracles), learning on use / taught / by chain / by the Ra-Seru, the learned list in the save, the new AP (Regulars add, specials spend, checked at each point of the chain), a validator for `DT_Arts` and the Art Tester's `Input`. A new Art is a row. Damage per hit and AP on the combatant are in the fight (Phase 5). See [ARTS.md](Guides/ARTS.md) |
| **Battle** | ✅ playable (roadmap phases 0 to 6; phase 7 in progress) | A fight is assembled over the current map and taken down ([BATTLE_STAGE.md](Guides/BATTLE_STAGE.md)) and **played** ([BATTLE.md](Guides/BATTLE.md)): commands then execution, the original's damage kernel hit by hit on a logic timeline, the physical enemy AI, Arts and AP, Spirit, formation advantage, the original's transitions, camera, voices and music, victory with the rewards paid once; `JRPG.Battle.*` tests and console commands to play without a HUD. A **placeholder command menu** (Start / Tactics / Attack inputs / Target / Final, with auto-battle tactics) drives the command phase without the console ([BATTLE.md › Battle UI](Guides/BATTLE.md#13-battle-ui)); the real HUD, items, the escape roll, drop / steal, VFX and magic are still to come; the open bugs are in [BATTLE_PHASE5_BUGS.md](BATTLE_PHASE5_BUGS.md). `DT_Enemies` has 179 enemies, `DT_EnemyActions` 1753 clips ([ENEMIES.md](Guides/ENEMIES.md)), `DT_Arts` 81 Arts and pieces ([ARTS.md](Guides/ARTS.md)). See [BATTLE_ROADMAP.md](BATTLE_ROADMAP.md) |

---

## Planned systems

Three systems have no implementation yet. They are listed here because groundwork for them
already exists in shipped code — knowing what is already reserved saves rebuilding it.

### Dialogue

| Already in place | Where |
|---|---|
| `OpenDialogue(FText Speaker, FText Text, TArray<FText> Options)` — **final signature**; the JS side only logs | `Public/UI/WebUISubsystem.h` |
| `DialogueActive` UI state, which already blocks the pause menu | `EJRPGUIState` |
| Shell section, CSS and a JS placeholder | `Content/UI/WebUI/src/*/30_dialogue.*` |
| **Shot-reverse-shot conversation camera — fully implemented** | `UCameraSubsystem::StartConversation` / `FocusSpeaker` / `EndConversation` |

The camera half is done: `FocusSpeaker` frames over the listener's shoulder onto the speaker
and alternates sides correctly, respecting the 180° rule. What is missing is the system
itself — a node/choice/condition format, its runtime, and an editor for it. Conditions can
read `UWorldStateSubsystem` event flags, which already exist.

### Cutscene

Nothing written. It makes sense after Dialogue, and the camera pieces it would need are
already there: `FocusOnLevelCamera`, the 13 framing presets and `AJRPGCameraZone`.

### Localization

| Already in place | Where |
|---|---|
| `FText` on every player-facing string (item names, character names, shop names, difficulty) | across the data structs |
| `NSLOCTEXT` on the built-in difficulty names and descriptions | `Private/Core/CoreSubsystem.cpp` |
| A **Language** cycler (English / Português BR) on the Options screen | `src/js/40_options.js` |

What is missing is everything that makes it work: culture switching at runtime, a string
table or `.locres` pipeline, and the WebUI's own text — the shell's strings are hardcoded in
the section files, so they need a mechanism of their own rather than Unreal's. The Language
cycler currently changes nothing.

> Worth checking before starting: the built-in difficulty **descriptions** inside `NSLOCTEXT`
> in `CoreSubsystem.cpp` are written in Portuguese, while rule 38 in
> [ARCHITECTURE.md](ARCHITECTURE.md) states that every player-facing string is English. If
> those descriptions reach the Options screen, that is a rule violation to fix in code — not
> a documentation issue.

---

### Known gaps in shipped systems

- **Equip screen** in the WebUI (Status works; there is no UI for it).
- **Party/shop screens** still show base stats instead of effective ones.
- **Options controls do not apply yet** — the screen is built, the values are not wired to
  `UGameUserSettings` / `AudioSubsystem`.
- **Condition effects in battle** depend on the battle system.

---

## Minimum Blueprint wiring in the game project

1. `GI_Main` inherits from `UJRPGGameInstance` (subsystems self-register).
2. **Map BeginPlay:** `WebUISubsystem → InitializeUIShell` (optionally
   `CoreSubsystem → SetCurrentMapDisplayName`) + Branch on
   `AudioSubsystem → IsBGMPlaying` → false: `PlayBGM(MapBGM)`.
3. **New Game:** `CoreSubsystem → StartNewGame` **before** `OpenLevel` of the starting map.
4. **Pause:** Input Action → `WebUISubsystem → OpenMenu`.
5. **Chest:** `WorldStateSubsystem → IsChestOpened/SetChestOpened` +
   `InventorySubsystem → AddItem` + `ShowItemPopup`.
6. **Save statue:** Interact → `WebUISubsystem → OpenSaveLoad`.
7. **Shopkeeper NPC:** Interact → `CameraSubsystem → FocusOnActor(self, ShopFront)` →
   (variant? Branch on `ShopSubsystem → IsShopAvailable`) →
   `WebUISubsystem → OpenShop(ShopID)` — the camera restores itself when the shop closes.
8. **Field vs battle:** `WorldStateSubsystem → SetIsInField(true/false)` — this gates the
   SAVE tab.
9. **Teleport:** Branch on `IsBGMPlaying AND IsBGMPersistent` → false: `StopBGM(fade)`
   before `OpenLevel`.
10. **PlaySound from BP:** feed `GetEffectiveSFXVolume`/`GetEffectiveAmbientVolume` into the
    Volume Multiplier.
11. **Core tables:** import `DT_StatGrowth`, `DT_GrowthCurve` and `DT_Difficulty` into
    `/Game/Data/` under those exact names — `CoreSubsystem` resolves them in `Initialize`.
    Without the two growth tables nobody gains stats (a warning appears in the log).
12. **Camera presets (optional):** import `Docs/Data/DT_CameraPresets.csv` as `DT_Camera` and
    call `CameraSubsystem → SetCameraPresetTable(DT_Camera)` at startup.
13. **Fixed camera per area (optional):** place a `JRPGCameraZone` in the level pointing its
    `LevelCamera` at a hand-placed CameraActor.
