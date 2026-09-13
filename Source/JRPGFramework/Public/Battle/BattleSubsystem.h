#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Inventory/ItemData.h"
#include "BattleSubsystem.generated.h"

/**
 * UBattleSubsystem
 * Stub do subsistema que orquestra a lógica de batalha, turnos e combate.
 */
UCLASS(BlueprintType, Blueprintable)
class JRPGFRAMEWORK_API UBattleSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UBattleSubsystem();

	/**
	 * Aplica o efeito temporário de um item no combate (Shield Elixir, Power Elixir, etc.).
	 */
	UFUNCTION(BlueprintCallable, Category = "JRPG|Battle")
	void ApplyBattleItemEffect(FName CharacterName, const FItemData& ItemData);
};
