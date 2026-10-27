#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "JRPGBattleSettings.generated.h"

class AJRPGBattleStage;

/**
 * UJRPGBattleSettings
 * Project Settings > Plugins > JRPG Battle. Qual palco de batalha o jogo usa: todo mapa jogado
 * ganha um (UJRPGBattleStageSubsystem), a não ser que já tenha um posto à mão.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "JRPG Battle"))
class JRPGFRAMEWORK_API UJRPGBattleSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UJRPGBattleSettings();

	/**
	 * O palco criado em cada mapa: o BP_BattleStage do jogo, com os modelos de batalha da party.
	 * Vazio = o AJRPGBattleStage do plugin, sem modelo de herói nenhum.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Stage")
	TSoftClassPtr<AJRPGBattleStage> StageClass;

	/** Criar o palco sozinho nos mapas que não têm um. Desligado, só os mapas com palco posto lutam. */
	UPROPERTY(config, EditAnywhere, Category = "Stage")
	bool bSpawnStageAutomatically = true;
};
