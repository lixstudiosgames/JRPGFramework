#pragma once

// NOME COM PREFIXO DE PROPOSITO: o UHT recusa dois headers com o mesmo nome de
// arquivo no build inteiro, e a engine tem
// Plugins/Online/OnlineFramework/Source/Party/Public/Party/PartyTypes.h.
// Nao renomeie de volta para PartyTypes.h.

#include "CoreMinimal.h"
#include "JRPGPartyTypes.generated.h"

/**
 * Os cinco slots de equipamento, derivados do que a DT_Items descreve:
 * arma tem WeaponType/AttackBonus, armadura tem ArmorSlot (Armor/Helmet/Shoes)
 * com UDF/LDF, e acessorio age por EffectClass.
 *
 * Novos valores SEMPRE no fim — o indice e serializado no save.
 */
UENUM(BlueprintType)
enum class EJRPGEquipSlot : uint8
{
	Weapon    UMETA(DisplayName = "Weapon"),
	Armor     UMETA(DisplayName = "Armor (Body)"),
	Helmet    UMETA(DisplayName = "Helmet (Head)"),
	Shoes     UMETA(DisplayName = "Shoes (Feet)"),
	Accessory UMETA(DisplayName = "Accessory"),

	MAX       UMETA(Hidden)
};

/**
 * FJRPGPartyMember
 * O registro de UM personagem do jogador. É o que persiste no save.
 *
 * O prefixo JRPG no nome do TIPO é obrigatório, não estilo: o UHT recusa dois
 * tipos refletidos que compartilhem o nome sem o prefixo de classe, e a engine
 * tem UPartyMember em Plugins/Online/OnlineFramework. "FPartyMember" e
 * "UPartyMember" seriam ambos "PartyMember" para o UHT.
 *
 * TRÊS ESTADOS INDEPENDENTES — a distinção é o coração do sistema:
 *
 *   bRecruited  entrou no grupo alguma vez. IRREVERSÍVEL: a partir daí os
 *               dados são do jogador para sempre, mesmo que o personagem passe
 *               o jogo inteiro fora da party.
 *   bAvailable  está na história agora. A Terra sai depois do Mount Rikuroa —
 *               deixa de estar disponível, mas o registro dela fica intacto.
 *   bActive     está na formação que luta. É o que o jogador liga e desliga
 *               para jogar só com um personagem.
 *
 * Um personagem inativo NÃO é tocado por nada: level, XP, atributos e
 * equipamento ficam congelados até ele voltar.
 *
 * POR QUE OS ATRIBUTOS SÃO GUARDADOS, e não recalculados do level: o ganho por
 * level tem jitter (RollStatGain sorteia), então dois personagens no mesmo
 * level têm números diferentes. O original guarda no registro e nós também.
 * UCoreSubsystem::GetStatAtLevel continua servindo para PREVISÃO, não como
 * fonte de verdade.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGPartyMember
{
	GENERATED_BODY()

	/** Row name em DT_Characters (ex: "Vahn"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party")
	FName Key;

	// ------------------------------------------------------------ estados

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Estado")
	bool bRecruited = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Estado")
	bool bAvailable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Estado")
	bool bActive = false;

	// ---------------------------------------------------------- progressão

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Progressão")
	int32 Level = 1;

	/** XP acumulado desde o level 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Progressão")
	int32 ExperienceTotal = 0;

	/**
	 * Slot da curva de XP, copiado de FCharacterData no recrutamento.
	 * Só os slots 1 e 2 levam correção de limiar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Progressão")
	int32 XPCurveSlot = 0;

	// ---------------------------------------------------------- atributos
	//
	// Em Legaia o atributo HP É o máximo de HP — o valor "atual" é separado.
	// Mesma coisa para MP. Os outros seis não têm par atual/máximo.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 HP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 MaxHP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 MP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 MaxMP = 0;

	/** Barra de comando das Arts, 0..100. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 AP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 AGL = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 ATK = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 UDF = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 LDF = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 SPD = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Atributos")
	int32 INT = 0;

	// ------------------------------------------------- reservado (ver nota)
	//
	// Estes dois entram VAZIOS agora, de propósito. Campo novo no save exige
	// subir o SaveVersion e invalidar os saves existentes — melhor pagar uma
	// vez só, agora que ainda não há save de verdade para perder.

	/**
	 * Itens equipados, um por EJRPGEquipSlot (NAME_None = slot vazio).
	 *
	 * O CAMPO mora aqui porque o registro do personagem é um só e o save é um
	 * array só — mas quem ESCREVE nele é o UStatusSubsystem. O Party é dono do
	 * dado; o Status é dono da regra.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Equipamento")
	TArray<FName> Equipment;

	/** Veneno, paralisia, petrificado… Também escritas pelo UStatusSubsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Status")
	TArray<FName> Conditions;

	/**
	 * Level do Ra-Seru que o personagem carrega (1..9 no original).
	 *
	 * Fica no registro porque é progressão do personagem, como o level dele.
	 * O que esse level CONCEDE é o Status que calcula.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Party|Progressão")
	int32 SeruLevel = 1;

	/** Morto = sem HP. Não recebe XP nem entra no divisor da divisão. */
	bool IsDead() const { return HP <= 0; }

	/** Conta para a divisão de XP: ativo, disponível e vivo. */
	bool IsFighting() const { return bRecruited && bAvailable && bActive && !IsDead(); }
};
