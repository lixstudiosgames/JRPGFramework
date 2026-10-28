#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Battle/BattleTypes.h"
#include "JRPGBattleSceneSubsystem.generated.h"

class AJRPGBattleArena;
class AJRPGBattlePawn;

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
 * Console (PIE): JRPG.Battle.Start <EnemyKey> [Quantos] [Level] e
 * JRPG.Battle.End [victory|defeat|escape|abort]. Guia: Docs/Guides/BATTLE_STAGE.md.
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
};
