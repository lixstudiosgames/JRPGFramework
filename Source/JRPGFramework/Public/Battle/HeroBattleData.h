#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
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
