#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "JRPGAnimNotify_LoopWindow.generated.h"

class UAnimSequenceBase;

/**
 * UJRPGAnimNotify_LoopWindow
 * O trecho do clip que o jogo original repete antes de seguir: a janela [+0x85, +0x86] da entrada de ação, Loops vezes (o +0x84;
 * RE battle-action.md, "+0x84 is the clip's loop count"). O tick de animação (FUN_80047430) volta o cursor do fim da janela para o
 * começo dela e gasta uma volta, até acabarem; então o clip segue até o fim. É o "agachado" do SpecialStarter do Vahn (quadros 13..14,
 * 5 vezes) e da Noa (7..8, 2 vezes), o Burning Flare, as finalizações de algumas Supers, as poses de vitória (255: até trocar de clip).
 *
 * Não faz nada sozinho: o UJRPGFieldEnemyAnimInstance repete o trecho ao tocar o clip, a timeline da luta soma a duração das voltas
 * (UBattleSubsystem, o ClipTiming) e a câmera própria das Arts lê as voltas que faltam. Posto pelo Extras/ue_import_monster_sounds.py
 * na trilha "Loop", do Extras/ps1_party_sounds.py.
 */
UCLASS(meta = (DisplayName = "Loop Window"))
class JRPGFRAMEWORK_API UJRPGAnimNotify_LoopWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** Quantas vezes o trecho repete (o +0x84 do original). 255 = até o clip ser trocado. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loop Window", meta = (ClampMin = "0", ClampMax = "255"))
	int32 Loops = 1;

	/** O valor de Loops que quer dizer "até trocar de clip". */
	static constexpr int32 Forever = 255;

	virtual FString GetNotifyName_Implementation() const override { return TEXT("Loop Window"); }

	/** O primeiro Loop Window do clip: o começo e o fim do trecho, em segundos do asset, e as voltas. Falso se o clip não tem. */
	static bool FindLoopWindow(const UAnimSequenceBase* Clip, float& OutStart, float& OutEnd, int32& OutLoops);
};
