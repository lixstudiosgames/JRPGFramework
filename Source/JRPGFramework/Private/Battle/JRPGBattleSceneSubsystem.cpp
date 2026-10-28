#include "Battle/JRPGBattleSceneSubsystem.h"
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
