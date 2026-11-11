#include "Battle/JRPGEncounterTypes.h"
#include "Battle/BossFightData.h"
#include "Core/CoreSubsystem.h"
#include "Engine/DataTable.h"

namespace
{
	/** Põe um inimigo da linha Index na luta, com level sorteado, e conta em Taken. */
	void AddCombatant(TArray<FJRPGEncounterCombatant>& Out, TArray<int32>& Taken, const TArray<FJRPGEncounterEnemy>& Lines, int32 Index)
	{
		const FJRPGEncounterEnemy& Line = Lines[Index];
		FJRPGEncounterCombatant& C = Out.AddDefaulted_GetRef();
		C.EnemyKey = Line.EnemyKey;
		C.Level = FMath::RandRange(Line.LevelMin, FMath::Max(Line.LevelMin, Line.LevelMax));
		++Taken[Index];
	}
}

TArray<FJRPGEncounterCombatant> UJRPGEncounterLibrary::RollEncounter(const FJRPGEncounterSetup& Setup)
{
	TArray<FJRPGEncounterCombatant> Out;
	const TArray<FJRPGEncounterEnemy>& Lines = Setup.Enemies;
	TArray<int32> Taken;
	Taken.Init(0, Lines.Num());

	// Os principais entram sempre
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		const FJRPGEncounterEnemy& Line = Lines[i];
		if (!Line.bAlwaysJoins || Line.EnemyKey.IsNone())
		{
			continue;
		}
		const int32 Count = FMath::RandRange(Line.CountMin, FMath::Max(Line.CountMin, Line.CountMax));
		for (int32 n = 0; n < Count; ++n)
		{
			AddCombatant(Out, Taken, Lines, i);
		}
	}

	// O resto completa o total por peso, sem passar do CountMax de cada linha
	const int32 Target = FMath::RandRange(Setup.EnemiesPerBattleMin, FMath::Max(Setup.EnemiesPerBattleMin, Setup.EnemiesPerBattleMax));
	while (Out.Num() < Target)
	{
		int32 TotalWeight = 0;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			const FJRPGEncounterEnemy& Line = Lines[i];
			if (!Line.bAlwaysJoins && !Line.EnemyKey.IsNone() && Line.Weight > 0 && Taken[i] < FMath::Max(Line.CountMax, 1))
			{
				TotalWeight += Line.Weight;
			}
		}
		if (TotalWeight <= 0)
		{
			break;
		}

		int32 Pick = FMath::RandRange(1, TotalWeight);
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			const FJRPGEncounterEnemy& Line = Lines[i];
			if (Line.bAlwaysJoins || Line.EnemyKey.IsNone() || Line.Weight <= 0 || Taken[i] >= FMath::Max(Line.CountMax, 1))
			{
				continue;
			}
			Pick -= Line.Weight;
			if (Pick <= 0)
			{
				AddCombatant(Out, Taken, Lines, i);
				break;
			}
		}
	}

	return Out;
}

const UDataTable* UJRPGEncounterLibrary::GetEnemyTable()
{
	// Cache fraco: sobrevive entre chamadas sem segurar a tabela se o editor a recarregar
	static TWeakObjectPtr<const UDataTable> Cached;
	if (!Cached.IsValid())
	{
		Cached = UCoreSubsystem::ResolveDataTable(TEXT("DT_Enemies"));
	}
	return Cached.Get();
}

bool UJRPGEncounterLibrary::GetEnemyData(FName EnemyKey, FJRPGEnemyData& OutData)
{
	const UDataTable* Table = GetEnemyTable();
	if (!Table || EnemyKey.IsNone())
	{
		return false;
	}
	if (const FJRPGEnemyData* Row = Table->FindRow<FJRPGEnemyData>(EnemyKey, TEXT("UJRPGEncounterLibrary::GetEnemyData"), false))
	{
		OutData = *Row;
		return true;
	}
	return false;
}

bool UJRPGEncounterLibrary::FindBossFight(const TArray<FJRPGEncounterCombatant>& Enemies, FJRPGBossData& OutBoss)
{
	bool bAnyBoss = false;
	TArray<FName> Keys;
	for (const FJRPGEncounterCombatant& Enemy : Enemies)
	{
		FJRPGEnemyData Row;
		bAnyBoss |= GetEnemyData(Enemy.EnemyKey, Row) && Row.bIsBoss;
		Keys.Add(Enemy.EnemyKey);
	}
	if (!bAnyBoss)
	{
		return false;
	}
	Keys.Sort(FNameLexicalLess());

	const FJRPGBossData* Found = nullptr;
	if (const UDataTable* Bosses = UCoreSubsystem::ResolveDataTable(TEXT("DT_Bosses")))
	{
		Bosses->ForeachRow<FJRPGBossData>(TEXT("FindBossFight"), [&](const FName&, const FJRPGBossData& Boss)
		{
			if (Found || Boss.Enemies.Num() != Keys.Num())
			{
				return;
			}
			TArray<FName> BossKeys = Boss.Enemies;
			BossKeys.Sort(FNameLexicalLess());
			if (BossKeys == Keys)
			{
				Found = &Boss;
			}
		});
	}
	if (Found)
	{
		OutBoss = *Found;
	}
	return Found != nullptr;
}

bool UJRPGEncounterLibrary::ResolveScriptedFight(const TArray<FJRPGEncounterCombatant>& Enemies)
{
	bool bAnyBoss = false;
	for (const FJRPGEncounterCombatant& Enemy : Enemies)
	{
		FJRPGEnemyData Row;
		bAnyBoss |= GetEnemyData(Enemy.EnemyKey, Row) && Row.bIsBoss;
	}
	if (!bAnyBoss)
	{
		return false;
	}
	FJRPGBossData Boss;
	if (FindBossFight(Enemies, Boss))
	{
		return Boss.bScriptedFight;
	}
	UE_LOG(LogTemp, Warning, TEXT("UJRPGEncounterLibrary: luta com chefe (%s) sem linha no DT_Bosses — luta roteirizada por padrão"),
		*FString::JoinBy(Enemies, TEXT(", "), [](const FJRPGEncounterCombatant& Enemy) { return Enemy.EnemyKey.ToString(); }));
	return true;
}

FJRPGEncounterSetup UJRPGEncounterLibrary::MakeSingleEnemyEncounter(FName EnemyKey, int32 LevelMin, int32 LevelMax)
{
	FJRPGEncounterSetup Setup;
	if (EnemyKey.IsNone())
	{
		return Setup;
	}
	FJRPGEncounterEnemy& Enemy = Setup.Enemies.AddDefaulted_GetRef();
	Enemy.EnemyKey = EnemyKey;
	Enemy.LevelMin = FMath::Max(1, LevelMin);
	Enemy.LevelMax = FMath::Max(Enemy.LevelMin, LevelMax);
	Enemy.CountMin = 1;
	Enemy.CountMax = 1;
	Enemy.bAlwaysJoins = true;
	Setup.EnemiesPerBattleMin = 1;
	Setup.EnemiesPerBattleMax = 1;
	return Setup;
}

TArray<FString> UJRPGEncounterLibrary::GetEnemyKeyOptions()
{
	TArray<FString> Out;
	if (const UDataTable* Table = GetEnemyTable())
	{
		for (const FName& Name : Table->GetRowNames())
		{
			Out.Add(Name.ToString());
		}
	}
	Out.Sort();
	return Out;
}

// =================================================================================================
// Encontros aleatórios: o contador de passos do original (FUN_801D9E1C)
// =================================================================================================

int32 UJRPGEncounterLibrary::RerollEncounterCounter(int32 RollA, int32 RollB)
{
	const int32 A = static_cast<int32>(static_cast<uint32>(RollA) % static_cast<uint32>(EncounterCounterMod));
	const int32 B = static_cast<int32>(static_cast<uint32>(RollB) % static_cast<uint32>(EncounterCounterMod));
	return EncounterCounterBase + A - B;
}

int32 UJRPGEncounterLibrary::TopUpEncounterCounter(int32 Counter, int32 RollA, int32 RollB)
{
	if (Counter >= EncounterCounterMod)
	{
		return Counter;
	}
	// (r + (r >>> 31)) >> 1: metade arredondando para zero (r é sempre positivo aqui)
	return Counter + RerollEncounterCounter(RollA, RollB) / 2;
}

bool UJRPGEncounterLibrary::StepEncounterCounter(int32& Counter, int32 Rate)
{
	const int32 Next = Counter - FMath::Max(Rate, 0);
	if (Next > 0)
	{
		Counter = Next;
		return false;
	}
	Counter = Next;
	return true;
}

int32 UJRPGEncounterLibrary::ScaleEncounterRate(int32 Rate, bool bLowRate, int32 LowRatePercent)
{
	const int32 Base = FMath::Max(Rate, 0);
	return bLowRate ? Base * FMath::Max(LowRatePercent, 0) / 100 : Base;
}

bool UJRPGEncounterLibrary::IsEncounterStep(FIntPoint PrevTile, FIntPoint NowTile)
{
	return PrevTile != NowTile && FMath::Abs(PrevTile.X - NowTile.X) < 2 && FMath::Abs(PrevTile.Y - NowTile.Y) < 2;
}
