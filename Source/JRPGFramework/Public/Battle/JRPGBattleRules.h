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

	/**
	 * O tier da pose de vitória de quem posa (FUN_8004E568, 0x8004E78C..0x8004E83C): 0 saudável (HP >= 3/4 do máximo), 1 (>= 1/2),
	 * 2 (>= 1/4), 3 abaixo disso. Duas passadas envelhecem o tier em 1 enquanto Round >= tier * 2 + 2 e tier < 3 (uma luta longa
	 * cansa a pose), e um status fraco (a máscara 0x107B do original) força o 4. Round = as rodadas já fechadas (0 na primeira).
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Rules")
	static int32 VictoryPoseTier(int32 HP, int32 MaxHP, int32 Round, bool bWeakStatus);

	/**
	 * A coluna da pose na tabela do herói (0x8004E870..0x8004EAEC): 0-1 o par saudável (Victory1 / 2), 2-3 o alternativo
	 * (VictoryAlt1 / 2), 4-5 o fraco (VictoryWeak1 / 2). PairRoll escolhe o par (tier 0: saudável 3 em 4; 1: meio a meio com o
	 * alternativo; 2: alternativo ou fraco; 3: fraco 3 em 4; 4: sempre fraco, sem gastar o sorteio) e MemberRoll o membro do par.
	 * Os sorteios são cosméticos: quem chama usa um stream que não é o das regras.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Battle|Rules")
	static int32 VictoryPoseColumn(int32 Tier, int32 PairRoll, int32 MemberRoll);

	/** O nome do clip da coluna (o papel que o Extras/ps1_party_sounds.py deu: AS_<Herói>_Battle_<nome>); NAME_None fora de 0..5. */
	static FName VictoryPoseClipName(int32 Column);
};
