#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Arts/ArtsData.h"
#include "HeroBattleData.generated.h"

/**
 * FJRPGRaSeruTierData
 * O ATK do Ra-Seru de um herói, por tier (linha da DataTable DT_RaSeruTiers, gerada por
 * Extras/generate_hero_battle_csv.py). O row name é o herói ("Vahn"); o tier é o SeruLevel dele.
 *
 * No original o Ra-Seru é o item do slot 3 ("Ra-Seru Meta $1..$9" na tabela de equipamentos do SCUS), com
 * ATK próprio; aqui o SeruLevel é um número no registro, e este ATK entra na luta como uma das cinco peças do
 * herói (FJRPGCombatant::PieceATK): ½ dele no golpe do botão Ra-Seru e em toda Art. Meta é do Vahn, Terra
 * da Noa, Ozma do Gala. Os tiers não trazem UDF, LDF nem SPD.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGRaSeruTierData : public FTableRowBase
{
	GENERATED_BODY()

	/** Igual ao row name (a key na DT_Characters). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ra-Seru")
	FName Character;

	/** O ATK de cada tier, do 1 ao último (Meta 9, Terra 8, Ozma 7). Passando do último, vale o último. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ra-Seru")
	TArray<int32> TierATK;
};

/**
 * FJRPGWeaponCostData
 * O custo do botão de Arms de uma arma para um herói no gauge de comandos (linha da DataTable
 * DT_WeaponCosts, gerada por Extras/generate_hero_battle_csv.py do arquivo de batalha de cada herói). O row
 * name é <Herói>_<Arma>. 30 é o custo da classe favorita, 42 uma classe ao lado, 54 bem fora; uma arma
 * sem linha custa 30 (UJRPGArtsRules::DefaultButtonCost). Os outros três botões custam sempre 30.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGWeaponCostData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Cost")
	FName Character;

	/** A key da arma na DT_Items. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Cost")
	FName Item;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon Cost")
	int32 ArmsCost = 30;
};

/**
 * FJRPGHeroSwingData
 * O golpe simples de um botão, por herói e equipamento (linha da DataTable DT_HeroSwings, gerada por
 * Extras/generate_hero_battle_csv.py do arquivo de batalha de cada herói). No original o golpe de direção é o registro de
 * golpe que o item equipado traz para o arquivo de batalha: a arma dirige o botão Arms, o tier do Ra-Seru o Ra-Seru, o calçado
 * os dois chutes (Low e High). O row name é <Herói>_<Item>_<Botão> (Gala_war_god_boots_Low).
 *
 * Item é a key do DT_Items; "default" é a seção padrão (nada equipado, ou o item que a tabela não traz); "seru_N" é o tier
 * N do Ra-Seru (o Ra-Seru aqui é o SeruLevel do herói, não um item). A luta usa a linha do item equipado e, sem ela, a
 * "default" do mesmo botão; sem a tabela, o byte padrão (UBattleFormulas::DefaultSwingPowerByte) e o quadro padrão
 * (FJRPGTimelineDefaults::SwingHitFrame).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGHeroSwingData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hero Swing")
	FName Character;

	/** A key do item no DT_Items, "default" ou "seru_N". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hero Swing")
	FName Item;

	/** O botão que o registro dirige. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hero Swing")
	EJRPGArtButton Button = EJRPGArtButton::Arms;

	/** Um byte de poder por golpe (1 ou 2 no disco): dá o multiplicador, a defesa (UDF / LDF) e a altura que alcança. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hero Swing")
	TArray<int32> PowerBytes;

	/** O quadro do clip em que cada golpe cai (o "beat" do disco), a mesma quantidade dos bytes de poder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hero Swing")
	TArray<int32> HitBeats;

	/** O rate do clip (1 ou 2): o beat B cai no quadro lógico ceil(32 * (B - 1) / (8 * Rate)). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hero Swing", meta = (ClampMin = "1"))
	int32 Rate = 2;

	/** O custo do botão no gauge de comandos (30 / 42 / 54). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hero Swing")
	int32 Cost = 30;
};
