#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JRPGMistVolume.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;

/**
 * AJRPGMistVolume
 * Caixa de névoa viva (faixas e redemoinhos) injetada na Volumetric Fog do mapa.
 *
 * A caixa é um cubo com o material de domínio Volume M_JRPGMist (shader em
 * Shaders/JRPGMist.ush). Ela só SOMA densidade à volumetric fog que já existe no mapa
 * — no projeto, a height fog do Ultra Dynamic Sky, que precisa estar com Volumetric Fog
 * ligado. A luz vem das mesmas luzes que iluminam a volumetric fog.
 *
 * Setup: arraste para o level, ajuste BoxExtent para cobrir a área andável (a base da
 * caixa é o chão da névoa) e mexa nos parâmetros — o preview atualiza no editor.
 * Quem mexe a névoa (jogador, NPCs) precisa de UJRPGMistDisturberComponent; o cenário
 * estático é lido sozinho pelo distance field.
 *
 * Guia: Docs/Guides/MIST.md.
 */
UCLASS(Blueprintable, BlueprintType)
class JRPGFRAMEWORK_API AJRPGMistVolume : public AActor
{
	GENERATED_BODY()

public:
	AJRPGMistVolume();

	/** Meia-extensão da caixa em uu. A base da caixa é o chão da névoa. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist", meta = (ClampMin = "10.0"))
	FVector BoxExtent = FVector(2000.0f, 2000.0f, 200.0f);

	/** Material Volume da névoa. Default: MI_JRPGMist_Default do plugin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist")
	TSoftObjectPtr<UMaterialInterface> MistMaterial;

	/** MPC escrita pelo UJRPGMistSubsystem com quem está mexendo a névoa. Default: MPC_JRPGMist do plugin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	// --- Aparência ---

	/** Densidade (extinction) no chão. Pequena: a volumetric fog acumula ao longo do raio. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.0"))
	float Density = 0.02f;

	/** Albedo: cor que a névoa espalha da luz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look")
	FLinearColor Albedo = FLinearColor(0.55f, 0.65f, 0.9f);

	/** Brilho próprio das faixas (0 = desligado). Dá o ar "místico" sem luz nenhuma. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look")
	FLinearColor RibbonGlow = FLinearColor(0.0f, 0.0f, 0.0f);

	/** Altura em uu em que a densidade cai para ~37%. Menor = mais rente ao chão. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look", meta = (ClampMin = "1.0"))
	float HeightFalloff = 90.0f;

	/** Tamanho de um "tile" do ruído em uu. Maior = redemoinhos maiores. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look", meta = (ClampMin = "1.0"))
	float NoiseScale = 2400.0f;

	/** Vento em uu/s (XY). Devagar: animação rápida borra com o temporal reprojection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look")
	FVector2D Wind = FVector2D(25.0f, 10.0f);

	/** Quanto o ruído lento entorta o rápido. É o que forma os redemoinhos. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.0"))
	float WarpStrength = 0.5f;

	/** Velocidade de giro do campo de warp, em rad/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look")
	float WarpSpin = 0.05f;

	/** Afinação das faixas. Maior = fios mais finos. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.01"))
	float RibbonSharpness = 10.0f;

	/** Peso dos fios sobre a névoa base (0 = só névoa suave). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RibbonAmount = 0.7f;

	/** Quanto os fios esticam na direção do vento. 1 = sem esticar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look", meta = (ClampMin = "1.0"))
	float RibbonStretch = 3.5f;

	/** Fração da área com fios (0..1). Fios em todo lugar parecem mármore. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RibbonCoverage = 0.55f;

	/** Largura em uu do esmaecimento nas bordas laterais da caixa (evita o "muro" de névoa). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Look", meta = (ClampMin = "1.0"))
	float EdgeFade = 400.0f;

	// --- Cenário (distance field; desligado no preset Low) ---

	/** Distância em uu até uma superfície em que a névoa começa a acumular e contornar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Scene", meta = (ClampMin = "1.0"))
	float PoolDistance = 80.0f;

	/** Quanto a névoa engrossa colada nos objetos (0 = nada). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Scene", meta = (ClampMin = "0.0"))
	float PoolAmount = 0.8f;

	/** Quanto o fluxo desvia pela tangente dos objetos. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Scene", meta = (ClampMin = "0.0"))
	float FlowAround = 0.6f;

	// --- Interação ---

	/** Quanto a névoa abre em volta de quem passa (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Interaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClearStrength = 0.85f;

	/** Ângulo máximo, em radianos, do redemoinho deixado por quem passa. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Mist|Interaction")
	float SwirlStrength = 1.6f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Mist")
	TObjectPtr<UStaticMeshComponent> MistBox;

	/** Reaplica todos os parâmetros no material (chame após mudar valores em runtime). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Mist")
	void ApplyMistParameters();

	/** MPC carregada (para o subsistema). */
	UMaterialParameterCollection* GetParameterCollection() const;

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MistMID;
};
