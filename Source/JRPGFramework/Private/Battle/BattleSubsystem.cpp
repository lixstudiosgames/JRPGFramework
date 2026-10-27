#include "Battle/BattleSubsystem.h"
#include "Party/PartySubsystem.h"
#include "World/WorldStateSubsystem.h"
#include "Engine/GameInstance.h"
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
	if (bInBattle)
	{
		UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: encontro recusado (já há uma luta)"));
		return false;
	}
	const double Now = FPlatformTime::Seconds();
	if (Now - LastEncounterTime < EncounterGraceTime)
	{
		UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: encontro recusado (o último foi há %.1fs)"), Now - LastEncounterTime);
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

bool UBattleSubsystem::BeginBattle(const FJRPGBattleSetup& Setup)
{
	if (bInBattle)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: BeginBattle com uma luta em andamento — ignorado"));
		return false;
	}
	if (Setup.Enemies.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: BeginBattle sem inimigos — ignorado"));
		return false;
	}

	TArray<FName> Party = Setup.Party;
	if (Party.Num() == 0)
	{
		if (const UPartySubsystem* PartySubsystem = GetGameInstance()->GetSubsystem<UPartySubsystem>())
		{
			Party = PartySubsystem->GetActiveParty();
		}
	}
	if (Party.Num() == 0)
	{
		Party = FallbackParty;
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: nenhuma party ativa — usando a FallbackParty (%d)"), Party.Num());
	}
	if (Party.Num() > MaxPartySlots)
	{
		Party.SetNum(MaxPartySlots);
	}

	CurrentSetup = Setup;
	CurrentSetup.Party = Party;
	if (CurrentSetup.Enemies.Num() > MaxEnemySlots)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleSubsystem: %d inimigos, MaxEnemySlots %d — os de sobra ficam de fora"), CurrentSetup.Enemies.Num(), MaxEnemySlots);
		CurrentSetup.Enemies.SetNum(MaxEnemySlots);
	}

	Combatants.Reset();
	for (int32 i = 0; i < Party.Num(); ++i)
	{
		FJRPGCombatant& C = Combatants.AddDefaulted_GetRef();
		C.SlotIndex = Combatants.Num() - 1;
		C.Side = EJRPGBattleSide::Party;
		C.SideIndex = i;
		C.Type = EJRPGCombatantType::Character;
		C.Key = Party[i];
		if (const UPartySubsystem* PartySubsystem = GetGameInstance()->GetSubsystem<UPartySubsystem>())
		{
			FJRPGPartyMember Member;
			if (PartySubsystem->GetMember(Party[i], Member))
			{
				C.Level = Member.Level;
			}
		}
	}
	for (int32 i = 0; i < CurrentSetup.Enemies.Num(); ++i)
	{
		FJRPGCombatant& C = Combatants.AddDefaulted_GetRef();
		C.SlotIndex = Combatants.Num() - 1;
		C.Side = EJRPGBattleSide::Enemy;
		C.SideIndex = i;
		C.Type = EJRPGCombatantType::Enemy;
		C.Key = CurrentSetup.Enemies[i].EnemyKey;
		C.Level = CurrentSetup.Enemies[i].Level;
	}

	bInBattle = true;
	LastEncounterTime = FPlatformTime::Seconds();
	if (UWorldStateSubsystem* WorldState = GetGameInstance()->GetSubsystem<UWorldStateSubsystem>())
	{
		bWasInField = WorldState->IsInField();
		WorldState->SetIsInField(false);
	}

	UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: luta começou — %d herói(s) contra %d inimigo(s)"), Party.Num(), CurrentSetup.Enemies.Num());
	OnBattleStarted.Broadcast(CurrentSetup, Combatants);
	return true;
}

void UBattleSubsystem::EndBattle(EJRPGBattleResult Result)
{
	if (!bInBattle)
	{
		return;
	}
	bInBattle = false;
	// O jogador volta ao Field em cima de onde a luta começou: sem a carência, o inimigo do lado
	// começaria outra na mesma hora
	LastEncounterTime = FPlatformTime::Seconds();
	if (bWasInField)
	{
		if (UWorldStateSubsystem* WorldState = GetGameInstance()->GetSubsystem<UWorldStateSubsystem>())
		{
			WorldState->SetIsInField(true);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("BattleSubsystem: luta acabou (%s)"), *StaticEnum<EJRPGBattleResult>()->GetNameStringByValue(static_cast<int64>(Result)));
	OnBattleEnded.Broadcast(Result);
}
