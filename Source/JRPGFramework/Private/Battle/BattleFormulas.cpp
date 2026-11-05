#include "Battle/BattleFormulas.h"

namespace
{
	/** 0x801F53E8 (PROT 0898, file 0x26BD0), linha = atacante, coluna = defensor, em %. Ids do original:
	 *  0 terra, 1 água, 2 fogo, 3 vento, 4 trovão, 5 luz, 6 trevas, 7 neutro. */
	constexpr int32 ElementMatrix[8][8] =
	{
		{  96, 100, 100, 104, 100, 100, 100, 100 },   // terra
		{ 100,  96, 104, 100, 100, 100, 100, 100 },   // água
		{ 100, 104,  96, 100, 100, 100, 100, 100 },   // fogo
		{ 104, 100, 100,  96, 100, 100, 100, 100 },   // vento
		{ 102, 102, 102, 102,  96, 100,  98, 100 },   // trovão
		{ 100, 100, 100, 100, 100,  96, 104, 100 },   // luz
		{ 100, 100, 100, 100, 100, 104,  96, 100 },   // trevas
		{ 100, 100, 100, 100, 100, 100, 100, 100 },   // neutro
	};

	/** (b - 0x0C) como o byte sem sinal do original (wrapping_sub): a base dos % 5 e % 10. */
	int32 PowerIndexBase(int32 PowerByte)
	{
		return static_cast<int32>(static_cast<uint8>(PowerByte - 0x0C));
	}

	int64 Positive(int64 Value)
	{
		return Value < 0 ? 0 : Value;
	}

	int32 ClampToInt(int64 Value)
	{
		return static_cast<int32>(FMath::Clamp<int64>(Value, 0, MAX_int32));
	}
}

// --- Bytes de poder -----------------------------------------------------------------------------

int32 UBattleFormulas::PowerScalar(int32 PowerByte)
{
	return PowerScalars[PowerIndexBase(PowerByte) % 5];
}

int32 UBattleFormulas::BlockScalar(int32 PowerByte)
{
	return BlockScalars[PowerIndexBase(PowerByte) % 5];
}

bool UBattleFormulas::TargetsUpper(int32 PowerByte)
{
	return (PowerIndexBase(PowerByte) % 10) < 5;
}

bool UBattleFormulas::LimbMisses(bool bAttackerIsHero, int32 PowerByte, int32 TargetClass)
{
	// 0x801EC488: só o atacante da party; 0x801EC49C: de 0x16 para cima alcança tudo
	if (!bAttackerIsHero || PowerByte >= ReachesAllByte)
	{
		return false;
	}
	switch (TargetClass)
	{
	case 2:  return PowerByte >= 0x11;   // voando: só os golpes abaixo de 0x11 alcançam
	case 3:  return PowerByte < 0x11;    // baixo: só 0x11..0x15 alcançam
	default: return false;
	}
}

int32 UBattleFormulas::DefaultSwingPowerByte(FName Hero, EJRPGArtButton Button)
{
	// Lido dos arquivos de batalha do disco (PROT 0863..0865), seção padrão (id 0) de cada slot de golpe: o calçado dá o
	// chute baixo (slot 0xE) e o alto (0xF); a arma e o Ra-Seru, os braços (0xC esquerdo, 0xD direito). A Noa é canhota:
	// o braço esquerdo dela é o Ra-Seru (Terra, 0x1D) e o direito a arma (0x18). RE: docs/reference/glitches/
	// turn-order-and-arts.md (a tabela dos calçados) e docs/subsystems/battle-action-queue.md (o byte do golpe é o do registro)
	static const FName Noa(TEXT("Noa"));
	static const FName Gala(TEXT("Gala"));
	switch (Button)
	{
	case EJRPGArtButton::Arms:   return SwingFreeHighByte;
	case EJRPGArtButton::RaSeru: return Hero == Noa ? SwingFreeLowByte : SwingFreeHighByte;
	case EJRPGArtButton::High:   return (Hero == Noa || Hero == Gala) ? SwingRestrictedHighByte : SwingFreeHighByte;
	default:                     return SwingRestrictedLowByte;
	}
}

int32 UBattleFormulas::SwingPowerByte(const FJRPGCombatant& Hero, EJRPGArtButton Button)
{
	const int32 Index = static_cast<int32>(Button);
	if (Hero.SwingBytes.IsValidIndex(Index) && Hero.SwingBytes[Index] > 0)
	{
		return Hero.SwingBytes[Index];
	}
	return DefaultSwingPowerByte(Hero.Key, Button);
}

int32 UBattleFormulas::ArtPowerByte(int32 HitPower, EJRPGArtHitTarget Target)
{
	// idx = a posição do poder em [12,18,20,22,28]; um valor fora da lista cai no mais próximo
	int32 Index = 0;
	int32 Best = MAX_int32;
	for (int32 i = 0; i < 5; ++i)
	{
		const int32 Distance = FMath::Abs(PowerScalars[i] - HitPower);
		if (Distance < Best)
		{
			Best = Distance;
			Index = i;
		}
	}
	switch (Target)
	{
	case EJRPGArtHitTarget::Lower:                return 0x1B + Index;
	case EJRPGArtHitTarget::UpperMissesShort:     return 0x0C + Index;
	case EJRPGArtHitTarget::LowerMissesFloating:  return 0x11 + Index;
	case EJRPGArtHitTarget::Upper:
	default:                                      return 0x16 + Index;
	}
}

// --- Elemento -----------------------------------------------------------------------------------

int32 UBattleFormulas::ElementId(EJRPGElement Element)
{
	switch (Element)
	{
	case EJRPGElement::Earth:   return 0;
	case EJRPGElement::Water:   return 1;
	case EJRPGElement::Fire:    return 2;
	case EJRPGElement::Wind:    return 3;
	case EJRPGElement::Thunder: return 4;
	case EJRPGElement::Light:   return 5;
	case EJRPGElement::Dark:    return 6;
	default:                    return 7;
	}
}

int32 UBattleFormulas::ElementPercent(EJRPGElement Attacker, EJRPGElement Defender)
{
	return ElementMatrix[ElementId(Attacker)][ElementId(Defender)];
}

// --- Dobra de equipamento -----------------------------------------------------------------------

int32 UBattleFormulas::EquipmentFold(EJRPGHitSource Source, const TArray<int32>& PieceATK)
{
	auto Piece = [&PieceATK](int32 Index) -> int32
	{
		return PieceATK.IsValidIndex(Index) ? PieceATK[Index] : 0;
	};

	switch (Source)
	{
	case EJRPGHitSource::Arms:
		return Piece(PieceWeapon) >> 1;
	case EJRPGHitSource::RaSeru:
		return Piece(PieceRaSeru) >> 1;
	case EJRPGHitSource::High:
	case EJRPGHitSource::Low:
		return Piece(PieceShoes) >> 1;
	case EJRPGHitSource::Art:
	{
		int32 Sum = 0;
		for (int32 i = 0; i < PieceCount; ++i)
		{
			Sum += Piece(i);
		}
		return Sum >> 1;
	}
	case EJRPGHitSource::Starter:
	case EJRPGHitSource::Monster:
	default:
		return 0;
	}
}

// --- Sorteios -----------------------------------------------------------------------------------

int32 UBattleFormulas::Rand15(FRandomStream& Rng)
{
	// Os bits baixos de um LCG alternam; os altos têm período. 15 bits como o rand() do PS1
	return static_cast<int32>((Rng.GetUnsignedInt() >> 16) & 0x7FFFu);
}

int32 UBattleFormulas::Rnd(int32 Value, FJRPGRand15 Draw)
{
	const int32 Modulus = (Value >> 3) + 1;
	return Value + (Draw() % Modulus);
}

int32 UBattleFormulas::ScalePercent(int64 Value, int32 Percent)
{
	return ClampToInt(Value * Percent / 100);
}

// --- O golpe ------------------------------------------------------------------------------------

bool UBattleFormulas::RollBlock(const FJRPGBlockInput& Input, FJRPGRand15 Draw)
{
	// Os termos da aproximação somam antes do máximo (block.rs: spd + atk*4/5 + ramp): de lado ou de costas o atacante passa
	// muito à frente e o block quase não sai; a caminhada longa dá ao defensor um pouco
	int64 A = Positive(static_cast<int64>(Input.AttackerSPD) + static_cast<int64>(Input.AttackerATK) * 4 / 5 + Positive(Input.AttackRamp));
	int64 D = Positive(static_cast<int64>(Input.DefenderSPD) + static_cast<int64>(Input.DefenderATK) * 4 / 5 + Positive(Input.GuardRamp));
	if (A < D)
	{
		A = D;
	}
	if (A == 0 || D == 0)
	{
		return false;   // a soma zero do original travaria o divu: não bloqueia e não sorteia
	}

	const int64 AttackRoll = Draw() % A;
	A += (AttackRoll * BlockScalar(Input.PowerByte)) >> 1;
	D += Draw() % D;

	if (Input.bDefenderSpirit)
	{
		D = D * 3 / 2;
	}
	if (Input.bAttackerArt)
	{
		A = A * 3 / 2;
	}
	return A < D;
}

bool UBattleFormulas::RollBlock(const FJRPGBlockInput& Input, FRandomStream& Rng)
{
	return RollBlock(Input, [&Rng]() { return Rand15(Rng); });
}

FJRPGMeleeResult UBattleFormulas::ResolveMelee(const FJRPGMeleeInput& Input, FJRPGRand15 Draw)
{
	const int64 Atk = Positive(Input.BaseOffense);
	const int64 Power = PowerScalar(Input.PowerByte);
	const int64 Juggle = Positive(Input.Juggle);
	const int32 Elem = Input.ElementPercent;

	// 1 - o ataque: (atk + rand % (atk/8 + 1)) * Power >> 4, o HP do atacante, o juggle, o ângulo
	const int64 AttackRoll = Rnd(static_cast<int32>(FMath::Min<int64>(Atk, MAX_int32)), Draw);
	int64 Offense = (AttackRoll * Power) >> 4;
	Offense += Positive(Input.AttackerHP) >> 8;
	Offense += (Juggle * Atk) >> 6;
	// O termo de ângulo (physical.rs: (attack_ramp * atk) >> 16): de costas, até atk/32 a mais
	Offense += (Positive(Input.AttackRamp) * Atk) >> 16;

	// 2 - a Art x13/10 e o elemento; o elemento sempre, então a Art o leva duas vezes
	if (Input.bArt)
	{
		Offense = Offense * 13 / 10;
		Offense = Offense * Elem / 100;
	}
	Offense = Offense * Elem / 100;

	// 3 - a defesa: def + rand % (def/8 + 1), a distância, o Spirit x3
	const int64 Def = Positive(Input.DefenderDefense);
	int64 Defense = Rnd(static_cast<int32>(FMath::Min<int64>(Def, MAX_int32)), Draw);
	// O termo de distância (physical.rs: (def * guard_ramp) >> 10), antes do x3 do Spirit
	Defense += (Def * Positive(Input.GuardRamp)) >> 10;
	if (Input.bDefenderGuarding)
	{
		Defense *= 3;
	}

	// 4 - o azarão: um ataque que não passa a defesa não vira 1, é REFEITO em cima dela
	FJRPGMeleeResult Result;
	const bool bClears = Offense > Defense + ((Offense * Power) >> 6) + ((Offense * Juggle) >> 6) + Juggle;
	if (!bClears)
	{
		Result.bUnderdog = true;
		const int64 Previous = Offense;
		const int64 Extra = Draw() % ((Previous >> 2) + 1);
		Offense = Defense + (((((Previous * 3) >> 2) + Extra) * Power) >> 6) + ((Juggle * Previous) >> 6) + Juggle;
		if (Input.bArt)
		{
			Offense = Offense * 11 / 10;
			Offense = Offense * Elem / 100;
		}
		Offense = Offense * Elem / 100;

		// o piso do chip: todo golpe que acerta dá pelo menos 3 (5 numa Art)
		if (!Input.bArt)
		{
			if (Offense <= Defense + Juggle + 3)
			{
				Offense = Defense + (Draw() % 3) + 3 + Juggle;
			}
		}
		else if (Offense <= Defense + Juggle + 5)
		{
			Offense = Defense + (Draw() % 4) + 5 + Juggle;
		}
	}

	// 5 - o teto e o dano
	Offense = FMath::Min<int64>(Offense, Defense + DamageCap);
	Result.Offense = ClampToInt(Offense);
	Result.Defense = ClampToInt(Defense);
	Result.Damage = ClampToInt(Offense - Defense);
	return Result;
}

FJRPGMeleeResult UBattleFormulas::ResolveMelee(const FJRPGMeleeInput& Input, FRandomStream& Rng)
{
	return ResolveMelee(Input, [&Rng]() { return Rand15(Rng); });
}

// --- Ordem de turno -----------------------------------------------------------------------------

int32 UBattleFormulas::InitiativeKey(bool bHero, int32 SPD, int32 HP, int32 MaxHP, bool bSlow, FJRPGRand15 Draw)
{
	const int32 Speed = FMath::Max(SPD, 0);
	int64 Key = static_cast<int64>(Speed) + (Draw() % ((Speed / 2) + 1)) + 1;

	const int64 Missing = Positive(static_cast<int64>(MaxHP) - HP);
	if (bHero)
	{
		if (HP < (MaxHP >> 2))
		{
			Key += Missing >> 4;
		}
		else if (HP < (MaxHP >> 1))
		{
			Key += Missing >> 5;
		}
		else
		{
			Key += Missing >> 6;
		}
	}
	else
	{
		Key += Missing >> 10;
	}

	if (bSlow)
	{
		Key >>= 1;
	}
	return ClampToInt(Key);
}

int32 UBattleFormulas::PickByInitiative(const TArray<int32>& Keys, FJRPGRand15 Draw)
{
	if (Keys.IsEmpty())
	{
		return INDEX_NONE;
	}

	// FUN_801DABA4 0x801DAC7C..0x801DAD60: a lista começa em [0] com o máximo na chave do assento 0; quem
	// SOBE o máximo zera a lista, entra nela e entra DE NOVO (o 'best == k' logo depois). Então um máximo
	// que não é do assento 0 aparece duas vezes e ganha 2/(empates+2) do sorteio.
	int32 Best = Keys[0];
	TArray<int32> List;
	List.Init(0, Keys.Num() + 1);
	int32 Count = 0;
	for (int32 Slot = 1; Slot < Keys.Num(); ++Slot)
	{
		const int32 Key = Keys[Slot];
		if (Best < Key)
		{
			Best = Key;
			Count = 0;
			List[0] = Slot;
		}
		if (Best == Key)
		{
			++Count;
			List[Count] = Slot;
		}
	}
	if (Best <= 0)
	{
		return INDEX_NONE;
	}
	return List[Draw() % (Count + 1)];
}

int32 UBattleFormulas::PickByInitiative(const TArray<int32>& Keys, FRandomStream& Rng)
{
	return PickByInitiative(Keys, [&Rng]() { return Rand15(Rng); });
}

TArray<int32> UBattleFormulas::EnemySwingQueue(const TArray<int32>& Costs, int32 Gauge, FJRPGRand15 Draw)
{
	TArray<int32> Queue;
	if (Costs.IsEmpty())
	{
		return Queue;
	}

	int32 Remaining = Gauge;
	int32 Misses = 0;
	while (Queue.Num() < MaxEnemyActions && Misses < MaxEnemyPickMisses)
	{
		const int32 Pick = Draw() % Costs.Num();
		const int32 Cost = Costs[Pick];
		// 0xFF é o "não é ataque" do original; um sorteio que o gauge não paga conta uma falta
		if (Cost != 0xFF && Remaining >= Cost)
		{
			Queue.Add(Pick);
			Remaining -= Cost;
		}
		else
		{
			++Misses;
		}
	}
	return Queue;
}

// --- Tempo --------------------------------------------------------------------------------------

int32 UBattleFormulas::ClipFramesToSteps(int32 ClipFrames, int32 GameSpeed, int32 Rate)
{
	if (ClipFrames <= 0)
	{
		return 0;
	}
	const int64 Divisor = static_cast<int64>(FMath::Max(GameSpeed, 1)) * FMath::Max(Rate, 1);
	return ClampToInt((static_cast<int64>(ClipFrames) * 32 + Divisor - 1) / Divisor);
}

int32 UBattleFormulas::HitStepForBeat(int32 Beat, int32 GameSpeed, int32 Rate)
{
	// FUN_801EC3E4 dispara no primeiro quadro com quadro + 1 >= beat, ou seja, no quadro beat - 1
	return ClipFramesToSteps(FMath::Max(Beat - 1, 0), GameSpeed, Rate);
}

int32 UBattleFormulas::ReactionWindow(int32 Beat, int32 GameSpeed, int32 Rate)
{
	return ClipFramesToSteps(Beat, GameSpeed, Rate);
}
