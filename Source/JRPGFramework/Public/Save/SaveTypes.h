#pragma once

#include "CoreMinimal.h"
#include "SaveTypes.generated.h"

/**
 * FSavePartyMemberDisplay
 * Snapshot de exibição de um membro da party dentro de um save (ícone/level/
 * HP/MP/AP mostrados no card de detalhes da tela de Save/Load).
 *
 * NOTA: o PartySubsystem/StatusSubsystem ainda não têm estado real — hoje este
 * array vai vazio no save e a UI mostra placeholders ("--"). O schema já está
 * pronto para quando a party existir de verdade.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FSavePartyMemberDisplay
{
	GENERATED_BODY()

	/** ID do personagem (ex: "vahn") — usado também para resolver o retrato na UI (images/<id>.png). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	FName CharacterID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	int32 HP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	int32 MaxHP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	int32 MP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	int32 MaxMP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	int32 AP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	int32 MaxAP = 0;
};

/**
 * FSaveSlotMetadata
 * Resumo de um slot de save para exibição na UI (grid 5x3 + card de detalhes).
 * Slots vazios têm bOccupied = false e os demais campos em default.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FSaveSlotMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	int32 SlotIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	bool bOccupied = false;

	/** Nome de exibição do mapa (SetCurrentMapDisplayName) ou o nome técnico do level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	FString MapDisplayName;

	/** Tempo de jogo formatado "HH:MM:SS". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	FString PlaytimeFormatted;

	/** Data/hora em que o save foi gravado, formatado "DD-MM-YYYY HH:MM". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	FString TimestampFormatted;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	int32 Gold = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Save")
	TArray<FSavePartyMemberDisplay> Party;
};
