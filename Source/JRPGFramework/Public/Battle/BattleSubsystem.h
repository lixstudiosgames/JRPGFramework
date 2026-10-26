#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Inventory/ItemData.h"
#include "Battle/JRPGEncounterTypes.h"
#include "BattleSubsystem.generated.h"

/** Um encontro começou: os inimigos sorteados para a luta. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnJRPGEncounterStarted, const TArray<FJRPGEncounterCombatant>&, Enemies);

/**
 * UBattleSubsystem
 * Orquestra a batalha. Por enquanto: a porta de entrada dos encontros (TryStartEncounter) e o
 * efeito de item em combate (stub).
 *
 * Não conhece AActor nenhum — quem está no mapa (o inimigo do Field, depois o palco da
 * batalha) fala com ele por chamadas e delegates. Ver Docs/BATTLE_ROADMAP.md.
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
	 * Pede para começar um encontro com estes inimigos. Recusa (false) se outro começou há menos
	 * de EncounterGraceTime — dois inimigos encostando no mesmo instante, ou o jogador ainda em
	 * cima do segundo depois do primeiro. Aceito, dispara OnEncounterStarted.
	 *
	 * Hoje a luta não existe ainda (Fase 4): o encontro só é anunciado.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	bool TryStartEncounter(const TArray<FJRPGEncounterCombatant>& Enemies);

	/** Segundos, depois de um encontro aceito, em que outro é recusado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Battle", meta = (ClampMin = "0.0"))
	float EncounterGraceTime = 3.0f;

	UPROPERTY(BlueprintAssignable, Category = "JRPG|Battle")
	FOnJRPGEncounterStarted OnEncounterStarted;

private:
	/** FPlatformTime::Seconds() do último encontro aceito (relógio real: vale com o jogo pausado). */
	double LastEncounterTime = -1.0e9;
};
