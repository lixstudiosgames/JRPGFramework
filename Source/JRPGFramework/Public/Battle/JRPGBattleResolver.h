#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleTypes.h"
#include "JRPGBattleResolver.generated.h"

class UDataTable;

/**
 * FJRPGTimelineDefaults
 * Os quadros PADRÃO por tipo de passo, quando o clip falta ou não traz os marcadores de golpe (Docs/BATTLE_ROADMAP.md ›
 * 5.0 › The logic timeline: "the fallback is data, not a guess at play time"). Em quadros lógicos (1/60 s) na
 * velocidade normal do jogo (escala 8); um passo em escala menor dura proporcionalmente mais. A party usa isto até
 * os UJRPGAnimNotify_Hit estarem nos clips; o uso é registrado no log, uma vez por tipo de passo na luta.
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGTimelineDefaults
{
	GENERATED_BODY()

	/** Do começo da ação até o primeiro passo: o atacante chega ao alvo (a apresentação anda; as regras esperam). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 LeadFrames = 30;

	/** Do último passo até a ação acabar: a recuperação. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 TailFrames = 24;

	/** A cauda quando a luta acaba no meio da ação (o último inimigo cai no meio de uma combo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 KillTailFrames = 30;

	/** Um golpe simples (SwingL..U): o tamanho do clip, o quadro do golpe e o corte (onde o próximo passo entra). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 SwingFrames = 48;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 SwingHitFrame = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 SwingCutFrame = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 ArtStarterFrames = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 SpecialStarterFrames = 36;

	/** Uma Art sem marcadores: o tempo até o primeiro golpe, entre golpes, e depois do último. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 ArtLeadFrames = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 ArtFramesPerHit = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 ArtTailFrames = 24;

	/** O golpe de um monstro sem linha no DT_EnemyActions (ou sem quadro de golpe): o clip e o quadro do golpe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 EnemySwingFrames = 80;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timeline")
	int32 EnemySwingHitFrame = 30;
};

/** O que o resolvedor pergunta sobre o clip de um passo da party. */
struct JRPGFRAMEWORK_API FJRPGClipQuery
{
	EJRPGActionStepKind Kind = EJRPGActionStepKind::Swing;

	/** SwingL.. / ArtStarter / SpecialStarter, ou a key da Art quando Kind é Art. */
	FName ClipName;

	FName ArtKey;

	/** A escala de velocidade do passo (8 / 4 / 2). */
	int32 GameSpeed = 8;

	/** Quantos golpes o passo deve ter (o HitPower da linha); 1 num golpe simples, 0 num starter. */
	int32 ExpectedHits = 0;
};

/** O que o clip diz sobre o tempo do passo, já em quadros lógicos na escala do passo. */
struct JRPGFRAMEWORK_API FJRPGClipTiming
{
	/** Quadros lógicos do clip inteiro; 0 = não sabe. */
	int32 Frames = 0;

	/** Os marcadores de golpe, em quadros lógicos; vazio se o clip não os tem ou não são ExpectedHits. */
	TArray<int32> HitFrames;

	/** O Chain Cut, em quadros lógicos; negativo se não há. */
	int32 CutFrame = -1;
};

/**
 * FJRPGBattleResolver
 * O núcleo da luta, SEM UObject nem subsistema: comandos e inputs entram, a resolução anda na linha do tempo
 * lógica (um quadro fixo de 1/60 s por StepFrame) e eventos saem. Tudo o que decide o resultado está em
 * FJRPGBattleState — a mesma semente, os mesmos comandos e os mesmos inputs nos mesmos quadros dão exatamente a
 * mesma lista de eventos (o teste que garante o PvP depois). O UBattleSubsystem tira a foto dos combatentes, chama
 * StepFrame num acumulador de passo fixo e escreve de volta no fim; os testes montam os combatentes à mão.
 *
 * A rodada é a do original: Commands (todo herói de jogador entra o comando dele; a IA decide quando a ação
 * começa) e Execution (a chave de iniciativa escolhe quem age, a ação toca na linha do tempo e cada golpe é
 * resolvido NO QUADRO DELE por ResolveHit — nunca por notify, nunca antes). Contratos e tabela de falhas:
 * Docs/BATTLE_ROADMAP.md › Phase 5.
 */
class JRPGFRAMEWORK_API FJRPGBattleResolver
{
public:
	/** O DT_Arts (BuildChain). Nulo = toda entrada vira golpe simples. */
	const UDataTable* ArtsTable = nullptr;

	FJRPGTimelineDefaults Defaults;

	/** Opcional: o tempo dos clips da party, lido do asset COMO DADO ao montar a ação. Sem ele valem os padrões. */
	TFunction<bool(FName Hero, const FJRPGClipQuery& Query, FJRPGClipTiming& OutTiming)> ClipTiming;

	/** Registra no log os quadros padrão usados (uma vez por tipo de passo na luta). */
	bool bLogFallbacks = true;

	/**
	 * SÓ PARA TESTES: no lugar do Rng da luta, cada sorteio de 15 bits sai daqui (uma sequência fixa repete as jogadas
	 * do RE). O aprender-no-uso das Arts (que pede um FRandomStream) continua no Rng; com a chance padrão de 100 ele
	 * não sorteia.
	 */
	TFunction<int32()> RandOverride;

	/** Começa a luta com estes combatentes (a foto). Reinicia o estado e os eventos e abre a rodada 1. */
	void Begin(const TArray<FJRPGCombatant>& InCombatants, int32 InSeed, bool bInScriptedFight, int32 InFormationAdvantage);

	/**
	 * Um comando, um ponto de entrada: confere que quem manda é o dono do slot (OwnerPlayer), que a rodada está
	 * em Commands, que o combatente está de pé, que o alvo é um adversário vivo e, nas Arts, que os bytes são
	 * botões e cabem no gauge. Nunca confia no payload (regra 22). Quando o último comando entra, a execução
	 * começa. OutWhy diz por que recusou.
	 */
	bool SubmitCommand(const FJRPGBattleCommand& Command, FString* OutWhy = nullptr);

	/**
	 * Um input durante a execução (o parry): confere o dono, que o slot está de pé e que o quadro carimbado não é
	 * do futuro, e o guarda. Ainda NÃO muda o resultado: a janela e o efeito do parry são a questão aberta 5 do
	 * roadmap — o contrato (carimbo, um input por golpe, o de fora da janela é ignorado) é o que fica pronto.
	 */
	bool SubmitInput(const FJRPGBattleInput& Input, FString* OutWhy = nullptr);

	/** Um quadro lógico. Não faz nada fora da execução (a rodada de comandos espera quem pensa). */
	void StepFrame();

	/**
	 * Segura a passagem de Commands para Execution: com todos os comandos entrados a rodada espera aqui (os comandos
	 * continuam sendo aceitos e guardados) até soltar. Quem solta dá o primeiro passo da execução, no mesmo chamado.
	 * É tempo de apresentação (a cena ainda está entrando), não regra: nenhum quadro anda em Commands de qualquer modo.
	 */
	void SetHoldExecution(bool bHold);

	/** Encerra a luta de fora (EndBattle chamado antes da hora): congela o estado e não produz mais eventos. */
	void Halt();

	const FJRPGBattleState& GetState() const { return State; }
	const TArray<FJRPGCombatant>& GetCombatants() const { return State.Combatants; }
	const TArray<FJRPGBattleEvent>& GetEvents() const { return Events; }

	/** Quem digita (Commands) ou age (Execution); -1 entre uma coisa e outra. */
	int32 GetActiveSlot() const { return State.ActiveSlot; }

	bool IsFinished() const { return State.bFinished; }
	EJRPGBattleResult GetResult() const { return State.Result; }

	/** A timeline da ação em curso; nulo se não há. */
	const FJRPGActionTimeline* GetActiveTimeline() const { return State.Action.bActive ? &State.Action.Timeline : nullptr; }

	/**
	 * A timeline de uma ação da luta (o ActionIndex dos eventos), mesmo depois de ela acabar: a apresentação toca os
	 * clips por ela, e um evento pode chegar quando a ação já andou. Nula se o índice não existe.
	 */
	const FJRPGActionTimeline* FindTimeline(int32 ActionIndex) const { return Timelines.IsValidIndex(ActionIndex) ? &Timelines[ActionIndex] : nullptr; }

	/** As recompensas só são pagas uma vez: a primeira chamada devolve true e marca, as outras false. */
	bool ClaimRewards();

	/** O adversário vivo de menor slot (FUN_801DB8B4); INDEX_NONE se não há. */
	int32 LowestLivingFoe(const FJRPGCombatant& Of) const;

	/** O texto de um evento, para o log e para a UI de debug. */
	FString DescribeEvent(const FJRPGBattleEvent& Event) const;

	/** O texto de um combatente: "Vahn#0". */
	FString SlotName(int32 Slot) const;

private:
	struct FPlanStep;

	// --- A rodada ---
	void StartRound();
	void EndRound();
	void BeginExecution();
	/** Avança até algo esperar: comandos, uma ação em curso ou o fim da luta. */
	void Pump();
	bool PickNext();
	void StartAction(int32 Slot);
	void EndAction(bool bCancelled);
	bool CheckFinish();
	void Finish(EJRPGBattleResult Result);
	bool NeedsAnyCommand(int32* OutSlot) const;
	void SeedInitiative();

	// --- Planejar a ação (a linha do tempo, sempre de dados) ---
	bool PlanHeroAction(const FJRPGCombatant& Hero, const FJRPGBattleCommand& Command, int32& OutTarget,
		FJRPGArtChain& OutChain, FJRPGActionTimeline& OutTimeline);
	bool PlanMonsterAction(FJRPGCombatant& Monster, int32& OutTarget, FJRPGActionTimeline& OutTimeline);
	void AppendArtSteps(const FJRPGCombatant& Hero, int32 Entry, FName ArtKey, TArray<FPlanStep>& Steps) const;
	void ExpandSequenceStep(const FJRPGCombatant& Hero, int32 Entry, FName Step, FName SelfKey, const FJRPGArtData& Self,
		int32 Depth, TArray<FPlanStep>& Steps) const;
	void ComposeHeroTimeline(const FJRPGCombatant& Hero, const FJRPGArtChain& Chain, TArray<FPlanStep>& Steps,
		FJRPGActionTimeline& Out);
	void ComposeEnemyTimeline(const FJRPGCombatant& Monster, const TArray<FJRPGBattleActionEntry>& Queue, FJRPGActionTimeline& Out);
	int32 DrawLivingFoe(const FJRPGCombatant& Of);
	void LogFallback(const FString& Key, const FString& Message);

	// --- Resolver ---
	void ProcessDue();
	void ResolveHit(int32 HitIndex);
	void ApplyMark(const FJRPGActionMark& Mark);
	void CutRestOfAction();
	bool RunConditions(FJRPGCombatant& Of, bool bTurnStart);
	void KillCombatant(FJRPGCombatant& Of, int32 KilledBy);

	// --- Ajudas ---
	bool IsLivingFoe(const FJRPGCombatant& Of, int32 Slot) const;
	bool HasCondition(const FJRPGCombatant& Of, FName Condition) const;
	void Emit(EJRPGBattleEventType Type, int32 Slot, int32 Target, int32 Value, int32 HitIndex = -1, int32 Flags = 0, FName ArtKey = NAME_None);
	/** Um sorteio de 15 bits do Rng da luta. */
	int32 Rand15();

	FJRPGBattleState State;
	TArray<FJRPGBattleEvent> Events;
	/** As timelines de todas as ações da luta, pelo ActionIndex (o índice é o contador, sem buracos). */
	TArray<FJRPGActionTimeline> Timelines;
	TSet<FString> LoggedFallbacks;
	/** Rodadas seguidas sem ação por FALTA DE PLANO (dado que falta); uma rodada ocupada por condição não conta. */
	int32 EmptyRounds = 0;
	/** Rodadas seguidas sem ação porque o turno de quem estava vivo se perdeu por condição (sono, atordoamento...). */
	int32 ConditionRounds = 0;
	bool bActionStartedThisRound = false;
	/** Segura Commands -> Execution (SetHoldExecution). */
	bool bHoldExecution = false;
	/** Alguém vivo perdeu o turno por uma condição nesta rodada (então a rodada vazia não é falta de dado). */
	bool bTurnLostToConditionThisRound = false;
};
