#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CharacterData.generated.h"

/**
 * Os 8 elementos do jogo. Usados pela afinidade dos personagens (forte/fraco)
 * e pelo elemento do Ra-Seru.
 *
 * NOTA: FItemData::ElementType ainda é FString livre. Quando o sistema de
 * elementos entrar de verdade, migrar aquele campo para cá.
 */
UENUM(BlueprintType)
enum class EJRPGElement : uint8
{
	None    UMETA(DisplayName = "None"),
	Fire    UMETA(DisplayName = "Fire"),
	Wind    UMETA(DisplayName = "Wind"),
	Thunder UMETA(DisplayName = "Thunder"),
	Water   UMETA(DisplayName = "Water"),
	Earth   UMETA(DisplayName = "Earth"),
	Light   UMETA(DisplayName = "Light"),
	Dark    UMETA(DisplayName = "Dark"),
	Evil    UMETA(DisplayName = "Evil")
};

/**
 * FCharacterData
 * Um personagem jogável (linha da DataTable DT_Characters, gerada de
 * Docs/Data/gamedata/characters.toml via Extras/generate_characters_csv.py).
 * O row name é o CharacterID usado pelo PartySubsystem e pela WebUI.
 *
 * Os stats iniciais são os do jogo original (US), decodificados da tabela de
 * new game em SCUS_942.54 (base 0x80078C4C, stride 26) — ver
 * Docs/Referencia/progressao.md.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FCharacterData : public FTableRowBase
{
	GENERATED_BODY()

	/** ID do personagem (ex: "Vahn"). Espelha o row name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	FName Key;

	/**
	 * Slot da curva de XP do original (Vahn 0, Noa 1, Gala 2, Terra 3).
	 *
	 * Nao e a posicao na party — e qual correcao de limiar o personagem usa em
	 * GetXPForLevelForSlot: o slot 1 sobe mais cedo, o 2 mais tarde, os demais
	 * usam a curva base. Ver Docs/Referencia/progressao.md.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	int32 XPCurveSlot = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	FText DisplayName;

	/**
	 * Nome do retrato na WebUI: images/<PortraitId>.png (o shell resolve o
	 * caminho). Hoje: vahn / noa / gala.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	FName PortraitId;

	// --- Ra-Seru ---

	/** Nome do Ra-Seru fundido ao personagem (Meta, Terra, Ozma). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Ra-Seru")
	FName RaSeru;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Ra-Seru")
	EJRPGElement RaSeruElement = EJRPGElement::None;

	// --- Afinidade elemental ---

	/** Elementos em que o personagem é forte. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Affinity")
	TArray<EJRPGElement> AffinityStrong;

	/** Elementos em que o personagem é fraco. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Affinity")
	TArray<EJRPGElement> AffinityWeak;

	// --- Combate ---

	/**
	 * Classes de arma favoritas (Knives, Swords, Claw, Club…). Cruzam com
	 * FItemData::WeaponType: arma da classe favorita encurta o botão de Arms
	 * na fila de ação.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Combat")
	TArray<FString> WeaponClasses;

	/**
	 * Canhoto: a Noa é a única do grupo, e por isso o botão de Arms dela usa a
	 * direção oposta na fila de ação.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Combat")
	bool bLeftHanded = false;

	// --- Atributos iniciais (level 1) ---
	// Valores do jogo original US. HP e MP são também o máximo inicial.
	// A curva de crescimento por level está em Docs/Referencia/progressao.md.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Base Stats")
	int32 BaseHP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Base Stats")
	int32 BaseMP = 0;

	/** Agilidade. Não confundir com o teto de 100 que o jogo grava à parte. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Base Stats")
	int32 BaseAGL = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Base Stats")
	int32 BaseATK = 0;

	/** Upper Defense Factor (defesa física). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Base Stats")
	int32 BaseUDF = 0;

	/** Lower Defense Factor (defesa mágica). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Base Stats")
	int32 BaseLDF = 0;

	/** Semente da ordem de turno. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Base Stats")
	int32 BaseSPD = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character|Base Stats")
	int32 BaseINT = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	FText Notes;
};
