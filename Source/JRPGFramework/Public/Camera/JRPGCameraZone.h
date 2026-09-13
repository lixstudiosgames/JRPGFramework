#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "JRPGCameraZone.generated.h"

class UBoxComponent;
class UCameraSubsystem;

/**
 * AJRPGCameraZone
 * Box trigger de câmera pronto para arrastar no level:
 * - Player ENTRA no box  → a view troca para a LevelCamera (câmera que você
 *   posicionou à mão no mapa), com blend.
 * - Player SAI do box    → a view volta para a câmera do player (FadeOutTime;
 *   0 = corte seco).
 *
 * Setup: arraste a zone para o level, redimensione o box, e no Details aponte
 * LevelCamera para um CameraActor/CineCamera colocado onde você quiser.
 * Detecta o player SEM tag: compara o ator que entrou com o pawn possuído.
 * Pode ser usado direto ou como base de Blueprint (Blueprintable).
 */
UCLASS(Blueprintable, BlueprintType)
class JRPGFRAMEWORK_API AJRPGCameraZone : public AActor
{
	GENERATED_BODY()

public:
	AJRPGCameraZone();

	/** Câmera do level usada enquanto o player está dentro do box (obrigatório). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera")
	TObjectPtr<AActor> LevelCamera;

	/** Blend da IDA para a LevelCamera ao entrar no box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera")
	float BlendInTime = 0.75f;

	/** Blend da VOLTA para o player ao sair do box (0 = corte seco). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera")
	float FadeOutTime = 0.75f;

	/**
	 * true = fechar uma UI (loja/menu) dentro da zona também devolve a câmera
	 * ao player. Default false: quem manda na câmera é entrar/sair do box.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera")
	bool bAutoRestoreOnUIClose = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Camera")
	TObjectPtr<UBoxComponent> TriggerBox;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	/** Liga a câmera da zona (idempotente — guarda por bZoneActive). */
	void ActivateZoneCamera();

	/** Desliga e devolve a câmera ao player (no-op se a zona não estava ativa). */
	void DeactivateZoneCamera();

	/**
	 * Sincroniza o estado da zona com a posição REAL do pawn. Necessário porque
	 * o load de save teleporta o player DEPOIS do BeginPlay (PostLoadMap + retry)
	 * e o evento de overlap do teleporte pode se perder — sem isto, dar load
	 * dentro da zona deixava o estado trocado até entrar/sair de novo.
	 * Roda por timer nos primeiros segundos do mapa e depois se desliga.
	 */
	void SyncPlayerOverlap();

	/** O ator é o pawn possuído pelo player? (sem depender de tag) */
	bool IsPlayerPawn(const AActor* Actor) const;

	UCameraSubsystem* GetCameraSubsystem() const;

	FTimerHandle SyncTimerHandle;
	int32 SyncChecksRemaining = 0;

	/** true = a câmera da zona está aplicada (o player está dentro). */
	bool bZoneActive = false;
};
