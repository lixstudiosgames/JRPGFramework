#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Battle/EnemyData.h"
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

	/** A linha do DT_Enemies. False se a tabela ou a key não existem. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Encounter")
	static bool GetEnemyData(FName EnemyKey, FJRPGEnemyData& OutData);

	/** O DT_Enemies (/Game/Data, senão /JRPGFramework/Data). Nulo se não foi importado. */
	static const UDataTable* GetEnemyTable();

	/** As keys do DT_Enemies, para o dropdown do EnemyKey no editor. */
	UFUNCTION()
	static TArray<FString> GetEnemyKeyOptions();
};
