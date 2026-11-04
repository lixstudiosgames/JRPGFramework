#include "Battle/JRPGBattleSceneSubsystem.h"
#include "Arts/ArtsSubsystem.h"
#include "Arts/JRPGArtsRules.h"
#include "Battle/BattleSubsystem.h"
#include "Battle/JRPGBattleArena.h"
#include "Battle/JRPGBattlePawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
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
		Battle->EndBattle(Result);
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

	/** A luta em andamento, esperando o comando de alguém; senão avisa e devolve nulo. */
	UBattleSubsystem* BattleWaitingForCommands(UWorld* World, const TCHAR* Command)
	{
		UBattleSubsystem* Battle = GetBattleSubsystem(World);
		if (!Battle || !Battle->IsInBattle())
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: nenhuma luta acontecendo"), Command);
			return nullptr;
		}
		if (Battle->GetState().Stage != EJRPGBattleStage::Commands || Battle->GetActiveSlot() < 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: a luta não está esperando comandos (a rodada está tocando)"), Command);
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
			UE_LOG(LogTemp, Warning, TEXT("JRPG.Battle.Arts: não entendi os botões '%s' (Arms, Ra-Seru, High, Low; ou A R H L; ou setas)"), *ButtonText);
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
		// O mapa saiu no meio da luta: ela não pode ficar aberta no GameInstance
		if (Battle->IsInBattle())
		{
			Battle->EndBattle(EJRPGBattleResult::Aborted);
		}
	}
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
		if (AJRPGBattlePawn* Actor = PawnOf(Event.SlotIndex))
		{
			if (const FJRPGActionTimeline* Timeline = Battle->FindTimeline(Event.ActionIndex))
			{
				Actor->BeginAction(*Timeline, PawnOf(Event.TargetSlot));
			}
		}
		break;
	case EJRPGBattleEventType::ActionEnd:
		// O fim da ação, inclusive a cortada (o último inimigo caiu no meio da combo): a cópia que o pawn tem pode ser mais
		// longa que a que valeu, então ele fecha agora em vez de esperar o TotalFrames dela
		if (AJRPGBattlePawn* Actor = PawnOf(Event.SlotIndex))
		{
			Actor->EndAction();
		}
		break;
	case EJRPGBattleEventType::Damage:
		if (AJRPGBattlePawn* Target = PawnOf(Event.TargetSlot))
		{
			Target->ShowEvent(Event, (Event.Flags & FJRPGBattleEventFlag::ArtHit) ? FString::Printf(TEXT("%d!"), Event.Value) : FString::FromInt(Event.Value), true);
		}
		break;
	case EJRPGBattleEventType::Miss:
		if (AJRPGBattlePawn* Target = PawnOf(Event.TargetSlot))
		{
			Target->ShowEvent(Event, TEXT("Miss"), true);
		}
		break;
	case EJRPGBattleEventType::Block:
		if (AJRPGBattlePawn* Target = PawnOf(Event.TargetSlot))
		{
			Target->ShowEvent(Event, TEXT("Block"), true);
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
	const FString Prompt = FString::Printf(TEXT("Round %d - command for %s (slot %d): JRPG.Battle.Attack [enemy] | JRPG.Battle.Arts <buttons> [enemy] | JRPG.Battle.Auto"),
		Battle->GetState().Round, *Battle->GetCombatants()[Slot].Key.ToString(), Slot);
	UE_LOG(LogTemp, Log, TEXT("BattleScene: %s"), *Prompt);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(7421, 30.0f, FColor::Cyan, Prompt);   // a mesma chave: a linha nova substitui a velha
	}
}

void UJRPGBattleSceneSubsystem::HandleLogicFrame(int32 Frame)
{
	AJRPGBattleArena* Current = Arena.Get();
	if (Phase != EPhase::Assembled || !Current)
	{
		return;
	}
	const UBattleSubsystem* Battle = GetBattleSubsystem(GetWorld());
	for (AJRPGBattlePawn* Pawn : Current->GetBattlePawns())
	{
		if (IsValid(Pawn))
		{
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
			Pawn->UpdatePlayback(Frame);
		}
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
					Actor->BeginAction(*Active, Current->GetPawnForSlot(Running.TargetSlot));
				}
			}
		}
		// A cena está de pé: o relógio da luta pode andar
		Battle->HoldLogic(false);
	}
}

void UJRPGBattleSceneSubsystem::LeaveBattleScene()
{
	if (AJRPGBattleArena* Current = Arena.Get())
	{
		Current->TeardownBattle();
	}
	ReleaseArena();
	Phase = EPhase::Idle;
	PendingCombatants.Reset();

	bFieldHidden = false;
	OnFieldShown.Broadcast();
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
