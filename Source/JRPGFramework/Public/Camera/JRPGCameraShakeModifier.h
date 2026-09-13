#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "Camera/CameraData.h"
#include "JRPGCameraShakeModifier.generated.h"

class UCameraSubsystem;

/**
 * UJRPGCameraShakeModifier
 * Shake procedural (Perlin noise por eixo) aplicado como UCameraModifier no
 * PlayerCameraManager — pós-processa o POV final, então funciona igual com a
 * câmera do pawn (BP) ou com a câmera gerenciada do CameraSubsystem.
 *
 * Uma única instância roda N shakes concorrentes. Criado e controlado apenas
 * pelo UCameraSubsystem (PlayCameraShake / StopAllCameraShakes) — não é
 * exposto a Blueprints.
 */
UCLASS()
class JRPGFRAMEWORK_API UJRPGCameraShakeModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	/** Inicia um shake. Duration = 0 no params significa loop até StopAll. */
	void AddShake(const FJRPGCameraShakeParams& Params, float Scale, ECameraShakePreset SourcePreset);

	/** Para todos os shakes: imediato (corte seco) ou com fade pelo BlendOutTime de cada um. */
	void StopAll(bool bImmediate);

	/** Há algum shake rodando? */
	bool HasActiveShakes() const { return ActiveShakes.Num() > 0; }

	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

	/** Dono — notificado quando cada shake termina (broadcast de OnCameraShakeFinished). */
	TWeakObjectPtr<UCameraSubsystem> OwnerSubsystem;

private:
	/** Um shake em andamento (POD interno — sem UPROPERTY de propósito). */
	struct FActiveShake
	{
		FJRPGCameraShakeParams Params;
		float Elapsed = 0.0f;
		float Scale = 1.0f;
		ECameraShakePreset SourcePreset = ECameraShakePreset::Custom;
		/** Seeds aleatórios por canal (LocXYZ, Pitch, Yaw, Roll, FOV) — dessincroniza os eixos. */
		float Seeds[7] = { 0 };
		/** true = fade-out de parada em andamento (StopAll suave ou fim de loop). */
		bool bStopping = false;
		/** Elapsed no momento em que o fade-out de parada começou. */
		float StopStartTime = 0.0f;
	};

	TArray<FActiveShake> ActiveShakes;
};
