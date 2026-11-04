// Testes da luta (BATTLE_ROADMAP.md › Phase 5): sem mapa. As contas (UBattleFormulas) com as jogadas do RE,
// o exemplo "Vahn vs Evil Fly" do battle-formulas.md e, mais adiante neste arquivo, a máquina de estados da
// luta (FJRPGBattleResolver). Session Frontend › Automation, filtro "JRPG.Battle".

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleFormulas.h"
#include "Battle/BattleTypes.h"

#define JRPG_BATTLE_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace JRPGBattleTests
{
	/** Uma fonte de sorteios que repete uma sequência fixa e, acabada, o último valor (como o seq_rng do RE). */
	struct FSequenceDraw
	{
		TArray<int32> Values;
		int32 Index = 0;
		int32 Count = 0;

		explicit FSequenceDraw(std::initializer_list<int32> InValues) : Values(InValues) {}

		int32 operator()()
		{
			++Count;
			const int32 Value = Values.IsEmpty() ? 0 : Values[FMath::Min(Index, Values.Num() - 1)];
			++Index;
			return Value;
		}
	};

	FJRPGMeleeInput Swing(int32 Atk, int32 Def)
	{
		FJRPGMeleeInput In;
		In.BaseOffense = Atk;
		In.DefenderDefense = Def;
		In.PowerByte = 0x0C;   // o multiplicador 12, como o `command_scalar` padrão dos testes do RE
		In.Juggle = 0;         // o combo_scale do RE começa em 0 nesses casos
		return In;
	}

	int32 Damage(const FJRPGMeleeInput& In, std::initializer_list<int32> Draws)
	{
		FSequenceDraw Draw(Draws);
		return UBattleFormulas::ResolveMelee(In, [&Draw]() { return Draw(); }).Damage;
	}

// Os testes ficam dentro do namespace (como os das Arts): nada de using namespace no escopo global.

// --- Bytes de poder --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattlePowerBytesTest, "JRPG.Battle.PowerBytes", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattlePowerBytesTest::RunTest(const FString& Parameters)
{
	// 0x0C..0x10 percorrem [12,18,20,22,28] em ordem; o % 5 repete
	for (int32 i = 0; i < 5; ++i)
	{
		TestEqual(FString::Printf(TEXT("escalar de 0x%02X"), 0x0C + i), UBattleFormulas::PowerScalar(0x0C + i), UBattleFormulas::PowerScalars[i]);
		TestEqual(FString::Printf(TEXT("escalar de 0x%02X"), 0x16 + i), UBattleFormulas::PowerScalar(0x16 + i), UBattleFormulas::PowerScalars[i]);
	}
	TestEqual(TEXT("o golpe simples de cima é poder 20"), UBattleFormulas::PowerScalar(0x18), 20);
	TestEqual(TEXT("o golpe simples de baixo é poder 20"), UBattleFormulas::PowerScalar(0x1D), 20);
	// (b - 0x0C) % 10 < 5 bate na defesa de cima; 0x0C..0x10 cima, 0x11..0x15 baixo, 0x16..0x1A cima, 0x1B..0x1F baixo
	for (int32 b = 0x0C; b <= 0x1F; ++b)
	{
		const bool bUpper = (b <= 0x10) || (b >= 0x16 && b <= 0x1A);
		TestEqual(FString::Printf(TEXT("0x%02X bate em cima"), b), UBattleFormulas::TargetsUpper(b), bUpper);
	}
	TestTrue(TEXT("0x18 é UDF"), UBattleFormulas::TargetsUpper(0x18));
	TestFalse(TEXT("0x1D é LDF"), UBattleFormulas::TargetsUpper(0x1D));
	TestEqual(TEXT("o peso do bloqueio"), UBattleFormulas::BlockScalar(0x0C), 6);
	TestEqual(TEXT("o peso do bloqueio, 5º"), UBattleFormulas::BlockScalar(0x10), 2);

	// O miss pela altura: só o herói; de 0x16 para cima alcança tudo; classe 2 (voando) e 3 (baixo)
	TestFalse(TEXT("monstro nunca erra pela altura"), UBattleFormulas::LimbMisses(false, 0x11, 2));
	TestFalse(TEXT("classe 0 alcança tudo"), UBattleFormulas::LimbMisses(true, 0x11, 0));
	for (int32 b = 0x0C; b <= 0x1F; ++b)
	{
		TestEqual(FString::Printf(TEXT("classe 2, 0x%02X"), b), UBattleFormulas::LimbMisses(true, b, 2), b >= 0x11 && b < 0x16);
		TestEqual(FString::Printf(TEXT("classe 3, 0x%02X"), b), UBattleFormulas::LimbMisses(true, b, 3), b < 0x11);
	}
	TestFalse(TEXT("0x18 alcança o voador"), UBattleFormulas::LimbMisses(true, UBattleFormulas::SwingUpperByte, 2));
	TestFalse(TEXT("0x1D alcança o baixo"), UBattleFormulas::LimbMisses(true, UBattleFormulas::SwingLowerByte, 3));

	// Os bytes que o herói produz: o golpe simples e o das Arts (HitPower x HitTarget)
	TestEqual(TEXT("Arms"), UBattleFormulas::SwingPowerByte(EJRPGArtButton::Arms), 0x18);
	TestEqual(TEXT("Ra-Seru"), UBattleFormulas::SwingPowerByte(EJRPGArtButton::RaSeru), 0x18);
	TestEqual(TEXT("High"), UBattleFormulas::SwingPowerByte(EJRPGArtButton::High), 0x18);
	TestEqual(TEXT("Low"), UBattleFormulas::SwingPowerByte(EJRPGArtButton::Low), 0x1D);
	const int32 Powers[5] = { 12, 18, 20, 22, 28 };
	for (int32 i = 0; i < 5; ++i)
	{
		const int32 Upper = UBattleFormulas::ArtPowerByte(Powers[i], EJRPGArtHitTarget::Upper);
		const int32 Lower = UBattleFormulas::ArtPowerByte(Powers[i], EJRPGArtHitTarget::Lower);
		const int32 Short = UBattleFormulas::ArtPowerByte(Powers[i], EJRPGArtHitTarget::UpperMissesShort);
		const int32 Floating = UBattleFormulas::ArtPowerByte(Powers[i], EJRPGArtHitTarget::LowerMissesFloating);
		TestEqual(TEXT("Upper"), Upper, 0x16 + i);
		TestEqual(TEXT("Lower"), Lower, 0x1B + i);
		TestEqual(TEXT("UpperMissesShort"), Short, 0x0C + i);
		TestEqual(TEXT("LowerMissesFloating"), Floating, 0x11 + i);
		// o byte volta ao multiplicador e à defesa que a linha pediu
		for (const int32 Byte : { Upper, Lower, Short, Floating })
		{
			TestEqual(TEXT("o multiplicador volta"), UBattleFormulas::PowerScalar(Byte), Powers[i]);
		}
		TestTrue(TEXT("Upper bate em UDF"), UBattleFormulas::TargetsUpper(Upper));
		TestFalse(TEXT("Lower bate em LDF"), UBattleFormulas::TargetsUpper(Lower));
		TestTrue(TEXT("UpperMissesShort bate em UDF"), UBattleFormulas::TargetsUpper(Short));
		TestFalse(TEXT("LowerMissesFloating bate em LDF"), UBattleFormulas::TargetsUpper(Floating));
		// e o miss: o "Lower, misses floating" erra o voador (classe 2) e o "Upper, misses short" o baixo (classe 3)
		TestTrue(TEXT("Lower misses floating"), UBattleFormulas::LimbMisses(true, Floating, 2));
		TestFalse(TEXT("Lower misses floating alcança o baixo"), UBattleFormulas::LimbMisses(true, Floating, 3));
		TestTrue(TEXT("Upper misses short"), UBattleFormulas::LimbMisses(true, Short, 3));
		TestFalse(TEXT("Upper misses short alcança o voador"), UBattleFormulas::LimbMisses(true, Short, 2));
		TestFalse(TEXT("Upper alcança todos"), UBattleFormulas::LimbMisses(true, Upper, 2) || UBattleFormulas::LimbMisses(true, Upper, 3));
	}
	return true;
}

// --- Elemento --------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleElementTest, "JRPG.Battle.Element", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleElementTest::RunTest(const FString& Parameters)
{
	using E = EJRPGElement;
	// O mesmo elemento 96, inclusive o trovão
	for (const E Element : { E::Fire, E::Water, E::Earth, E::Wind, E::Thunder, E::Light, E::Dark })
	{
		TestEqual(TEXT("o mesmo elemento"), UBattleFormulas::ElementPercent(Element, Element), 96);
	}
	// Os pares opostos 104, nos dois sentidos
	TestEqual(TEXT("fogo contra água"), UBattleFormulas::ElementPercent(E::Fire, E::Water), 104);
	TestEqual(TEXT("água contra fogo"), UBattleFormulas::ElementPercent(E::Water, E::Fire), 104);
	TestEqual(TEXT("terra contra vento"), UBattleFormulas::ElementPercent(E::Earth, E::Wind), 104);
	TestEqual(TEXT("vento contra terra"), UBattleFormulas::ElementPercent(E::Wind, E::Earth), 104);
	TestEqual(TEXT("luz contra trevas"), UBattleFormulas::ElementPercent(E::Light, E::Dark), 104);
	TestEqual(TEXT("trevas contra luz"), UBattleFormulas::ElementPercent(E::Dark, E::Light), 104);
	// O trovão ataca todo mundo a 102 (98 contra trevas, 100 contra luz, 100 contra o neutro)
	TestEqual(TEXT("trovão contra fogo"), UBattleFormulas::ElementPercent(E::Thunder, E::Fire), 102);
	TestEqual(TEXT("trovão contra terra"), UBattleFormulas::ElementPercent(E::Thunder, E::Earth), 102);
	TestEqual(TEXT("trovão contra trevas"), UBattleFormulas::ElementPercent(E::Thunder, E::Dark), 98);
	TestEqual(TEXT("trovão contra luz"), UBattleFormulas::ElementPercent(E::Thunder, E::Light), 100);
	TestEqual(TEXT("ninguém ataca o trovão a 102"), UBattleFormulas::ElementPercent(E::Fire, E::Thunder), 100);
	// O resto e o neutro 100
	TestEqual(TEXT("fogo contra vento"), UBattleFormulas::ElementPercent(E::Fire, E::Wind), 100);
	TestEqual(TEXT("neutro ataca"), UBattleFormulas::ElementPercent(E::None, E::Fire), 100);
	TestEqual(TEXT("contra neutro"), UBattleFormulas::ElementPercent(E::Fire, E::None), 100);
	TestEqual(TEXT("Evil é neutro"), UBattleFormulas::ElementPercent(E::Evil, E::Light), 100);
	return true;
}

// --- Dobra de equipamento --------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleFoldTest, "JRPG.Battle.EquipmentFold", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleFoldTest::RunTest(const FString& Parameters)
{
	// Weapon, Ra-Seru (o tier), Shoes, Body, Head — o exemplo do RE (Vahn: faca 98, Ra-Seru 100, botas 86)
	const TArray<int32> Pieces = { 98, 100, 86, 5, 3 };
	using S = EJRPGHitSource;
	TestEqual(TEXT("Arms: ½ da arma"), UBattleFormulas::EquipmentFold(S::Arms, Pieces), 49);
	TestEqual(TEXT("Ra-Seru: ½ do tier"), UBattleFormulas::EquipmentFold(S::RaSeru, Pieces), 50);
	TestEqual(TEXT("High: ½ do calçado"), UBattleFormulas::EquipmentFold(S::High, Pieces), 43);
	TestEqual(TEXT("Low: ½ do calçado"), UBattleFormulas::EquipmentFold(S::Low, Pieces), 43);
	TestEqual(TEXT("o starter não dobra nada"), UBattleFormulas::EquipmentFold(S::Starter, Pieces), 0);
	TestEqual(TEXT("o monstro não dobra nada"), UBattleFormulas::EquipmentFold(S::Monster, Pieces), 0);
	TestEqual(TEXT("uma Art: ½ da soma das cinco"), UBattleFormulas::EquipmentFold(S::Art, Pieces), (98 + 100 + 86 + 5 + 3) / 2);
	// O ½ trunca: 5 >> 1 = 2
	TestEqual(TEXT("o ½ trunca"), UBattleFormulas::EquipmentFold(S::Arms, TArray<int32>({ 5 })), 2);
	TestEqual(TEXT("sem peças"), UBattleFormulas::EquipmentFold(S::Art, TArray<int32>()), 0);
	return true;
}

// --- O núcleo do dano ------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleMeleeTest, "JRPG.Battle.Melee", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleMeleeTest::RunTest(const FString& Parameters)
{
	// Os casos à mão do RE (crates/engine-battle-vm/tests/battle_physical_predamage.rs), com rand() = 0
	// atk 100, poder 12, def 10: raw = (100*12)>>4 = 75, guard 10, passa (10 + 14 < 75): 65
	TestEqual(TEXT("um golpe que passa"), Damage(Swing(100, 10), { 0 }), 65);

	// o HP do atacante soma HP >> 8
	FJRPGMeleeInput WithHP = Swing(100, 10);
	WithHP.AttackerHP = 1024;
	TestEqual(TEXT("HP >> 8"), Damage(WithHP, { 0 }), 69);

	// o azarão: atk 10 contra def 400 não vira 1, vira o piso do chip (guard + 3)
	TestEqual(TEXT("azarão, piso do chip"), Damage(Swing(10, 400), { 0 }), 3);
	// e cresce com o atacante: atk 400 contra def 4000: 4000 + ((300*3>>2)*12>>6 = 42): dano 42
	TestEqual(TEXT("azarão, remendo"), Damage(Swing(400, 4000), { 0 }), 42);

	// a Art: 75 * 13 / 10 = 97, dano 87
	FJRPGMeleeInput Art = Swing(100, 10);
	Art.bArt = true;
	TestEqual(TEXT("Art x13/10"), Damage(Art, { 0 }), 87);

	// o Spirit triplica a defesa: 75 - 30
	FJRPGMeleeInput Spirit = Swing(100, 10);
	Spirit.bDefenderGuarding = true;
	TestEqual(TEXT("Spirit"), Damage(Spirit, { 0 }), 45);

	// o elemento escala o ataque: 75 * 50 / 100 = 37, dano 27
	FJRPGMeleeInput Elem = Swing(100, 10);
	Elem.ElementPercent = 50;
	TestEqual(TEXT("elemento"), Damage(Elem, { 0 }), 27);
	// a Art leva o elemento duas vezes: 75 -> 97 -> 97*80/100 = 77 -> 77*80/100 = 61, dano 51
	FJRPGMeleeInput ArtElem = Swing(100, 10);
	ArtElem.bArt = true;
	ArtElem.ElementPercent = 80;
	TestEqual(TEXT("a Art toma o elemento duas vezes"), Damage(ArtElem, { 0 }), 51);

	// o teto: o excesso acima de Defense + 9999 é cortado
	TestEqual(TEXT("teto 9999"), Damage(Swing(60000, 0), { 0 }), 9999);

	// os sorteios: um golpe que passa gasta 2 (ataque, defesa); o azarão +1 (remendo) e o piso +1
	FSequenceDraw Clearing({ 0 });
	UBattleFormulas::ResolveMelee(Swing(100, 10), [&Clearing]() { return Clearing(); });
	TestEqual(TEXT("dois sorteios"), Clearing.Count, 2);
	FSequenceDraw Underdog({ 0 });
	const FJRPGMeleeResult Rewritten = UBattleFormulas::ResolveMelee(Swing(10, 400), [&Underdog]() { return Underdog(); });
	TestEqual(TEXT("ataque, defesa, remendo e piso"), Underdog.Count, 4);
	TestTrue(TEXT("marcado como azarão"), Rewritten.bUnderdog);

	// nunca zero: todo golpe que acerta dá pelo menos 3
	for (int32 Atk : { 1, 10, 50, 200 })
	{
		for (int32 Def : { 0, 20, 300, 5000 })
		{
			for (int32 Draw : { 0, 3, 7 })
			{
				TestTrue(FString::Printf(TEXT("atk %d def %d: dano >= 3"), Atk, Def), Damage(Swing(Atk, Def), { Draw }) >= 3);
			}
		}
	}

	// Mais ataque, mais dano; mais defesa, menos
	int32 Previous = 0;
	for (int32 Atk : { 10, 40, 100, 400, 1000 })
	{
		const int32 D = Damage(Swing(Atk, 200), { 0 });
		TestTrue(FString::Printf(TEXT("atk %d não bate menos que o anterior"), Atk), D >= Previous);
		Previous = D;
	}
	Previous = MAX_int32;
	for (int32 Def : { 0, 50, 200, 800, 4000 })
	{
		const int32 D = Damage(Swing(500, Def), { 0 });
		TestTrue(FString::Printf(TEXT("def %d não deixa passar mais que a anterior"), Def), D <= Previous);
		Previous = D;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleEvilFlyTest, "JRPG.Battle.EvilFly", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleEvilFlyTest::RunTest(const FString& Parameters)
{
	// O exemplo resolvido do RE (battle-formulas.md › "Worked example - Vahn vs Evil Fly", o playtest do
	// ZetaPhoenix): Vahn ATK base 188, arma 98, calçado 86, Ra-Seru 100, HP 2747; Evil Fly UDF 42 / LDF 49,
	// elemento neutro. O combo: braço esquerdo, braço direito, depois a Hyper Elbow (poder 20, LDF).
	// Os sorteios são os que ele observou (jogada do ataque, jogada da defesa).
	//
	// O RE soma um termo de distância à defesa do 1º golpe (42*30/1024 = 1) que este remake larga (as posições
	// não alimentam as regras, Deliberate departures): o golpe 1 dá 275 aqui e 274 lá. Os outros dois batem.
	const TArray<int32> Pieces = { 98, 100, 86, 0, 0 };

	auto Hit = [&](EJRPGHitSource Source, int32 PowerByte, int32 Juggle, bool bArt, int32 Def, int32 AttackDraw, int32 DefenseDraw) -> FJRPGMeleeResult
	{
		FJRPGMeleeInput In;
		In.BaseOffense = 188 + UBattleFormulas::EquipmentFold(Source, Pieces);
		In.AttackerHP = 2747;
		In.DefenderDefense = Def;
		In.PowerByte = PowerByte;
		In.Juggle = Juggle;
		In.bArt = bArt;
		FSequenceDraw Draw({ AttackDraw, DefenseDraw });
		return UBattleFormulas::ResolveMelee(In, [&Draw]() { return Draw(); });
	};

	// 1 - braço esquerdo: 188 + 98/2 = 237; +7 -> 244; 244*20>>4 = 305; +10 (HP) +3 (juggle 1: 237>>6) = 318; UDF 42+1 = 43
	const FJRPGMeleeResult H1 = Hit(EJRPGHitSource::Arms, 0x18, 1, false, 42, 7, 1);
	TestEqual(TEXT("golpe 1, o ataque"), H1.Offense, 318);
	TestEqual(TEXT("golpe 1, a defesa"), H1.Defense, 43);
	TestEqual(TEXT("golpe 1, o dano"), H1.Damage, 275);

	// 2 - braço direito: 188 + 100/2 = 238; +10 -> 248; 248*20>>4 = 310; +10 +7 (juggle 2: 2*238>>6) = 327; UDF 42+2 = 44
	const FJRPGMeleeResult H2 = Hit(EJRPGHitSource::RaSeru, 0x18, 2, false, 42, 10, 2);
	TestEqual(TEXT("golpe 2, o ataque"), H2.Offense, 327);
	TestEqual(TEXT("golpe 2, a defesa"), H2.Defense, 44);
	TestEqual(TEXT("golpe 2, o dano"), H2.Damage, 283);

	// 3 - a Art: 188 + (98+100+86)/2 = 330; +20 -> 350; 350*20>>4 = 437; +10 +5 = 452; x13/10 = 587; LDF 49+2 = 51
	const int32 HyperElbow = UBattleFormulas::ArtPowerByte(20, EJRPGArtHitTarget::Lower);
	const FJRPGMeleeResult H3 = Hit(EJRPGHitSource::Art, HyperElbow, 1, true, 49, 20, 2);
	TestEqual(TEXT("golpe 3, o ataque"), H3.Offense, 587);
	TestEqual(TEXT("golpe 3, a defesa"), H3.Defense, 51);
	TestEqual(TEXT("golpe 3, o dano"), H3.Damage, 536);
	TestFalse(TEXT("o golpe 3 não precisou do remendo"), H3.bUnderdog);

	// Com o ATK do menu (tudo equipado) em vez do base, o golpe 1 sairia de 472: o que a regra evita
	TestEqual(TEXT("o Base Offense do golpe 1"), 188 + UBattleFormulas::EquipmentFold(EJRPGHitSource::Arms, Pieces), 237);
	return true;
}

// --- Bloqueio --------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleBlockTest, "JRPG.Battle.Block", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleBlockTest::RunTest(const FString& Parameters)
{
	FJRPGBlockInput In;
	In.AttackerSPD = 50;
	In.AttackerATK = 100;   // 50 + 80 = 130
	In.DefenderSPD = 40;
	In.DefenderATK = 50;    // 40 + 40 = 80
	In.PowerByte = 0x0C;    // peso 6

	// a = max(130, 80) = 130; a += (0 % 130) * 6 >> 1 = 130; d += 79 % 80 -> 159: bloqueia (130 < 159)
	FSequenceDraw Block({ 0, 79 });
	TestTrue(TEXT("o defensor bloqueia"), UBattleFormulas::RollBlock(In, [&Block]() { return Block(); }));
	TestEqual(TEXT("dois sorteios"), Block.Count, 2);

	// a += (129 % 130) * 6 >> 1 = 129*3 = 387 -> 517; d += 0 -> 80: não bloqueia
	FSequenceDraw Land({ 129, 0 });
	TestFalse(TEXT("o golpe acerta"), UBattleFormulas::RollBlock(In, [&Land]() { return Land(); }));

	// uma Art do atacante x3/2: 130 -> 195 contra d = 80 + 79 = 159: não bloqueia
	FJRPGBlockInput ArtIn = In;
	ArtIn.bAttackerArt = true;
	FSequenceDraw ArtDraw({ 0, 79 });
	TestFalse(TEXT("a Art atropela o bloqueio"), UBattleFormulas::RollBlock(ArtIn, [&ArtDraw]() { return ArtDraw(); }));

	// o Spirit do defensor x3/2: d = 80 + 20 = 100 -> 150 contra a = 130 + 0 = 130: bloqueia
	FJRPGBlockInput SpiritIn = In;
	SpiritIn.bDefenderSpirit = true;
	FSequenceDraw SpiritDraw({ 0, 20 });
	TestTrue(TEXT("o Spirit ajuda"), UBattleFormulas::RollBlock(SpiritIn, [&SpiritDraw]() { return SpiritDraw(); }));

	// soma zero dos dois lados: não bloqueia e não sorteia
	FJRPGBlockInput Zero;
	FSequenceDraw None({ 0 });
	TestFalse(TEXT("soma zero"), UBattleFormulas::RollBlock(Zero, [&None]() { return None(); }));
	TestEqual(TEXT("e nenhum sorteio"), None.Count, 0);
	return true;
}

// --- Iniciativa e a fila da IA ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleInitiativeTest, "JRPG.Battle.Initiative", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleInitiativeTest::RunTest(const FString& Parameters)
{
	auto Key = [](bool bHero, int32 SPD, int32 HP, int32 MaxHP, bool bSlow, int32 Draw)
	{
		FSequenceDraw D({ Draw });
		return UBattleFormulas::InitiativeKey(bHero, SPD, HP, MaxHP, bSlow, [&D]() { return D(); });
	};

	// SPD + rand % (SPD/2 + 1) + 1; sem ferimento o termo do HP é 0
	TestEqual(TEXT("chave base"), Key(true, 40, 100, 100, false, 0), 41);
	TestEqual(TEXT("com o sorteio no máximo"), Key(true, 40, 100, 100, false, 20), 61);
	TestEqual(TEXT("o módulo nunca é zero"), Key(true, 0, 100, 100, false, 5), 1);

	// o herói ferido ganha mais: abaixo de ¼ (faltante >> 4), abaixo de ½ (>> 5), o resto (>> 6)
	TestEqual(TEXT("herói abaixo de ¼"), Key(true, 0, 100, 1024, false, 0), 1 + (924 >> 4));
	TestEqual(TEXT("herói abaixo de ½"), Key(true, 0, 500, 1024, false, 0), 1 + (524 >> 5));
	TestEqual(TEXT("herói quase inteiro"), Key(true, 0, 1000, 1024, false, 0), 1 + (24 >> 6));
	// o monstro ferido quase não ganha: >> 10
	TestEqual(TEXT("monstro ferido"), Key(false, 0, 100, 1024, false, 0), 1 + (924 >> 10));
	TestTrue(TEXT("o herói ganha mais que o monstro"), Key(true, 0, 100, 1024, false, 0) > Key(false, 0, 100, 1024, false, 0));
	// HP acima do máximo não vira bônus enorme
	TestEqual(TEXT("HP acima do máximo"), Key(true, 0, 900, 100, false, 0), 1);
	// Slow divide por 2
	TestEqual(TEXT("Slow"), Key(true, 40, 100, 100, true, 0), 41 >> 1);

	// O próximo a agir: a maior chave; empates sorteados como o original
	auto Pick = [](std::initializer_list<int32> Keys, int32 Draw)
	{
		FSequenceDraw D({ Draw });
		return UBattleFormulas::PickByInitiative(TArray<int32>(Keys), [&D]() { return D(); });
	};
	TestEqual(TEXT("a maior chave"), Pick({ 5, 9, 7 }, 0), 1);
	TestEqual(TEXT("a maior no assento 0"), Pick({ 9, 5, 7 }, 0), 0);
	// [5, 9, 9]: a lista é [1, 1, 2] (o 1 entra duas vezes, quem sobe o máximo): rand % 3 -> 1, 1, 2
	TestEqual(TEXT("empate, sorteio 0"), Pick({ 5, 9, 9 }, 0), 1);
	TestEqual(TEXT("empate, sorteio 1"), Pick({ 5, 9, 9 }, 1), 1);
	TestEqual(TEXT("empate, sorteio 2"), Pick({ 5, 9, 9 }, 2), 2);
	// [9, 5, 9]: o assento 0 já tem o máximo: a lista é [0, 2], rand % 2 -> 0, 2
	TestEqual(TEXT("empate com o assento 0, sorteio 0"), Pick({ 9, 5, 9 }, 0), 0);
	TestEqual(TEXT("empate com o assento 0, sorteio 1"), Pick({ 9, 5, 9 }, 1), 2);
	// todas gastas: nada, e nenhum sorteio
	FSequenceDraw Spent({ 0 });
	TestEqual(TEXT("todas as chaves gastas"), UBattleFormulas::PickByInitiative(TArray<int32>({ 0, 0, 0 }), [&Spent]() { return Spent(); }), (int32)INDEX_NONE);
	TestEqual(TEXT("sem sorteio"), Spent.Count, 0);

	// A mesma semente dá a mesma sequência de escolhas
	for (int32 Seed = 1; Seed <= 6; ++Seed)
	{
		FRandomStream A(Seed);
		FRandomStream B(Seed);
		TArray<int32> KeysA = { 12, 30, 30, 30, 7 };
		TArray<int32> KeysB = KeysA;
		TArray<int32> OrderA;
		TArray<int32> OrderB;
		for (int32 n = 0; n < 5; ++n)
		{
			const int32 PA = UBattleFormulas::PickByInitiative(KeysA, A);
			const int32 PB = UBattleFormulas::PickByInitiative(KeysB, B);
			OrderA.Add(PA);
			OrderB.Add(PB);
			if (PA != INDEX_NONE) { KeysA[PA] = 0; }
			if (PB != INDEX_NONE) { KeysB[PB] = 0; }
		}
		TestTrue(FString::Printf(TEXT("semente %d: a mesma ordem"), Seed), OrderA == OrderB);
		TestFalse(TEXT("ninguém age duas vezes"), OrderA.Num() != 5 || OrderA.Contains(INDEX_NONE));
		TArray<int32> Sorted = OrderA;
		Sorted.Sort();
		TestTrue(TEXT("todos agem uma vez"), Sorted == TArray<int32>({ 0, 1, 2, 3, 4 }));
		TestEqual(TEXT("o 30 agem antes do 12 e do 7"), OrderA.Contains(4) && OrderA.Find(4) == 4, true);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleEnemyQueueTest, "JRPG.Battle.EnemyQueue", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleEnemyQueueTest::RunTest(const FString& Parameters)
{
	auto Queue = [](const TArray<int32>& Costs, int32 Gauge, std::initializer_list<int32> Draws)
	{
		FSequenceDraw D(Draws);
		return UBattleFormulas::EnemySwingQueue(Costs, Gauge, [&D]() { return D(); });
	};

	// Os casos do RE (enemy_budget.rs): um golpe de custo 20, gauge 60: exatamente 3
	TestEqual(TEXT("60 / 20 = 3 golpes"), Queue({ 20 }, 60, { 0 }).Num(), 3);
	// custo zero nunca esvazia o gauge: o teto de 15
	TestEqual(TEXT("teto de 15 golpes"), Queue({ 0 }, 100, { 0 }).Num(), UBattleFormulas::MaxEnemyActions);
	// o gauge paga um custo 30 uma vez só
	TestEqual(TEXT("um golpe"), Queue({ 30 }, 30, { 0 }).Num(), 1);
	// o sorteio que não paga conta uma falta, e o seguinte barato entra: 4 golpes do índice 1 (40 / 10)
	const TArray<int32> Two = Queue({ 50, 10 }, 40, { 0, 1, 1, 1, 1 });
	int32 Cheap = 0;
	for (const int32 Pick : Two) { Cheap += Pick == 1 ? 1 : 0; }
	TestEqual(TEXT("quatro golpes baratos"), Cheap, 4);
	TestFalse(TEXT("o caro nunca é pago"), Two.Contains(0));
	// 0xFF é o "não é ataque" do original
	TestTrue(TEXT("0xFF nunca entra"), Queue({ 0xFF }, 100, { 0 }).IsEmpty());
	// sem candidato nenhum, nenhum sorteio
	FSequenceDraw None({ 0 });
	TestTrue(TEXT("sem candidatos"), UBattleFormulas::EnemySwingQueue(TArray<int32>(), 100, [&None]() { return None(); }).IsEmpty());
	TestEqual(TEXT("sem sorteio"), None.Count, 0);
	return true;
}

// --- Tempo e sorteios ------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleTimeTest, "JRPG.Battle.Time", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleTimeTest::RunTest(const FString& Parameters)
{
	// Escala 8, rate 2: 2 quadros lógicos por quadro do clip (30 quadros por segundo no relógio de 60)
	TestEqual(TEXT("40 quadros de clip"), UBattleFormulas::ClipFramesToSteps(40, 8, 2), 80);
	TestEqual(TEXT("rate 1 é metade da velocidade"), UBattleFormulas::ClipFramesToSteps(40, 8, 1), 160);
	TestEqual(TEXT("a Art a escala 4"), UBattleFormulas::ClipFramesToSteps(40, 4, 2), 160);
	TestEqual(TEXT("depois do SpecialStarter, escala 2"), UBattleFormulas::ClipFramesToSteps(40, 2, 2), 320);
	TestEqual(TEXT("clip vazio"), UBattleFormulas::ClipFramesToSteps(0, 8, 2), 0);
	TestEqual(TEXT("arredonda para cima"), UBattleFormulas::ClipFramesToSteps(1, 8, 3), 2);
	// O golpe do Gimard (beat 7, rate 2): dispara no quadro 6 do clip -> 12 quadros lógicos
	TestEqual(TEXT("o golpe do Gimard"), UBattleFormulas::HitStepForBeat(7, 8, 2), 12);
	TestEqual(TEXT("beat 1 dispara no começo"), UBattleFormulas::HitStepForBeat(1, 8, 2), 0);

	// As janelas de juggle medidas pelo RE (battle-formulas.md › The juggle window)
	TestEqual(TEXT("Rogue f1/2"), UBattleFormulas::ReactionWindow(1, 8, 2), 2);
	TestEqual(TEXT("Gobu Gobu f8/2"), UBattleFormulas::ReactionWindow(8, 8, 2), 16);
	TestEqual(TEXT("Zeto f16/2"), UBattleFormulas::ReactionWindow(16, 8, 2), 32);
	TestEqual(TEXT("Caruban f10/1"), UBattleFormulas::ReactionWindow(10, 8, 1), 40);
	TestEqual(TEXT("Vahn f3/1"), UBattleFormulas::ReactionWindow(3, 8, 1), 12);
	TestEqual(TEXT("Gilium f8/2 sob uma Art (escala 4)"), UBattleFormulas::ReactionWindow(8, 4, 2), 32);
	TestEqual(TEXT("sem beat, sem janela"), UBattleFormulas::ReactionWindow(0, 8, 2), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleRandTest, "JRPG.Battle.Rand", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleRandTest::RunTest(const FString& Parameters)
{
	// O rand() de 15 bits da luta: no intervalo, igual para a mesma semente, e não constante
	FRandomStream A(12345);
	FRandomStream B(12345);
	int32 Min = MAX_int32;
	int32 Max = 0;
	TSet<int32> Seen;
	for (int32 i = 0; i < 2000; ++i)
	{
		const int32 X = UBattleFormulas::Rand15(A);
		const int32 Y = UBattleFormulas::Rand15(B);
		TestEqual(TEXT("mesma semente, mesmo sorteio"), X, Y);
		Min = FMath::Min(Min, X);
		Max = FMath::Max(Max, X);
		Seen.Add(X);
	}
	TestTrue(TEXT("de 0 a 0x7FFF"), Min >= 0 && Max <= 0x7FFF);
	TestTrue(TEXT("espalhado"), Seen.Num() > 1500 && Max > 0x6000 && Min < 0x1000);
	// E um sorteio de ResolveMelee pelo FRandomStream é repetível
	FRandomStream C(7);
	FRandomStream D(7);
	FJRPGMeleeInput In = Swing(300, 120);
	In.Juggle = 1;
	for (int32 i = 0; i < 20; ++i)
	{
		TestEqual(TEXT("mesmo dano"), UBattleFormulas::ResolveMelee(In, C).Damage, UBattleFormulas::ResolveMelee(In, D).Damage);
	}
	return true;
}

} // namespace JRPGBattleTests

#endif // WITH_DEV_AUTOMATION_TESTS
