#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Battle/BattleTypes.h"
#include "Battle/JRPGBattleVoices.h"
#include "JRPGBattleSceneSubsystem.generated.h"

class AJRPGBattleArena;
class AJRPGBattlePawn;
class UBattleSubsystem;
class USoundBase;
class UMaterialInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJRPGBattleSceneEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGBattleSceneAssembled, AJRPGBattleArena*, Arena, const TArray<AJRPGBattlePawn*>&, Pawns);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGFormationShifted, FVector, Translation, int32, Frames);

/**
 * A abertura da luta, depois da tela abrir e antes do primeiro prompt (Docs/Reference/BATTLE_RE_SWEEP.md seções 2.1 e 10.3, parte
 * 5.2): a varredura da câmera (o contador 0x80..0xC0 do FUN_80046A20) e os nomes dos inimigos com a linha de formação (o fluxo
 * 0x0A / 0x0B, 90 quadros, 120 com vantagem). As regras e o menu esperam as duas.
 */
UENUM(BlueprintType)
enum class EJRPGBattleIntroStage : uint8
{
	/** Sem abertura: a luta anda (ou nem começou). */
	None,
	/** A câmera sobe e se aproxima, depois fecha no centro (o caso 2). */
	Sweep,
	/** Os nomes dos inimigos e a linha de formação na tela. */
	Names
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGBattleIntroStage, EJRPGBattleIntroStage, Stage);

/**
 * UJRPGBattleSceneSubsystem
 * A ponte entre o UBattleSubsystem (que não conhece ator nenhum) e o mapa. Existe sozinho em todo
 * mapa jogado — não há nada para pôr na cena nem para configurar; quem é configurada é a arena.
 *
 * Começo de luta: escolhe a arena (AJRPGBattleArena::FindArenaFor no ponto do encontro; mapa sem
 * arena ganha uma provisória ali), escurece a tela (TransitionTime da arena) e, com ela TODA
 * preta, dispara OnFieldHidden (os inimigos do Field somem e voltam para casa) e manda a arena
 * montar a luta. Fim: escurece, a arena desmonta, OnFieldShown, a tela abre.
 *
 * Na luta (Fase 5) é também a ponte dos EVENTOS para os pawns: o ActionStart manda o pawn de quem age tocar a timeline
 * lógica (BeginAction), cada quadro lógico anda a reprodução (UpdatePlayback), e os fatos resolvidos (dano, miss,
 * bloqueio, queda, AP, Art) viram a reação do alvo e o texto em cima da cabeça. A luta fica segurada (HoldLogic)
 * enquanto a cena entra.
 *
 * Console (PIE): JRPG.Battle.Start <EnemyKey> [Quantos] [Level] e
 * JRPG.Battle.End [victory|defeat|escape|abort] (Docs/Guides/BATTLE_STAGE.md); para jogar sem HUD (o HUD é da Fase 7):
 * JRPG.Battle.Attack [inimigo], JRPG.Battle.Arts <botões> [inimigo] e JRPG.Battle.Auto (Docs/Guides/BATTLE.md).
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGBattleSceneSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Tela preta no começo da luta: o Field está para sumir (os inimigos se escondem e resetam). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Scene")
	FOnJRPGBattleSceneEvent OnFieldHidden;

	/** A luta está montada na arena (ainda com a tela preta, abrindo em seguida). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Scene")
	FOnJRPGBattleSceneAssembled OnSceneAssembled;

	/** Tela preta no fim da luta: a cena foi desmontada e o Field volta a aparecer. */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Scene")
	FOnJRPGBattleSceneEvent OnFieldShown;

	/**
	 * A formação foi recentrada (AJRPGBattleArena::bRecentreFormation): Translation é quanto o centro dela andou, em mundo
	 * (o alvo do golpe até o centro da arena); Frames 0 = de uma vez (como o original), > 0 = deslizando nesses quadros lógicos.
	 * É o gancho das câmeras de ação: o original soma o mesmo deslocamento no foco da câmera no mesmo quadro ("focus pair"),
	 * e por isso o reposicionamento não se vê. Uma câmera que segue isto fica parada em relação aos combatentes.
	 */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Scene")
	FOnJRPGFormationShifted OnFormationShifted;

	/** A abertura mudou de fase (a varredura, os nomes, e None quando acaba: aí a luta anda e o menu pode abrir). */
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle Scene")
	FOnJRPGBattleIntroStage OnIntroStageChanged;

	/** Em que fase da abertura a luta está (None fora dela). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Scene")
	EJRPGBattleIntroStage GetIntroStage() const { return IntroStage; }

	/** Segundos desde o começo da abertura (o relógio da varredura da câmera); 0 fora dela. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Scene")
	float GetIntroElapsedSeconds() const;

	/** A arena da luta atual (pode ser a provisória); nula fora de luta. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Scene")
	AJRPGBattleArena* GetArena() const;

	/** O Field está escondido pela luta (entre OnFieldHidden e OnFieldShown). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Scene")
	bool IsFieldHidden() const { return bFieldHidden; }

	/**
	 * Esconde o prompt de debug na tela ("Round 1 - command for Vahn: JRPG.Battle.Attack...") e o aviso vermelho de comando recusado
	 * enquanto a UI de comando da luta (UJRPGBattleUISubsystem) está de pé; as duas linhas continuam no log. Os comandos de console seguem
	 * valendo — é só a tela que fica limpa.
	 */
	void SetDebugPromptSuppressed(bool bSuppress);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	enum class EPhase : uint8
	{
		Idle,
		/** Escurecendo para começar; a cena ainda não existe. */
		Entering,
		Assembled,
		/** Escurecendo para sair; a cena ainda está lá. */
		Leaving
	};

	UFUNCTION()
	void HandleBattleStarted(const FJRPGBattleSetup& Setup, const TArray<FJRPGCombatant>& Combatants);

	UFUNCTION()
	void HandleBattleEnded(EJRPGBattleResult Result);

	/** Um fato da resolução: o pawn certo toca a reação e mostra o texto. */
	UFUNCTION()
	void HandleBattleEvent(const FJRPGBattleEvent& Event);

	/** Quem digita / age mudou: avisa na tela de quem é o comando. */
	UFUNCTION()
	void HandleSlotChanged(int32 Slot, EJRPGBattleStage Stage);

	/** Um comando foi recusado: o motivo na tela, em vermelho, ao lado do prompt (a luta não parece travada). */
	UFUNCTION()
	void HandleCommandRefused(int32 Slot, const FString& Why);

	/** O prompt de quem digita, na tela e no log. */
	void ShowCommandPrompt(const UBattleSubsystem& Battle, int32 Slot) const;

	/** O Field voltou ao jogador: começa a carência de depois da luta no UBattleSubsystem. */
	void NoteFieldReturnedToBattleSubsystem();

	/** A cópia da timeline com o StartFrame no relógio de apresentação (o dos pawns): soma o UBattleSubsystem::GetPresentationOffset. */
	static FJRPGActionTimeline ToPresentationTimeline(const UBattleSubsystem& Battle, const FJRPGActionTimeline& Timeline);

	/** Um quadro lógico andou (o relógio de apresentação, UBattleSubsystem::GetPresentationFrame): os pawns tocam em compasso. */
	UFUNCTION()
	void HandleLogicFrame(int32 Frame);

	/**
	 * Planeja o recentrar da formação (ver AJRPGBattleArena::bRecentreFormation): Focus no centro da arena, ou o centro de todos
	 * quando nulo; comprime o que passa do limite. Desliza a partir do quadro Frame; um recentrar pela metade termina na hora.
	 */
	void PlanFormationShift(const AJRPGBattlePawn* Focus, int32 Frame);
	/** Anda o deslize do recentrar e a separação, um quadro (antes dos pawns tocarem o quadro deles). */
	void StepFormation(int32 Frame);
	/**
	 * Mede a aproximação da ação que Actor começou (o ângulo entre as frentes e os quadros da caminhada) e a manda às regras, se
	 * esta máquina é a autoridade (UBattleSubsystem::bApproachAuthority). Só para ação de golpe com alvo.
	 */
	static void SubmitApproachFor(UBattleSubsystem& Battle, const AJRPGBattlePawn& Actor, const AJRPGBattlePawn* Target,
		const FJRPGActionTimeline& Timeline);
	/** Anda o recentrar em curso pela fração dada (0..1) do total de cada um; o destino de quem caminha anda com o alvo dele. */
	void ApplyFormationShift(float Fraction);

	/** Põe a dilatação do tempo do mundo do slow das Arts (1 = normal); só mexe quando muda. */
	void SetShoutDilation(float Dilation);
	float ShoutDilation = 1.0f;
	/** A janela do slow em curso (tempo real). */
	double ShoutUntilRealTime = 0.0;

	/**
	 * Os commits do herói que age (o passo que ele começou): o grito, a fanfarra e a janela do slow saem daqui. Anda os passos da
	 * timeline das regras do último visto (VoiceActionIndex / VoiceStepIndex) até o que toca agora, nenhum pulado.
	 */
	void CommitActingSteps(const UBattleSubsystem& Battle, const AJRPGBattlePawn& Acting);
	int32 VoiceActionIndex = INDEX_NONE;
	int32 VoiceStepIndex = INDEX_NONE;

	/** O clarão do alvo do cursor (AJRPGBattleArena::TargetHighlightStrength), um quadro. */
	void UpdateTargetHighlight();

	/** O fantasma perto da câmera (AJRPGBattleArena::bCameraGhost), um quadro. */
	void UpdateCameraGhost(const UBattleSubsystem* Battle, const AJRPGBattlePawn* Acting);
	/** A opacidade de agora de cada pawn (anda até o alvo em CameraGhostFadeSeconds). */
	TMap<TWeakObjectPtr<AJRPGBattlePawn>, float> GhostOpacity;
	/** Até quando cada pawn segura o fantasma depois do teste o pegar (tempo real; AJRPGBattleArena::CameraGhostHoldSeconds). */
	TMap<TWeakObjectPtr<AJRPGBattlePawn>, double> GhostHoldUntil;
	/** O material do fantasma da arena da luta, carregado uma vez quando a cena abre (nulo = o dither). */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GhostMaterial;

	/** O canal de voz da luta (grito, fanfarra, grunhido do block). */
	UPROPERTY()
	FJRPGBattleVoices Voices;

	/** A música da luta entra (corte seco), guardando a do Field; sai e devolve a do Field. */
	void StartBattleMusic(const FJRPGBattleSetup& Setup);
	void RestoreFieldMusic();
	UPROPERTY()
	TObjectPtr<USoundBase> FieldMusic;
	bool bFieldMusicPersistent = false;
	bool bBattleMusicStarted = false;

	/** O make-way: os outros saem da linha entre Attacker e Target. */
	void MakeWay(const AJRPGBattlePawn* Attacker, const AJRPGBattlePawn* Target, int32 Frame);

	/** O recentrar em curso: quanto cada pawn anda no total, de que quadro, em quantos, e quanto já andou (0..1). */
	TMap<TWeakObjectPtr<AJRPGBattlePawn>, FVector> FormationShift;
	int32 FormationShiftStart = 0;
	int32 FormationShiftFrames = 1;
	float FormationShiftDone = 1.0f;

	/** Tela preta no começo: esconde o Field e monta. */
	void EnterBattleScene();
	/** Tela preta no fim: desmonta e mostra o Field. */
	void LeaveBattleScene();
	/** A vantagem de formação: o lado em desvantagem começa de costas (até agir ou apanhar). */
	void TurnAmbushedSideAway(AJRPGBattleArena& Current) const;

	/** A abertura: arma a varredura (ou nada, sem abertura na arena) antes de a cena avisar que está montada. */
	void BeginIntro();
	/** A varredura acabou: os nomes. */
	void EnterIntroNames();
	/** A abertura acabou: a ação que já começou entra no pawn e as regras andam. */
	void FinishIntro();
	/** Corta a abertura (a luta acabou no meio dela). */
	void CancelIntro();
	void SetIntroStage(EJRPGBattleIntroStage Stage);

	EJRPGBattleIntroStage IntroStage = EJRPGBattleIntroStage::None;
	double IntroStartTime = 0.0;
	FTimerHandle IntroTimer;

	void ChooseArena(const FJRPGBattleSetup& Setup);
	void ReleaseArena();
	float GetTransitionTime() const { return TransitionTime; }
	void Fade(bool bToBlack);

	TWeakObjectPtr<AJRPGBattleArena> Arena;
	/** A arena foi criada aqui (mapa sem arena) e morre com a luta. */
	bool bOwnsArena = false;
	bool bFieldHidden = false;

	/** O fade da arena da luta, guardado: a provisória morre antes da tela abrir no fim. */
	float TransitionTime = 0.35f;
	FLinearColor TransitionColor = FLinearColor::Black;

	FJRPGBattleSetup PendingSetup;
	TArray<FJRPGCombatant> PendingCombatants;

	EPhase Phase = EPhase::Idle;
	FTimerHandle TransitionTimer;

	/** A UI de comando está de pé: o prompt de debug na tela fica escondido (ver SetDebugPromptSuppressed). */
	bool bDebugPromptSuppressed = false;
};
