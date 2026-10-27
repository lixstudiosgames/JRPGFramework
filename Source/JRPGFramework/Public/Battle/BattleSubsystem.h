#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Inventory/ItemData.h"
#include "Battle/JRPGEncounterTypes.h"
#include "Battle/BattleTypes.h"
#include "BattleSubsystem.generated.h"

/** Um encontro começou: os inimigos sorteados para a luta. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGEncounterStarted, const TArray<FJRPGEncounterCombatant>&, Enemies);

/** Uma luta começou: o pedido e os combatentes (heróis primeiro, depois inimigos). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJRPGBattleStarted, const FJRPGBattleSetup&, Setup, const TArray<FJRPGCombatant>&, Combatants);

/** A luta acabou. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGBattleEnded, EJRPGBattleResult, Result);

/**
 * UBattleSubsystem
 * Orquestra a batalha. Hoje: a porta de entrada dos encontros (TryStartEncounter), o começo e o
 * fim de uma luta (BeginBattle / EndBattle) e o efeito de item em combate (stub). Os turnos, o
 * dano e a vitória são a Fase 5.
 *
 * Não conhece AActor nenhum — quem está no mapa fala com ele por chamadas e delegates: o inimigo
 * do Field pede a luta, o AJRPGBattleStage escuta OnBattleStarted / OnBattleEnded e monta e
 * desmonta a cena. Ver Docs/BATTLE_ROADMAP.md.
 *
 * Console (PIE): JRPG.Battle.Start <EnemyKey> [Quantos] [Level] e JRPG.Battle.End
 * [victory|defeat|escape|abort] — ficam no AJRPGBattleStage, que sabe onde está o jogador.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UBattleSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UBattleSubsystem();

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
	 * Começa uma luta: monta a lista de combatentes (a party de Setup.Party, ou a ativa; os
	 * inimigos de Setup.Enemies), tira o jogador do Field (o SAVE some do menu) e dispara
	 * OnBattleStarted. False se já há uma luta ou se não há inimigo nenhum.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	bool BeginBattle(const FJRPGBattleSetup& Setup);

	/** Encerra a luta atual e dispara OnBattleEnded. Sem luta, não faz nada. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void EndBattle(EJRPGBattleResult Result);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	bool IsInBattle() const { return bInBattle; }

	/** Os combatentes da luta atual: heróis primeiro, depois inimigos. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	const TArray<FJRPGCombatant>& GetCombatants() const { return Combatants; }

	/** O pedido da luta atual (ou da última). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Battle")
	const FJRPGBattleSetup& GetBattleSetup() const { return CurrentSetup; }

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

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGEncounterStarted OnEncounterStarted;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGBattleStarted OnBattleStarted;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGBattleEnded OnBattleEnded;

private:
	/** FPlatformTime::Seconds() do último encontro aceito / luta encerrada (relógio real: vale com o jogo pausado). */
	double LastEncounterTime = -1.0e9;

	bool bInBattle = false;
	/** Estava no Field quando a luta começou (devolve o SAVE ao acabar). */
	bool bWasInField = false;

	FJRPGBattleSetup CurrentSetup;
	TArray<FJRPGCombatant> Combatants;
};
