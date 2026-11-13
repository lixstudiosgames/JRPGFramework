#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VFX/JRPGVFXTypes.h"
#include "JRPGVFXRecipe.generated.h"

/**
 * UJRPGVFXRecipe
 * Um efeito visual como dado: a lista de camadas (FJRPGVFXLayer) que o UJRPGVFXSubsystem toca numa linha de tempo. A forma
 * do efeito vive nos materiais (malhas com shader) e nos sistemas Niagara que as camadas apontam; a receita só diz o que
 * entra, quando, apoiado em quem e com que parâmetros. Asset do projeto (Content Browser › Miscellaneous › Data Asset ›
 * JRPGVFXRecipe); o plugin não traz nenhuma.
 *
 * Exemplo, um mini tornado de fogo no alvo: 2 camadas Mesh (os funis, com o material do fogo e a faixa "Scale" abrindo),
 * 1 Niagara (as brasas em espiral), 1 Light (o clarão laranja, faixa "Intensity" em Pulse), 1 CameraShake.
 */
UCLASS(BlueprintType)
class JRPGFRAMEWORK_API UJRPGVFXRecipe : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Quanto o efeito dura, em segundos. 0 = o fim da última camada com duração própria. Sem efeito numa receita em loop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe", meta = (ClampMin = "0.0"))
	float Duration = 0.0f;

	/**
	 * O efeito fica até o StopEffect (uma aura, um status): as camadas com Duration 0 não terminam e as faixas delas repetem
	 * a cada volta (GetLength). As camadas com duração própria tocam uma vez, no começo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	bool bLooping = false;

	/**
	 * Num StopEffect suave, em quanto tempo o efeito some: as luzes apagam, as malhas recebem o FadeParameter de 1 a 0 e o
	 * Niagara para de emitir. 0 = some na hora.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe", meta = (ClampMin = "0.0"))
	float FadeOutTime = 0.25f;

	/** O parâmetro escalar que os materiais das camadas Mesh recebem durante o FadeOutTime (1 = inteiro, 0 = sumiu). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FName FadeParameter = TEXT("JRPGVFXFade");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe", meta = (TitleProperty = "Label"))
	TArray<FJRPGVFXLayer> Layers;

	/** A duração do efeito (uma volta, se em loop): Duration, ou o fim da última camada com duração própria. Nunca 0. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|VFX")
	float GetLength() const;
};
