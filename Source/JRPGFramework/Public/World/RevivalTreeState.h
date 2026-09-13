#pragma once

#include "CoreMinimal.h"
#include "RevivalTreeState.generated.h"

/**
 * ERevivalTreeStage
 * Estágio de estado/purificação de uma Genesis Tree (Revival Tree).
 */
UENUM(BlueprintType)
enum class ERevivalTreeStage : uint8
{
	Idle          UMETA(DisplayName = "Idle"),
	Awake         UMETA(DisplayName = "Awake"),
	Grow          UMETA(DisplayName = "Grow"),
	Purification  UMETA(DisplayName = "Purification"),
	FullRestored  UMETA(DisplayName = "Full Restored")
};

/**
 * FRevivalTreeState
 * Estrutura representando o estado atual de uma Genesis Tree.
 */
USTRUCT(BlueprintType)
struct FRevivalTreeState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|World")
	FName TreeID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JRPG|World")
	ERevivalTreeStage Stage = ERevivalTreeStage::Idle;
};
