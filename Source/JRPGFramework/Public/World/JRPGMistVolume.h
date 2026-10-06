#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JRPGMistVolume.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class UArrowComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;

/** O que o volume desenha. */
UENUM(BlueprintType)
enum class EJRPGMistMode : uint8
{
	/** Névoa larga rente ao chão, com fios e redemoinhos. */
	GroundMist  UMETA(DisplayName = "Ground Mist"),
	/** Poucas linhas finas correndo numa direção, com trails: fumaça de túnel de vento. */
	FlowLines   UMETA(DisplayName = "Flow Lines"),
	/** Ondas circulares saindo do pivô em pulsos, com linhas radiais opcionais. */
	PulseRings  UMETA(DisplayName = "Pulse Rings"),
	/** Névoa de chão + Flow Lines juntas, levadas pelo mesmo vento (a direção do fluxo). */
	GroundAndFlow UMETA(DisplayName = "Ground + Flow")
};

/**
 * AJRPGMistVolume
 * Caixa de névoa viva (faixas e redemoinhos) injetada na Volumetric Fog do mapa.
 *
 * A caixa é um cubo com o material de domínio Volume M_JRPGMist (shader em
 * Shaders/JRPGMist.ush). Ela só SOMA densidade à volumetric fog que já existe no mapa
 * — no projeto, a height fog do Ultra Dynamic Sky, que precisa estar com Volumetric Fog
 * ligado. A luz vem das mesmas luzes que iluminam a volumetric fog.
 *
 * Setup: arraste para o level e APOIE NO TERRENO — o pivô do ator é o centro do chão da
 * névoa. O tamanho vem da escala do ator (gizmo de escala ou Scale no Details): escala 1 =
 * 100 uu. Default 40 x 40 x 4 = 4000 x 4000 x 400 uu. O contorno da caixa aparece no
 * editor (some no jogo). Os parâmetros atualizam o preview na hora.
 * Mode escolhe entre névoa de chão, Flow Lines (linhas de túnel de vento) e Pulse Rings
 * (ondas circulares saindo do pivô). No Flow Lines a direção é a seta do ator (gire em yaw)
 * ou, com FlowTarget, do ator para o alvo.
 *
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

	/**
	 * O que o volume desenha. Trocar o modo no Details carrega o preset dele (densidade,
	 * altura e os parâmetros do modo) e esconde as opções dos outros modos. Em runtime use
	 * ApplyModePreset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist")
	EJRPGMistMode Mode = EJRPGMistMode::GroundMist;

	/** Material Volume da névoa. Default: MI_JRPGMist_Default do plugin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist")
	TSoftObjectPtr<UMaterialInterface> MistMaterial;

	/** MPC escrita pelo UJRPGMistSubsystem com quem está mexendo a névoa. Default: MPC_JRPGMist do plugin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	// --- Aparência ---

	/**
	 * Extinção no chão, por METRO (a volumetric fog divide a extinção do material por 100).
	 * ~0.5 = véu leve, ~1 = névoa clara, 3+ = parede de névoa.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.0"))
	float Density = 1.0f;

	/**
	 * Albedo: quanto da luz a névoa espalha, por canal. Perto de 1 = branca (a cor final é a
	 * da luz que bate nela — lua, sol, lampiões do UDS). Abaixo de ~0.8 ela escurece e tinge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look")
	FLinearColor Albedo = FLinearColor(0.95f, 0.95f, 0.95f);

	/** Brilho próprio das faixas (0 = desligado). Dá o ar "místico" sem luz nenhuma. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look")
	FLinearColor RibbonGlow = FLinearColor(0.0f, 0.0f, 0.0f);

	/** Altura em uu em que a densidade cai para ~37%. Menor = mais rente ao chão. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (ClampMin = "1.0"))
	float HeightFalloff = 90.0f;

	/** Tamanho de um "tile" do ruído em uu. Maior = redemoinhos maiores. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (ClampMin = "1.0"))
	float NoiseScale = 2400.0f;

	/** Vento em uu/s (XY). Devagar: animação rápida borra com o temporal reprojection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (EditCondition = "Mode == EJRPGMistMode::GroundMist", EditConditionHides))
	FVector2D Wind = FVector2D(25.0f, 10.0f);

	/** Quanto o ruído lento entorta o rápido. É o que forma os redemoinhos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::GroundMist || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float WarpStrength = 0.5f;

	/** Velocidade de giro do campo de warp, em rad/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (EditCondition = "Mode == EJRPGMistMode::GroundMist || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float WarpSpin = 0.05f;

	/** Afinação das faixas. Maior = fios mais finos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.01", EditCondition = "Mode == EJRPGMistMode::GroundMist || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float RibbonSharpness = 10.0f;

	/** Peso dos fios sobre a névoa base (0 = só névoa suave). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Mode == EJRPGMistMode::GroundMist || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float RibbonAmount = 0.7f;

	/** Quanto os fios esticam na direção do vento. 1 = sem esticar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::GroundMist || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float RibbonStretch = 3.5f;

	/** Fração da área com fios (0..1). Fios em todo lugar parecem mármore. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Mode == EJRPGMistMode::GroundMist || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float RibbonCoverage = 0.55f;

	/** Largura em uu do esmaecimento nas bordas laterais da caixa (evita o "muro" de névoa). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Look", meta = (ClampMin = "1.0"))
	float EdgeFade = 400.0f;

	// --- Cenário (distance field; desligado no preset Low) ---

	/** Distância em uu até uma superfície em que a névoa começa a acumular e contornar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Scene", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::GroundMist || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float PoolDistance = 80.0f;

	/** Quanto a névoa engrossa colada nos objetos (0 = nada). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Scene", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::GroundMist || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float PoolAmount = 0.8f;

	/** Quanto o fluxo desvia pela tangente dos objetos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Scene", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::GroundMist || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float FlowAround = 0.6f;

	// --- Flow Lines (Mode = Flow Lines) ---

	/** Para onde as linhas vão. Vazio = na direção da seta do ator (gire o ator em yaw). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	TObjectPtr<AActor> FlowTarget;

	/** Velocidade dos traços em uu/s. Cada linha varia ±25%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float FlowSpeed = 300.0f;

	/** Distância em uu entre linhas vizinhas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float LineSpacing = 260.0f;

	/**
	 * Largura de cada linha em uu (ela ainda engrossa e afina com os redemoinhos). A volumetric
	 * fog tem resolução baixa (uma célula cobre ~16 px da tela): abaixo de ~30 uu borra.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float LineWidth = 55.0f;

	/** Força dos redemoinhos que empurram as linhas, em uu (0 = só as curvas em S). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float EddyStrength = 180.0f;

	/** Tamanho dos redemoinhos em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "100.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float EddySize = 5000.0f;

	/** Densidade das linhas em relação ao Density (no Ground + Flow, equilibra linhas e névoa). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float LineDensity = 1.0f;

	/**
	 * Ground + Flow: velocidade da névoa de chão como fração do FlowSpeed. A névoa larga
	 * rápida borra com o temporal reprojection da volumetric fog — por isso ela anda mais
	 * devagar que as linhas, mas na mesma direção.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float GroundDriftRatio = 0.3f;

	/** Fração das linhas que existem (0..1). Menor = poucas linhas soltas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float LineCoverage = 0.5f;

	/** Quanto as linhas curvam para os lados, em uu (0 = retas). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float LineCurve = 350.0f;

	/** Comprimento de uma curva (de um S) ao longo do fluxo, em uu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float CurveLength = 1600.0f;

	/** Velocidade com que as curvas mudam de forma, em rad/s (0 = curvas paradas). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float CurveDrift = 0.15f;

	/** Período de um traço em uu (traço + intervalo até o próximo na mesma linha). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float TrailLength = 1800.0f;

	/** Quanto do período o traço ocupa (0.05..1). 1 = linha quase contínua. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "0.05", ClampMax = "1.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float TrailFill = 0.75f;

	/** Distância em uu de um objeto em que as linhas começam a desviar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float AvoidDistance = 250.0f;

	/** Quanto as linhas desviam dos objetos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Flow", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::FlowLines || Mode == EJRPGMistMode::GroundAndFlow", EditConditionHides))
	float AvoidStrength = 1.0f;

	// --- Pulse Rings (Mode = Pulse Rings). O centro é o pivô do ator ---

	/** Segundos entre um pulso e o próximo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Pulse", meta = (ClampMin = "0.05", EditCondition = "Mode == EJRPGMistMode::PulseRings", EditConditionHides))
	float PulseInterval = 1.5f;

	/** Velocidade da onda em uu/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Pulse", meta = (EditCondition = "Mode == EJRPGMistMode::PulseRings", EditConditionHides))
	float PulseSpeed = 400.0f;

	/** Largura de cada anel em uu. Abaixo de ~40 a volumetric fog borra. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Pulse", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::PulseRings", EditConditionHides))
	float RingWidth = 70.0f;

	/** Raio em uu em que a onda termina de sumir. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Pulse", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::PulseRings", EditConditionHides))
	float PulseMaxRadius = 1800.0f;

	/** Quanto os anéis ondulam, em uu (0 = círculos perfeitos). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Pulse", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::PulseRings", EditConditionHides))
	float RingWiggle = 40.0f;

	/** Quantidade de linhas radiais (0 = só os anéis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Pulse", meta = (ClampMin = "0", EditCondition = "Mode == EJRPGMistMode::PulseRings", EditConditionHides))
	int32 RadialLines = 12;

	/** Intensidade das linhas radiais em relação aos anéis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Pulse", meta = (ClampMin = "0.0", EditCondition = "Mode == EJRPGMistMode::PulseRings", EditConditionHides))
	float RadialAmount = 0.4f;

	/** Afinação das linhas radiais. Maior = mais finas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Pulse", meta = (ClampMin = "1.0", EditCondition = "Mode == EJRPGMistMode::PulseRings", EditConditionHides))
	float RadialSharpness = 6.0f;

	/** Giro das linhas radiais em rad/s (0 = paradas). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Pulse", meta = (EditCondition = "Mode == EJRPGMistMode::PulseRings", EditConditionHides))
	float RadialSpin = 0.1f;

	// --- Interação ---

	/** Quanto a névoa abre em volta de quem passa (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Interaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClearStrength = 0.85f;

	/** Ângulo máximo, em radianos, do redemoinho deixado por quem passa. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Interaction")
	float SwirlStrength = 1.6f;

	/** Pivô do ator = centro do chão da névoa. A escala do ator define o tamanho. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Mist")
	TObjectPtr<USceneComponent> Root;

	/** O cubo com o material de volume (cubo de 100 uu, base no pivô). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Mist")
	TObjectPtr<UStaticMeshComponent> MistBox;

#if WITH_EDITORONLY_DATA
	/** Contorno da caixa, só no editor: o material de volume não aparece no viewport sozinho. */
	UPROPERTY(VisibleAnywhere, Category = "JRPG|Mist")
	TObjectPtr<UBoxComponent> EditorOutline;

	/** Direção do fluxo no modo Flow Lines (sem FlowTarget), só no editor. */
	UPROPERTY(VisibleAnywhere, Category = "JRPG|Mist")
	TObjectPtr<UArrowComponent> EditorFlowArrow;
#endif

	/** Direção do fluxo em XY (normalizada): para o FlowTarget, ou a seta do ator. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Mist")
	FVector2D GetFlowDirection() const;

	/** Troca o modo e carrega o preset dele (sobrescreve os valores daquele modo). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Mist")
	void ApplyModePreset(EJRPGMistMode NewMode);

	/** Reaplica todos os parâmetros no material. Chame depois de mudar valores em runtime (Blueprint). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Mist")
	void ApplyMistParameters();

	/** MPC carregada (para o subsistema). */
	UMaterialParameterCollection* GetParameterCollection() const;

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MistMID;
};
