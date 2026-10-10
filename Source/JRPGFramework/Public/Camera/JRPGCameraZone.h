#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Field/JRPGFieldTypes.h"
#include "JRPGCameraZone.generated.h"

class UBoxComponent;
class UCameraSubsystem;

/** O que a zona faz com a câmera. */
UENUM(BlueprintType)
enum class EJRPGCameraZoneMode : uint8
{
	/** Troca a view para uma câmera posicionada à mão no level (LevelCamera). */
	LevelCamera UMETA(DisplayName = "Level Camera"),
	/** Continua na câmera do personagem, mudando braço/ângulo/FOV (PlayerCamera). */
	PlayerCamera UMETA(DisplayName = "Player Camera")
};

/**
 * AJRPGCameraZone
 * Box trigger de câmera pronto para arrastar no level:
 * - Player ENTRA no box  → a câmera da zona (Level Camera: uma câmera posicionada à mão;
 *   Player Camera: o braço do personagem com outro ângulo/distância/FOV), com blend.
 * - Player SAI do box    → volta para o que valia antes (FadeOutTime; 0 = corte seco).
 *
 * Com o AJRPGFieldCharacter as zonas formam uma pilha: vale a de maior Priority e, empatando,
 * a última em que ele entrou. Sair de uma zona volta para a que continua valendo — num
 * corredor de zonas encostadas, sair da anterior não desfaz a da frente — e, fora de todas,
 * para a câmera base do mapa (AJRPGFieldMapSettings). Nascer dentro de uma zona (load de
 * save) já começa com a câmera dela. Outros pawns: só Level Camera, entra e sai sem pilha.
 *
 * Setup: arraste a zone para o level, redimensione o box e escolha o Mode.
 * Detecta o player SEM tag: compara o ator que entrou com o pawn possuído.
 * Pode ser usado direto ou como base de Blueprint (Blueprintable).
 */
UCLASS(Blueprintable, BlueprintType)
class JRPGFRAMEWORK_API AJRPGCameraZone : public AActor
{
	GENERATED_BODY()

public:
	AJRPGCameraZone();

	/** O que a zona faz: trocar para uma câmera do level ou mudar a câmera do personagem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera")
	EJRPGCameraZoneMode Mode = EJRPGCameraZoneMode::LevelCamera;

	/** Level Camera: a câmera posicionada no level usada enquanto o player está dentro. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera", meta = (EditCondition = "Mode == EJRPGCameraZoneMode::LevelCamera", EditConditionHides))
	TObjectPtr<AActor> LevelCamera;

	/** Player Camera: a câmera do personagem dentro da zona (AJRPGFieldCharacter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera", meta = (EditCondition = "Mode == EJRPGCameraZoneMode::PlayerCamera", EditConditionHides))
	FJRPGFieldCameraSettings PlayerCamera;

	/** Zonas sobrepostas: vale a de maior prioridade; empatando, a última em que entrou. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera|Control", meta = (InlineEditConditionToggle))
	bool bOverrideControl = false;

	/**
	 * De onde vem o "cima" do controle dentro da zona (AJRPGFieldCharacter). Fixed Yaw para
	 * cavernas e câmeras fixas: o controle segue o mapa, não o ângulo da câmera.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera|Control", meta = (EditCondition = "bOverrideControl"))
	EJRPGFieldControlMode ControlMode = EJRPGFieldControlMode::FixedYaw;

	/** Fixed Yaw: o yaw do mundo que é "cima" no controle dentro da zona. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera|Control", meta = (EditCondition = "bOverrideControl && ControlMode == EJRPGFieldControlMode::FixedYaw"))
	float ControlYaw = 0.0f;

	/** Blend da IDA para a câmera da zona ao entrar no box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Camera")
	float BlendInTime = 0.75f;

	/** Blend da VOLTA para a câmera anterior ao sair do box (0 = corte seco). */
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

	/**
	 * Liga a câmera da zona (idempotente — guarda por bZoneActive). Com o AJRPGFieldCharacter,
	 * entra na pilha de zonas dele; com outro pawn, troca direto para a LevelCamera.
	 */
	void ActivateZoneCamera(AActor* PlayerPawn);

	/** Desliga: sai da pilha, ou devolve a câmera ao player (no-op se a zona não estava ativa). */
	void DeactivateZoneCamera(AActor* PlayerPawn);

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
