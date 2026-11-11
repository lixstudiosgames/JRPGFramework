#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Battle/EnemyData.h"
#include "Battle/BossFightData.h"
#include "JRPGEncounterTypes.generated.h"

/**
 * Uma linha de quem pode entrar numa luta: um inimigo do DT_Enemies, a faixa de level, o peso
 * no sorteio e quantos dele entram.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGEncounterEnemy
{
	GENERATED_BODY()

	/** Row do DT_Enemies ("gimard", "gimard_lv2"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter", meta = (GetOptions = "JRPGFramework.JRPGEncounterLibrary.GetEnemyKeyOptions"))
	FName EnemyKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter", meta = (ClampMin = "1"))
	int32 LevelMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter", meta = (ClampMin = "1"))
	int32 LevelMax = 1;

	/** Peso no sorteio dos que não são principais (0 = nunca sorteado). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter", meta = (ClampMin = "0"))
	int32 Weight = 1;

	/** Quantos deste inimigo, no mínimo e no máximo, entram na mesma luta. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter", meta = (ClampMin = "0"))
	int32 CountMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter", meta = (ClampMin = "0"))
	int32 CountMax = 1;

	/**
	 * O "inimigo principal": entra em toda luta deste encontro (CountMin..CountMax dele). É
	 * também o modelo que o inimigo do Field mostra, quando ele não tem visual próprio.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter")
	bool bAlwaysJoins = false;
};

/** Quem pode aparecer numa luta e quantos inimigos ela tem. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGEncounterSetup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter")
	TArray<FJRPGEncounterEnemy> Enemies;

	/** Total de inimigos na luta. Os principais entram primeiro; o resto completa por sorteio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter", meta = (ClampMin = "1"))
	int32 EnemiesPerBattleMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter", meta = (ClampMin = "1"))
	int32 EnemiesPerBattleMax = 3;
};

/** Um inimigo sorteado para a luta. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGEncounterCombatant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter")
	FName EnemyKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Encounter")
	int32 Level = 1;
};

/**
 * UJRPGEncounterLibrary
 * O sorteio de uma luta e o acesso ao DT_Enemies, para quem monta encontros (o inimigo do
 * Field hoje, a batalha depois).
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGEncounterLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Sorteia quem entra na luta: primeiro todos os bAlwaysJoins (cada um com CountMin..Max),
	 * depois, por Weight, os outros até o total de EnemiesPerBattleMin..Max — sem passar do
	 * CountMax de cada linha. O level de cada um sai da faixa da sua linha.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Encounter")
	static TArray<FJRPGEncounterCombatant> RollEncounter(const FJRPGEncounterSetup& Setup);

	/**
	 * Um encontro de um inimigo só: ele, sempre, um por luta, com o level sorteado na faixa. É o
	 * que o "Enemy" do inimigo do Field e da zona vira quando não há Custom Encounter.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Encounter")
	static FJRPGEncounterSetup MakeSingleEnemyEncounter(FName EnemyKey, int32 LevelMin = 1, int32 LevelMax = 1);

	/**
	 * A flag de luta roteirizada (FJRPGBattleSetup::bScriptedFight) de uma luta pelos inimigos que entram nela. Quem
	 * monta a luta (o AJRPGFieldEnemy, o AJRPGBattleWaves) copia isto para o Setup antes do BeginBattle:
	 *  - nenhum inimigo é bIsBoss (DT_Enemies): encontro aleatório, falso;
	 *  - senão, a linha do DT_Bosses cujos lutadores são exatamente estes (qualquer ordem): o bScriptedFight dela
	 *    (a Lapis e o treino do Tetsu são lutas da história com a flag desligada);
	 *  - chefe sem linha no DT_Bosses (ou sem a tabela): verdadeiro, o normal de uma luta de chefe (e um aviso no log).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Encounter")
	static bool ResolveScriptedFight(const TArray<FJRPGEncounterCombatant>& Enemies);

	/**
	 * A linha do DT_Bosses desta luta: a que tem exatamente estes inimigos (em qualquer ordem). Falso se nenhum é chefe
	 * (bIsBoss), ou se nenhuma linha casa.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Encounter")
	static bool FindBossFight(const TArray<FJRPGEncounterCombatant>& Enemies, FJRPGBossData& OutBoss);

	/** A linha do DT_Enemies. False se a tabela ou a key não existem. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Encounter")
	static bool GetEnemyData(FName EnemyKey, FJRPGEnemyData& OutData);

	/** O DT_Enemies (/Game/Data, senão /JRPGFramework/Data). Nulo se não foi importado. */
	static const UDataTable* GetEnemyTable();

	/** As keys do DT_Enemies, para o dropdown do EnemyKey no editor. */
	UFUNCTION()
	static TArray<FString> GetEnemyKeyOptions();

	// --- Encontros aleatórios: o contador de passos do original (FUN_801D9E1C, RE docs/formats/encounter.md) ---------------

	/** O centro do contador (0x3ce = 974) e o módulo de cada sorteio (0x1e7 = 487). */
	static constexpr int32 EncounterCounterBase = 0x3CE;
	static constexpr int32 EncounterCounterMod = 0x1E7;

	/**
	 * O contador depois de uma luta (FUN_801DDF48): 0x3ce + (A % 0x1e7) - (B % 0x1e7), de 488 a 1460 (média 974). RollA /
	 * RollB: dois sorteios quaisquer (o módulo é sem sinal, como no original).
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Encounter")
	static int32 RerollEncounterCounter(int32 RollA, int32 RollB);

	/**
	 * A entrada num mapa (FUN_8003AB2C, 0x8003AC78..0x8003ACAC): com o contador abaixo de 487 soma metade de um sorteio novo
	 * (arredondando para zero); de 487 para cima ele passa pela porta como está.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Encounter")
	static int32 TopUpEncounterCounter(int32 Counter, int32 RollA, int32 RollB);

	/**
	 * Um passo: desconta Rate do contador. True = a luta sai (o contador ia a <= 0; quem chama sorteia o contador de novo com
	 * RerollEncounterCounter). Enquanto fica positivo, só desconta. Um contador já em 0 dispara até com taxa 0 (o bgtz do
	 * original em 0x801DA218 cai direto).
	 */
	static bool StepEncounterCounter(int32& Counter, int32 Rate);

	/** A taxa com a flag de taxa baixa da zona: Rate * LowRatePercent / 100 (50 = o >> 1 da flag 0x1E do original). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Encounter")
	static int32 ScaleEncounterRate(int32 Rate, bool bLowRate, int32 LowRatePercent);

	/**
	 * Mudar de tile é um passo só se andou no máximo um tile em cada eixo (o teste slti 0x2 em 0x801D9EF0 / 0x801D9F08): um
	 * teletransporte ou uma porta não conta, nem desconta, nem sorteia.
	 */
	UFUNCTION(BlueprintPure, Category = "JRPG|Encounter")
	static bool IsEncounterStep(FIntPoint PrevTile, FIntPoint NowTile);
};
