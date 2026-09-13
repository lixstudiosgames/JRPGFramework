#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraData.h"
#include "JRPGCameraActor.generated.h"

/**
 * AJRPGCameraActor
 * Câmera gerenciada do UCameraSubsystem — uma única instância por mapa,
 * spawnada transient sob demanda e reusada por todos os focos. Morre junto
 * com o mapa (o subsystem só guarda weak ptr e respawna no mundo novo).
 *
 * O tick faz o trabalho contínuo (follow com lag, órbita, enquadramento de
 * grupo) e valida o alvo TODO frame: alvo destruído dispara OnFollowTargetLost
 * e o subsystem restaura a câmera do player.
 */
UCLASS(NotPlaceable)
class JRPGFRAMEWORK_API AJRPGCameraActor : public ACameraActor
{
	GENERATED_BODY()

public:
	AJRPGCameraActor(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;

	// --- Chamados apenas pelo UCameraSubsystem (C++) ---

	/** Enquadra um alvo com o preset dado. Posiciona imediato (snap) — o blend visual é do SetViewTargetWithBlend. */
	void SetupFocus(AActor* Target, const FCameraPresetRow& Preset);

	/** Enquadra um grupo de atores (bounds combinados cabem no FOV com o padding dado). */
	void SetupGroup(const TArray<AActor*>& Targets, const FCameraPresetRow& BasePreset, float Padding);

	/** Começa a orbitar o alvo do foco atual (graus/segundo; negativo inverte o sentido). */
	void StartOrbit(float DegPerSec);

	/** Congela a órbita mantendo o foco no ângulo atual. */
	void StopOrbit();

	/** Volta ao modo inerte (câmera parada onde está). */
	void ClearMode();

	/** O alvo do foco (ou todos do grupo) foi destruído — o subsystem restaura o player. */
	FSimpleMulticastDelegate OnFollowTargetLost;

private:
	/** Modo interno — nunca exposto a BP (enum plain de propósito). */
	enum class EManagedCameraMode : uint8
	{
		Idle,
		Focus,
		Orbit,
		Group
	};

	/** Pose desejada para o foco atual; false se o alvo morreu. */
	bool ComputeFocusView(FVector& OutPos, FRotator& OutRot) const;

	/** Pose desejada para o grupo (poda mortos); false se não sobrou ninguém. */
	bool ComputeGroupView(FVector& OutPos, FRotator& OutRot);

	/** Aplica a pose com interpolação (LagSpeed > 0) ou snap. */
	void ApplyView(const FVector& Pos, const FRotator& Rot, float DeltaSeconds, float LagSpeed);

	/** FOV + profundidade de campo (foco cravado no personagem, fundo desfocado). */
	void ApplyLensSettings(const FCameraPresetRow& Preset);

	EManagedCameraMode Mode = EManagedCameraMode::Idle;
	FCameraPresetRow ActivePreset;
	TWeakObjectPtr<AActor> FollowTarget;
	TArray<TWeakObjectPtr<AActor>> GroupTargets;
	float GroupPadding = 1.15f;
	float OrbitSpeedDegPerSec = 0.0f;
	float OrbitYawAccum = 0.0f;
};
