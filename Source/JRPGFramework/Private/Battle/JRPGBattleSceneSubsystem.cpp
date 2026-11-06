#include "Battle/JRPGBattleSceneSubsystem.h"
#include "Arts/ArtsSubsystem.h"
#include "Arts/JRPGArtsRules.h"
#include "Battle/BattleFormulas.h"
#include "Battle/BattleSubsystem.h"
#include "Battle/JRPGBattleArena.h"
#include "Battle/JRPGBattlePawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
	UBattleSubsystem* GetBattleSubsystem(const UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UBattleSubsystem>() : nullptr;
	}

	/** JRPG.Battle.Start <EnemyKey> [Quantos] [Level]: uma luta onde o jogador está. */
	void StartBattleCommand(const TArray<FString>& Args, UWorld* World)
	{
		UBattleSubsystem* Battle = GetBattleSubsystem(World);
		if (!World || !World->IsGameWorld() || !Battle)
		{
			UE_LOG(LogTemp, Warning, TEXT("JRPG.Battle.Start: só no jogo (PIE)"));
			return;
		}
		const FName Key = Args.Num() > 0 ? FName(*Args[0]) : FName(TEXT("gimard"));
		const int32 Count = Args.Num() > 1 ? FMath::Max(1, FCString::Atoi(*Args[1])) : 1;
		const int32 Level = Args.Num() > 2 ? FMath::Max(1, FCString::Atoi(*Args[2])) : 1;

		FJRPGBattleSetup Setup;
		for (int32 i = 0; i < Count; ++i)
		{
			FJRPGEncounterCombatant& Enemy = Setup.Enemies.AddDefaulted_GetRef();
			Enemy.EnemyKey = Key;
			Enemy.Level = Level;
		}
		const APlayerController* PC = World->GetFirstPlayerController();
		if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
		{
			Setup.Location = Pawn->GetActorLocation();
			Setup.FacingYaw = Pawn->GetActorRotation().Yaw;
		}
		Battle->BeginBattle(Setup);
	}

	/** JRPG.Battle.End [victory|defeat|escape|abort]: encerra a luta atual (vitória por padrão). */
	void EndBattleCommand(const TArray<FString>& Args, UWorld* World)
	{
		UBattleSubsystem* Battle = GetBattleSubsystem(World);
		if (!Battle || !Battle->IsInBattle())
		{
			UE_LOG(LogTemp, Warning, TEXT("JRPG.Battle.End: nenhuma luta acontecendo"));
			return;
		}
		EJRPGBattleResult Result = EJRPGBattleResult::Victory;
		const FString Word = Args.Num() > 0 ? Args[0].ToLower() : FString();
		if (Word.StartsWith(TEXT("d")))
		{
			Result = EJRPGBattleResult::Defeat;
		}
		else if (Word.StartsWith(TEXT("e")))
		{
			Result = EJRPGBattleResult::Escaped;
		}
		else if (Word.StartsWith(TEXT("a")))
		{
			Result = EJRPGBattleResult::Aborted;
		}
		// Atalho de teste: a vitória paga o EXP e o Gold de todos os inimigos, vivos também (a de verdade só conta os caídos)
		Battle->EndBattleForDebug(Result, Result == EJRPGBattleResult::Victory);
	}

	TAutoConsoleVariable<int32> CVarPrintBattleEvents(
		TEXT("JRPG.Battle.PrintEvents"), 1,
		TEXT("1 (padrão) escreve na tela cada evento da luta (dano, miss, bloqueio, queda, Art...); 0 só no log."),
		ECVF_Default);

	/** O alvo de um comando de debug: o N-ésimo inimigo (0 = o primeiro) se o argumento é um número; senão o vivo de menor slot. */
	int32 PickDebugTarget(const UBattleSubsystem& Battle, const FJRPGCombatant& Hero, const FString* Arg)
	{
		if (Arg && Arg->IsNumeric())
		{
			const int32 Wanted = FCString::Atoi(**Arg);
			for (const FJRPGCombatant& C : Battle.GetCombatants())
			{
				if (C.Side == EJRPGBattleSide::Enemy && C.SideIndex == Wanted)
				{
					return C.SlotIndex;
				}
			}
			return INDEX_NONE;
		}
		for (const FJRPGCombatant& C : Battle.GetCombatants())
		{
			if (C.bAlive && C.Team != Hero.Team)
			{
				return C.SlotIndex;
			}
		}
		return INDEX_NONE;
	}

	/** A chave da linha de recusa na tela, ao lado do prompt (7421): a linha nova substitui a velha. */
	constexpr uint64 RefusalMessageKey = 7422;
	constexpr uint64 PromptMessageKey = 7421;

	/** O resumo da combo no fim da ação: "Hit N" na linha de cima, "Damage X" na de baixo (a linha nova substitui a velha). */
	constexpr uint64 ComboHitMessageKey = 7430;
	constexpr uint64 ComboDamageMessageKey = 7431;
	constexpr float ComboMessageSeconds = 10.0f;

	/** Um aviso de comando que não valeu: no log E na tela (senão a luta parece travada esperando o comando). */
	void ShowCommandNotice(const FString& Text)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleScene: %s"), *Text);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(RefusalMessageKey, 12.0f, FColor::Red, Text);
		}
	}

	/** A luta em andamento, esperando o comando de alguém; senão avisa e devolve nulo. */
	UBattleSubsystem* BattleWaitingForCommands(UWorld* World, const TCHAR* Command)
	{
		UBattleSubsystem* Battle = GetBattleSubsystem(World);
		if (!Battle || !Battle->IsInBattle())
		{
			ShowCommandNotice(FString::Printf(TEXT("%s: nenhuma luta acontecendo"), Command));
			return nullptr;
		}
		if (Battle->GetState().Stage != EJRPGBattleStage::Commands || Battle->GetActiveSlot() < 0)
		{
			ShowCommandNotice(FString::Printf(TEXT("%s: a luta não está esperando comandos (a rodada está tocando)"), Command));
			return nullptr;
		}
		return Battle;
	}

	/** JRPG.Battle.Attack [inimigo]: o golpe de Arms de quem está digitando (o próximo herói sem comando). */
	void AttackCommand(const TArray<FString>& Args, UWorld* World)
	{
		UBattleSubsystem* Battle = BattleWaitingForCommands(World, TEXT("JRPG.Battle.Attack"));
		if (!Battle)
		{
			return;
		}
		const FJRPGCombatant Hero = Battle->GetCombatants()[Battle->GetActiveSlot()];
		const int32 Target = PickDebugTarget(*Battle, Hero, Args.Num() > 0 ? &Args[0] : nullptr);
		FJRPGBattleCommand Command;
		Command.PlayerIndex = Hero.OwnerPlayer;
		Command.Slot = Hero.SlotIndex;
		Command.Kind = EJRPGBattleCommandKind::Attack;
		Command.TargetSlot = Target;
		Battle->SubmitCommand(Command);
	}

	/** JRPG.Battle.Arts <botões> [inimigo]: a cadeia de Arts ("Arms Ra-Seru Arms", "A R A", "High Low High 0"). */
	void ArtsCommand(const TArray<FString>& Args, UWorld* World)
	{
		UBattleSubsystem* Battle = BattleWaitingForCommands(World, TEXT("JRPG.Battle.Arts"));
		if (!Battle)
		{
			return;
		}
		const FJRPGCombatant Hero = Battle->GetCombatants()[Battle->GetActiveSlot()];
		// Os argumentos que são número viram o alvo (o último); o resto são os botões
		FString ButtonText;
		const FString* TargetArg = nullptr;
		for (const FString& Arg : Args)
		{
			if (Arg.IsNumeric())
			{
				TargetArg = &Arg;
			}
			else
			{
				ButtonText += Arg + TEXT(" ");
			}
		}
		TArray<EJRPGArtButton> Buttons;
		if (!UJRPGArtsRules::ParseButtons(ButtonText, Hero.bLeftHanded, Buttons) || Buttons.IsEmpty())
		{
			ShowCommandNotice(FString::Printf(TEXT("JRPG.Battle.Arts: não entendi os botões '%s' (Arms, Ra-Seru, High, Low; ou A R H L; ou setas)"), *ButtonText));
			return;
		}
		FJRPGBattleCommand Command;
		Command.PlayerIndex = Hero.OwnerPlayer;
		Command.Slot = Hero.SlotIndex;
		Command.Kind = EJRPGBattleCommandKind::Arts;
		Command.TargetSlot = PickDebugTarget(*Battle, Hero, TargetArg);
		Command.ArtBytes = UJRPGArtsRules::EncodeArtBytes(Buttons);
		Battle->SubmitCommand(Command);
	}

	/** JRPG.Battle.Auto: todo herói que ainda não comandou ataca o inimigo vivo de menor slot. */
	void AutoCommand(const TArray<FString>& Args, UWorld* World)
	{
		UBattleSubsystem* Battle = BattleWaitingForCommands(World, TEXT("JRPG.Battle.Auto"));
		if (!Battle)
		{
			return;
		}
		for (int32 Slot = 0; Slot < Battle->GetCombatants().Num() && Battle->GetState().Stage == EJRPGBattleStage::Commands; ++Slot)
		{
			const FJRPGCombatant Hero = Battle->GetCombatants()[Slot];
			if (!Hero.bAlive || Hero.Type == EJRPGCombatantType::Enemy || Hero.OwnerPlayer < 0
				|| Battle->GetState().Commands[Slot].Kind != EJRPGBattleCommandKind::None)
			{
				continue;
			}
			FJRPGBattleCommand Command;
			Command.PlayerIndex = Hero.OwnerPlayer;
			Command.Slot = Slot;
			Command.Kind = EJRPGBattleCommandKind::Attack;
			Command.TargetSlot = PickDebugTarget(*Battle, Hero, nullptr);
			Battle->SubmitCommand(Command);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GAttackBattleCommand(
		TEXT("JRPG.Battle.Attack"),
		TEXT("O golpe de Arms de quem está digitando: JRPG.Battle.Attack [inimigo] (0 = o primeiro inimigo; sem número, o vivo de menor slot)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AttackCommand));

	FAutoConsoleCommandWithWorldAndArgs GArtsBattleCommand(
		TEXT("JRPG.Battle.Arts"),
		TEXT("A cadeia de Arts de quem está digitando: JRPG.Battle.Arts <botões> [inimigo], ex.: JRPG.Battle.Arts High Low High 0 (ou \"A R A\")"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ArtsCommand));

	FAutoConsoleCommandWithWorldAndArgs GAutoBattleCommand(
		TEXT("JRPG.Battle.Auto"),
		TEXT("Todo herói que ainda não comandou ataca o inimigo vivo de menor slot (para andar a rodada sem digitar um a um)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AutoCommand));

	FAutoConsoleCommandWithWorldAndArgs GStartBattleCommand(
		TEXT("JRPG.Battle.Start"),
		TEXT("Começa uma luta onde o jogador está: JRPG.Battle.Start <EnemyKey> [Quantos] [Level]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StartBattleCommand));

	FAutoConsoleCommandWithWorldAndArgs GEndBattleCommand(
		TEXT("JRPG.Battle.End"),
		TEXT("Encerra a luta atual: JRPG.Battle.End [victory|defeat|escape|abort] (victory por padrão)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&EndBattleCommand));
}

// Definido aqui (não inline no header): o Get() do TWeakObjectPtr converte para a arena, que pede o tipo completo
AJRPGBattleArena* UJRPGBattleSceneSubsystem::GetArena() const
{
	return Arena.Get();
}

bool UJRPGBattleSceneSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UJRPGBattleSceneSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (UBattleSubsystem* Battle = GetBattleSubsystem(&InWorld))
	{
		Battle->OnBattleStarted.AddDynamic(this, &UJRPGBattleSceneSubsystem::HandleBattleStarted);
		Battle->OnBattleEnded.AddDynamic(this, &UJRPGBattleSceneSubsystem::HandleBattleEnded);
		Battle->OnBattleEvent.AddDynamic(this, &UJRPGBattleSceneSubsystem::HandleBattleEvent);
		Battle->OnSlotChanged.AddDynamic(this, &UJRPGBattleSceneSubsystem::HandleSlotChanged);
		Battle->OnLogicFrame.AddDynamic(this, &UJRPGBattleSceneSubsystem::HandleLogicFrame);
		Battle->OnCommandRefused.AddDynamic(this, &UJRPGBattleSceneSubsystem::HandleCommandRefused);
	}
}

void UJRPGBattleSceneSubsystem::Deinitialize()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(TransitionTimer);
	}
	UBattleSubsystem* Battle = GetBattleSubsystem(World);
	if (Battle)
	{
		Battle->OnBattleStarted.RemoveDynamic(this, &UJRPGBattleSceneSubsystem::HandleBattleStarted);
		Battle->OnBattleEnded.RemoveDynamic(this, &UJRPGBattleSceneSubsystem::HandleBattleEnded);
		Battle->OnBattleEvent.RemoveDynamic(this, &UJRPGBattleSceneSubsystem::HandleBattleEvent);
		Battle->OnSlotChanged.RemoveDynamic(this, &UJRPGBattleSceneSubsystem::HandleSlotChanged);
		Battle->OnLogicFrame.RemoveDynamic(this, &UJRPGBattleSceneSubsystem::HandleLogicFrame);
		Battle->OnCommandRefused.RemoveDynamic(this, &UJRPGBattleSceneSubsystem::HandleCommandRefused);
		// O mapa saiu no meio da luta: ela não pode ficar aberta no GameInstance
		if (Battle->IsInBattle())
		{
			Battle->EndBattle(EJRPGBattleResult::Aborted);
		}
	}
	SetShoutDilation(1.0f);
	Arena = nullptr;
	Phase = EPhase::Idle;
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Começo e fim
// ---------------------------------------------------------------------------------------------

void UJRPGBattleSceneSubsystem::HandleBattleStarted(const FJRPGBattleSetup& Setup, const TArray<FJRPGCombatant>& Combatants)
{
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	if (Phase != EPhase::Idle)
	{
		// Uma luta nova antes da anterior sair da tela: desmonta a velha na hora
		Timers.ClearTimer(TransitionTimer);
		LeaveBattleScene();
	}

	PendingSetup = Setup;
	PendingCombatants = Combatants;
	ChooseArena(Setup);
	Phase = EPhase::Entering;

	// A luta só anda com a cena montada e a tela aberta: a resolução espera (os comandos entram, nada toca) para
	// que nenhum golpe aconteça com a tela preta e sem pawn para mostrá-lo
	if (UBattleSubsystem* Battle = GetBattleSubsystem(GetWorld()))
	{
		Battle->HoldLogic(true);
	}

	// Tudo para na hora do toque: o jogador e a party aqui, os inimigos do Field no OnBattleStarted.
	// Somem só com a tela toda preta
	if (AJRPGBattleArena* Current = Arena.Get())
	{
		Current->StopFieldForBattle();
	}
	if (GetTransitionTime() > 0.0f)
	{
		Fade(true);
		Timers.SetTimer(TransitionTimer, FTimerDelegate::CreateUObject(this, &UJRPGBattleSceneSubsystem::EnterBattleScene), GetTransitionTime(), false);
	}
	else
	{
		EnterBattleScene();
	}
}

void UJRPGBattleSceneSubsystem::HandleBattleEnded(EJRPGBattleResult Result)
{
	// A luta acabou no meio de uma Art: o tempo volta ao normal antes do fade (ele anda no tempo do mundo)
	ShoutUntilRealTime = 0.0;
	SetShoutDilation(1.0f);
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	if (Phase == EPhase::Entering)
	{
		// Acabou antes da cena existir: o Field não chegou a sumir; devolve o controle e abre a tela
		Timers.ClearTimer(TransitionTimer);
		if (GetTransitionTime() > 0.0f)
		{
			Fade(false);
		}
		if (AJRPGBattleArena* Current = Arena.Get())
		{
			Current->TeardownBattle();
		}
		ReleaseArena();
		Phase = EPhase::Idle;
		NoteFieldReturnedToBattleSubsystem();
		OnFieldShown.Broadcast();
		return;
	}
	if (Phase != EPhase::Assembled)
	{
		return;
	}

	Phase = EPhase::Leaving;
	if (GetTransitionTime() > 0.0f)
	{
		Fade(true);
		Timers.SetTimer(TransitionTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			LeaveBattleScene();
			Fade(false);
		}), GetTransitionTime(), false);
	}
	else
	{
		LeaveBattleScene();
	}
}

// ---------------------------------------------------------------------------------------------
// Os eventos da luta -> os pawns
// ---------------------------------------------------------------------------------------------

void UJRPGBattleSceneSubsystem::HandleBattleEvent(const FJRPGBattleEvent& Event)
{
	UBattleSubsystem* Battle = GetBattleSubsystem(GetWorld());
	if (!Battle)
	{
		return;
	}
	const FString Line = Battle->DescribeEvent(Event);
	if (CVarPrintBattleEvents.GetValueOnGameThread() != 0 && GEngine)
	{
		const FColor Color = Event.Type == EJRPGBattleEventType::Damage ? FColor(255, 220, 160)
			: Event.Type == EJRPGBattleEventType::Death ? FColor(255, 140, 60)
			: Event.Type == EJRPGBattleEventType::RoundStart ? FColor(120, 200, 255) : FColor(200, 200, 200);
		GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.0f, Color, Line);
	}

	// Só com a cena de pé há pawn para mostrar
	AJRPGBattleArena* Current = Arena.Get();
	if (Phase != EPhase::Assembled || !Current)
	{
		return;
	}
	auto PawnOf = [Current](int32 Slot) { return Current->GetPawnForSlot(Slot); };

	switch (Event.Type)
	{
	case EJRPGBattleEventType::ActionStart:
		// O recuo dentro do alcance que o atacante anterior seguia fazendo depois da ação dele acaba aqui (o done band do original)
		for (AJRPGBattlePawn* Pawn : Current->GetBattlePawns())
		{
			if (IsValid(Pawn))
			{
				Pawn->EndPostActionSpacing();
			}
		}
		// A ação nova apaga o resumo da combo da anterior
		if (GEngine)
		{
			GEngine->RemoveOnScreenDebugMessage(ComboHitMessageKey);
			GEngine->RemoveOnScreenDebugMessage(ComboDamageMessageKey);
		}
		if (AJRPGBattlePawn* Actor = PawnOf(Event.SlotIndex))
		{
			if (const FJRPGActionTimeline* Timeline = Battle->FindTimeline(Event.ActionIndex))
			{
				AJRPGBattlePawn* Target = PawnOf(Event.TargetSlot);
				// Uma ação em si mesmo (o Spirit): sem alvo, ninguém anda nem mira
				if (Target == Actor)
				{
					Target = nullptr;
				}
				// O recentrar do original no começo da ação: o alvo de um golpe vai para o centro da arena (a ação sem golpe
				// centra todos). Desliza com tudo o que vier depois (a caminhada parte de onde estão e anda junto)
				const bool bMelee = Timeline->Steps.ContainsByPredicate([](const FJRPGActionStep& Step) { return Step.Kind != EJRPGActionStepKind::SpecialStarter; });
				PlanFormationShift(bMelee && Target != Actor ? Target : nullptr, Event.Frame);
				Actor->BeginAction(ToPresentationTimeline(*Battle, *Timeline), Target);
				// A caminhada em velocidade constante que passa dos LeadFrames: as regras esperam o atacante chegar
				Battle->StallResolver(Actor->GetApproachStallFrames());
				// A geometria da aproximação vira entrada das regras (o original a gasta no primeiro golpe, no block e no dano)
				SubmitApproachFor(*Battle, *Actor, Target, *Timeline);
				// Quem está em volta sai da linha do atacante que vai andar
				if (Actor->IsWalkingApproach())
				{
					MakeWay(Actor, Target, Event.Frame);
				}
			}
		}
		break;
	case EJRPGBattleEventType::RoundStart:
		// O começo da rodada do original (FUN_801DB318): comprime e centra a formação inteira
		PlanFormationShift(nullptr, Event.Frame);
		break;
	case EJRPGBattleEventType::ActionEnd:
		// O fim da ação, inclusive a cortada (o último inimigo caiu no meio da combo): a cópia que o pawn tem pode ser mais
		// longa que a que valeu, então ele fecha agora em vez de esperar o TotalFrames dela
		if (AJRPGBattlePawn* Actor = PawnOf(Event.SlotIndex))
		{
			Actor->EndAction(Event.Frame);
		}
		break;
	case EJRPGBattleEventType::Damage:
		// O resultado do golpe vai a quem bateu: o som de impacto que esperava por ele toca (ou não, no Miss)
		if (AJRPGBattlePawn* Striker = PawnOf(Event.SlotIndex))
		{
			Striker->NoteHitResult(Event);
		}
		if (AJRPGBattlePawn* Target = PawnOf(Event.TargetSlot))
		{
			Target->ShowEvent(Event, (Event.Flags & FJRPGBattleEventFlag::ArtHit) ? FString::Printf(TEXT("%d!"), Event.Value) : FString::FromInt(Event.Value), true);
			// O defensor vira para quem bateu e recua um pouco (a reação do golpe: o knockback, limitado pela arena)
			Target->ReactToHit(PawnOf(Event.SlotIndex), Event.Frame, (Event.Flags & FJRPGBattleEventFlag::Knockdown) != 0, false);
		}
		break;
	case EJRPGBattleEventType::ComboTotal:
		// O fim da ação, como o cluster do original: "Hit N" em cima e "Damage X" embaixo, X = o dano de todos os golpes, o
		// que passou do HP também. Um golpe só mostra apenas o Damage (o DAMAGE do original). O número em cima de cada alvo fica
		if (GEngine)
		{
			if (Event.Count >= 2)
			{
				GEngine->AddOnScreenDebugMessage(ComboHitMessageKey, ComboMessageSeconds, FColor::Yellow, FString::Printf(TEXT("Hit %d"), Event.Count));
			}
			else
			{
				GEngine->RemoveOnScreenDebugMessage(ComboHitMessageKey);
			}
			GEngine->AddOnScreenDebugMessage(ComboDamageMessageKey, ComboMessageSeconds, FColor::Orange, FString::Printf(TEXT("Damage %d"), Event.Value));
		}
		break;
	case EJRPGBattleEventType::Miss:
		if (AJRPGBattlePawn* Striker = PawnOf(Event.SlotIndex))
		{
			Striker->NoteHitResult(Event);
		}
		if (AJRPGBattlePawn* Target = PawnOf(Event.TargetSlot))
		{
			// O único Miss das regras é o de altura (um golpe baixo num voador, um alto num baixinho): diz isso, para não parecer um
			// golpe que "não pegou" (bug 29)
			Target->ShowEvent(Event, TEXT("Miss (height)"), true);
		}
		break;
	case EJRPGBattleEventType::Block:
		if (AJRPGBattlePawn* Striker = PawnOf(Event.SlotIndex))
		{
			Striker->NoteHitResult(Event);
		}
		if (AJRPGBattlePawn* Target = PawnOf(Event.TargetSlot))
		{
			Target->ShowEvent(Event, TEXT("Block"), true);
			Target->ReactToHit(PawnOf(Event.SlotIndex), Event.Frame, false, true);
		}
		break;
	case EJRPGBattleEventType::Death:
		if (AJRPGBattlePawn* Fallen = PawnOf(Event.SlotIndex))
		{
			Fallen->ShowEvent(Event, TEXT("KO"), true);
		}
		break;
	case EJRPGBattleEventType::APChanged:
		if (AJRPGBattlePawn* Actor = PawnOf(Event.SlotIndex))
		{
			Actor->ShowEvent(Event, FString::Printf(TEXT("AP %d"), Event.Value), false);
		}
		break;
	case EJRPGBattleEventType::ArtRecognized:
	case EJRPGBattleEventType::ArtLearned:
		if (AJRPGBattlePawn* Actor = PawnOf(Event.SlotIndex))
		{
			const FString ArtName = UArtsSubsystem::GetArtDisplayName(Event.ArtKey).ToString();
			Actor->ShowEvent(Event, Event.Type == EJRPGBattleEventType::ArtLearned ? FString::Printf(TEXT("Learned %s!"), *ArtName) : ArtName, false);
		}
		break;
	default:
		break;
	}
}

void UJRPGBattleSceneSubsystem::HandleSlotChanged(int32 Slot, EJRPGBattleStage Stage)
{
	const UBattleSubsystem* Battle = GetBattleSubsystem(GetWorld());
	if (!Battle || Stage != EJRPGBattleStage::Commands || !Battle->GetCombatants().IsValidIndex(Slot))
	{
		return;
	}
	// Quem digita mudou: a recusa de antes (de outro herói, de outra rodada) não vale mais
	if (GEngine)
	{
		GEngine->RemoveOnScreenDebugMessage(RefusalMessageKey);
	}
	ShowCommandPrompt(*Battle, Slot);
}

void UJRPGBattleSceneSubsystem::SetDebugPromptSuppressed(bool bSuppress)
{
	bDebugPromptSuppressed = bSuppress;
	if (bSuppress && GEngine)
	{
		// A UI de comando subiu: o prompt que já estava na tela (e o aviso vermelho) saem
		GEngine->RemoveOnScreenDebugMessage(PromptMessageKey);
		GEngine->RemoveOnScreenDebugMessage(RefusalMessageKey);
	}
}

void UJRPGBattleSceneSubsystem::ShowCommandPrompt(const UBattleSubsystem& Battle, int32 Slot) const
{
	if (!Battle.GetCombatants().IsValidIndex(Slot) || bDebugPromptSuppressed)
	{
		return;
	}
	const FString Prompt = FString::Printf(TEXT("Round %d - command for %s (slot %d): JRPG.Battle.Attack [enemy] | JRPG.Battle.Arts <buttons> [enemy] | JRPG.Battle.Auto"),
		Battle.GetState().Round, *Battle.GetCombatants()[Slot].Key.ToString(), Slot);
	UE_LOG(LogTemp, Log, TEXT("BattleScene: %s"), *Prompt);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(PromptMessageKey, 60.0f, FColor::Cyan, Prompt);   // a mesma chave: a linha nova substitui a velha
	}
}

void UJRPGBattleSceneSubsystem::HandleCommandRefused(int32 Slot, const FString& Why)
{
	const UBattleSubsystem* Battle = GetBattleSubsystem(GetWorld());
	const FString Who = Battle && Battle->GetCombatants().IsValidIndex(Slot)
		? FString::Printf(TEXT("%s (slot %d)"), *Battle->GetCombatants()[Slot].Key.ToString(), Slot) : FString::Printf(TEXT("slot %d"), Slot);
	if (bDebugPromptSuppressed)
	{
		// A UI de comando mostra o motivo no menu: aqui só o log
		UE_LOG(LogTemp, Warning, TEXT("BattleScene: Command refused for %s: %s"), *Who, *Why);
		return;
	}
	// Ao lado do prompt, que continua de pé: o comando é do mesmo herói, de novo
	ShowCommandNotice(FString::Printf(TEXT("Command refused for %s: %s"), *Who, *Why));
	if (Battle)
	{
		ShowCommandPrompt(*Battle, Battle->GetActiveSlot());
	}
}

FJRPGActionTimeline UJRPGBattleSceneSubsystem::ToPresentationTimeline(const UBattleSubsystem& Battle, const FJRPGActionTimeline& Timeline)
{
	// A timeline vem com o StartFrame das regras (o State.Frame, que só anda na Execution); os pawns andam pelo relógio de
	// apresentação (OnLogicFrame, que também anda na digitação dos comandos). Os frames dos passos e dos golpes são relativos
	FJRPGActionTimeline Shifted = Timeline;
	Shifted.StartFrame += Battle.GetPresentationOffset();
	return Shifted;
}

void UJRPGBattleSceneSubsystem::HandleLogicFrame(int32 Frame)
{
	// Frame é o relógio de apresentação (UBattleSubsystem::GetPresentationFrame): monotônico, de 1 em 1, no mesmo relógio que o
	// Event.Frame dos eventos e as timelines de ToPresentationTimeline
	AJRPGBattleArena* Current = Arena.Get();
	if (Phase != EPhase::Assembled || !Current)
	{
		return;
	}
	const UBattleSubsystem* Battle = GetBattleSubsystem(GetWorld());
	// A formação anda antes dos pawns: o que eles guardam em posição (a caminhada, o deslize) já vem transladado
	StepFormation(Frame);
	// As regras ficaram paradas à espera da aproximação (PvP): a ação do pawn espera junto, para o golpe não tocar antes do dano
	const bool bRulesHeld = Battle && Battle->WasHeldForApproach();
	// A câmera lenta das Arts vale para a luta inteira (o byte +0x21D de cada ator): quem age pede, os outros tocam nela
	const AJRPGBattlePawn* Acting = nullptr;
	int32 WorldSpeed = UBattleFormulas::NormalGameSpeed;
	for (const AJRPGBattlePawn* Pawn : Current->GetBattlePawns())
	{
		if (IsValid(Pawn) && Pawn->GetActiveActionIndex() != INDEX_NONE)
		{
			Acting = Pawn;
			WorldSpeed = Pawn->GetWorldSpeedRequest(Frame);
			break;
		}
	}
	// O slow do grito das Arts (AJRPGBattleArena::ArtsShoutTimeDilation / ArtsShoutSeconds): a leitura da voz no CD começa no
	// starter de uma Art conhecida e dura um tempo, não a Art inteira. O dash (Hyper / Super / Miracle / Art nova) e os clips das
	// Arts já têm a velocidade do original (o resto congelado, 1/4, 1/2): o slow ali seria em dobro
	if (Acting && Acting->GetCombatant().Side == EJRPGBattleSide::Party)
	{
		FJRPGActionStep Step;
		float ClipSeconds = 0.0f;
		float ClipLength = 0.0f;
		if (Acting->GetPlayingStep(Step, ClipSeconds, ClipLength) && Step.Kind == EJRPGActionStepKind::ArtStarter)
		{
			const int64 Key = (static_cast<int64>(Acting->GetActiveActionIndex()) << 32) | static_cast<uint32>(Step.StartFrame);
			if (Key != ShoutStepKey)
			{
				ShoutStepKey = Key;
				ShoutUntilRealTime = GetWorld()->GetRealTimeSeconds() + FMath::Max(Current->ArtsShoutSeconds, 0.0f);
			}
		}
	}
	const bool bShouting = GetWorld()->GetRealTimeSeconds() < ShoutUntilRealTime;
	SetShoutDilation(bShouting ? FMath::Clamp(Current->ArtsShoutTimeDilation, 0.1f, 1.0f) : 1.0f);

	for (AJRPGBattlePawn* Pawn : Current->GetBattlePawns())
	{
		if (IsValid(Pawn))
		{
			Pawn->SetWorldAnimSpeed(Pawn == Acting ? UBattleFormulas::NormalGameSpeed : WorldSpeed);
			// A ação pode ter sido cortada depois do ActionStart (o último inimigo caiu no meio da combo): relê a timeline
			// que valeu, para nenhum passo depois do corte tocar
			const int32 ActionIndex = Pawn->GetActiveActionIndex();
			if (Battle && ActionIndex != INDEX_NONE)
			{
				if (const FJRPGActionTimeline* Live = Battle->FindTimeline(ActionIndex))
				{
					Pawn->SyncActionTimeline(*Live);
				}
			}
			if (bRulesHeld)
			{
				Pawn->DelayAction(1);
			}
			Pawn->UpdatePlayback(Frame);
		}
	}
}

void UJRPGBattleSceneSubsystem::SetShoutDilation(float Dilation)
{
	if (FMath::IsNearlyEqual(Dilation, ShoutDilation))
	{
		return;
	}
	ShoutDilation = Dilation;
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, Dilation);
	}
}

void UJRPGBattleSceneSubsystem::EnterBattleScene()
{
	bFieldHidden = true;
	OnFieldHidden.Broadcast();

	if (AJRPGBattleArena* Current = Arena.Get())
	{
		Current->AssembleBattle(PendingSetup, PendingCombatants);
	}
	Phase = EPhase::Assembled;
	if (GetTransitionTime() > 0.0f)
	{
		Fade(false);
	}

	AJRPGBattleArena* Current = Arena.Get();
	OnSceneAssembled.Broadcast(Current, Current ? Current->GetBattlePawns() : TArray<AJRPGBattlePawn*>());

	if (UBattleSubsystem* Battle = GetBattleSubsystem(GetWorld()))
	{
		// Uma ação que já começou com a cena de fora (só IA, ou um comando de antes) perdeu o ActionStart: começa nela agora
		if (const FJRPGActionTimeline* Active = Battle->GetActiveTimeline())
		{
			const FJRPGActiveAction& Running = Battle->GetState().Action;
			if (Current)
			{
				if (AJRPGBattlePawn* Actor = Current->GetPawnForSlot(Active->SlotIndex))
				{
					AJRPGBattlePawn* RunningTarget = Current->GetPawnForSlot(Running.TargetSlot);
					Actor->BeginAction(ToPresentationTimeline(*Battle, *Active), RunningTarget);
					Battle->StallResolver(Actor->GetApproachStallFrames());
					// A ação perdeu o ActionStart: a aproximação dela também sai daqui (senão, no PvP, a luta para no primeiro golpe)
					SubmitApproachFor(*Battle, *Actor, RunningTarget, *Active);
				}
			}
		}
		// A cena está de pé: o relógio da luta pode andar
		Battle->HoldLogic(false);
	}
}

void UJRPGBattleSceneSubsystem::LeaveBattleScene()
{
	ShoutUntilRealTime = 0.0;
	SetShoutDilation(1.0f);
	if (AJRPGBattleArena* Current = Arena.Get())
	{
		Current->TeardownBattle();
	}
	ReleaseArena();
	Phase = EPhase::Idle;
	PendingCombatants.Reset();

	bFieldHidden = false;
	NoteFieldReturnedToBattleSubsystem();
	OnFieldShown.Broadcast();
}

void UJRPGBattleSceneSubsystem::NoteFieldReturnedToBattleSubsystem()
{
	// O Field voltou: a carência de depois da luta (nenhum encontro novo, ninguém persegue) conta daqui
	if (UBattleSubsystem* Battle = GetBattleSubsystem(GetWorld()))
	{
		Battle->NoteFieldReturned();
	}
}

// ---------------------------------------------------------------------------------------------

void UJRPGBattleSceneSubsystem::ChooseArena(const FJRPGBattleSetup& Setup)
{
	bOwnsArena = false;
	Arena = AJRPGBattleArena::FindArenaFor(GetWorld(), Setup.Location);
	if (Arena.IsValid())
	{
		TransitionTime = Arena->TransitionTime;
		TransitionColor = Arena->TransitionColor;
		return;
	}

	// Mapa sem arena: uma provisória no ponto do encontro, a party olhando para o inimigo
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	if (AJRPGBattleArena* Temp = GetWorld()->SpawnActor<AJRPGBattleArena>(AJRPGBattleArena::StaticClass(),
		Setup.Location, FRotator(0.0f, Setup.FacingYaw, 0.0f), Params))
	{
		Temp->bEnabled = false; // FindArenaFor não acha a provisória
		Temp->bUseArea = false;
		Temp->bUseAsFallback = false;
		Arena = Temp;
		bOwnsArena = true;
		TransitionTime = Temp->TransitionTime;
		TransitionColor = Temp->TransitionColor;
		UE_LOG(LogTemp, Log, TEXT("BattleScene: mapa sem arena — a luta é montada no ponto do encontro"));
	}
}

void UJRPGBattleSceneSubsystem::ReleaseArena()
{
	if (bOwnsArena && Arena.IsValid())
	{
		Arena->Destroy();
	}
	Arena = nullptr;
	bOwnsArena = false;
}

void UJRPGBattleSceneSubsystem::Fade(bool bToBlack)
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr)
	{
		CameraManager->StartCameraFade(bToBlack ? 0.0f : 1.0f, bToBlack ? 1.0f : 0.0f, TransitionTime,
			TransitionColor, false, bToBlack);
	}
}

// =================================================================================================
// A formação: o recentrar, o make-way e a separação do original (só apresentação; as regras não têm geometria)
// =================================================================================================

void UJRPGBattleSceneSubsystem::PlanFormationShift(const AJRPGBattlePawn* Focus, int32 Frame)
{
	AJRPGBattleArena* Current = Arena.Get();
	if (!Current || !Current->bRecentreFormation)
	{
		return;
	}
	// Um recentrar pela metade termina agora: o novo parte de onde todos estariam
	if (FormationShiftDone < 1.0f)
	{
		ApplyFormationShift(1.0f - FormationShiftDone);
	}
	FormationShift.Reset();
	FormationShiftDone = 1.0f;

	// As posições no espaço da arena (os assentos são relativos a ela; o centro dela é a origem do palco do original)
	const FTransform ArenaTransform = Current->GetActorTransform();
	TArray<AJRPGBattlePawn*> Pawns;
	TArray<FVector> Local;
	for (AJRPGBattlePawn* Pawn : Current->GetBattlePawns())
	{
		if (IsValid(Pawn) && Pawn->IsStanding())
		{
			Pawns.Add(Pawn);
			Local.Add(ArenaTransform.InverseTransformPosition(Pawn->GetSettledLocation()));
		}
	}
	if (Pawns.Num() == 0)
	{
		return;
	}
	const float UnitsToCm = Pawns[0]->GetUnitsToCm();
	const double Limit = Current->FormationSpanLimitUnits * UnitsToCm;

	// 1. Comprime o eixo mais largo que o limite (FUN_801DB318: coord * 0x800 / largura)
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		double Min = TNumericLimits<double>::Max();
		double Max = TNumericLimits<double>::Lowest();
		for (const FVector& P : Local)
		{
			Min = FMath::Min(Min, P[Axis]);
			Max = FMath::Max(Max, P[Axis]);
		}
		const double Span = Max - Min;
		if (Limit > 0.0 && Span > Limit)
		{
			for (FVector& P : Local)
			{
				P[Axis] *= Limit / Span;
			}
		}
	}

	// 2. O centro: o alvo do golpe (a caixa só dele), senão o meio da caixa de todos
	FVector Center = FVector::ZeroVector;
	const int32 FocusIndex = Focus ? Pawns.IndexOfByKey(Focus) : INDEX_NONE;
	if (FocusIndex != INDEX_NONE)
	{
		Center = Local[FocusIndex];
	}
	else
	{
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			double Min = TNumericLimits<double>::Max();
			double Max = TNumericLimits<double>::Lowest();
			for (const FVector& P : Local)
			{
				Min = FMath::Min(Min, P[Axis]);
				Max = FMath::Max(Max, P[Axis]);
			}
			Center[Axis] = (Min + Max) * 0.5;
		}
	}
	Center.Z = 0.0;

	// 3. O que cada um anda, em mundo
	bool bAnyMove = false;
	for (int32 i = 0; i < Pawns.Num(); ++i)
	{
		const FVector Before = Pawns[i]->GetSettledLocation();
		FVector After = ArenaTransform.TransformPosition(Local[i] - Center);
		FVector Delta = After - Before;
		Delta.Z = 0.0;
		if (Delta.Size2D() > 1.0)
		{
			bAnyMove = true;
		}
		FormationShift.Add(Pawns[i], Delta);
	}
	if (!bAnyMove)
	{
		FormationShift.Reset();
		return;
	}
	// O quanto o centro da formação andou, em mundo: o que a câmera de ação soma no foco dela (o "focus pair" do original)
	FVector Translation = ArenaTransform.TransformVector(-Center);
	Translation.Z = 0.0;

	if (Current->RecentreFrames <= 0)
	{
		// Como o original: de uma vez, neste quadro (a câmera que segue o OnFormationShifted anda junto e ninguém vê)
		ApplyFormationShift(1.0f);
		FormationShift.Reset();
		OnFormationShifted.Broadcast(Translation, 0);
		return;
	}
	FormationShiftStart = Frame;
	FormationShiftFrames = Current->RecentreFrames;
	FormationShiftDone = 0.0f;
	OnFormationShifted.Broadcast(Translation, FormationShiftFrames);
}

void UJRPGBattleSceneSubsystem::MakeWay(const AJRPGBattlePawn* Attacker, const AJRPGBattlePawn* Target, int32 Frame)
{
	AJRPGBattleArena* Current = Arena.Get();
	if (!Current || !Current->bMakeWay || !Attacker || !Target)
	{
		return;
	}
	const FVector From = Attacker->GetActorLocation();
	FVector Lane = Target->GetSettledBodyLocation() - From;
	Lane.Z = 0.0;
	const double LaneLength = Lane.Size2D();
	if (LaneLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector Heading = Lane / LaneLength;
	// A classe do monstro da luta corpo a corpo: a do alvo para um herói, a do próprio atacante para um monstro
	const int32 SizeClass = FMath::Max(Attacker->GetSizeClass() > 0 ? Attacker->GetSizeClass() : Target->GetSizeClass(), 1);
	const float UnitsToCm = Attacker->GetUnitsToCm();
	// O passo do original: classe * 8 unidades, a 8 unidades por vsync (a contagem drena a classe pelo passo do quadro)
	const float FullStep = static_cast<float>(SizeClass) * 8.0f * UnitsToCm;
	const float StepPerFrame = FMath::Max(8.0f * UnitsToCm, 1.0f);
	// Onde a caminhada para: a borda do alcance antes do alvo; ninguém além disso está no caminho
	const double WalkEnd = FMath::Max(LaneLength - static_cast<double>(Attacker->ReachFor(Target)), 0.0);
	const FVector Left(-Heading.Y, Heading.X, 0.0);
	for (AJRPGBattlePawn* Pawn : Current->GetBattlePawns())
	{
		if (!IsValid(Pawn) || Pawn == Attacker || Pawn == Target || !Pawn->IsStanding())
		{
			continue;
		}
		const FVector ToPawn = Pawn->GetSettledBodyLocation() - From;
		const double Across = FVector::DotProduct(ToPawn, Left);
		// Cada um sai para o lado da linha em que já está (meia volta de um lado, h + 0x400; do outro, h - 0x400)
		const float Side = Across >= 0.0 ? 1.0f : -1.0f;
		float Distance = FullStep;
		if (Current->bMakeWayOnlyInLane)
		{
			// Melhoria nossa (o original move todo mundo, perto ou longe): só sai quem está no corredor do atacante, e só o que
			// falta para liberar o caminho. A largura do corredor é a distância em que a separação empurraria os dois corpos
			const double HalfWidth = (Attacker->SeparationRadiusCm() + Pawn->SeparationRadiusCm()) / 6.0 * Current->MakeWayLaneScale;
			const double Along = FVector::DotProduct(ToPawn, Heading);
			if (Along <= 0.0 || Along >= WalkEnd + HalfWidth || FMath::Abs(Across) >= HalfWidth)
			{
				continue;
			}
			Distance = FMath::Min(FullStep, static_cast<float>(HalfWidth - FMath::Abs(Across)));
		}
		if (Distance <= 1.0f)
		{
			continue;
		}
		const int32 Frames = FMath::Max(FMath::CeilToInt(Distance / StepPerFrame), 8);
		Pawn->StepAside(Left * Side * Distance, Frame, Frames);
	}
}

void UJRPGBattleSceneSubsystem::StepFormation(int32 Frame)
{
	AJRPGBattleArena* Current = Arena.Get();
	if (!Current)
	{
		return;
	}
	// O deslize do recentrar: cada quadro anda a fração que falta até o alpha deste quadro
	if (FormationShiftDone < 1.0f)
	{
		const float Alpha = FMath::Clamp(static_cast<float>(Frame - FormationShiftStart) / static_cast<float>(FormationShiftFrames), 0.0f, 1.0f);
		const float Step = Alpha - FormationShiftDone;
		if (Step > 0.0f)
		{
			ApplyFormationShift(Step);
			FormationShiftDone = Alpha;
		}
		if (FormationShiftDone >= 1.0f)
		{
			FormationShift.Reset();
		}
	}

	// A separação: todo par de quem está em pé, mais perto que (r1 + r2) / 6, se afasta (o original roda os pares nas duas
	// ordens, então cada um anda o passo duas vezes)
	if (!Current->bSeparation || Current->SeparationStepUnits <= 0.0f)
	{
		return;
	}
	TArray<AJRPGBattlePawn*> Standing;
	for (AJRPGBattlePawn* Pawn : Current->GetBattlePawns())
	{
		if (IsValid(Pawn) && Pawn->IsStanding())
		{
			Standing.Add(Pawn);
		}
	}
	for (int32 i = 0; i < Standing.Num(); ++i)
	{
		for (int32 j = i + 1; j < Standing.Num(); ++j)
		{
			AJRPGBattlePawn* A = Standing[i];
			AJRPGBattlePawn* B = Standing[j];
			// FUN_80050BB8 mede os dois CORPOS (body pairs) e empurra as posições: um clip que leva o corpo para cima do outro
			// também separa
			const FVector Gap = FVector(B->GetBodyLocation() - A->GetBodyLocation()) * FVector(1.0, 1.0, 0.0);
			const double Threshold = (A->SeparationRadiusCm() + B->SeparationRadiusCm()) / 6.0;
			const double Dist = Gap.Size2D();
			if (Dist >= Threshold)
			{
				continue;
			}
			// Em cima um do outro: separa por um eixo qualquer, fixo
			const FVector Dir = Dist > KINDA_SMALL_NUMBER ? Gap / Dist : FVector(1.0, 0.0, 0.0);
			const FVector Push = Dir * (2.0 * Current->SeparationStepUnits * A->GetUnitsToCm());
			const FVector NewA = A->GetActorLocation() - Push;
			const FVector NewB = B->GetActorLocation() + Push;
			// Ninguém é empurrado para fora da arena (só vale para quem está dentro dela, como o ClampToArena)
			if (!Current->bUseArea || !Current->ContainsPoint(A->GetActorLocation()) || Current->ContainsPoint(NewA))
			{
				A->TranslateBy(-Push);
			}
			if (!Current->bUseArea || !Current->ContainsPoint(B->GetActorLocation()) || Current->ContainsPoint(NewB))
			{
				B->TranslateBy(Push);
			}
		}
	}
}

void UJRPGBattleSceneSubsystem::ApplyFormationShift(float Fraction)
{
	for (const TPair<TWeakObjectPtr<AJRPGBattlePawn>, FVector>& Pair : FormationShift)
	{
		AJRPGBattlePawn* Pawn = Pair.Key.Get();
		if (!Pawn)
		{
			continue;
		}
		Pawn->TranslateBy(Pair.Value * Fraction);
		// O destino da caminhada é relativo ao alvo: anda o que o alvo andou (com a compressão, cada um anda um tanto)
		if (Pawn->IsWalkingApproach())
		{
			AJRPGBattlePawn* Goal = Pawn->GetActionTarget();
			const FVector* GoalShift = Goal ? FormationShift.Find(TWeakObjectPtr<AJRPGBattlePawn>(Goal)) : nullptr;
			const FVector TargetDelta = GoalShift ? *GoalShift : FVector::ZeroVector;
			Pawn->TranslateApproachGoal((TargetDelta - Pair.Value) * Fraction);
		}
	}
}

void UJRPGBattleSceneSubsystem::SubmitApproachFor(UBattleSubsystem& Battle, const AJRPGBattlePawn& Actor, const AJRPGBattlePawn* Target,
	const FJRPGActionTimeline& Timeline)
{
	if (!Battle.bApproachAuthority || !Target || Target == &Actor)
	{
		return;
	}
	// Só ação de golpe (a magia e o item não passam pelo núcleo do golpe)
	const bool bMelee = Timeline.Steps.ContainsByPredicate([](const FJRPGActionStep& Step) { return Step.Kind != EJRPGActionStepKind::SpecialStarter; });
	if (!bMelee)
	{
		return;
	}
	// O ângulo entre as frentes, dobrado como o 0x801E3068..0x801E30C8 do original: 0 de frente .. 0x800 de costas (0x1000 a volta)
	auto ToUnits = [](double Yaw)
	{
		return FMath::RoundToInt32(FRotator::NormalizeAxis(Yaw) / 360.0 * 4096.0) & 0xFFF;
	};
	const int32 Diff = (ToUnits(Actor.GetActorRotation().Yaw) - ToUnits(Target->GetActorRotation().Yaw)) & 0xFFF;
	const int32 Folded = Diff < 0x800 ? 0x1000 - Diff : Diff;
	Battle.SubmitApproach(Timeline.ActionIndex, Folded - 0x800, Actor.GetApproachFrames());
}
