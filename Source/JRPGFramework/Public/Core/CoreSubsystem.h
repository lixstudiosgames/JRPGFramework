#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Core/ProgressionTypes.h"
#include "CoreSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGoldChangedSignature, int32, NewGold);

/** Disparado quando a dificuldade muda (SetDifficulty). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDifficultyChangedSignature, EJRPGDifficulty, NewDifficulty);

/**
 * UCoreSubsystem
 * Estado global central da partida: Gold, tempo de jogo (playtime), nome de
 * exibição do mapa atual, posição do player para o save e o fluxo de New Game.
 *
 * Vive no GameInstance (sobrevive a trocas de mapa/OpenLevel) e é a fonte de
 * verdade que o USaveSubsystem coleta/restaura no Save/Load. Planejado para
 * crescer com outras funções centrais do jogo.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UCoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ============================================================
	// GOLD
	// ============================================================

	/** Adiciona Gold (quantias <= 0 são ignoradas). Dispara OnGoldChanged. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Gold")
	void AddGold(int32 Amount);

	/**
	 * Remove Gold. Retorna false (sem alterar nada) se o jogador não tiver a
	 * quantia — ideal para validar compras em loja. Dispara OnGoldChanged.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Gold")
	bool RemoveGold(int32 Amount);

	/** Valor atual de Gold do jogador. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Gold")
	int32 GetGold() const { return Gold; }

	/** Disparado sempre que o Gold muda (AddGold/RemoveGold/StartNewGame/Load). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Core|Gold")
	FOnGoldChangedSignature OnGoldChanged;

	// ============================================================
	// PLAYTIME (tempo de jogo)
	// ============================================================

	/**
	 * Começa a contar o tempo de jogo. Chamar ao iniciar novo jogo (o
	 * StartNewGame já chama) e é re-ancorado automaticamente após um Load.
	 * Sem tick: o total é calculado on-demand a partir do relógio da plataforma.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Playtime")
	void StartPlaytimeTracking();

	/**
	 * Congela o cronômetro consolidando o decorrido no acumulado. Usado ao sair
	 * da partida para o main menu — sem isso o relógio continua correndo na tela
	 * de título e infla o tempo do próximo save. StartPlaytimeTracking retoma.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Playtime")
	void StopPlaytimeTracking();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Playtime")
	double GetPlaytimeSeconds() const;

	/** Tempo de jogo formatado "HH:MM:SS". */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Playtime")
	FString GetPlaytimeFormatted() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Playtime")
	bool IsPlaytimeTracking() const { return bPlaytimeTracking; }

	/** Formata segundos como "HH:MM:SS" (helper compartilhado com o SaveSubsystem). */
	static FString FormatPlaytimeSeconds(double Seconds);

	// ============================================================
	// MAPA ATUAL
	// ============================================================

	/**
	 * Define o nome bonito do mapa atual para a UI de save (ex: "Rim Elm").
	 * Chamar no BeginPlay de cada mapa. Sem isso, a UI mostra o nome técnico
	 * do level.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Map")
	void SetCurrentMapDisplayName(FText DisplayName);

	/** Nome de exibição do mapa atual (fallback = GetCurrentMapName()). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Map")
	FString GetCurrentMapDisplayName() const;

	/** Nome curto do level atual (sem prefixo PIE) — o mesmo aceito pelo OpenLevel. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Map")
	FString GetCurrentMapName() const;

	// ============================================================
	// POSIÇÃO DO PLAYER
	// ============================================================

	/**
	 * Posição do pawn e rotação de controle do player local — o que o save
	 * persiste. Retorna false se não houver pawn possuído no momento.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Player")
	bool GetPlayerSaveTransform(FVector& OutLocation, FRotator& OutRotation) const;

	// ============================================================
	// NEW GAME
	// ============================================================

	/**
	 * Reseta a partida para o estado inicial: inventário, estado do mundo
	 * (flags/baús/trees), Gold = 0, playtime = 0, começa a contar o tempo e
	 * marca bIsNewGame = true. Chamar no menu principal ANTES do OpenLevel do
	 * mapa inicial. NÃO abre mapa — o Blueprint decide o level inicial.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core")
	void StartNewGame();

	/**
	 * True se a partida atual começou por StartNewGame; vira false quando um
	 * save é carregado. Útil para cutscenes/setup exclusivos de jogo novo.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core")
	bool IsNewGame() const { return bIsNewGame; }

	UFUNCTION(BlueprintCallable, Category = "JRPG|Core")
	void SetIsNewGame(bool bValue) { bIsNewGame = bValue; }

	/**
	 * Resolve as DataTables de progressao e dificuldade a partir dos caminhos
	 * padrao. Ver StatGrowthTable.
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ============================================================
	// PROGRESSÃO (curva de XP)
	// ============================================================

	/**
	 * XP acumulado necessário para ALCANÇAR `Level` (2..99), contando do
	 * level 1. GetXPForLevel(1) = 0.
	 *
	 * Fórmula do jogo original (US), derivada de FUN_801E9504:
	 *   delta(n)  = n*n/4 + 1                (tabela DAT_80076AF4)
	 *   soma(L)   = delta(1) + ... + delta(L)
	 *   limiar    = soma * 9999999 / 0x140FE     se L <  17
	 *   limiar    = soma * 121                   se L >= 17
	 * Confere com o jogo em L2 = 121, L37->L38 = 535546 e L99 = 9646483.
	 * Ver Docs/Referencia/progressao.md.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Progression")
	int32 GetXPForLevel(int32 Level) const;

	/**
	 * Mesma curva com a correção por personagem do original: o slot 1 (Noa)
	 * sobe um pouco mais cedo e o slot 2 (Gala) um pouco mais tarde
	 * (+-limiar*0x14/divisor). No jogo o divisor vem de uma tabela do disco;
	 * aqui usamos a aproximação `125 * Level`, que reproduz os limiares de
	 * new game (Vahn 121, Noa 102, Gala 140).
	 *
	 * Roster: 0 = Vahn, 1 = Noa, 2 = Gala, 3 = Terra (a loba). A Terra usa a
	 * curva base, igual ao Vahn — é o que o disco mostra no campo de próximo
	 * level do new game.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Progression")
	int32 GetXPForLevelForSlot(int32 Level, int32 PartySlot) const;

	/** Level (1..99) correspondente a um XP acumulado, na curva base. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Progression")
	int32 GetLevelForXP(int32 CumulativeXP) const;

	/** Level máximo do jogo. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Progression")
	int32 GetMaxLevel() const { return MaxLevel; }

	/**
	 * Teto ABSOLUTO do level: 500.
	 *
	 * Não é gosto — é aritmética. O XP acumulado é int32, e a curva cresce com
	 * o cubo do level: no L500 o total é 1.256.690.754, mas no L598 já passa de
	 * 2.147.483.647 e estoura. 500 deixa margem confortável.
	 *
	 * Para ir além disto seria preciso passar o XP para int64.
	 */
	static constexpr int32 AbsoluteMaxLevel = 500;

	/**
	 * Muda o teto de level (para o New Game+). Fica preso em [1, 500].
	 *
	 * Acima do 99 NÃO existe tabela: a curva de crescimento é extrapolada
	 * repetindo a linha 98. Isso não inventa nada — as três curvas do original
	 * já são constantes em 64 desde o level 50, então o ritmo acima do 99 é
	 * exatamente o mesmo da segunda metade do jogo.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Progression")
	void SetMaxLevel(int32 NewMax);

	// ============================================================
	// TETOS DE ATRIBUTO
	// ============================================================

	/**
	 * Teto de um atributo. O default reproduz o original: HP 9999, AGL 280,
	 * o resto 999.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Progression")
	int32 GetStatCap(FName Stat) const;

	/**
	 * Muda o teto de um atributo (para o New Game+).
	 *
	 * ATENÇÃO — NÃO MEXA NA AGL. O 280 não é um número redondo qualquer: é o
	 * máximo de art blocks que o sistema de Arts comporta. Subir a AGL geraria
	 * mais blocks do que a barra de comando consegue mostrar, e quebraria a UI
	 * de batalha junto. Os outros sete são livres.
	 *
	 * Lembre que HP acima de 9999 vira 5 dígitos: os cards de status e do
	 * Save/Load estão desenhados para 4.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Progression")
	void SetStatCap(FName Stat, int32 NewCap);

	/**
	 * Ganho DETERMINÍSTICO de um atributo ao subir de `FromLevel` para o
	 * seguinte, sem o sorteio:
	 *
	 *   ganho = (MaxValue - GrowthStart) * curva[CurveRow][FromLevel-1] / 9408
	 *   ganho = max(1, ganho)
	 *
	 * O jogo ainda soma `rand() % (2*Jitter+1) - Jitter` por cima — use
	 * RollStatGain para incluir essa variação. Precisa de StatGrowthTable e
	 * GrowthCurveTable atribuídas; sem elas devolve 0.
	 *
	 * Como cada curva soma exatamente 9408 ao longo dos 98 níveis, os ganhos
	 * acumulam para exatamente (MaxValue - GrowthStart): todo atributo cai
	 * preciso no seu teto no level 99.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Progression")
	int32 GetStatGainCore(FName Character, FName Stat, int32 FromLevel) const;

	/** GetStatGainCore mais o sorteio de jitter do original. Piso de 1. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Progression")
	int32 RollStatGain(FName Character, FName Stat, int32 FromLevel) const;

	/**
	 * Valor de um atributo num level, somando os ganhos determinísticos do
	 * level 1 até lá. Sem o jitter, então é o valor "médio" — serve para
	 * previsão de curva e para o LegaiaStudio, não para o save do jogador.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Progression")
	int32 GetStatAtLevel(FName Character, FName Stat, int32 Level) const;

	/**
	 * Este personagem sobe de atributo na dificuldade atual?
	 *
	 * false para a Terra no Normal — ela não cresce no original, e a gente
	 * reproduz isso. Use para decidir se mostra os números dela na UI ou se
	 * mostra "????" como o jogo de origem faz.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Progression")
	bool DoesCharacterGrow(FName Character) const;

	/**
	 * DataTable de FStatGrowthRow (DT_StatGrowth).
	 *
	 * Resolvida sozinha no Initialize: procura /Game/Data/DT_StatGrowth e, se
	 * nao achar, /JRPGFramework/Data/DT_StatGrowth. Basta importar o CSV com
	 * esse nome na pasta Data do projeto — nao precisa ligar nada.
	 *
	 * SEM ela ninguem cresce de atributo. O aviso sai no log e o comando `info`
	 * do dev menu mostra AUSENTE.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "JRPG|Core|Progression")
	TObjectPtr<UDataTable> StatGrowthTable;

	/** DataTable de FGrowthCurveRow (DT_GrowthCurve). Mesma resolucao automatica. */
	UPROPERTY(BlueprintReadWrite, Category = "JRPG|Core|Progression")
	TObjectPtr<UDataTable> GrowthCurveTable;

	/**
	 * Teto de level da partida. 99 é o do jogo original; o New Game+ sobe isto
	 * via SetMaxLevel. Leia por GetMaxLevel().
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Core|Progression")
	int32 MaxLevel = 99;

	/**
	 * Teto por atributo, preenchido no Initialize com os valores do original.
	 * Leia por GetStatCap(); escreva por SetStatCap().
	 */
	UPROPERTY(BlueprintReadOnly, Category = "JRPG|Core|Progression")
	TMap<FName, int32> StatCaps;

	/** Troca a tabela de crescimento em runtime (o normal e deixar a automatica). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Progression")
	void SetStatGrowthTable(UDataTable* NewTable);

	/** Troca as curvas de crescimento em runtime. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Progression")
	void SetGrowthCurveTable(UDataTable* NewTable);

	// ============================================================
	// DIFICULDADE
	// ============================================================

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Difficulty")
	EJRPGDifficulty GetDifficulty() const { return Difficulty; }

	/** Troca a dificuldade e dispara OnDifficultyChanged se mudou. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Difficulty")
	void SetDifficulty(EJRPGDifficulty NewDifficulty);

	/**
	 * Multiplicadores da dificuldade atual. Vêm de DifficultyTable quando ela
	 * está atribuída; senão dos defaults embutidos (Normal 1.0 / Hard 1.5 /
	 * Juggernaut 2.5), para o sistema funcionar sem depender de asset.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Difficulty")
	FDifficultyScaling GetDifficultyScaling() const;

	/** Escala o HP de um inimigo pela dificuldade atual. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Difficulty")
	int32 ScaleEnemyHP(int32 BaseValue) const;

	/** Escala o ataque de um inimigo pela dificuldade atual. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Difficulty")
	int32 ScaleEnemyATK(int32 BaseValue) const;

	/** Escala a defesa (UDF/LDF) de um inimigo pela dificuldade atual. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Difficulty")
	int32 ScaleEnemyDEF(int32 BaseValue) const;

	/** Escala o XP concedido por um inimigo pela dificuldade atual. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Difficulty")
	int32 ScaleXPReward(int32 BaseValue) const;

	/** Escala o gold concedido por um inimigo pela dificuldade atual. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Core|Difficulty")
	int32 ScaleGoldReward(int32 BaseValue) const;

	/**
	 * DataTable de FDifficultyScaling (DT_Difficulty). Row name = nome do valor
	 * do enum.
	 *
	 * Resolvida sozinha no Initialize, como as de progressao. E OPCIONAL: sem
	 * ela o subsistema usa os multiplicadores embutidos no C++, que sao os
	 * mesmos do CSV.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "JRPG|Core|Difficulty")
	TObjectPtr<UDataTable> DifficultyTable;

	/** Troca a tabela de dificuldade em runtime. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Core|Difficulty")
	void SetDifficultyTable(UDataTable* NewTable);

	/** Disparado quando a dificuldade muda. */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Core|Difficulty")
	FOnDifficultyChangedSignature OnDifficultyChanged;

	// --- Usado pelo USaveSubsystem ao aplicar um Load (não exposto a BP) ---
	void RestoreFromSave(int32 InGold, double InPlaytimeSeconds, const FString& InMapDisplayName);

private:
	int32 Gold = 0;

	/** Dificuldade atual da partida. Normal = jogo original. */
	EJRPGDifficulty Difficulty = EJRPGDifficulty::Normal;

	/** True desde o StartNewGame até um save ser carregado. */
	bool bIsNewGame = false;

	/** Nome bonito do mapa atual; vazio = usar o nome técnico do level. */
	FString CurrentMapDisplayName;

	bool bPlaytimeTracking = false;

	/** Tempo acumulado até a última âncora (segundos). */
	double AccumulatedPlaytimeSeconds = 0.0;

	/** FPlatformTime::Seconds() no momento em que o tracking (re)começou. */
	double TrackingStartPlatformTime = 0.0;
};
