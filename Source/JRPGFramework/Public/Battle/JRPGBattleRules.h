#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Battle/EnemyData.h"
#include "Core/ProgressionTypes.h"
#include "JRPGBattleRules.generated.h"

/** Os stats que um inimigo leva para a luta, depois das regras da versão. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGEnemyBattleStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 HP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 MP = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 AGL = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 ATK = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 UDF = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 LDF = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 SPD = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	int32 INT = 0;
};

/**
 * UJRPGBattleRules
 * O que cada versão do original faz com o registro cru do monstro (a DT_Enemies): o boost dos
 * stats ao carregar a luta e o EXP/Gold do fim. Funções puras, inteiras e truncadas como no
 * PS1; a dificuldade (UCoreSubsystem::Scale*) entra DEPOIS, por quem chama.
 *
 * Fontes (legend-of-legaia-re): docs/subsystems/battle.md › "Battle-load stat boost" e
 * "No boost on the PAL executables"; docs/subsystems/battle-formulas.md › "Victory spoils" e
 * "Regional difference". Conferido contra as capturas de lá: Gaza (Sim-Seru) cru
 * 288/222/200/220 luta com 360/444/400/247 na USA; Gobu Gobu aleatório 17/15/14/10 luta com
 * 17/25/24/12; um Gimard (60 de gold) paga 15 na USA; Zeto (8000 G / 9000 EXP), party de três,
 * paga 2000 G + 2250 EXP cada na USA e 4000 G + 3000 EXP cada na versão japonesa.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGBattleRules : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Os stats com que o inimigo entra na luta (FUN_80054CB0).
	 *
	 * USA, luta roteirizada (bScriptedFight): ATK x5/4, UDF x2, LDF x2, INT x9/8.
	 * USA, encontro aleatório: ATK igual, UDF/LDF x7/4 (dois shifts truncando), INT x5/4.
	 * Japan: o registro como está.
	 * HP, MP, AGL e SPD nunca mudam.
	 *
	 * bScriptedFight é a flag da formação no original (o byte de cabeçalho da linha do
	 * encontro), NÃO "é chefe": a luta de treino do Tetsu e a do Lapis são roteirizadas e usam
	 * o perfil do encontro aleatório. Quem monta a luta decide (Fase 5).
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Rules")
	static FJRPGEnemyBattleStats InstallEnemyStats(const FJRPGEnemyData& Enemy, bool bScriptedFight, EJRPGRuleset Ruleset);

	/**
	 * O EXP que a luta paga, antes de dividir: a soma do EXP cru dos inimigos derrotados, x3/4
	 * na USA (soma - soma>>2), inteiro na versão japonesa.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Rules")
	static int32 VictoryEXPPool(int32 RawEXPSum, EJRPGRuleset Ruleset);

	/** A parte de cada vivo: divisão com teto (divu do original), 0 sem ninguém vivo. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Rules")
	static int32 VictoryEXPShare(int32 EXPPool, int32 AliveCount);

	/**
	 * O Gold que a luta paga. Cada inimigo derrotado soma gold>>1; com o Golden Book (alguém
	 * vivo com a habilidade) o acumulado ganha +acc>>2; na USA ainda corta pela metade
	 * (acc - acc>>1). Por inimigo, porque o shift trunca um de cada vez.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Rules")
	static int32 VictoryGold(const TArray<int32>& DefeatedEnemyGold, bool bGoldenBook, EJRPGRuleset Ruleset);
};
