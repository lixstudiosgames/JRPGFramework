#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "JRPGFieldEnemyController.generated.h"

/**
 * AJRPGFieldEnemyController
 * Controller dos inimigos que andam pelo Field (AJRPGFieldEnemy).
 *
 * Não tem lógica: as decisões (passear, perseguir, voltar) ficam no próprio inimigo, que conhece
 * a sua zona e o seu encontro. É um AAIController pelo path following (o MoveTo no navmesh).
 */
UCLASS(NotBlueprintable, Transient)
class JRPGFRAMEWORK_API AJRPGFieldEnemyController : public AAIController
{
	GENERATED_BODY()

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
