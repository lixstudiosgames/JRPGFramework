#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VFX/JRPGVFXTypes.h"
#include "JRPGVFXSubsystem.generated.h"

class UJRPGVFXRecipe;
class UMeshComponent;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;

/** O estado de uma camada de um efeito tocando (fora da reflexão: os componentes são do ator hospedeiro ou do pool do Niagara). */
struct FJRPGVFXLayerState
{
	/** O componente da camada: UNiagaraComponent, UStaticMeshComponent ou UPointLightComponent. Nulo em BodyParam / CameraShake. */
	TWeakObjectPtr<USceneComponent> Component;
	/** O ator cujos materiais a camada BodyParam escreve. */
	TWeakObjectPtr<AActor> BodyActor;
	/** A escala no mundo com que a camada nasceu (a faixa "Scale" multiplica isto). */
	FVector BaseScale = FVector::OneVector;
	bool bStarted = false;
	bool bDone = false;
};

/** Um efeito tocando. */
USTRUCT()
struct FJRPGVFXInstance
{
	GENERATED_BODY()

	int32 Id = 0;

	UPROPERTY()
	TObjectPtr<const UJRPGVFXRecipe> Recipe;

	UPROPERTY()
	FJRPGVFXContext Context;

	TArray<FJRPGVFXLayerState> Layers;
	float Elapsed = 0.0f;
	/** StopEffect suave pedido: o efeito some em FadeOutTime. */
	bool bStopping = false;
	float StopElapsed = 0.0f;
};

/**
 * UJRPGVFXSubsystem
 * Toca os efeitos visuais do jogo a partir de receitas (UJRPGVFXRecipe): cada efeito é uma linha de tempo de camadas —
 * malhas com shader (a forma), sistemas Niagara (as partículas), luzes, parâmetros no corpo dos atores e shakes de câmera.
 * O subsistema cria, move, anima e recolhe os componentes; a aparência vem dos assets que a receita aponta.
 *
 * Quem dispara: a apresentação da luta e os clips (UJRPGAnimNotify_VFX), nos pontos em que o original dispara os efeitos
 * dele. A lógica da luta não sabe que isto existe, e a aleatoriedade daqui vem de um stream cosmético, nunca do Rng da luta.
 *
 * Niagara: os user parameters abaixo são escritos em toda camada Niagara, se o sistema os tiver —
 *   Color (LinearColor) e EffectScale (float): do contexto, no disparo;
 *   TargetPosition e InstigatorPosition (Position): o centro do alvo e de quem causou, a cada frame (um enxame que voa até
 *   o herói segue o herói).
 *
 * As malhas e as luzes vêm de um pool num ator hospedeiro por mundo; o Niagara usa o pool dele.
 *
 * Console (PIE): JRPG.VFX.Play <Id do DT_VFX | caminho da receita> [escala] toca no pawn do jogador;
 * JRPG.VFX.StopAll para tudo.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGVFXSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** O subsistema do mundo de um objeto (nulo sem mundo). */
	static UJRPGVFXSubsystem* Get(const UObject* WorldContextObject);

	/** Toca a receita. O recibo só importa para parar um efeito em loop; um efeito comum termina sozinho. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|VFX")
	FJRPGVFXHandle PlayEffect(const UJRPGVFXRecipe* Recipe, const FJRPGVFXContext& Context);

	/** Toca a receita da linha EffectId do DT_VFX. Recibo inválido se a linha ou a receita não existem. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|VFX")
	FJRPGVFXHandle PlayEffectById(FName EffectId, const FJRPGVFXContext& Context);

	/** A receita da linha EffectId do DT_VFX (carrega se preciso); nula se não há. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|VFX")
	static UJRPGVFXRecipe* FindRecipe(FName EffectId);

	/** Para o efeito: suave (some no FadeOutTime da receita) ou na hora. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|VFX")
	void StopEffect(FJRPGVFXHandle Handle, bool bImmediate = false);

	/** Para tudo (fim de luta, troca de cena). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|VFX")
	void StopAllEffects(bool bImmediate = true);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "JRPG|VFX")
	bool IsEffectPlaying(FJRPGVFXHandle Handle) const;

	/** Quantos efeitos estão tocando (testes e debug). */
	int32 GetNumActiveEffects() const { return Instances.Num(); }

	// UWorldSubsystem / FTickableGameObject
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	/** O preview de animação também toca os efeitos dos notifies. */
	virtual bool IsTickableInEditor() const override { return true; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	/** Onde uma camada nasce. */
	struct FAnchorPoint
	{
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		/** A quem a camada se prende (nulo = ao mundo). */
		USceneComponent* Parent = nullptr;
		FName Socket;
		AActor* Actor = nullptr;
		/** O fator de bScaleByAnchorSize (1 sem ele). */
		float SizeScale = 1.0f;
	};

	static AActor* AnchorActor(const FJRPGVFXLayer& Layer, const FJRPGVFXContext& Context);
	static FAnchorPoint ResolveAnchor(const FJRPGVFXLayer& Layer, const FJRPGVFXContext& Context);
	/** O centro do volume do ator (Fallback sem ator). */
	static FVector ActorCenter(const AActor* Actor, const FVector& Fallback);

	void StartLayer(FJRPGVFXInstance& Instance, int32 LayerIndex);
	/** Escreve as faixas da camada no tempo Alpha (0..1), com o Fade do StopEffect suave (1 = inteiro). */
	void UpdateLayer(FJRPGVFXInstance& Instance, int32 LayerIndex, float Alpha, float Fade);
	/** Deixa o corpo no valor final das faixas e devolve o componente (bImmediate: as partículas vivas somem junto). */
	void EndLayer(FJRPGVFXInstance& Instance, int32 LayerIndex, bool bImmediate);
	void EndInstance(FJRPGVFXInstance& Instance, bool bImmediate);
	void ReleaseComponent(FJRPGVFXLayerState& State, bool bImmediate);

	/** O tempo 0..1 da camada no instante Elapsed do efeito. */
	static float LayerAlpha(const UJRPGVFXRecipe& Recipe, const FJRPGVFXLayer& Layer, float Elapsed);

	AActor* EnsureHost();
	UStaticMeshComponent* AcquireMesh();
	UPointLightComponent* AcquireLight();
	void ReleaseMesh(UStaticMeshComponent* Component);
	void ReleaseLight(UPointLightComponent* Component);
	/** Põe o componente do pool no ponto de apoio (e preso a ele, se a camada pede). */
	static void PlaceComponent(USceneComponent& Component, const FJRPGVFXLayer& Layer, const FAnchorPoint& Point, const FTransform& World);

	static void SetBodyScalar(AActor* Actor, FName Name, float Value);
	static void SetBodyColor(AActor* Actor, FName Name, const FLinearColor& Value);

	UPROPERTY(Transient)
	TArray<FJRPGVFXInstance> Instances;

	/** O dono das malhas e luzes dos efeitos (um por mundo, criado no primeiro uso). */
	UPROPERTY(Transient)
	TObjectPtr<AActor> Host;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FreeMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> FreeLights;

	/** Aleatoriedade só de aparência. Nunca o Rng da luta. */
	FRandomStream CosmeticRng;

	int32 NextId = 1;
};
