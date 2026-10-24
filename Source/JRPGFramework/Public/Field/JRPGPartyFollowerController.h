#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "JRPGPartyFollowerController.generated.h"

/**
 * AJRPGPartyFollowerController
 * Controller dos membros da party que andam atrás do líder no Field.
 *
 * Não tem lógica: o seguir (para onde ir, quando sair e quando parar, teleporte de segurança)
 * fica no UJRPGFieldPartyComponent, que conhece o líder e a formação inteira. É um AAIController
 * pelo path following (o MoveTo no navmesh, modo Leash); no modo Trail o componente anda com
 * AddMovementInput. É ele também que pega o antigo líder quando o jogador troca de personagem (R3).
 */
UCLASS(NotBlueprintable, Transient)
class JRPGFRAMEWORK_API AJRPGPartyFollowerController : public AAIController
{
	GENERATED_BODY()

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
