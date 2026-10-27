#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Battle/BattleTypes.h"
#include "Battle/JRPGEncounterTypes.h"
#include "JRPGBattleWaves.generated.h"

class AJRPGBattleArena;
class UBillboardComponent;

/** Uma onda: um nome para a tela e quem pode entrar nela. */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleWave
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	FString Name;

	/** Sorteado como os do Field. Onda fixa: uma linha com bAlwaysJoins e CountMin = CountMax. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	FJRPGEncounterSetup Encounter;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGWaveStarted, int32, WaveIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJRPGWavesCleared);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGWavesFailed, EJRPGBattleResult, Result);

/**
 * AJRPGBattleWaves
 * Lutas em sequência, uma onda depois da outra, no estilo do Muscle Dome: arraste para o mapa,
 * escolha a party e monte as ondas — aqui no Details ou num arquivo JSON (WavesFile), que o
 * LegaiaStudio edita (aba "Ondas").
 *
 * Com WavesFile, o arquivo é lido a cada play: edite no Studio, salve, dê play. "Import Waves
 * File" copia o arquivo para as propriedades (o que fica salvo no mapa, para o build que não
 * leva o JSON). Formato: Docs/Guides/BATTLE_STAGE.md.
 *
 * Vencer uma onda chama a próxima depois de DelayBetweenWaves; perder, fugir ou abortar para
 * tudo. Funciona com o GameMode do Field: a party fica no ringue entre as ondas.
 *
 * Fase 4 ainda não tem combate: a onda acaba pelo console (JRPG.Battle.End victory).
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGBattleWaves : public AActor
{
	GENERATED_BODY()

public:
	AJRPGBattleWaves();

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> EditorIcon;
#endif

	/**
	 * O JSON das ondas, relativo à pasta Content do projeto ("Data/Waves/muscle_dome.json").
	 * Vazio = usar as propriedades abaixo. Para o JSON ir no build: Project Settings > Packaging >
	 * Additional Non-Asset Directories to Package > Data/Waves.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	FString WavesFile;

	/** As ondas, na ordem (substituídas pelo WavesFile, se ele existir). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	TArray<FJRPGBattleWave> Waves;

	/** Quem luta (keys do DT_Characters, na ordem da formação). Vazio = a party ativa. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	TArray<FName> Party;

	/** Onde as lutas acontecem. Vazio = a arena escolhida pela posição deste ator. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	TObjectPtr<AJRPGBattleArena> Arena;

	/** Começar sozinho ao dar play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	bool bAutoStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves", meta = (ClampMin = "0.0"))
	float StartDelay = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves", meta = (ClampMin = "0.0"))
	float DelayBetweenWaves = 2.0f;

	/** Depois da última, volta para a primeira. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	bool bLoop = false;

	/** Escreve na tela cada onda, vitória e derrota. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	bool bPrintDebug = true;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Waves")
	FOnJRPGWaveStarted OnWaveStarted;

	/** Todas as ondas vencidas (sem bLoop). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Waves")
	FOnJRPGWavesCleared OnWavesCleared;

	/** Uma onda perdida, fugida ou abortada: as ondas param. */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Waves")
	FOnJRPGWavesFailed OnWavesFailed;

	/** Começa da primeira onda. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Waves")
	void StartWaves();

	/** Para de chamar ondas (a luta em andamento continua até acabar). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Waves")
	void StopWaves();

	/** Começa esta onda agora. False se ela não existe ou se já há uma luta. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Waves")
	bool StartWave(int32 WaveIndex);

	/** A onda atual (ou a última), 0 = a primeira; -1 antes de começar. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Waves")
	int32 GetCurrentWave() const { return CurrentWave; }

	/**
	 * Lê ondas, party, delays e loop de um JSON (o texto, não o caminho). False com o motivo em
	 * OutError se o texto não é um JSON de ondas; nesse caso nada muda.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Waves")
	bool LoadWavesFromJson(const FString& Json, FString& OutError);

	/** Lê o WavesFile. False se ele não existe ou não é válido (as propriedades ficam como estão). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle Waves")
	bool LoadWavesFile(FString& OutError);

	/** Copia o WavesFile para as propriedades (que ficam salvas no mapa). */
	UFUNCTION(CallInEditor, Category = "JRPG|Battle Waves")
	void ImportWavesFile();

	/** O caminho completo do WavesFile no disco. */
	FString GetWavesFilePath() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleBattleEnded(EJRPGBattleResult Result);

	void ScheduleWave(int32 WaveIndex, float Delay);
	FString DescribeWave(int32 WaveIndex) const;
	void Print(const FString& Message, const FColor& Color = FColor::Silver) const;

	int32 CurrentWave = -1;
	bool bRunning = false;
	/** A luta em andamento é uma onda nossa (e não um inimigo do Field). */
	bool bWaveInProgress = false;
	FTimerHandle WaveTimer;
};
