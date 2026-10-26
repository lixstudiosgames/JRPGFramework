#include "Battle/BattleSubsystem.h"
#include "HAL/PlatformTime.h"

UBattleSubsystem::UBattleSubsystem()
{
}

void UBattleSubsystem::ApplyBattleItemEffect(FName CharacterName, const FItemData& ItemData)
{
	// TODO: Implementar lógica de aplicar buff de item temporário na batalha (Ex: Power Elixir)
	UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: Aplicando efeito temporario do item %s no combate para o personagem %s"), *ItemData.Name.ToString(), *CharacterName.ToString());
}

bool UBattleSubsystem::TryStartEncounter(const TArray<FJRPGEncounterCombatant>& Enemies)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastEncounterTime < EncounterGraceTime)
	{
		UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: encontro recusado (outro começou há %.1fs)"), Now - LastEncounterTime);
		return false;
	}
	if (Enemies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: encontro sem inimigos recusado — o encounter está vazio?"));
		return false;
	}

	LastEncounterTime = Now;
	UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: encontro com %d inimigo(s)"), Enemies.Num());
	OnEncounterStarted.Broadcast(Enemies);
	return true;
}
