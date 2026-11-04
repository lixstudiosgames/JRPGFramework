#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "JRPGAnimNotify_Hit.generated.h"

/**
 * UJRPGAnimNotify_Hit
 * Marca no clip o quadro de um golpe: um notify por golpe, na ordem do HitPower / HitTarget da
 * linha do DT_Arts (o primeiro notify é o golpe 0). NÃO aplica dano: a luta lê esses quadros do
 * asset como dado e resolve cada golpe quando a timeline lógica chega nele (BATTLE_ROADMAP.md ›
 * Phase 5 › State machine); na apresentação, o notify só mostra o evento que a lógica já emitiu.
 *
 * O UJRPGArtsValidator confere que um clip com notifies de golpe tem tantos quanto o HitPower diz.
 * Trilha sugerida: "Hits".
 */
UCLASS(meta = (DisplayName = "Art Hit"))
class JRPGFRAMEWORK_API UJRPGAnimNotify_Hit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override { return TEXT("Hit"); }

	/** Os instantes dos golpes do clip, em segundos do asset, em ordem. Vazio se não há. */
	static TArray<float> FindHitTimes(const class UAnimSequenceBase* Clip);
};
