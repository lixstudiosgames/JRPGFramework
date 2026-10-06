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
	 * Deixa um rastro que se fecha devagar. Só vale para o pawn controlado pelo jogador — o
	 * rastro ocupa vários slots do shader, então NPCs nunca deixam rastro.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail")
	bool bLeavesTrail = true;

	/** Segundos até o rastro se fechar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail", meta = (ClampMin = "0.2"))
	float TrailLifetime = 4.0f;

	/** Quanto o rastro alarga até sumir (0 = mesma largura, 1 = dobra). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Mist|Trail", meta = (ClampMin = "0.0"))
	float TrailWidening = 0.8f;

	/** Força atual (0..Strength) pela velocidade do dono. */
	float GetCurrentStrength(FVector& OutVelocity) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
