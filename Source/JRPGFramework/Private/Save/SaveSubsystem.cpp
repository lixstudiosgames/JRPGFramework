#include "Save/SaveSubsystem.h"
#include "Save/JRPGSaveGame.h"
#include "Core/CoreSubsystem.h"
#include "Engine/GameInstance.h"
#include "Inventory/InventorySubsystem.h"
#include "World/WorldStateSubsystem.h"
#include "Party/PartySubsystem.h"
#include "Audio/AudioSubsystem.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

// Carência antes de o Load spawnar o pawn ele mesmo.
//
// NÃO é uma espera por algo que vai chegar: o GameMode do projeto escolhe o
// PlayerStart por TAG (é assim que os portais decidem onde o jogador aparece no
// mapa de destino) e um load não vem de portal nenhum, então ele não acha
// PlayerStart e não spawna — o log da engine mostra o `FindPlayerStart: NO
// PLAYERSTART` seguido do `SpawnActor failed` acontecendo ANTES deste código
// rodar. A carência existe só para não brigar com um GameMode que spawne um
// tick depois; passou disso, quem spawna é o Load.
static constexpr int32 RepositionMaxRetries = 3;

void USaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &USaveSubsystem::HandlePostLoadMap);
}

void USaveSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	PendingLoadSave = nullptr;

	Super::Deinitialize();
}

// ============================================================
// SAVE
// ============================================================

bool USaveSubsystem::SaveGame(int32 SlotIndex)
{
	if (!IsValidSlotIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("SaveSubsystem: SaveGame — slot inválido %d (esperado 0..%d)."),
			SlotIndex, NumSaveSlots - 1);
		return false;
	}

	UCoreSubsystem* Core = GetCore();
	if (!Core)
	{
		return false;
	}

	UJRPGSaveGame* Save = Cast<UJRPGSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UJRPGSaveGame::StaticClass()));
	if (!Save)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveSubsystem: Falha ao criar o objeto de save."));
		return false;
	}

	Save->SaveVersion = UJRPGSaveGame::CurrentSaveVersion;
	Save->Timestamp = FDateTime::Now();

	// --- Localização (CoreSubsystem) ---
	Save->MapName = Core->GetCurrentMapName();
	Save->MapDisplayName = Core->GetCurrentMapDisplayName();
	if (!Core->GetPlayerSaveTransform(Save->PlayerLocation, Save->PlayerRotation))
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveSubsystem: Sem pawn possuído no momento do save — posição salva como origem."));
	}

	// --- Progressão (CoreSubsystem) ---
	Save->Gold = Core->GetGold();
	Save->PlaytimeSeconds = Core->GetPlaytimeSeconds();
	if (!Core->IsPlaytimeTracking())
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveSubsystem: Playtime nunca foi iniciado (StartNewGame/StartPlaytimeTracking) — salvando %.0fs."),
			Save->PlaytimeSeconds);
	}

	// --- Subsystems ---
	UGameInstance* GI = GetGameInstance();
	if (UInventorySubsystem* Inventory = GI->GetSubsystem<UInventorySubsystem>())
	{
		Save->InventorySlots = Inventory->GetInventorySlots();
	}
	if (UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
	{
		Save->EventFlags = WorldState->GetAllEventFlags();
		Save->OpenedChests = WorldState->GetOpenedChests();
		Save->RevivalTrees = WorldState->GetAllRevivalTreeStates();
	}

	if (UPartySubsystem* Party = GI->GetSubsystem<UPartySubsystem>())
	{
		// O roster inteiro é a fonte de verdade.
		Save->Party = Party->GetPartyStateForSave();

		// PartyDisplay é a projeção que o grid de Save/Load lê: só a formação,
		// só o que aparece no card.
		Save->PartyDisplay.Reset();
		for (const FJRPGPartyMember& M : Party->GetActiveMembers())
		{
			FSavePartyMemberDisplay D;
			D.CharacterID = M.Key;
			D.Level = M.Level;
			D.HP = M.HP;
			D.MaxHP = M.MaxHP;
			D.MP = M.MP;
			D.MaxMP = M.MaxMP;
			D.AP = M.AP;
			D.MaxAP = 100;
			Save->PartyDisplay.Add(D);
		}
	}

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, SlotName(SlotIndex), 0);
	if (bOk)
	{
		UE_LOG(LogTemp, Log, TEXT("SaveSubsystem: Jogo salvo no slot %d ('%s', mapa '%s', %d itens, gold %d)."),
			SlotIndex, *SlotName(SlotIndex), *Save->MapName, Save->InventorySlots.Num(), Save->Gold);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("SaveSubsystem: Falha ao gravar o slot %d em disco."), SlotIndex);
	}
	return bOk;
}

// ============================================================
// LOAD
// ============================================================

bool USaveSubsystem::LoadGame(int32 SlotIndex)
{
	if (!IsValidSlotIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("SaveSubsystem: LoadGame — slot inválido %d."), SlotIndex);
		return false;
	}

	UJRPGSaveGame* Save = Cast<UJRPGSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SlotName(SlotIndex), 0));
	if (!Save)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveSubsystem: LoadGame — slot %d vazio ou corrompido."), SlotIndex);
		return false;
	}

	if (Save->SaveVersion > UJRPGSaveGame::CurrentSaveVersion)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveSubsystem: LoadGame — slot %d tem versão %d, mais nova que a suportada (%d)."),
			SlotIndex, Save->SaveVersion, UJRPGSaveGame::CurrentSaveVersion);
		return false;
	}

	if (Save->MapName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("SaveSubsystem: LoadGame — slot %d não tem nome de mapa."), SlotIndex);
		return false;
	}

	// NADA é restaurado agora: o estado só é aplicado quando o novo mapa
	// terminar de carregar (HandlePostLoadMap consome o PendingLoadSave).
	PendingLoadSave = Save;

	// A BGM é PERSISTENTE entre mapas de propósito — é o que evita o corte da
	// música ao passar de porta. No load isso vira defeito: a faixa do lugar
	// onde o jogador estava continuaria tocando por cima do lugar para onde ele
	// voltou. Silêncio, e o mapa novo escolhe a sua.
	//
	// Corte seco (0.0), não fade: o componente é criado com bAutoDestroy=false,
	// então um FadeOut deixaria um componente parado para trás depois que o
	// StopBGM larga a referência. O mapa vai sumir de qualquer jeito.
	if (UAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UAudioSubsystem>())
	{
		Audio->StopBGM(0.0f);
	}

	// Recarrega SEMPRE, mesmo que seja o mapa em que já estamos. Sem isso, um
	// load "no mesmo lugar" deixaria de pé tudo que o mapa criou desde que foi
	// aberto — baú já aberto voltaria fechado no save mas continuaria aberto na
	// tela, NPC movido continuaria movido. OpenLevel com travel absoluto força
	// o LoadMap do zero.
	const FString MapaAtual = GetCore() ? GetCore()->GetCurrentMapName() : FString();
	UE_LOG(LogTemp, Log, TEXT("SaveSubsystem: Carregando slot %d — abrindo mapa '%s'%s."),
		SlotIndex, *Save->MapName,
		MapaAtual.Equals(Save->MapName, ESearchCase::IgnoreCase)
			? TEXT(" (mesmo mapa — recarregando do zero)") : TEXT(""));

	UGameplayStatics::OpenLevel(this, FName(*Save->MapName), /*bAbsolute=*/true);
	return true;
}

void USaveSubsystem::HandlePostLoadMap(UWorld* NewWorld)
{
	if (!PendingLoadSave || !NewWorld)
	{
		return;
	}

	// Protege contra worlds de outros GameInstances (ex: múltiplos clients PIE)
	if (NewWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}

	UJRPGSaveGame* Save = PendingLoadSave;
	PendingLoadSave = nullptr;

	ApplyLoadedState(Save, NewWorld);
}

void USaveSubsystem::ApplyLoadedState(UJRPGSaveGame* Save, UWorld* World)
{
	UCoreSubsystem* Core = GetCore();
	if (!Core || !Save)
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (UInventorySubsystem* Inventory = GI->GetSubsystem<UInventorySubsystem>())
	{
		Inventory->LoadInventoryState(Save->InventorySlots);
	}
	if (UWorldStateSubsystem* WorldState = GI->GetSubsystem<UWorldStateSubsystem>())
	{
		WorldState->LoadWorldState(Save->EventFlags, Save->OpenedChests, Save->RevivalTrees);
	}
	if (UPartySubsystem* Party = GI->GetSubsystem<UPartySubsystem>())
	{
		Party->LoadPartyState(Save->Party);
	}

	Core->RestoreFromSave(Save->Gold, Save->PlaytimeSeconds, Save->MapDisplayName);

	TryRepositionPlayer(World, Save->PlayerLocation, Save->PlayerRotation, RepositionMaxRetries);

	UE_LOG(LogTemp, Log, TEXT("SaveSubsystem: Estado do save aplicado no mapa '%s' (gold %d, playtime %.0fs)."),
		*Save->MapName, Save->Gold, Save->PlaytimeSeconds);
}

void USaveSubsystem::TryRepositionPlayer(UWorld* World, FVector Location, FRotator Rotation, int32 RetriesLeft)
{
	if (!World)
	{
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	if (Pawn)
	{
		Pawn->SetActorLocationAndRotation(Location, Rotation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
		PC->SetControlRotation(Rotation);
		UE_LOG(LogTemp, Log, TEXT("SaveSubsystem: Player reposicionado em (%s)."), *Location.ToCompactString());
		return;
	}

	if (RetriesLeft <= 0)
	{
		// Caminho NORMAL de um load, não uma falha: o GameMode acha o
		// PlayerStart por tag (o esquema dos portais) e um load não traz tag
		// nenhuma, então não há pawn para reposicionar. O Load spawna o seu.
		//
		// A alternativa seria pôr em cada mapa um PlayerStart sem tag para o
		// GameMode achar — mas aí ele viraria também o destino silencioso de
		// qualquer portal com a tag errada, escondendo o defeito. Melhor o Load
		// assumir a responsabilidade: ele sabe exatamente onde o jogador estava.
		if (PC && SpawnPlayerPawnAt(World, PC, Location, Rotation))
		{
			UE_LOG(LogTemp, Log,
				TEXT("SaveSubsystem: Pawn spawnado pelo Load em (%s) — o GameMode não spawnou ")
				TEXT("(PlayerStart é por tag, e load não vem de portal)."),
				*Location.ToCompactString());
			return;
		}

		UE_LOG(LogTemp, Error,
			TEXT("SaveSubsystem: Sem pawn e sem conseguir spawnar um — o jogador ficou sem corpo. ")
			TEXT("Verifique o DefaultPawnClass do GameMode do mapa '%s'."),
			*World->GetMapName());
		return;
	}

	// O GameMode pode spawnar o pawn alguns ticks depois — tenta de novo
	World->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(this, [this, WeakWorld = TWeakObjectPtr<UWorld>(World), Location, Rotation, RetriesLeft]()
		{
			TryRepositionPlayer(WeakWorld.Get(), Location, Rotation, RetriesLeft - 1);
		}));
}

APawn* USaveSubsystem::SpawnPlayerPawnAt(UWorld* World, APlayerController* PC, FVector Location, FRotator Rotation)
{
	AGameModeBase* GM = World ? World->GetAuthGameMode() : nullptr;
	if (!GM || !PC)
	{
		return nullptr;
	}

	UClass* PawnClass = GM->GetDefaultPawnClassForController(PC);
	if (!PawnClass)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Instigator = PC->GetInstigator();
	Params.ObjectFlags |= RF_Transient;
	// AlwaysSpawn e não Adjust*: a posição vem do save, é onde o jogador estava
	// de pé. Deixar a engine empurrar o spawn para "um lugar livre por perto"
	// só faria o jogador reaparecer fora do lugar em que salvou.
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APawn* Novo = World->SpawnActor<APawn>(PawnClass, Location, Rotation, Params);
	if (!Novo)
	{
		return nullptr;
	}

	// Spawnar -> posicionar -> possuir, nessa ordem. O SpawnActor já recebe a
	// transform, mas o teleporte explícito garante o ponto exato do save
	// (movimento de personagem costuma reagir ao spawn) e acerta a física antes
	// de o controller assumir.
	Novo->SetActorLocationAndRotation(Location, Rotation, /*bSweep=*/false, nullptr,
		ETeleportType::TeleportPhysics);

	PC->Possess(Novo);
	PC->SetControlRotation(Rotation);
	return Novo;
}

// ============================================================
// SLOTS / METADADOS
// ============================================================

bool USaveSubsystem::DoesSlotExist(int32 SlotIndex) const
{
	return IsValidSlotIndex(SlotIndex) && UGameplayStatics::DoesSaveGameExist(SlotName(SlotIndex), 0);
}

bool USaveSubsystem::DeleteSlot(int32 SlotIndex)
{
	if (!DoesSlotExist(SlotIndex))
	{
		return false;
	}
	return UGameplayStatics::DeleteGameInSlot(SlotName(SlotIndex), 0);
}

FSaveSlotMetadata USaveSubsystem::GetSlotMetadata(int32 SlotIndex) const
{
	FSaveSlotMetadata Meta;
	Meta.SlotIndex = SlotIndex;

	if (!DoesSlotExist(SlotIndex))
	{
		return Meta;
	}

	UJRPGSaveGame* Save = Cast<UJRPGSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SlotName(SlotIndex), 0));
	if (!Save)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveSubsystem: Slot %d existe mas não pôde ser lido — tratado como vazio."), SlotIndex);
		return Meta;
	}
	if (Save->SaveVersion > UJRPGSaveGame::CurrentSaveVersion)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveSubsystem: Slot %d tem versão incompatível (%d) — tratado como vazio."),
			SlotIndex, Save->SaveVersion);
		return Meta;
	}

	Meta.bOccupied = true;
	Meta.MapDisplayName = Save->MapDisplayName.IsEmpty() ? Save->MapName : Save->MapDisplayName;
	Meta.PlaytimeFormatted = UCoreSubsystem::FormatPlaytimeSeconds(Save->PlaytimeSeconds);
	Meta.TimestampFormatted = Save->Timestamp.ToString(TEXT("%d-%m-%Y %H:%M"));
	Meta.Gold = Save->Gold;
	Meta.Party = Save->PartyDisplay;
	return Meta;
}

TArray<FSaveSlotMetadata> USaveSubsystem::GetAllSlotsMetadata() const
{
	TArray<FSaveSlotMetadata> AllSlots;
	AllSlots.Reserve(NumSaveSlots);
	for (int32 i = 0; i < NumSaveSlots; ++i)
	{
		AllSlots.Add(GetSlotMetadata(i));
	}
	return AllSlots;
}

// ============================================================
// INTERNOS
// ============================================================

FString USaveSubsystem::SlotName(int32 SlotIndex)
{
	return FString::Printf(TEXT("JRPGSave_%d"), SlotIndex);
}

bool USaveSubsystem::IsValidSlotIndex(int32 SlotIndex)
{
	return SlotIndex >= 0 && SlotIndex < NumSaveSlots;
}

UCoreSubsystem* USaveSubsystem::GetCore() const
{
	UCoreSubsystem* Core = GetGameInstance()->GetSubsystem<UCoreSubsystem>();
	if (!Core)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveSubsystem: UCoreSubsystem indisponível — Save/Load abortado."));
	}
	return Core;
}
