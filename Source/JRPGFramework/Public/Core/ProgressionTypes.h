#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ProgressionTypes.generated.h"

/**
 * Dificuldade da partida. Normal reproduz o jogo original (US) exatamente:
 * todos os multiplicadores valem 1.0.
 *
 * Novas dificuldades SEMPRE no fim — os valores são serializados em Blueprints
 * e nos saves.
 */
UENUM(BlueprintType)
enum class EJRPGDifficulty : uint8
{
	Normal     UMETA(DisplayName = "Normal"),
	Hard       UMETA(DisplayName = "Hard"),
	Juggernaut UMETA(DisplayName = "Juggernaut")
};

/**
 * FDifficultyScaling
 * Multiplicadores de uma dificuldade (linha da DataTable DT_Difficulty).
 * O row name é o nome do valor de EJRPGDifficulty ("Normal", "Hard",
 * "Juggernaut").
 *
 * REGRA DE DESIGN: a dificuldade mexe nos INIMIGOS. Os personagens que existem
 * no original crescem sempre com a curva do original, então level, stats e XP
 * continuam comparáveis entre dificuldades — e o Status menu nunca mostra
 * número que não bate com o jogo de origem.
 *
 * ÚNICA EXCEÇÃO, deliberada: quem NÃO sobe de level no original (a Terra) pode
 * ser liberado para crescer nas dificuldades altas, via o `MinDifficulty` de
 * FStatGrowthRow. Isso nunca altera Vahn, Noa nem Gala.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FDifficultyScaling : public FTableRowBase
{
	GENERATED_BODY()

	/** Nome mostrado na tela de opções. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
	FText DisplayName;

	/** Descrição curta mostrada abaixo da opção. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty")
	FText Description;

	/** Multiplicador do HP máximo dos inimigos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty|Enemy")
	float EnemyHP = 1.0f;

	/** Multiplicador do ataque dos inimigos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty|Enemy")
	float EnemyATK = 1.0f;

	/** Multiplicador da defesa (UDF e LDF) dos inimigos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty|Enemy")
	float EnemyDEF = 1.0f;

	/**
	 * Multiplicador do XP concedido pelos inimigos. Acima de 1.0 compensa a
	 * luta mais longa; em 1.0 a dificuldade não altera o ritmo de progressão.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty|Reward")
	float XPReward = 1.0f;

	/** Multiplicador do gold concedido pelos inimigos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Difficulty|Reward")
	float GoldReward = 1.0f;
};

/**
 * FLevelCurveRow
 * Uma linha da curva de XP (DataTable DT_LevelCurve). O row name é o número do
 * level alcançado.
 *
 * ATENÇÃO: em runtime quem manda é a FÓRMULA em UCoreSubsystem::GetXPForLevel,
 * derivada do original. Esta tabela existe para consulta e tuning no
 * LegaiaStudio — se você mexer nela, me peça para levar os valores para o C++.
 * Ver Docs/Referencia/progressao.md.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FLevelCurveRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Level alcançado ao cruzar XPTotal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Curve")
	int32 Level = 1;

	/** XP acumulado necessário para alcançar este level (a partir do level 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Curve")
	int32 XPTotal = 0;

	/** XP a mais que o level anterior — só para leitura, XPTotal é o que vale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Level Curve")
	int32 XPFromPrevious = 0;
};

/**
 * FStatGrowthRow
 * Parâmetros de crescimento de UM atributo de UM personagem (linha da
 * DataTable DT_StatGrowth). Row name = "Vahn_HP", "Noa_ATK", …
 *
 * Vahn, Noa e Gala vêm do SCUS_942.54 do disco (bloco DAT_80076918), extraídos
 * por Extras/extract_growth_from_disc.py.
 *
 * A TERRA É EXCEÇÃO: o bloco do disco tem só 3 registros, então os tetos dela
 * foram DESENHADOS por nós (ela entra forte e estaciona). Os valores moram no
 * dicionário DESIGNED do mesmo script, para que rodar o extrator de novo
 * reconstrua a tabela inteira em vez de apagá-la.
 *
 * Ver Docs/Referencia/progressao.md.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FStatGrowthRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Vahn / Noa / Gala / Terra. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Growth")
	FName Character;

	/** HP, MP, AGL, ATK, UDF, LDF, SPD ou INT. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Growth")
	FName Stat;

	/**
	 * Âncora da fórmula de crescimento. PODE DIFERIR do atributo inicial em
	 * DT_Characters: o jogo retoca o template de entrada de Vahn e Noa quando
	 * eles se juntam ao grupo. Gala bate nos 8.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Growth")
	int32 GrowthStart = 0;

	/** Teto do atributo no level 99. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Growth")
	int32 MaxValue = 0;

	/** Variação aleatória do ganho: o sorteio é [-Jitter, +Jitter]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Growth")
	int32 Jitter = 0;

	/** Qual das curvas de DT_GrowthCurve este atributo usa (0, 1 ou 2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Growth")
	int32 CurveRow = 0;

	/**
	 * Dificuldade MÍNIMA para este atributo crescer. Abaixo dela o ganho é 0 e
	 * o atributo fica parado no GrowthStart a partida inteira.
	 *
	 * Normal (o default) = cresce sempre, que é o caso dos três do disco.
	 *
	 * Existe por causa da Terra: no jogo original ela NÃO sobe de level — é
	 * por isso que não há parâmetros de crescimento pra ela no `SCUS_942.54`.
	 * No Normal a gente reproduz isso; no Hard e no Juggernaut ela cresce,
	 * para ter como ajudar de verdade num jogo mais pesado.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stat Growth")
	EJRPGDifficulty MinDifficulty = EJRPGDifficulty::Normal;
};

/**
 * FGrowthCurveRow
 * Um nível das três curvas de crescimento (DataTable DT_GrowthCurve).
 * Row name = o número do level de origem.
 *
 * Cada curva soma exatamente 9408 ao longo dos 98 níveis, então em aritmética
 * exata os ganhos acumulariam para (MaxValue - GrowthStart) certinho. O
 * original divide com inteiro (a magic multiply 0x6F74AE27 >> 44), então cada
 * level perde a fração: no L99 o atributo fica um pouco ABAIXO do MaxValue.
 * Isso é fiel ao jogo — não "conserte" acumulando a curva antes de dividir.
 * Ver Docs/Referencia/progressao.md.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FGrowthCurveRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Level de ORIGEM (o ganho de L->L+1 usa a linha L). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth Curve")
	int32 Level = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth Curve")
	int32 Row0 = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth Curve")
	int32 Row1 = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Growth Curve")
	int32 Row2 = 0;
};
