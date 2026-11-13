#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "JRPGAnimNotify_VFX.generated.h"

class UJRPGVFXRecipe;

/**
 * UJRPGAnimNotify_VFX
 * Dispara um efeito (UJRPGVFXSubsystem) num quadro do clip: o ponto em que o original dispara o efeito dele. Quem toca o clip
 * é o Instigator; o Target é o alvo da ação em curso (AJRPGBattlePawn::GetActionTarget) com bOnActionTarget, ou o próprio
 * dono. Fora da luta (preview de animação, outro ator) o efeito sai no dono.
 *
 * Um notify não guarda o recibo: receitas em loop são recusadas aqui (ficariam para sempre).
 * Trilha sugerida: "VFX".
 */
UCLASS(meta = (DisplayName = "Battle VFX"))
class JRPGFRAMEWORK_API UJRPGAnimNotify_VFX : public UAnimNotify
{
	GENERATED_BODY()

public:
	UJRPGAnimNotify_VFX();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	/** A receita a tocar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	TObjectPtr<UJRPGVFXRecipe> Recipe;

	/** Sem Recipe: a linha do DT_VFX (é o que os notifies importados do disco usam). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FName EffectId;

	/** O Target do efeito é o alvo da ação em curso (um golpe acertando); desligado, é quem toca o clip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	bool bOnActionTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VFX", meta = (ClampMin = "0.01"))
	float Scale = 1.0f;
};
