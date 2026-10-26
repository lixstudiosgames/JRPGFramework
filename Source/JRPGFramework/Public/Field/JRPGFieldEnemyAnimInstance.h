#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "JRPGFieldEnemyAnimInstance.generated.h"

class UAnimSequence;
struct FJRPGFieldEnemyAnimProxy;

/**
 * Nó raiz do UJRPGFieldEnemyAnimInstance: toca um clip e, quando ele troca, mistura o anterior com
 * o novo por BlendTime segundos (crossfade). Os dois entram como tick records com peso, então o
 * tempo, o loop e os AnimNotifies (os sons) são os da engine, como no modo single node.
 */
USTRUCT()
struct JRPGFRAMEWORK_API FAnimNode_JRPGCrossfade : public FAnimNode_Base
{
	GENERATED_BODY()

	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;

	FJRPGFieldEnemyAnimProxy* Proxy = nullptr;
};

/** Um clip tocando: a sequência e o tempo dele. */
struct FJRPGCrossfadeLayer
{
	UAnimSequence* Sequence = nullptr;
	float Time = 0.0f;
	float PlayRate = 1.0f;
	FMarkerTickRecord Marker;
	FDeltaTimeRecord DeltaRecord;
};

USTRUCT()
struct JRPGFRAMEWORK_API FJRPGFieldEnemyAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FJRPGFieldEnemyAnimProxy() = default;
	explicit FJRPGFieldEnemyAnimProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	FJRPGCrossfadeLayer Current;
	FJRPGCrossfadeLayer Previous;
	/** Peso do clip atual (0..1); o anterior leva o resto. */
	float BlendAlpha = 1.0f;
	float BlendTime = 0.2f;

	FAnimNode_JRPGCrossfade Node;
};

/**
 * UJRPGFieldEnemyAnimInstance
 * A animação dos inimigos do Field sem Anim Blueprint: troca de clip com mistura (Idle -> Walk ->
 * Run sem corte seco). Nativa e sem esqueleto fixo, então serve para todos os monstros — cada
 * monstro do PS1 tem o seu esqueleto, e um Anim Blueprint fica preso a um só.
 *
 * O AJRPGFieldEnemy usa esta classe quando o visual não traz AnimClass, e chama PlayClip a cada
 * frame com o clip que quer (Idle/Walk/Run pela velocidade).
 */
UCLASS(Transient, NotBlueprintable)
class JRPGFRAMEWORK_API UJRPGFieldEnemyAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Passa a tocar este clip (em loop), misturando com o anterior por BlendTime segundos. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Enemy")
	void PlayClip(UAnimSequence* Clip, float PlayRate = 1.0f, float BlendTime = 0.2f);

	UAnimSequence* GetRequestedClip() const { return RequestedClip; }
	float GetRequestedRate() const { return RequestedRate; }
	float GetRequestedBlendTime() const { return RequestedBlendTime; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	/**
	 * Os AnimNotify_PlaySound dos clips são da batalha (2D, ouvidos em todo lugar). No Field quem
	 * toca é o AJRPGFieldEnemy::PlayClipSound: 3D, raio curto, um por vez.
	 */
	virtual bool HandleNotify(const FAnimNotifyEvent& AnimNotifyEvent) override;

private:
	/** O clip pedido e o que está saindo: referências para o GC não coletar no meio da mistura. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> RequestedClip;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FadingClip;

	float RequestedRate = 1.0f;
	float RequestedBlendTime = 0.2f;
};
