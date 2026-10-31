#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BossFightData.generated.h"

/**
 * FJRPGBossData
 * Uma luta de chefe (linha da DataTable DT_Bosses, gerada de Docs/Data/gamedata/bosses.toml via
 * Extras/generate_bosses_csv.py). É a camada da LUTA, não do combatente: os stats de cada chefe
 * estão no DT_Enemies (bIsBoss), e esta linha diz quem entra, os golpes especiais com nome, a
 * recompensa da luta e o nível recomendado. As lutas do torneio da Muscle Dome levam Tournament.
 *
 * Linha nova (feita à mão ou no LegaiaStudio) é uma luta nova: o gerador nunca a apaga.
 * Guia: Docs/Guides/ENEMIES.md, "Boss fights".
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBossData : public FTableRowBase
{
	GENERATED_BODY()

	/** Igual ao row name ("caruban", "songi_1st", "caruban_expert"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FName Key;

	/** Nome mostrado na luta. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FText DisplayName;

	/** Onde a luta acontece no original. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FString Location;

	/** O código da luta no guia de referência (B0001..B0014, s0005); vazio nas da Muscle Dome. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FString FightCode;

	/** Quem entra na luta: rows do DT_Enemies, um por lutador (Vigurox2 = viguro, viguro). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	TArray<FName> Enemies;

	/** HP do chefe pelo guia, para conferência (o da luta é o do DT_Enemies). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Reference")
	int32 HP = 0;

	/** MP do chefe pelo guia, para conferência. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Reference")
	int32 MP = 0;

	/** Os golpes especiais com nome ("Fire Breath"); o ataque comum não entra. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Attacks")
	TArray<FString> SpecialAttacks;

	/** O custo de MP de cada golpe especial, na mesma ordem. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Attacks")
	TArray<int32> SpecialAttackMP;

	/** Recompensa da luta (vitória). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Rewards")
	int32 EXP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Rewards")
	int32 Gold = 0;

	/** Key na DT_Items do item ganho na vitória. None = nenhum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Rewards")
	FName RewardItem;

	/** Nível recomendado da party ("6-7", "27"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FString RecommendedLevel;

	/** Torneio da Muscle Dome (Expert, Master); None nas lutas da história. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FName Tournament;

	/** Como a luta funciona (regras especiais, fraquezas). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FString Notes;
};
