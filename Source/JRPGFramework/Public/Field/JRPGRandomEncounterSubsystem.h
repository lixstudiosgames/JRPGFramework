#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "JRPGRandomEncounterSubsystem.generated.h"

class AJRPGEncounterZone;

/**
 * UJRPGRandomEncounterSubsystem
 * Os encontros aleatórios do original (FUN_801D9E1C, RE docs/formats/encounter.md), no Field: conta os passos do jogador dentro
 * das AJRPGEncounterZone com bRandomEncounters e, quando o contador global (UBattleSubsystem::GetEncounterCounter) chega a
 * zero, pede a luta à zona.
 *
 * - Um passo = entrar num tile vizinho (o tile tem o StepLength da zona; o do original, 128 unidades = 55.4 cm). Andar mais
 *   de um tile de uma vez (teletransporte, porta, warp) não é passo: não desconta nem sorteia.
 * - A zona do passo: a de maior Priority entre as que contêm o jogador (ContainsPoint), ligadas pelas flags; empate, a que
 *   se registrou primeiro. Fora de toda zona com taxa, nada conta (e nada desconta).
 * - Não conta nada (só guarda o tile) com menu ou diálogo aberto (WebUI fora de HudOnly), com o controle do Field
 *   desligado (porta, cena), durante uma luta ou com o Field escondido, e na carência de depois da luta
 *   (UBattleSubsystem::IsInEncounterGrace) — como os "steps that do not roll" do original.
 * - Na entrada do mapa, o top-up do contador (UBattleSubsystem::TopUpEncounterCounterOnMapEntry).
 *
 * Guia: Docs/Guides/FIELD_ENEMIES.md, "Random encounters".
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGRandomEncounterSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterZone(AJRPGEncounterZone* Zone);
	void UnregisterZone(AJRPGEncounterZone* Zone);

	/** A zona onde o jogador está agora (a que conta os passos), ou nulo. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|Random Encounters")
	AJRPGEncounterZone* GetCurrentZone() const { return CurrentZone.Get(); }

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** A zona com taxa que contém o ponto (maior Priority; empate, a primeira registrada). */
	AJRPGEncounterZone* FindZoneAt(const FVector& Point) const;
	/** Os holds do original: menu, diálogo, controle desligado, luta, Field escondido, carência. */
	bool IsRollHeld(const APawn* Player) const;

	TArray<TWeakObjectPtr<AJRPGEncounterZone>> Zones;
	TWeakObjectPtr<AJRPGEncounterZone> CurrentZone;
	FIntPoint LastTile = FIntPoint::ZeroValue;
	bool bHasTile = false;
};
