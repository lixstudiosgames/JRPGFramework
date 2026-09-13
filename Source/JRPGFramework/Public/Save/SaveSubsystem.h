#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Save/SaveTypes.h"
#include "SaveSubsystem.generated.h"

class APawn;
class APlayerController;

class UJRPGSaveGame;
class UCoreSubsystem;

/**
 * USaveSubsystem
 * Orquestrador de Save/Load em 15 slots ("JRPGSave_0".."JRPGSave_14").
 *
 * SaveGame(Slot) coleta TUDO automaticamente: inventário, flags de evento,
 * baús abertos, Revival Trees, Gold, playtime, mapa atual e posição do player
 * (os quatro últimos vivem no UCoreSubsystem).
 *
 * LoadGame(Slot) abre o mapa salvo (OpenLevel) e, quando o novo level termina
 * de carregar (PostLoadMapWithWorld), restaura todos os dados e reposiciona o
 * player — nada é aplicado antes do mapa novo existir.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API USaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 NumSaveSlots = 15;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ============================================================
	// SAVE / LOAD
	// ============================================================

	/** Grava o estado completo da partida no slot (0..14). Sobrescreve sem perguntar. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Save")
	bool SaveGame(int32 SlotIndex);

	/**
	 * Carrega o slot: abre o mapa salvo e restaura tudo (dados + posição do
	 * player) quando o level terminar de carregar. Retorna false se o slot
	 * estiver vazio, corrompido ou de versão incompatível.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Save")
	bool LoadGame(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Save")
	bool DoesSlotExist(int32 SlotIndex) const;

	/** Apaga o arquivo de save do slot. Retorna false se o slot já estava vazio. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Save")
	bool DeleteSlot(int32 SlotIndex);

	/** Resumo de um slot para a UI (bOccupied=false se vazio/corrompido). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Save")
	FSaveSlotMetadata GetSlotMetadata(int32 SlotIndex) const;

	/** Resumo dos 15 slots, na ordem 0..14 — usado pela tela de Save/Load. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Save")
	TArray<FSaveSlotMetadata> GetAllSlotsMetadata() const;

private:
	static FString SlotName(int32 SlotIndex);
	static bool IsValidSlotIndex(int32 SlotIndex);

	/** Subsystem central (Gold/playtime/mapa/posição) — nunca nulo em runtime normal. */
	UCoreSubsystem* GetCore() const;

	/** Consome PendingLoadSave quando o mapa do Load termina de carregar. */
	void HandlePostLoadMap(UWorld* NewWorld);

	/** Restaura subsystems + GameInstance e agenda o reposicionamento do player. */
	void ApplyLoadedState(UJRPGSaveGame* Save, UWorld* World);

	/** Teleporta o pawn; se ainda não spawnou, tenta de novo no próximo tick. */
	void TryRepositionPlayer(UWorld* World, FVector Location, FRotator Rotation, int32 RetriesLeft);

	/**
	 * Spawna o pawn do jogador na posição do save e possui.
	 *
	 * É o caminho NORMAL de um load neste projeto, não um remendo. O GameMode
	 * escolhe o PlayerStart por TAG — é assim que cada portal decide onde o
	 * jogador aparece no mapa de destino — e um load não vem de portal nenhum,
	 * então o GameMode não acha PlayerStart e não spawna ninguém.
	 *
	 * A alternativa seria um PlayerStart sem tag em cada mapa, mas ele viraria
	 * o destino silencioso de qualquer portal com a tag errada, escondendo o
	 * defeito. O Load sabe exatamente onde o jogador estava; é ele quem spawna.
	 */
	APawn* SpawnPlayerPawnAt(UWorld* World, APlayerController* PC, FVector Location, FRotator Rotation);

	/**
	 * Save aguardando o novo mapa carregar. UPROPERTY é OBRIGATÓRIO: o GC roda
	 * durante o LoadMap e coletaria o objeto sem esta referência.
	 */
	UPROPERTY()
	TObjectPtr<UJRPGSaveGame> PendingLoadSave;

	FDelegateHandle PostLoadMapHandle;
};
