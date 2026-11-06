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
	/** Em loop (o idle, o andar) ou de uma vez só (o golpe, a reação): o de uma vez só para no último quadro e fica nele. */
	bool bLooping = true;
	/** O número do pedido que o pôs a tocar (UJRPGFieldEnemyAnimInstance::GetRequestSerial): o mesmo clip pedido de novo recomeça. */
	int32 Serial = 0;
	/** O trecho que o clip repete antes de seguir (UJRPGAnimNotify_LoopWindow): começo, fim (segundos) e voltas que faltam (0 = nenhuma). */
	float LoopStart = 0.0f;
	float LoopEnd = 0.0f;
	int32 LoopsLeft = 0;
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
	/**
	 * Passa a tocar este clip, misturando com o anterior por BlendTime segundos. bLoop falso toca uma vez e fica no último quadro
	 * (a luta toca golpes e reações assim, na mesma instância do idle: trocar a malha para o modo single node e voltar deixava a
	 * Noa parada depois de uma Art); um clip de uma vez pedido de novo recomeça do quadro 0, sem mistura.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Enemy")
	void PlayClip(UAnimSequence* Clip, float PlayRate = 1.0f, float BlendTime = 0.2f, bool bLoop = true);

	UAnimSequence* GetRequestedClip() const { return RequestedClip; }
	float GetRequestedRate() const { return RequestedRate; }
	float GetRequestedBlendTime() const { return RequestedBlendTime; }
	bool GetRequestedLooping() const { return bRequestedLooping; }
	int32 GetRequestSerial() const { return RequestSerial; }

	/**
	 * O clip que toca agora: o tempo dele (segundos do asset, já com as voltas do trecho de laço) e as voltas que faltam (o +0x21B do
	 * original, que a câmera própria das Arts lê). Falso sem clip.
	 */
	bool GetCurrentClipCursor(const UAnimSequence* Clip, float& OutSeconds, int32& OutLoopsLeft, bool& bOutHasLoopWindow) const;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	/**
	 * Os AnimNotify_PlaySound dos clips. No Field quem toca é o AJRPGFieldEnemy::PlayClipSound: 3D, raio curto, um por vez. Na
	 * luta é o AJRPGBattlePawn::HandleClipSound: o som perto de um golpe só toca se o golpe conectou (um Miss não faz o som do
	 * impacto); os outros (voz, passo) tocam como o notify manda. Qualquer outro dono cai no notify da engine.
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
	bool bRequestedLooping = true;
	/** Sobe a cada pedido de um clip de uma vez só: o proxy recomeça o clip quando o número muda, mesmo sendo o mesmo asset. */
	int32 RequestSerial = 0;
};
