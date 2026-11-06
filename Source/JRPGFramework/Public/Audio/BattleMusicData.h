#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BattleMusicData.generated.h"

class USoundBase;
class USoundWave;

/**
 * FJRPGBattleMusicData
 * Uma música de luta (linha da DataTable DT_BattleMusic, gerada por Extras/ps1_battle_music.py do disco do usuário). RowName =
 * o nome que as lutas usam ("Battle1", "Boss1", "Songi"...): AJRPGBattleArena::BattleMusic, FJRPGBattleSetup::BattleMusic,
 * FJRPGBossData::Music. No original é o banco de luta 0x36F + SoundSet, cópia da faixa BgmId do music_01.
 *
 * Intro toca uma vez e Loop em seguida, em laço, sem emenda (UAudioSubsystem::GetIntroLoopSound). Sem Intro, só o Loop (que pode
 * ser qualquer som: um SoundCue próprio, por exemplo).
 */
USTRUCT(BlueprintType)
struct JRPGFRAMEWORK_API FJRPGBattleMusicData : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Battle Music")
	FString DisplayName;

	/** O índice no banco de luta do original (o "sound set" da cena, 0..9). Só referência. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Battle Music")
	int32 SoundSet = 0;

	/** A faixa do music_01 (o id 2000 + índice do sound test). Só referência. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Battle Music")
	int32 BgmId = 0;

	/** A introdução, uma vez; vazia = a música começa direto no laço. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Battle Music")
	TSoftObjectPtr<USoundWave> Intro;

	/** O laço (em loop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Battle Music")
	TSoftObjectPtr<USoundBase> Loop;
};
