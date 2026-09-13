#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Inventory/ItemData.h"
#include "World/EventFlag.h"
#include "World/RevivalTreeState.h"
#include "Save/SaveTypes.h"
#include "Party/JRPGPartyTypes.h"
#include "JRPGSaveGame.generated.h"

/**
 * UJRPGSaveGame
 * Snapshot completo de uma partida, gravado em disco (um arquivo por slot:
 * "JRPGSave_0".."JRPGSave_14"). O USaveSubsystem coleta tudo automaticamente
 * no SaveGame() e restaura tudo no LoadGame() — nenhum sistema grava aqui direto.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Incrementar sempre que o layout deste save mudar de forma incompatível. */
	// v2: entrou o roster da party (FJRPGPartyMember).
	// v3: FInventorySlot ganhou AcquiredOrder (filtro "último coletado").
	// v4: FJRPGPartyMember ganhou SeruLevel e os 5 slots de Equipment.
	// Save de versão anterior é tratado como vazio.
	static constexpr int32 CurrentSaveVersion = 4;

	UPROPERTY()
	int32 SaveVersion = CurrentSaveVersion;

	/** Momento em que o save foi gravado (FDateTime::Now()). */
	UPROPERTY()
	FDateTime Timestamp;

	// --- Localização ---

	/** Nome curto do level (sem prefixo PIE) — usado no OpenLevel do Load. */
	UPROPERTY()
	FString MapName;

	/** Nome de exibição do mapa para a UI (fallback = MapName). */
	UPROPERTY()
	FString MapDisplayName;

	UPROPERTY()
	FVector PlayerLocation = FVector::ZeroVector;

	UPROPERTY()
	FRotator PlayerRotation = FRotator::ZeroRotator;

	// --- Progressão ---

	UPROPERTY()
	int32 Gold = 0;

	UPROPERTY()
	double PlaytimeSeconds = 0.0;

	// --- Estado dos subsystems ---

	UPROPERTY()
	TArray<FInventorySlot> InventorySlots;

	UPROPERTY()
	TArray<FEventFlag> EventFlags;

	UPROPERTY()
	TArray<FName> OpenedChests;

	UPROPERTY()
	TArray<FRevivalTreeState> RevivalTrees;

	// --- Exibição da party na UI de save ---
	// TODO: preencher quando PartySubsystem/StatusSubsystem tiverem estado real.
	UPROPERTY()
	TArray<FSavePartyMemberDisplay> PartyDisplay;

	/**
	 * O ROSTER INTEIRO — todo mundo já recrutado, esteja na formação ou não.
	 *
	 * É a fonte de verdade da party. O PartyDisplay acima é só a projeção que a
	 * tela de Save/Load desenha sem precisar carregar isto tudo.
	 *
	 * A ordem guarda a formação: os ativos vêm primeiro, na ordem escolhida.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "JRPG|Save")
	TArray<FJRPGPartyMember> Party;
};
