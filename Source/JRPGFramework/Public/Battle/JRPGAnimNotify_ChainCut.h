#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "JRPGAnimNotify_ChainCut.generated.h"

/**
 * UJRPGAnimNotify_ChainCut
 * Marca no clip o ponto em que o jogo original corta o golpe quando há outra ação na fila do
 * turno: o próximo clip entra aqui, sem a volta à guarda. Não faz nada sozinho — quem toca uma
 * fila (a luta, o AJRPGArtTester) procura este notify e emenda o próximo passo nele. Sem ação na
 * fila, o clip toca até o fim.
 *
 * A regra é a do tick de animação da batalha (FUN_80047430, RE: monster-animation.md, "Event-frame
 * list"): com a trava do clip (+0x76) em 0 e uma ação atrás, ele termina assim que o quadro passa
 * do primeiro beat (+0x10) + 2. Os golpes de direção da party têm beat 7 e trava 0 (corte no
 * quadro 10); as Arts têm a trava ligada e tocam inteiras. Posto pelo
 * Extras/ue_import_monster_sounds.py, na trilha "Chain".
 */
UCLASS(meta = (DisplayName = "Chain Cut"))
class JRPGFRAMEWORK_API UJRPGAnimNotify_ChainCut : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override { return TEXT("Chain Cut"); }

	/** O instante do primeiro Chain Cut do clip, em segundos do asset; negativo se não há. */
	static float FindCutTime(const class UAnimSequenceBase* Clip);
};
