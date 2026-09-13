#include "Battle/BattleSubsystem.h"

UBattleSubsystem::UBattleSubsystem()
{
}

void UBattleSubsystem::ApplyBattleItemEffect(FName CharacterName, const FItemData& ItemData)
{
	// TODO: Implementar lógica de aplicar buff de item temporário na batalha (Ex: Power Elixir)
	UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: Aplicando efeito temporario do item %s no combate para o personagem %s"), *ItemData.Name.ToString(), *CharacterName.ToString());
}
