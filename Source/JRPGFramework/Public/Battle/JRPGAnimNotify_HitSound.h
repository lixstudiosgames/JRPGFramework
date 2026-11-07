#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "JRPGAnimNotify_HitSound.generated.h"

class USoundBase;

/**
 * UJRPGAnimNotify_HitSound
 * O som do impacto de um golpe, que depende do resultado dele: na luta (AJRPGBattlePawn) toca o HitSound se o golpe conectou
 * (Damage ou Block) e o MissSound (opcional) se ele errou pela altura do alvo. É a forma explícita do que o pawn já faz com o
 * PlaySound da engine perto de um golpe (AJRPGBattlePawn::HitSoundLeadFrames / HitSoundLagFrames): para clips novos, onde o
 * notify é posto no quadro do impacto e não deve depender de uma janela. Pertence ao golpe mais próximo, no passo que toca.
 *
 * Fora da luta (o preview do editor, um pawn que não é de batalha) toca o HitSound como o PlaySound toca. Um visual com o
 * AnimClass próprio (FJRPGBattleVisual::AnimClass) toca os notifies da engine sozinho, sem o pawn: aí só este notify é
 * condicionado ao golpe. Os parâmetros são os do Play Sound da engine.
 */
UCLASS(meta = (DisplayName = "Battle Hit Sound"))
class JRPGFRAMEWORK_API UJRPGAnimNotify_HitSound : public UAnimNotify
{
	GENERATED_BODY()

public:
	UJRPGAnimNotify_HitSound();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	/** O som do impacto: toca quando o golpe conecta (Damage ou Block). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Sound")
	TObjectPtr<USoundBase> HitSound;

	/** O som de quando o golpe erra (opcional): nulo = silêncio, ou o MissSound do pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Sound")
	TObjectPtr<USoundBase> MissSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Sound")
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Sound")
	float PitchMultiplier = 1.0f;

	/** O som segue a malha (preso ao bone AttachName, se posto). Como no Play Sound da engine (UE 5.8), AttachName só vale com isto ligado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Sound")
	bool bFollow = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Sound")
	FName AttachName;
};
