#include "Battle/JRPGBattleStage.h"
#include "Battle/BattleSubsystem.h"
#include "Battle/JRPGBattleArena.h"
#include "Battle/JRPGBattlePawn.h"
#include "Battle/JRPGBattleSettings.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Camera/CameraSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BillboardComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Field/JRPGFieldCharacter.h"
#include "Field/JRPGFieldPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	UBattleSubsystem* GetBattleSubsystem(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
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

AJRPGBattleStage::AJRPGBattleStage()
{
	PrimaryActorTick.bCanEverTick = false;
	PawnClass = AJRPGBattlePawn::StaticClass();

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
#if WITH_EDITORONLY_DATA
	EditorIcon = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("EditorIcon"));
	if (EditorIcon)
	{
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Icon(TEXT("/Engine/EditorResources/S_Actor.S_Actor"));
		EditorIcon->Sprite = Icon.Get();
		EditorIcon->SetupAttachment(GetRootComponent());
		EditorIcon->SetHiddenInGame(true);
		EditorIcon->bIsScreenSizeScaled = true;
	}
#endif
}

AJRPGBattleStage* AJRPGBattleStage::GetBattleStage(const UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AJRPGBattleStage> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AJRPGBattleStage::BeginPlay()
{
	Super::BeginPlay();

	if (UBattleSubsystem* Battle = GetBattleSubsystem(this))
	{
		Battle->OnBattleStarted.AddDynamic(this, &AJRPGBattleStage::HandleBattleStarted);
		Battle->OnBattleEnded.AddDynamic(this, &AJRPGBattleStage::HandleBattleEnded);
	}
}

void AJRPGBattleStage::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	UBattleSubsystem* Battle = GetBattleSubsystem(this);
	if (Battle)
	{
		Battle->OnBattleStarted.RemoveDynamic(this, &AJRPGBattleStage::HandleBattleStarted);
		Battle->OnBattleEnded.RemoveDynamic(this, &AJRPGBattleStage::HandleBattleEnded);
	}
	// Ator destruído com o mapa ainda de pé: devolve o Field. Mapa indo embora: o mundo leva tudo
	if (Phase != EPhase::Idle && (EndPlayReason == EEndPlayReason::Destroyed || EndPlayReason == EEndPlayReason::RemovedFromWorld))
	{
		Teardown();
	}
	// O mapa saiu no meio da luta: ela não pode ficar aberta no GameInstance
	if (Battle && Battle->IsInBattle())
	{
		Battle->EndBattle(EJRPGBattleResult::Aborted);
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------
// Começo e fim
// ---------------------------------------------------------------------------------------------

void AJRPGBattleStage::HandleBattleStarted(const FJRPGBattleSetup& Setup, const TArray<FJRPGCombatant>& Combatants)
{
	if (Phase != EPhase::Idle)
	{
		// Uma luta nova antes da anterior sair da tela: desmonta a velha na hora
		GetWorldTimerManager().ClearTimer(TransitionTimer);
		Teardown();
	}
	PendingSetup = Setup;
	PendingCombatants = Combatants;
	Phase = EPhase::Entering;

	if (TransitionTime > 0.0f)
	{
		Fade(true);
		GetWorldTimerManager().SetTimer(TransitionTimer, this, &AJRPGBattleStage::Assemble, TransitionTime, false);
	}
	else
	{
		Assemble();
	}
}

void AJRPGBattleStage::HandleBattleEnded(EJRPGBattleResult Result)
{
	Print(FString::Printf(TEXT("luta acabou (%s)"), *StaticEnum<EJRPGBattleResult>()->GetNameStringByValue(static_cast<int64>(Result))), FColor::Cyan);

	if (Phase == EPhase::Entering)
	{
		// Acabou antes da cena existir: só abre a tela de novo
		GetWorldTimerManager().ClearTimer(TransitionTimer);
		Phase = EPhase::Idle;
		if (TransitionTime > 0.0f)
		{
			Fade(false);
		}
		return;
	}
	if (Phase != EPhase::Assembled)
	{
		return;
	}

	Phase = EPhase::Leaving;
	if (TransitionTime > 0.0f)
	{
		Fade(true);
		GetWorldTimerManager().SetTimer(TransitionTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			Teardown();
			Fade(false);
		}), TransitionTime, false);
	}
	else
	{
		Teardown();
	}
}

void AJRPGBattleStage::Assemble()
{
	int32 PartyCount = 0;
	int32 EnemyCount = 0;
	for (const FJRPGCombatant& C : PendingCombatants)
	{
		(C.Side == EJRPGBattleSide::Party ? PartyCount : EnemyCount)++;
	}

	// Primeiro some o Field: a cápsula do jogador não pode atrapalhar o chão dos lugares
	HideFieldActors();
	DisableFieldInput();
	ChooseArena();
	if (Arena.IsValid())
	{
		for (AActor* Actor : Arena->HideDuringBattle)
		{
			HideActor(Actor, false);
		}
	}

	const TArray<FJRPGArenaSeat> Seats = Arena.IsValid() ? Arena->GetFormation(PartyCount, EnemyCount) : TArray<FJRPGArenaSeat>();
	UClass* SpawnClass = PawnClass ? PawnClass.Get() : AJRPGBattlePawn::StaticClass();

	for (const FJRPGCombatant& C : PendingCombatants)
	{
		const int32 SeatIndex = C.Side == EJRPGBattleSide::Party ? C.SideIndex : PartyCount + C.SideIndex;
		FTransform Feet(FRotator(0.0f, PendingSetup.FacingYaw, 0.0f), PendingSetup.Location);
		if (Seats.IsValidIndex(SeatIndex))
		{
			Feet = Seats[SeatIndex].Transform;
			if (Seats[SeatIndex].Status == EJRPGArenaSeatStatus::Blocked)
			{
				Print(FString::Printf(TEXT("%s no lugar %d: %s"), *C.Key.ToString(), SeatIndex, *Seats[SeatIndex].Problem), FColor::Red);
			}
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		AJRPGBattlePawn* Pawn = GetWorld()->SpawnActor<AJRPGBattlePawn>(SpawnClass, Feet, Params);
		if (!Pawn)
		{
			Print(FString::Printf(TEXT("não consegui criar o pawn de %s"), *C.Key.ToString()), FColor::Red);
			continue;
		}

		FJRPGBattleVisual Visual;
		FString Source;
		if (!ResolveVisual(C, Visual, Source))
		{
			Print(FString::Printf(TEXT("%s sem modelo de batalha (%s)"), *C.Key.ToString(), *Source), FColor::Orange);
		}
		Pawn->InitCombatant(C, Visual, Feet);
		Pawns.Add(Pawn);
	}

	FrameCamera();
	Phase = EPhase::Assembled;
	if (TransitionTime > 0.0f)
	{
		Fade(false);
	}

	Print(FString::Printf(TEXT("cena montada: %d herói(s), %d inimigo(s) na arena %s"), PartyCount, EnemyCount,
		Arena.IsValid() ? *Arena->GetActorNameOrLabel() : TEXT("?")), FColor::Cyan);

	TArray<AJRPGBattlePawn*> Out = GetPawns();
	OnStageAssembled.Broadcast(Out);
}

void AJRPGBattleStage::Teardown()
{
	for (AJRPGBattlePawn* Pawn : Pawns)
	{
		if (IsValid(Pawn))
		{
			Pawn->Destroy();
		}
	}
	Pawns.Reset();

	if (bOwnsArena && Arena.IsValid())
	{
		Arena->Destroy();
	}
	Arena = nullptr;
	bOwnsArena = false;

	RestoreHiddenActors();
	RestoreFieldInput();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UCameraSubsystem* Camera = GI->GetSubsystem<UCameraSubsystem>())
		{
			Camera->RestoreToPlayerCamera(TransitionTime > 0.0f ? 0.0f : 0.5f);
		}
	}

	const bool bWasAssembled = Phase == EPhase::Assembled || Phase == EPhase::Leaving;
	Phase = EPhase::Idle;
	PendingCombatants.Reset();
	if (bWasAssembled)
	{
		Print(TEXT("cena desmontada"), FColor::Cyan);
		OnStageCleared.Broadcast();
	}
}

// ---------------------------------------------------------------------------------------------
// Partes da montagem
// ---------------------------------------------------------------------------------------------

void AJRPGBattleStage::ChooseArena()
{
	Arena = AJRPGBattleArena::FindArenaFor(this, PendingSetup.Location);
	bOwnsArena = false;
	if (Arena.IsValid())
	{
		return;
	}

	// Mapa sem arena: uma provisória no ponto do encontro, a party olhando para o inimigo
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AJRPGBattleArena* Temp = GetWorld()->SpawnActor<AJRPGBattleArena>(AJRPGBattleArena::StaticClass(),
		PendingSetup.Location, FRotator(0.0f, PendingSetup.FacingYaw, 0.0f), Params);
	if (Temp)
	{
		Temp->bEnabled = false; // FindArenaFor não acha a provisória
		Temp->bUseArea = false;
		Temp->bUseAsFallback = false;
		Arena = Temp;
		bOwnsArena = true;
		Print(TEXT("mapa sem arena — a luta é montada no ponto do encontro"), FColor::Orange);
	}
}

void AJRPGBattleStage::HideFieldActors()
{
	// O personagem controlado e os seguidores da party (todos AJRPGFieldCharacter)
	if (const APlayerController* PC = GetPlayerController())
	{
		HideActor(PC->GetPawn(), true);
	}
	for (TActorIterator<AJRPGFieldCharacter> It(GetWorld()); It; ++It)
	{
		HideActor(*It, true);
	}
}

void AJRPGBattleStage::HideActor(AActor* Actor, bool bFreeze)
{
	if (!IsValid(Actor))
	{
		return;
	}
	for (const FHiddenActor& Hidden : HiddenActors)
	{
		if (Hidden.Actor.Get() == Actor)
		{
			return;
		}
	}
	FHiddenActor& Entry = HiddenActors.AddDefaulted_GetRef();
	Entry.Actor = Actor;
	Entry.bWasHidden = Actor->IsHidden();
	Entry.bHadCollision = Actor->GetActorEnableCollision();
	Entry.TimeDilation = Actor->CustomTimeDilation;

	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	if (bFreeze)
	{
		// Parado de verdade: movimento, animação e o MoveTo dos seguidores
		Actor->CustomTimeDilation = 0.0f;
	}
}

void AJRPGBattleStage::RestoreHiddenActors()
{
	for (const FHiddenActor& Hidden : HiddenActors)
	{
		if (AActor* Actor = Hidden.Actor.Get())
		{
			Actor->SetActorHiddenInGame(Hidden.bWasHidden);
			Actor->SetActorEnableCollision(Hidden.bHadCollision);
			Actor->CustomTimeDilation = Hidden.TimeDilation;
		}
	}
	HiddenActors.Reset();
}

void AJRPGBattleStage::DisableFieldInput()
{
	APlayerController* PC = GetPlayerController();
	InputController = PC;
	bRestoreFieldInput = false;
	bRestoreMoveInput = false;
	if (!PC)
	{
		return;
	}
	if (AJRPGFieldPlayerController* FieldPC = Cast<AJRPGFieldPlayerController>(PC))
	{
		bRestoreFieldInput = FieldPC->IsFieldInputEnabled();
		FieldPC->SetFieldInputEnabled(false);
	}
	else
	{
		PC->SetIgnoreMoveInput(true);
		bRestoreMoveInput = true;
	}
}

void AJRPGBattleStage::RestoreFieldInput()
{
	APlayerController* PC = InputController.Get();
	if (PC)
	{
		if (bRestoreFieldInput)
		{
			if (AJRPGFieldPlayerController* FieldPC = Cast<AJRPGFieldPlayerController>(PC))
			{
				FieldPC->SetFieldInputEnabled(true);
			}
		}
		if (bRestoreMoveInput)
		{
			PC->SetIgnoreMoveInput(false);
		}
	}
	InputController = nullptr;
	bRestoreFieldInput = false;
	bRestoreMoveInput = false;
}

void AJRPGBattleStage::FrameCamera()
{
	UGameInstance* GI = GetGameInstance();
	UCameraSubsystem* Camera = GI ? GI->GetSubsystem<UCameraSubsystem>() : nullptr;
	if (!Camera)
	{
		return;
	}
	// Debaixo do fade a câmera corta; sem fade, desliza
	const float Blend = TransitionTime > 0.0f ? 0.0f : 0.75f;

	if (Arena.IsValid() && IsValid(Arena->BattleCamera))
	{
		Camera->FocusOnLevelCamera(Arena->BattleCamera, Blend, false, 0.0f);
		return;
	}

	// O preset gira em volta do primeiro do grupo: um inimigo primeiro põe a câmera atrás da party
	TArray<AActor*> Group;
	const EJRPGBattleSide First = bCameraBehindParty ? EJRPGBattleSide::Enemy : EJRPGBattleSide::Party;
	for (AJRPGBattlePawn* Pawn : GetPawns())
	{
		if (Pawn->GetCombatant().Side == First)
		{
			Group.Add(Pawn);
		}
	}
	for (AJRPGBattlePawn* Pawn : GetPawns())
	{
		if (Pawn->GetCombatant().Side != First)
		{
			Group.Add(Pawn);
		}
	}
	if (Group.Num() > 0)
	{
		Camera->FrameGroup(Group, Blend, CameraPadding, CameraPreset, false);
	}
}

bool AJRPGBattleStage::ResolveVisual(const FJRPGCombatant& Combatant, FJRPGBattleVisual& OutVisual, FString& OutSource) const
{
	if (Combatant.Side == EJRPGBattleSide::Party)
	{
		if (const FJRPGBattleVisual* Found = PartyVisuals.Find(Combatant.Key))
		{
			if (!Found->Mesh.IsNull())
			{
				OutVisual = *Found;
				OutSource = TEXT("PartyVisuals");
				return true;
			}
		}
		OutSource = TEXT("fora do PartyVisuals do palco");
	}
	else
	{
		FJRPGEnemyData Data;
		if (UJRPGEncounterLibrary::GetEnemyData(Combatant.Key, Data) && !Data.Mesh.IsNull())
		{
			OutVisual.Mesh = Data.Mesh;
			OutVisual.IdleAnim = Data.Anims.Idle;
			OutSource = TEXT("DT_Enemies");
			return true;
		}
		OutSource = TEXT("sem Mesh no DT_Enemies");
	}

	OutVisual = FallbackVisual;
	return !FallbackVisual.Mesh.IsNull();
}

void AJRPGBattleStage::Fade(bool bToBlack)
{
	const APlayerController* PC = GetPlayerController();
	if (APlayerCameraManager* CameraManager = PC ? PC->PlayerCameraManager.Get() : nullptr)
	{
		CameraManager->StartCameraFade(bToBlack ? 0.0f : 1.0f, bToBlack ? 1.0f : 0.0f, TransitionTime,
			TransitionColor, false, bToBlack);
	}
}

APlayerController* AJRPGBattleStage::GetPlayerController() const
{
	return GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
}

TArray<AJRPGBattlePawn*> AJRPGBattleStage::GetPawns() const
{
	TArray<AJRPGBattlePawn*> Out;
	for (AJRPGBattlePawn* Pawn : Pawns)
	{
		if (IsValid(Pawn))
		{
			Out.Add(Pawn);
		}
	}
	return Out;
}

AJRPGBattlePawn* AJRPGBattleStage::GetPawnForSlot(int32 SlotIndex) const
{
	for (AJRPGBattlePawn* Pawn : Pawns)
	{
		if (IsValid(Pawn) && Pawn->GetCombatant().SlotIndex == SlotIndex)
		{
			return Pawn;
		}
	}
	return nullptr;
}

void AJRPGBattleStage::Print(const FString& Message, const FColor& Color) const
{
	if (!bPrintDebug)
	{
		return;
	}
	const FString Line = FString::Printf(TEXT("BattleStage: %s"), *Message);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, Color, Line);
	}
}

// ---------------------------------------------------------------------------------------------
// UJRPGBattleStageSubsystem
// ---------------------------------------------------------------------------------------------

bool UJRPGBattleStageSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UJRPGBattleStageSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UJRPGBattleSettings* Settings = GetDefault<UJRPGBattleSettings>();
	if (!Settings->bSpawnStageAutomatically || AJRPGBattleStage::GetBattleStage(&InWorld))
	{
		return;
	}
	UClass* StageClass = Settings->StageClass.LoadSynchronous();
	if (!StageClass)
	{
		if (!Settings->StageClass.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("BattleStage: StageClass %s não carregou — usando o AJRPGBattleStage do plugin"), *Settings->StageClass.ToString());
		}
		StageClass = AJRPGBattleStage::StaticClass();
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	InWorld.SpawnActor<AJRPGBattleStage>(StageClass, FTransform::Identity, Params);
}
