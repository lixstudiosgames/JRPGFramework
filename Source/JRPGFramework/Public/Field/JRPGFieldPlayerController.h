#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "JRPGFieldPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * AJRPGFieldPlayerController
 * Controller do Field: lê o input (Enhanced Input) e move o pawn.
 *
 * - Move: por padrão relativo à câmera que está na tela (a do personagem, uma câmera do
 *   level, uma JRPGCameraZone...) — "cima" é "para longe da câmera", em qualquer mapa ou
 *   ângulo. Com AJRPGFieldCharacter, o mapa ou a zona podem trocar para um yaw fixo do mundo
 *   (cavernas, câmeras fixas). Funciona com qualquer pawn; com AJRPGFieldCharacter também corre.
 * - Segura a direção quando a câmera muda (bHoldDirectionOnCameraChange): enquanto o
 *   direcional continua apertado, "cima" continua sendo o de quando começou a andar — trocar
 *   de câmera fixa no meio do caminho não vira o personagem para trás.
 * - Sprint: segurar corre (AJRPGFieldCharacter::SetSprinting).
 * - Menu: abre o menu do jogo (UWebUISubsystem::OpenMenu) — só a partir do HUD; com o menu
 *   aberto quem recebe o input é a UI, e é ela que fecha.
 * - Interact: no APERTO do botão, interage com o alvo do UJRPGInteractionComponent do pawn.
 *   Segurar não repete: no controle o botão é o mesmo de correr, e chegar correndo num NPC
 *   com ele segurado não interage — só soltando e apertando de novo.
 *
 * Input pronto sem nenhum asset: se MappingContext ou as ações estiverem vazios, o controller
 * cria os padrões em runtime (WASD, setas e analógico esquerdo para andar; Shift e o botão de
 * baixo do controle para correr; E e o mesmo botão de baixo para interagir; Tab e o botão de
 * cima para o menu). Para trocar teclas ou usar o remapeamento
 * do Enhanced Input, crie os assets e ponha nos campos do Blueprint filho.
 *
 * Guarda a direção do personagem na rotação de controle: o save grava a rotação de controle,
 * e sem isto o personagem voltava de um load olhando para o lado errado.
 *
 * Guia: Docs/Guides/PLAYER.md.
 */
UCLASS(Blueprintable)
class JRPGFRAMEWORK_API AJRPGFieldPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AJRPGFieldPlayerController();

	/** Contexto do Field. Vazio = o padrão criado em runtime (ver acima). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	/** Andar (Axis2D: X = direita, Y = frente). Vazio = o padrão. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Input")
	TObjectPtr<UInputAction> MoveAction;

	/** Correr (Bool, segurar). Vazio = o padrão. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Input")
	TObjectPtr<UInputAction> SprintAction;

	/**
	 * Interagir (Bool, no aperto). Vazio = o padrão. Se dividir o botão com o SprintAction,
	 * desligue bConsumeInput nas duas ações para o mesmo botão chegar às duas.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Input")
	TObjectPtr<UInputAction> InteractAction;

	/** Abrir o menu do jogo (Bool, no aperto). Vazio = o padrão. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Input")
	TObjectPtr<UInputAction> MenuAction;

	/**
	 * Abre o menu do jogo, se a UI está só no HUD (sem loja, diálogo ou outra tela aberta).
	 * É o que o MenuAction chama; dá para chamar de um Blueprint também.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Input")
	void OpenGameMenu();

	/**
	 * Enquanto o direcional segue apertado, mantém o "cima" de quando começou a andar, mesmo se
	 * a câmera trocar (zona com câmera fixa, blend de ângulo). Soltou, vale a câmera nova.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|Field|Input")
	bool bHoldDirectionOnCameraChange = true;

	/** Prioridade do contexto do Field (menus por cima usam uma maior). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "JRPG|Field|Input")
	int32 MappingPriority = 0;

	/**
	 * Liga/desliga o controle do personagem (cutscene, diálogo, menu): tira o contexto do Field,
	 * para o personagem e solta a corrida. A UI continua recebendo input.
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Field|Input")
	void SetFieldInputEnabled(bool bEnabled);

	/** True se o contexto do Field está ativo. */
	UFUNCTION(BlueprintPure, Category = "JRPG|Field|Input")
	bool IsFieldInputEnabled() const { return bFieldInputEnabled; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	/** Cria em runtime o que estiver vazio (contexto e ações padrão). */
	void EnsureDefaultInput();

	void HandleMove(const FInputActionValue& Value);
	void HandleMoveCompleted(const FInputActionValue& Value);

	/** O yaw que é "cima" agora (câmera na tela, ou o fixo do mapa/zona). */
	float GetControlReferenceYaw() const;
	void HandleSprintStarted(const FInputActionValue& Value);
	void HandleInteract(const FInputActionValue& Value);
	void HandleMenu(const FInputActionValue& Value);
	void HandleSprintCompleted(const FInputActionValue& Value);

	bool bFieldInputEnabled = true;

	/** Direcional apertado e o "cima" guardado no começo (bHoldDirectionOnCameraChange). */
	bool bMoveHeld = false;
	float HeldReferenceYaw = 0.0f;
};
