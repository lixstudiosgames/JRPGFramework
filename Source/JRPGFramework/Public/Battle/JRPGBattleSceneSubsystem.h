#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Battle/BattleTypes.h"
#include "JRPGBattleSceneSubsystem.generated.h"

class AJRPGBattleArena;
class AJRPGBattlePawn;
class UBattleSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnJRPGBattleSceneEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGBattleSceneAssembled, AJRPGBattleArena*, Arena, const TArray<AJRPGBattlePawn*>&, Pawns);

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

	/** A arena da luta atual (pode ser a provisória); nula fora de luta. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle Scene")
	AJRPGBattleArena* GetArena() const { return Arena.Get(); }

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

	/** Um quadro lógico andou: os pawns tocam em compasso. */
	UFUNCTION()
	void HandleLogicFrame(int32 Frame);

	/** Tela preta no começo: esconde o Field e monta. */
	void EnterBattleScene();
	/** Tela preta no fim: desmonta e mostra o Field. */
	void LeaveBattleScene();

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
