#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "JRPGInteractable.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UJRPGInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * IJRPGInteractable
 * Qualquer coisa com que o jogador interage: NPC, porta, baú, save, loja...
 *
 * Blueprint: Class Settings → Implemented Interfaces → JRPG Interactable, e implemente o
 * evento Interact. O ator decide sozinho se aceita (baú já aberto, NPC que só fala depois de
 * um evento...): o jogador só avisa que interagiu.
 *
 * Para ser encontrado, o ator precisa de uma colisão que sobreponha a cápsula do personagem
 * (uma Box com overlap em Pawn): o alcance de cada um é o tamanho dela. Quem escolhe o alvo e
 * chama o Interact é o UJRPGInteractionComponent do personagem.
 */
class JRPGFRAMEWORK_API IJRPGInteractable
{
	GENERATED_BODY()

public:
	/** O jogador interagiu com este ator. InteractingActor = o personagem que apertou o botão. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "JRPG|Interaction")
	void Interact(AActor* InteractingActor);
};
