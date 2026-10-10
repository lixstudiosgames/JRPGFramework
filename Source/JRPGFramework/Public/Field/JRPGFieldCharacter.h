#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Field/JRPGFieldTypes.h"
#include "JRPGFieldCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UJRPGMistDisturberComponent;
class UJRPGInteractionComponent;
class AJRPGCameraZone;

/**
 * AJRPGFieldCharacter
 * Personagem do Field: anda pelo mapa com a câmera de cima. Não tem nada de batalha.
 *
 * Só sabe se mover — quem lê o input é o AJRPGFieldPlayerController, que manda a direção
 * (AddMovementInput) e liga/desliga a corrida (SetSprinting). Assim o mesmo personagem serve
 * para ser possuído por uma IA depois (um membro da party seguindo o líder).
 *
 * Câmera em camadas:
 *  - BaseCamera: o padrão. No BeginPlay vem do AJRPGFieldMapSettings do mapa, se houver.
 *  - AJRPGCameraZone: pilha de zonas por cima da base. Vale a de maior Priority (empate: a
 *    última em que entrou); sair volta para a que continua valendo, e fora de todas, para a
 *    base. Zona Level Camera troca para uma câmera do level; Player Camera muda este braço.
 *  - Trocas entre configs do braço fazem blend (BlendInTime/FadeOutTime das zonas).
 * O mesmo vale para o controle (BaseControlMode, ou o da zona com bOverrideControl).
 *
 * Vem com UJRPGMistDisturberComponent (abre a névoa e deixa rastro), UJRPGInteractionComponent
 * (com quem interagir) e declara o Field ao entrar no mapa (WorldStateSubsystem::SetIsInField), o que libera o Save no menu de pausa.
 *
 * Uso: crie um Blueprint filho, ponha a malha e o Anim Blueprint, e use-o como Default Pawn
 * do GameMode junto com o AJRPGFieldPlayerController. Guia: Docs/Guides/PLAYER.md.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGFieldCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AJRPGFieldCharacter();

	/** Braço da câmera: rotação absoluta (do mundo), sem colisão. Configurado pela BaseCamera. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	/** Câmera de cima. O CameraSubsystem volta para ela (RestoreToPlayerCamera). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Camera")
	TObjectPtr<UCameraComponent> FieldCamera;

	/** Mexe a névoa da JRPGMist em volta do personagem e deixa o rastro. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Field")
	TObjectPtr<UJRPGMistDisturberComponent> MistDisturber;

	/** Escolhe com quem interagir (IJRPGInteractable) e chama o Interact dele. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "JRPG|Field")
	TObjectPtr<UJRPGInteractionComponent> Interaction;

	// --- Câmera e controle ---

	/**
	 * Câmera base (fora de qualquer AJRPGCameraZone). Substituída pela do AJRPGFieldMapSettings
	 * do mapa no BeginPlay. É o que aparece no viewport do Blueprint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Camera")
	FJRPGFieldCameraSettings BaseCamera;

	/** De onde vem o "cima" do controle fora das zonas. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Camera")
	EJRPGFieldControlMode BaseControlMode = EJRPGFieldControlMode::CameraRelative;

	/** Fixed Yaw: o yaw do mundo que é "cima" no controle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Camera", meta = (EditCondition = "BaseControlMode == EJRPGFieldControlMode::FixedYaw", EditConditionHides))
	float BaseControlYaw = 0.0f;

	/**
	 * Troca a câmera base em runtime (cutscene que muda a área, Level Blueprint...). Se não há
	 * zona ativa, aplica com blend; dentro de uma zona, vale ao sair dela.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Camera")
	void SetBaseCamera(const FJRPGFieldCameraSettings& NewCamera, float BlendTime = 0.5f);

	/** Troca o controle base em runtime. */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Camera")
	void SetBaseControl(EJRPGFieldControlMode NewMode, float NewControlYaw = 0.0f);

	/** A config do braço aplicada agora (no meio de um blend, o valor do momento). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Camera")
	FJRPGFieldCameraSettings GetCurrentCamera() const { return CurrentCamera; }

	/** A zona de câmera que vale agora (nullptr = câmera base). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Camera")
	AJRPGCameraZone* GetActiveCameraZone() const;

	/** O yaw que é "cima" no controle agora, dada a câmera que está na tela. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Camera")
	float GetControlReferenceYaw(float ScreenCameraYaw) const;

	/** Chamados pela AJRPGCameraZone. Idempotentes. */
	void EnterCameraZone(AJRPGCameraZone* Zone);
	void ExitCameraZone(AJRPGCameraZone* Zone);

	// --- Movimento ---

	/** Velocidade andando, em uu/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Movement", meta = (ClampMin = "0.0"))
	float WalkSpeed = 300.0f;

	/** Velocidade correndo, em uu/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Movement", meta = (ClampMin = "0.0"))
	float SprintSpeed = 700.0f;

	/** Desligado = o botão de correr não faz nada (World Map, cidades onde não se corre...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Movement")
	bool bCanSprint = true;

	/**
	 * Declara o Field ao começar (WorldStateSubsystem::SetIsInField(true)): este personagem só
	 * existe em mapas de campo. Desligue se o mapa declara por conta própria.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field")
	bool bDeclareFieldOnBeginPlay = true;

	/** Liga/desliga a corrida (o controller chama ao segurar/soltar o botão). */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Movement")
	void SetSprinting(bool bNewSprinting);

	/** True se está correndo agora (botão segurado e bCanSprint). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Movement")
	bool IsSprinting() const { return bSprinting; }

	/** Velocidade no chão agora, em uu/s (para o Anim Blueprint escolher idle/andar/correr). */
	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Movement")
	float GetGroundSpeed() const;

	/** Disparado quando a corrida liga ou desliga. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSprintChanged, bool, bIsSprinting);
	UPROPERTY(BlueprintAssignable, Category = "JRPG|Field|Movement")
	FOnSprintChanged OnSprintChanged;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Aplica WalkSpeed/SprintSpeed no CharacterMovement conforme o estado. */
	void ApplyMoveSpeed();

	/** Escreve uma config no braço e na câmera. */
	void ApplyCameraToComponents(const FJRPGFieldCameraSettings& Settings);

	/** Recalcula qual zona vale e leva a câmera até ela (BlendTime 0 = na hora). */
	void RefreshView(float BlendTime);

	/** Zona que vale agora, entre as que o personagem está dentro (nullptr = base). */
	AJRPGCameraZone* FindTopZone() const;

	bool bSprinting = false;

	/** Zonas em que o personagem está, na ordem em que entrou. */
	TArray<TWeakObjectPtr<AJRPGCameraZone>> CameraZones;

	/** Zona aplicada por último (para saber o que mudou). */
	TWeakObjectPtr<AJRPGCameraZone> AppliedZone;

	/** True enquanto a view está numa câmera do level (zona Level Camera). */
	bool bViewingLevelCamera = false;

	/** Braço: o valor de agora e o blend em andamento. */
	FJRPGFieldCameraSettings CurrentCamera;
	FJRPGFieldCameraSettings BlendFrom;
	FJRPGFieldCameraSettings BlendTo;
	float BlendDuration = 0.0f;
	float BlendElapsed = 0.0f;
};
