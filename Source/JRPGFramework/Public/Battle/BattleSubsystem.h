#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Inventory/ItemData.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Battle/BattleTypes.h"
#include "Battle/JRPGBattleResolver.h"
#include "Battle/EnemyActionData.h"
#include "Battle/JRPGBattleCamera.h"
#include "Core/ProgressionTypes.h"
#include "BattleSubsystem.generated.h"

class UCoreSubsystem;
class UDataTable;

/** Um encontro começou: os inimigos sorteados para a luta. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGEncounterStarted, const TArray<FJRPGEncounterCombatant>&, Enemies);

/** Uma luta começou: o pedido e os combatentes (heróis primeiro, depois inimigos). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGBattleStarted, const FJRPGBattleSetup&, Setup, const TArray<FJRPGCombatant>&, Combatants);

/** A luta acabou. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGBattleEnded, EJRPGBattleResult, Result);

/** A resolução produziu um fato (dano, miss, bloqueio, queda...), na ordem em que acontece. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGBattleEvent, const FJRPGBattleEvent&, Event);

/**
 * Um quadro lógico (1/60 s) andou: a apresentação toca os clips em compasso com ele. Frame é o RELÓGIO DE APRESENTAÇÃO
 * (UBattleSubsystem::GetPresentationFrame): sobe de 1 em 1 a cada passo, monotônico, sem pular, inclusive nos passos em que o
 * resolvedor não anda (a digitação dos comandos, o hold da vitória / derrota). Não é o State.Frame das regras.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGLogicFrame, int32, Frame);

/** Quem digita (Commands) ou age (Execution) mudou; Slot -1 = ninguém. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGSlotChanged, int32, Slot, EJRPGBattleStage, Stage);

/** Um comando foi recusado: o slot dele e por quê (o texto das regras, para mostrar ao lado do prompt). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGCommandRefused, int32, Slot, const FString&, Why);

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
	// A pausa só vale para o objeto ligado a um mundo: sem GetTickableGameObjectWorld, o FTickableGameObject cai na
	// chamada sem mundo do fim do UGameEngine::Tick (TickObjects(nullptr, LEVELTICK_All, bIsPaused = false)), que
	// ignora a pausa. Com o mundo do jogo, quem tica é o UWorld::Tick, que respeita IsTickableWhenPaused (false aqui).
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !IsTemplate() && bInBattle && !bLogicHeld; }
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual UWorld* GetTickableGameObjectWorld() const override;
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
	 * O fim de debug (JRPG.Battle.End): como EndBattle, mas com bPayEveryEnemy a Vitória paga o EXP e o Gold de TODOS os
	 * inimigos da luta, vivos também (a vitória de verdade só conta os derrotados). Para testar a recompensa.
	 */
	void EndBattleForDebug(EJRPGBattleResult Result, bool bPayEveryEnemy);

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

	/**
	 * O relógio de apresentação: um contador monotônico que anda 1 por passo lógico (1/60 s) sempre que o jogo não está pausado
	 * nem em HoldLogic, mesmo quando o resolvedor não anda (Commands, vitória / derrota decididas). Os pawns tocam reações, o fade de
	 * morte, o idle e os sons por ele. É também o valor de OnLogicFrame. As regras seguem pelo State.Frame, que só anda na
	 * Execution; a diferença entre os dois é GetPresentationOffset.
	 */
	int32 GetPresentationFrame() const { return PresentationFrame; }

	/**
	 * Quantos quadros de apresentação passaram sem o resolvedor andar (PresentationFrame - State.Frame). Soma-se a um quadro das
	 * regras (State.Frame, FJRPGActionTimeline::StartFrame) para pô-lo no relógio de apresentação. O OnBattleEvent já entrega o
	 * Event.Frame somado; o GetEventLog() e as timelines vêm com o quadro cru das regras.
	 */
	int32 GetPresentationOffset() const { return PresentationOffset; }

	/** Todos os eventos da luta, na ordem em que a resolução os produziu. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	const TArray<FJRPGBattleEvent>& GetEventLog() const { return Resolver.GetEvents(); }

	/** A linha do tempo da ação em curso (para a apresentação tocar os clips em compasso); nula fora de uma ação. */
	const FJRPGActionTimeline* GetActiveTimeline() const { return Resolver.GetActiveTimeline(); }

	/** A linha do tempo de uma ação pelo ActionIndex dos eventos, mesmo depois de ela acabar. */
	const FJRPGActionTimeline* FindTimeline(int32 ActionIndex) const { return Resolver.FindTimeline(ActionIndex); }

	/** O texto de um evento ("[f132 a3] Vahn#0 hits Gimard#3 for 275"). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	FString DescribeEvent(const FJRPGBattleEvent& Event) const { return Resolver.DescribeEvent(Event); }

	/**
	 * Segura o relógio da luta (a cena ainda está entrando, com a tela preta): nenhum quadro anda e a rodada não passa de
	 * Commands para Execution. Os comandos continuam sendo aceitos e ficam guardados; ao soltar, a execução começa e o
	 * ActionStart chega à cena já montada. O UJRPGBattleSceneSubsystem segura no OnBattleStarted e solta quando a cena monta.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void HoldLogic(bool bHold);

	/**
	 * Para o resolvedor por Frames quadros enquanto o relógio de apresentação segue (os pawns animam). A cena usa para a caminhada
	 * em velocidade constante: um alvo longe leva mais que os LeadFrames das regras, e os golpes esperam o atacante chegar. As
	 * regras não mudam (o resolvedor só anda mais tarde), então a luta continua determinística; soma com uma parada em curso.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void StallResolver(int32 Frames);

	/**
	 * A aproximação da ação em curso, medida pela cena: o termo de ângulo (o quanto o alvo não está de frente, 0..0x800) e o de
	 * distância (os quadros que o atacante andou) do original, gastos pelo primeiro golpe da ação no block e no dano. Entra nas
	 * regras como entrada (FJRPGBattleResolver::SubmitApproach, limitada e gravada com a luta), não como leitura de posição.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	bool SubmitApproach(int32 ActionIndex, int32 AttackRamp, int32 GuardRamp);

	/**
	 * PvP: segura o resolvedor antes do primeiro golpe de uma ação até a aproximação dela chegar (a autoridade mede e manda; as
	 * duas máquinas aplicam a mesma). No jogo local a cena a entrega no mesmo quadro do ActionStart, então desligado não muda nada.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	bool bWaitForApproachInput = false;

	/**
	 * A opção Battle Camera do jogo (Close / Normal / Far; a tela de opções, aba Game): a câmera da luta a segue
	 * (AJRPGBattleCamera::bFollowGameOption). Salva no slot de configurações "JRPGSettings", não no save da partida.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	EJRPGBattleCameraOption GetBattleCameraOption() const { return BattleCameraOption; }

	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void SetBattleCameraOption(EJRPGBattleCameraOption Option);

	/**
	 * Esta máquina mede a aproximação e a manda (a cena chama SubmitApproach no ActionStart). No PvP só a autoridade mede; a
	 * outra recebe a medida pela rede e a aplica com SubmitApproach, e a cena dela não manda a sua (seria aceita no lugar da
	 * da autoridade e as duas lutas divergiriam).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	bool bApproachAuthority = true;

	/** O último passo segurou o resolvedor à espera da aproximação: a cena atrasa a ação do pawn o mesmo tanto. */
	bool WasHeldForApproach() const { return bHeldForApproach; }

	/**
	 * O combatente de um inimigo a partir do DT_Enemies, pelas regras da versão (InstallEnemyStats) e a dificuldade
	 * (Core pode ser nulo: sem dificuldade). Actions são as linhas do DT_EnemyActions dele (qualquer ordem). Estática
	 * e sem subsistema, para os testes montarem a mesma foto que a luta.
	 */
	static FJRPGCombatant MakeEnemyCombatant(const FJRPGEnemyData& Row, const TArray<FJRPGEnemyActionData>& Actions, int32 Level,
		bool bScriptedFight, EJRPGRuleset Ruleset, const UCoreSubsystem* Core);

	/**
	 * Preenche o golpe simples de cada botão do herói pelo que ele tem equipado, da DT_HeroSwings (FJRPGHeroSwingData): a
	 * linha <Herói>_<Item>_<Botão> do item (a arma no Arms, o tier "seru_N" no Ra-Seru, o calçado no Low e no High) e, sem ela,
	 * a <Herói>_default_<Botão>. Põe SwingBytes, SwingProfiles e o custo do botão (só da linha do próprio item, ou da padrão
	 * quando o slot está vazio; um item sem linha mantém o custo que já estava). Table nula ou de outra struct: não faz nada.
	 * Estática e sem subsistema, para os testes.
	 */
	static void ApplyHeroSwings(FJRPGCombatant& Hero, const UDataTable* Table, FName Weapon, FName Shoes, int32 SeruLevel);

	/** Segundos, depois de um encontro aceito ou de uma luta encerrada, em que outro é recusado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0.0"))
	float EncounterGraceTime = 3.0f;

	/**
	 * Segundos, a partir de o Field aparecer de novo depois de uma luta (NoteFieldReturned), em que nenhum encontro começa
	 * e os inimigos do Field nem perseguem: dá tempo de o jogador sair de cima de quem estava ao lado.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0.0"))
	float PostBattleGraceTime = 5.0f;

	/** Em carência: um encontro novo é recusado e os inimigos do Field não detectam o jogador. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	bool IsInEncounterGrace() const;

	/** A cena devolveu o Field ao jogador (a tela abriu depois da luta): começa a PostBattleGraceTime. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void NoteFieldReturned();

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

	/**
	 * O beat do flinch e do block de cada herói (rate 1), medido nos arquivos de batalha: Vahn 3, Noa 2, Gala 4. Vale no
	 * lugar do beat de HeroFlinch / HeroBlock para quem está na lista; quem não está usa os dois acima.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle")
	TMap<FName, int32> HeroReactionBeats;

	/**
	 * Quadros lógicos entre a luta decidida (a vitória é testada só depois da reação do alvo e da faixa Done de 60 vsyncs, no
	 * fim da última ação) e o EndBattle. O original segura a tela de resultados com a pose de vitória por 0x100 = 256 vsyncs
	 * (o gp+0xA54 conta até 0x100; os ~80 de antes são espera do CD, que não existe aqui). A pose de vitória do líder ainda não
	 * é tocada: os pawns ficam no idle. O fade de saída do cenário vem depois disto (UJRPGBattleSceneSubsystem).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0"))
	int32 VictoryHoldFrames = 256;

	/**
	 * O mesmo na derrota: o original cai na última fase do mesmo sequenciador e gasta os 0x100 mais o fade de uns 66 vsyncs, uns
	 * 322 do sinal à saída (inferido, não capturado). Os 322 são a sequência de limpeza do original, já com o fade de saída dela; o
	 * fade do nosso cenário (UJRPGBattleSceneSubsystem) vem depois deste hold, à parte.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0"))
	int32 DefeatHoldFrames = 322;

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

	/**
	 * Cada fato da resolução, na ordem (a UI monta o texto; a apresentação mostra quando o clip chega). O Event.Frame aqui já está no
	 * relógio de apresentação (quadro das regras + GetPresentationOffset), o mesmo do OnLogicFrame; o GetEventLog() traz o cru.
	 */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGBattleEvent OnBattleEvent;

	/** Quem digita / age mudou (para a câmera e a UI). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGSlotChanged OnSlotChanged;

	/** Um comando foi recusado (e por quê): a UI mostra ao lado do prompt. */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGCommandRefused OnCommandRefused;

	/**
	 * Cada quadro lógico, depois dos eventos dele: a apresentação anda os clips por este contador (o relógio de apresentação, não
	 * o State.Frame; ver GetPresentationFrame). Não anda com o jogo pausado nem com HoldLogic.
	 */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGLogicFrame OnLogicFrame;

private:
	/** A opção Battle Camera (lida do "JRPGSettings" no Initialize). */
	EJRPGBattleCameraOption BattleCameraOption = EJRPGBattleCameraOption::Normal;

	/** A foto dos combatentes (heróis pelo Party / Status / Inventory, inimigos pelo DT_Enemies e DT_EnemyActions). */
	bool BuildSnapshot(const FJRPGBattleSetup& Setup, TArray<FJRPGCombatant>& OutCombatants) const;
	FJRPGCombatant MakeHeroCombatant(FName Key, int32 SideIndex) const;

	/** Quantos quadros a luta decidida segura antes do EndBattle: o hold da vitória / da derrota; Fuga e Aborted fecham na hora. */
	int32 FinishHoldFor(EJRPGBattleResult Result) const;

	/** Escreve a luta no roster (Vitória e Fuga) e paga a recompensa (Vitória). */
	void WriteBack(EJRPGBattleResult Result);
	void PayRewards();

	/** Manda os eventos novos ao delegate e ao log, e avisa quando quem age mudou. */
	void FlushEvents();

	/** FPlatformTime::Seconds() do último encontro aceito / luta encerrada (relógio real: vale com o jogo pausado). */
	double LastEncounterTime = -1.0e9;

	/** FPlatformTime::Seconds() até quando vale a carência de depois da luta (NoteFieldReturned). */
	double PostBattleGraceUntil = -1.0e9;

	/** O fim de debug em curso paga todos os inimigos (EndBattleForDebug). */
	bool bDebugPayEveryEnemy = false;

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

	/** O DT_ArtHits (o quadro de cada golpe das Arts, do disco), preso pelo mesmo motivo; nulo sem a tabela. */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> ArtHitsTable;

	double Accumulator = 0.0;
	int32 FlushedEvents = 0;
	int32 LastNotifiedSlot = -2;
	EJRPGBattleStage LastNotifiedStage = EJRPGBattleStage::None;
	/** Passos desde a luta decidida: conta só o hold da vitória / derrota (FinishHoldFor). */
	int32 FramesSinceFinish = 0;

	/** O relógio de apresentação (GetPresentationFrame) e a diferença dele para o State.Frame das regras. */
	int32 PresentationFrame = 0;
	int32 PresentationOffset = 0;
	/** Quadros que o resolvedor ainda fica parado (StallResolver). */
	int32 ResolverStallFrames = 0;
	/** O último StepFrame segurou o resolvedor à espera da aproximação (bWaitForApproachInput). */
	bool bHeldForApproach = false;
};
