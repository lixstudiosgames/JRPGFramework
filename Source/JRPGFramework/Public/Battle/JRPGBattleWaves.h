#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Battle/BattleTypes.h"
#include "Battle/JRPGEncounterTypes.h"
#include "JRPGBattleWaves.generated.h"

class AJRPGBattleArena;
class UBillboardComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGWaveStarted, int32, WaveIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJRPGWavesCleared);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGWavesFailed, EJRPGBattleResult, Result);

/**
 * AJRPGBattleWaves
 * Lutas em sequência, uma onda depois da outra, no estilo do Muscle Dome: arraste para o mapa,
 * escolha a party e monte as ondas. Cada onda é um encounter (sorteado como os do Field; para
 * uma onda fixa, uma linha com bAlwaysJoins e CountMin = CountMax).
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

	/** As ondas, na ordem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle Waves")
	TArray<FJRPGEncounterSetup> Waves;

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

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleBattleEnded(EJRPGBattleResult Result);

	void ScheduleWave(int32 WaveIndex, float Delay);
	void Print(const FString& Message, const FColor& Color = FColor::Silver) const;

	int32 CurrentWave = -1;
	bool bRunning = false;
	/** A luta em andamento é uma onda nossa (e não um inimigo do Field). */
	bool bWaveInProgress = false;
	FTimerHandle WaveTimer;
};
