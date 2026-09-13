#pragma once

#include "CoreMinimal.h"
#include "EventFlag.generated.h"

/**
 * FEventFlag
 * Estrutura que representa uma flag de evento de história ou diálogo.
 */
USTRUCT(BlueprintType)
struct FEventFlag
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|World")
	FName FlagID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|World")
	bool bValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|World")
	FString Metadata;
};
