#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JRPGMistDisturberComponent.generated.h"

/**
 * UJRPGMistDisturberComponent
 * Faz o dono mexer a névoa da JRPGMist: abre um círculo em volta dele e, andando,
 * gira a névoa ao redor (redemoinho).
 *
 * Vai no BP de quem se MOVE: o jogador, NPCs, inimigos, um barril com física.
 * O cenário estático não precisa disto — o shader lê o distance field.
 *
 * A força escala com a velocidade do dono (GetVelocity): parado não mexe, a não ser que
 * IdleStrength > 0. O pawn controlado pelo jogador deixa rastro sozinho (desligue
 * bLeavesTrail para não deixar); os outros ocupam um slot cada. Quem entra nos 8 slots
 * do shader é decidido por UJRPGMistSubsystem — o custo não cresce com o número de
 * componentes.
 *
 * Sem tick próprio: só se registra no subsistema.
 */
UCLASS(ClassGroup = (JRPG), meta = (BlueprintSpawnableComponent))
class JRPGFRAMEWORK_API UJRPGMistDisturberComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJRPGMistDisturberComponent();

	/** Raio em uu da área afetada. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist", meta = (ClampMin = "1.0"))
	float Radius = 220.0f;

	/** Força máxima (atingida em FullSpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist", meta = (ClampMin = "0.0"))
	float Strength = 1.0f;

	/** Força com o dono parado. 0 = parado não mexe a névoa. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist", meta = (ClampMin = "0.0"))
	float IdleStrength = 0.0f;

	/** Abaixo desta velocidade (uu/s) conta como parado. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist", meta = (ClampMin = "0.0"))
	float MinSpeed = 20.0f;

	/** Velocidade (uu/s) em que a força chega ao máximo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist", meta = (ClampMin = "1.0"))
	float FullSpeed = 350.0f;

	/** Escala do redemoinho deste dono (0 = só abre, não gira). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist", meta = (ClampMin = "0.0"))
	float SwirlScale = 1.0f;

	/**
	 * Deixa um rastro que se fecha devagar, pintado numa textura que acompanha o jogador: segue
	 * o caminho exato, qualquer curva. Vale para o jogador e para NPCs (cada um com a sua config
	 * de Trail); NPC a mais de TrailAreaSize / 2 do jogador não deixa rastro. O jogador manda na
	 * área e na resolução da textura (TrailAreaSize, TrailResolution, bDebugDrawTrail).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail")
	bool bLeavesTrail = true;

	/** Segundos até o rastro se fechar (o centro; as bordas, mais fracas, fecham antes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail", meta = (ClampMin = "0.2"))
	float TrailLifetime = 4.0f;

	/**
	 * Fração do TrailLifetime em que o caminho fica aberto por inteiro (0..0.98): a névoa não
	 * entra nele. Depois disso ele fecha suave até o fim da vida. 0 = começa a fechar logo que
	 * o jogador passa (a névoa trazida pelo vento vai enchendo o caminho aos poucos).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail", meta = (ClampMin = "0.0", ClampMax = "0.98"))
	float TrailHold = 0.8f;

	/** Quanto o rastro alarga e amacia até sumir, como fração do Radius (0 = não alarga). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail", meta = (ClampMin = "0.0"))
	float TrailWidening = 0.8f;

	/**
	 * Borda do rastro (0..1). 0 = suave, a abertura cai devagar da linha para fora; 1 = faixa
	 * aberta por igual com borda marcada. A largura não muda: a borda fica onde o rastro abre
	 * pela metade, então Radius, Strength e TrailWidening continuam mandando no tamanho.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrailEdgeHardness = 0.6f;

	/**
	 * Lado em uu da área em volta do jogador que guarda o rastro (a textura anda com ele). O
	 * rastro que sai da área some. Com o tempo de vida padrão, 6000 cobre ~15 m de cada lado.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail", meta = (ClampMin = "500.0"))
	float TrailAreaSize = 6000.0f;

	/**
	 * Resolução da textura do rastro (32..512). Texel = TrailAreaSize / isto: com o padrão,
	 * ~31 uu — mais fino que a volumetric fog consegue mostrar. Mais alto = mais CPU e upload.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail", meta = (ClampMin = "32", ClampMax = "512"))
	int32 TrailResolution = 192;

	/**
	 * Desenha o rastro no mundo como o shader lê: um ponto por texel pintado (verde = aberto,
	 * vermelho = fechando) e o contorno da área da textura (ciano). Só em builds com debug draw
	 * (editor/development, não Shipping).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail")
	bool bDebugDrawTrail = false;

	/** Força atual (0..Strength) pela velocidade do dono. */
	float GetCurrentStrength(FVector& OutVelocity) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
