#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Inventory/ItemData.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Battle/BattleTypes.h"
#include "Battle/JRPGBattleResolver.h"
#include "Battle/EnemyActionData.h"
#include "Core/ProgressionTypes.h"
#include "BattleSubsystem.generated.h"

class UCoreSubsystem;

/** Um encontro começou: os inimigos sorteados para a luta. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGEncounterStarted, const TArray<FJRPGEncounterCombatant>&, Enemies);

/** Uma luta começou: o pedido e os combatentes (heróis primeiro, depois inimigos). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGBattleStarted, const FJRPGBattleSetup&, Setup, const TArray<FJRPGCombatant>&, Combatants);

/** A luta acabou. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGBattleEnded, EJRPGBattleResult, Result);

/** A resolução produziu um fato (dano, miss, bloqueio, queda...), na ordem em que acontece. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGBattleEvent, const FJRPGBattleEvent&, Event);

/** Quem digita (Commands) ou age (Execution) mudou; Slot -1 = ninguém. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGSlotChanged, int32, Slot, EJRPGBattleStage, Stage);

/**
 * UBattleSubsystem
 * Orquestra a batalha. Os encontros (TryStartEncounter), o começo e o fim de uma luta (BeginBattle /
 * EndBattle) e, desde a Fase 5, a luta em si: a foto dos combatentes, a rodada Commands -> Execution, o dano
 * golpe a golpe numa linha do tempo lógica, a IA física do original, as Arts na luta, e no fim a
 * escrita de volta no roster e a recompensa (paga uma vez só).
 *
 * As REGRAS moram em FJRPGBattleResolver (sem UObject; os testes o usam direto) e UBattleFormulas (contas
 * puras). Aqui ficam o que precisa de subsistema: tirar a foto do UPartySubsystem / UStatusSubsystem / do
 * inventário e dos DataTables, andar o relógio (FTickableGameObject com acumulador de passo fixo: o DeltaTime
 * bruto nunca chega às regras, e o jogo pausado não anda), e escrever o resultado de volta.
 *
 * Não conhece AActor nenhum — quem está no mapa fala com ele por chamadas e delegates: o inimigo do Field
 * pede a luta, o UJRPGBattleSceneSubsystem (do mundo) escuta OnBattleStarted / OnBattleEnded / OnBattleEvent e
 * manda a AJRPGBattleArena montar a cena e os AJRPGBattlePawn mostrarem. Ver Docs/BATTLE_ROADMAP.md e
 * Docs/Guides/BATTLE.md.
 *
 * Console (PIE): JRPG.Battle.Start / End / Attack / Arts / Auto — ficam no UJRPGBattleSceneSubsystem, que sabe
 * onde está o jogador.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UBattleSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UBattleSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- FTickableGameObject: só anda durante a luta; o jogo pausado não anda ---
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !IsTemplate() && bInBattle && !bLogicHeld; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UBattleSubsystem, STATGROUP_Tickables); }

	/**
	 * Aplica o efeito temporário de um item no combate (Shield Elixir, Power Elixir, etc.).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void ApplyBattleItemEffect(FName CharacterName, const FItemData& ItemData);

	/**
	 * Pede para começar um encontro com estes inimigos. Recusa (false) durante uma luta e se
	 * outro encontro começou (ou uma luta acabou) há menos de EncounterGraceTime — dois inimigos
	 * encostando no mesmo instante, ou o jogador ainda em cima do segundo depois do primeiro.
	 * Aceito, dispara OnEncounterStarted; quem pediu chama BeginBattle em seguida.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	bool TryStartEncounter(const TArray<FJRPGEncounterCombatant>& Enemies);

	/**
	 * Começa uma luta: tira a foto dos combatentes (a party de Setup.Party, ou a ativa; os inimigos de
	 * Setup.Enemies pelas regras da versão e a dificuldade), tira o jogador do Field (o SAVE some do menu), abre a
	 * rodada 1 e dispara OnBattleStarted. False se já há uma luta ou se não há inimigo nenhum. Setup.Seed 0 sorteia
	 * uma semente (guardada em GetBattleSetup(), para repetir a luta).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	bool BeginBattle(const FJRPGBattleSetup& Setup);

	/**
	 * Encerra a luta atual e dispara OnBattleEnded. Sem luta, não faz nada (uma segunda chamada não paga nada de
	 * novo). Escreve de volta no roster só na Vitória e na Fuga (HP, MP, AP, as Artes aprendidas; na Fuga o caído
	 * levanta com 1 de HP); a Vitória paga EXP e Gold uma vez só aos heróis vivos da luta; Derrota e Aborted não
	 * escrevem nada.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void EndBattle(EJRPGBattleResult Result);

	/**
	 * Um comando, um ponto de entrada: confere que quem manda é o dono do slot e o resto (regra 22; a recusa
	 * sai no log com o motivo). Nunca chame isto direto de um callback da UI sem passar o jogador que mandou.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	bool SubmitCommand(const FJRPGBattleCommand& Command);

	/** Um input durante a execução (o parry), carimbado com o quadro lógico; ver FJRPGBattleResolver::SubmitInput. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	bool SubmitInput(const FJRPGBattleInput& Input);

	/**
	 * Um quadro lógico (1/60 s). O Tick chama isto no acumulador; fica público para os testes e o debug andarem a
	 * luta sem relógio.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void StepFrame();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	bool IsInBattle() const { return bInBattle; }

	/** A luta inteira: os combatentes vivos, o Rng, a rodada, o quadro. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	const FJRPGBattleState& GetState() const { return Resolver.GetState(); }

	/** Os combatentes da luta atual (a foto que a resolução vai gastando): heróis primeiro, depois inimigos. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	const TArray<FJRPGCombatant>& GetCombatants() const { return Resolver.GetCombatants(); }

	/** O pedido da luta atual (ou da última), com a semente que valeu. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	const FJRPGBattleSetup& GetBattleSetup() const { return CurrentSetup; }

	/** Quem digita o comando (Commands) ou age (Execution); -1 entre uma coisa e outra. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	int32 GetActiveSlot() const { return Resolver.GetActiveSlot(); }

	/** Todos os eventos da luta, na ordem em que a resolução os produziu. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	const TArray<FJRPGBattleEvent>& GetEventLog() const { return Resolver.GetEvents(); }

	/** A linha do tempo da ação em curso (para a apresentação tocar os clips em compasso); nula fora de uma ação. */
	const FJRPGActionTimeline* GetActiveTimeline() const { return Resolver.GetActiveTimeline(); }

	/** O texto de um evento ("[f132 a3] Vahn#0 hits Gimard#3 for 275"). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	FString DescribeEvent(const FJRPGBattleEvent& Event) const { return Resolver.DescribeEvent(Event); }

	/**
	 * Segura o relógio da luta (a cena ainda está entrando, com a tela preta): nada anda e os comandos esperam.
	 * O UJRPGBattleSceneSubsystem segura no OnBattleStarted e solta quando a cena monta.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void HoldLogic(bool bHold) { bLogicHeld = bHold; }

	/**
	 * O combatente de um inimigo a partir do DT_Enemies, pelas regras da versão (InstallEnemyStats) e a dificuldade
	 * (Core pode ser nulo: sem dificuldade). Actions são as linhas do DT_EnemyActions dele (qualquer ordem). Estática
	 * e sem subsistema, para os testes montarem a mesma foto que a luta.
	 */
	static FJRPGCombatant MakeEnemyCombatant(const FJRPGEnemyData& Row, const TArray<FJRPGEnemyActionData>& Actions, int32 Level,
		bool bScriptedFight, EJRPGRuleset Ruleset, const UCoreSubsystem* Core);

	/** Segundos, depois de um encontro aceito ou de uma luta encerrada, em que outro é recusado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0.0"))
	float EncounterGraceTime = 3.0f;

	/** Quantos heróis entram numa luta (os Seru como membros de apoio aumentam isto). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "1"))
	int32 MaxPartySlots = 3;

	/** Quantos inimigos cabem numa luta; o resto do sorteio fica de fora. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "1"))
	int32 MaxEnemySlots = 5;

	/**
	 * A party quando não há party ativa nenhuma (PIE direto num mapa, sem new game nem save):
	 * a luta ainda monta, para testar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TArray<FName> FallbackParty = { TEXT("Vahn"), TEXT("Noa"), TEXT("Gala") };

	/** Os quadros padrão por tipo de passo, quando o clip falta ou não traz os marcadores de golpe (a party, por enquanto). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FJRPGTimelineDefaults TimelineDefaults;

	/**
	 * A janela de reação dos heróis (o juggle): o beat e o rate do flinch e do block. O original mede 3 / 1 no flinch
	 * do Vahn (12 quadros); o block do herói não está medido, vale o mesmo até alguém medir.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FJRPGBattleReactionClip HeroFlinch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	FJRPGBattleReactionClip HeroBlock;

	/** Quadros lógicos entre a luta decidida (último inimigo caído) e o EndBattle: a queda e a pose tocam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0"))
	int32 FinishDelayFrames = 90;

	/** O passo do relógio lógico: 1/60 s. Não mude em jogo: os quadros de golpe são contados nele. */
	static constexpr double LogicStep = 1.0 / 60.0;

	/** Quantos passos um Tick pode dar de uma vez (uma pausa longa do jogo não vira uma rajada de golpes). */
	static constexpr int32 MaxStepsPerTick = 12;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGEncounterStarted OnEncounterStarted;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGBattleStarted OnBattleStarted;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGBattleEnded OnBattleEnded;

	/** Cada fato da resolução, na ordem (a UI monta o texto; a apresentação mostra quando o clip chega). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGBattleEvent OnBattleEvent;

	/** Quem digita / age mudou (para a câmera e a UI). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGSlotChanged OnSlotChanged;

private:
	/** A foto dos combatentes (heróis pelo Party / Status / Inventory, inimigos pelo DT_Enemies e DT_EnemyActions). */
	bool BuildSnapshot(const FJRPGBattleSetup& Setup, TArray<FJRPGCombatant>& OutCombatants) const;
	FJRPGCombatant MakeHeroCombatant(FName Key, int32 SideIndex) const;

	/** Escreve a luta no roster (Vitória e Fuga) e paga a recompensa (Vitória). */
	void WriteBack(EJRPGBattleResult Result);
	void PayRewards();

	/** Manda os eventos novos ao delegate e ao log, e avisa quando quem age mudou. */
	void FlushEvents();

	/** FPlatformTime::Seconds() do último encontro aceito / luta encerrada (relógio real: vale com o jogo pausado). */
	double LastEncounterTime = -1.0e9;

	bool bInBattle = false;
	/** Estava no Field quando a luta começou (devolve o SAVE ao acabar). */
	bool bWasInField = false;

	/** Segura o relógio (a cena entrando). */
	bool bLogicHeld = false;

	FJRPGBattleSetup CurrentSetup;

	/** As regras: a máquina de estados da luta, sem UObject. */
	FJRPGBattleResolver Resolver;

	/** O DT_Arts da luta, preso aqui para o GC não levar enquanto o resolvedor aponta para ele. */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> ArtsTable;

	double Accumulator = 0.0;
	int32 FlushedEvents = 0;
	int32 LastNotifiedSlot = -2;
	EJRPGBattleStage LastNotifiedStage = EJRPGBattleStage::None;
	int32 FramesSinceFinish = 0;
};
