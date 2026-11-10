#include "Battle/JRPGBattleRules.h"

FJRPGEnemyBattleStats UJRPGBattleRules::InstallEnemyStats(const FJRPGEnemyData& Enemy, bool bScriptedFight, EJRPGRuleset Ruleset)
{
	FJRPGEnemyBattleStats Out;
	Out.HP  = Enemy.BaseHP;
	Out.MP  = Enemy.BaseMP;
	Out.AGL = Enemy.BaseAGL;
	Out.ATK = Enemy.BaseATK;
	Out.UDF = Enemy.BaseUDF;
	Out.LDF = Enemy.BaseLDF;
	Out.SPD = Enemy.BaseSPD;
	Out.INT = Enemy.BaseINT;

	// A versão japonesa (e as PAL) copia o registro e segue: sem boost nenhum.
	if (Ruleset != EJRPGRuleset::USA)
	{
		return Out;
	}

	if (bScriptedFight)
	{
		// Perfil B (flag de luta roteirizada ligada).
		Out.ATK += Out.ATK >> 2;  // x5/4
		Out.UDF *= 2;
		Out.LDF *= 2;
		Out.INT += Out.INT >> 3;  // x9/8
	}
	else
	{
		// Perfil A (encontro aleatório). Cada shift trunca sozinho: 975 -> 1705, não 1706.
		Out.UDF += (Out.UDF >> 1) + (Out.UDF >> 2);  // x7/4
		Out.LDF += (Out.LDF >> 1) + (Out.LDF >> 2);  // x7/4
		Out.INT += Out.INT >> 2;                     // x5/4
	}
	return Out;
}

int32 UJRPGBattleRules::VictoryEXPPool(int32 RawEXPSum, EJRPGRuleset Ruleset)
{
	if (RawEXPSum <= 0)
	{
		return 0;
	}
	// FUN_8004E568, 0x8004F0B8: sum -= sum >> 2. Ausente nos executáveis JP e PAL.
	return Ruleset == EJRPGRuleset::USA ? RawEXPSum - (RawEXPSum >> 2) : RawEXPSum;
}

int32 UJRPGBattleRules::VictoryEXPShare(int32 EXPPool, int32 AliveCount)
{
	if (EXPPool <= 0 || AliveCount <= 0)
	{
		return 0;
	}
	// 0x8004F198: divisão arredondando para cima.
	return static_cast<int32>((static_cast<int64>(EXPPool) + AliveCount - 1) / AliveCount);
}

int32 UJRPGBattleRules::VictoryGold(const TArray<int32>& DefeatedEnemyGold, bool bGoldenBook, EJRPGRuleset Ruleset)
{
	int64 Acc = 0;
	for (const int32 Gold : DefeatedEnemyGold)
	{
		if (Gold > 0)
		{
			Acc += Gold >> 1;  // 0x8004EFBC, um inimigo de cada vez
		}
	}
	if (bGoldenBook)
	{
		Acc += Acc >> 2;  // 0x8004F094
	}
	if (Ruleset == EJRPGRuleset::USA)
	{
		Acc -= Acc >> 1;  // 0x8004F0DC: o segundo corte, só na USA
	}
	return static_cast<int32>(FMath::Min<int64>(Acc, MAX_int32));
}

int32 UJRPGBattleRules::VictoryPoseTier(int32 HP, int32 MaxHP, int32 Round, bool bWeakStatus)
{
	if (bWeakStatus)
	{
		return 4;  // 0x8004E830: qualquer status da máscara 0x107B
	}
	const int32 Max = FMath::Max(MaxHP, 0);
	int32 Tier = 0;
	if (HP < (Max >> 1))
	{
		Tier = HP < (Max >> 2) ? 3 : 2;
	}
	else if (HP < ((Max * 3) >> 2))
	{
		Tier = 1;
	}
	// Duas passadas: a luta longa envelhece a pose
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		if (Round >= Tier * 2 + 2 && Tier < 3)
		{
			++Tier;
		}
	}
	return Tier;
}

int32 UJRPGBattleRules::VictoryPoseColumn(int32 Tier, int32 PairRoll, int32 MemberRoll)
{
	int32 PairBase = 4;
	switch (Tier)
	{
	case 0:  PairBase = (PairRoll & 3) != 0 ? 0 : 2; break;
	case 1:  PairBase = (PairRoll & 1) != 0 ? 0 : 2; break;
	case 2:  PairBase = (PairRoll & 1) != 0 ? 2 : 4; break;
	case 3:  PairBase = (PairRoll & 3) != 0 ? 4 : 2; break;
	default: break;
	}
	return PairBase + (MemberRoll & 1);
}

FName UJRPGBattleRules::VictoryPoseClipName(int32 Column)
{
	static const FName Names[6] = {
		TEXT("Victory1"), TEXT("Victory2"), TEXT("VictoryAlt1"), TEXT("VictoryAlt2"), TEXT("VictoryWeak1"), TEXT("VictoryWeak2")
	};
	return Column >= 0 && Column < 6 ? Names[Column] : NAME_None;
}
