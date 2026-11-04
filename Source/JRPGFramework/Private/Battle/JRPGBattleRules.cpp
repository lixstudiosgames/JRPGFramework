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
