#include "Field/JRPGPartyFollowerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"

void AJRPGPartyFollowerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Path following pela aceleração: chegando no fim do caminho ele freia, em vez de parar seco
	if (UPawnMovementComponent* Move = InPawn ? InPawn->GetMovementComponent() : nullptr)
	{
		Move->GetNavMovementProperties()->bUseAccelerationForPaths = true;
	}
}
