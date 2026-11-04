// Testes da luta (BATTLE_ROADMAP.md › Phase 5): sem mapa. As contas (UBattleFormulas) com as jogadas do RE,
// o exemplo "Vahn vs Evil Fly" do battle-formulas.md e a máquina de estados da luta (FJRPGBattleResolver):
// a tabela de falhas da Fase 5 e o teste de determinismo. Os combatentes são montados à mão; as Arts usam um
// DT_Arts de teste em memória (e o do projeto nos casos das Supers). Session Frontend › Automation, filtro
// "JRPG.Battle".

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Arts/ArtsData.h"
#include "Arts/JRPGArtsRules.h"
#include "Battle/BattleFormulas.h"
#include "Battle/BattleSubsystem.h"
#include "Battle/BattleTypes.h"
#include "Battle/JRPGBattleResolver.h"
#include "Battle/JRPGBattleRules.h"
#include "Core/CoreSubsystem.h"
#include "Engine/DataTable.h"
#include "UObject/Package.h"

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

// =================================================================================================
// A máquina de estados: a tabela de falhas da Fase 5 e o determinismo
// =================================================================================================

namespace
{
	using FFight = FJRPGBattleResolver;

	const EJRPGArtButton Ar = EJRPGArtButton::Arms;
	const EJRPGArtButton Se = EJRPGArtButton::RaSeru;
	const EJRPGArtButton Hi = EJRPGArtButton::High;
	const EJRPGArtButton Lo = EJRPGArtButton::Low;

	/** Um herói de teste: do jogador 0, time 0, sem equipamento, o gauge cheio. */
	FJRPGCombatant Hero(int32 Slot, FName Key, int32 HP, int32 ATK, int32 SPD, int32 AGL = 280)
	{
		FJRPGCombatant C;
		C.SlotIndex = Slot;
		C.Side = EJRPGBattleSide::Party;
		C.Type = EJRPGCombatantType::Character;
		C.Team = 0;
		C.OwnerPlayer = 0;
		C.Key = Key;
		C.HP = C.MaxHP = HP;
		C.ATK = ATK;
		C.UDF = 50;
		C.LDF = 50;
		C.SPD = SPD;
		C.AGL = AGL;
		C.MaxCommandGauge = C.CommandGauge = AGL;
		C.PieceATK.Init(0, UBattleFormulas::PieceCount);
		C.ButtonCost.Init(30, 4);
		C.MaxAP = 100;
		C.bHasRaSeru = true;
		C.FlinchHigh.Beat = 3;
		C.FlinchLow.Beat = 3;
		return C;
	}

	/** Um monstro de teste: o time 1, a IA, um golpe como o Attack1 do Gimard (beat 7, rate 2, 40 quadros, AGL 28). */
	FJRPGCombatant Foe(int32 Slot, FName Key, int32 HP, int32 ATK, int32 SPD, int32 AGL = 60)
	{
		FJRPGCombatant C;
		C.SlotIndex = Slot;
		C.Side = EJRPGBattleSide::Enemy;
		C.Type = EJRPGCombatantType::Enemy;
		C.Team = 1;
		C.OwnerPlayer = -1;
		C.Key = Key;
		C.HP = C.MaxHP = HP;
		C.ATK = ATK;
		C.UDF = 40;
		C.LDF = 60;
		C.SPD = SPD;
		C.AGL = AGL;
		C.MaxCommandGauge = C.CommandGauge = AGL;
		FJRPGBattleActionEntry& Swing = C.Swings.AddDefaulted_GetRef();
		Swing.ActionIndex = 6;
		Swing.Tag = 0x0D;
		Swing.Power = { 0x0D, 0, 0, 0 };
		Swing.HitFrames = { 7 };
		Swing.AGLCost = 28;
		Swing.Rate = 2;
		Swing.FrameCount = 40;
		Swing.Clip = TEXT("Attack1");
		return C;
	}

	FJRPGBattleCommand Attack(int32 Slot, int32 Target)
	{
		FJRPGBattleCommand Command;
		Command.PlayerIndex = 0;
		Command.Slot = Slot;
		Command.Kind = EJRPGBattleCommandKind::Attack;
		Command.TargetSlot = Target;
		return Command;
	}

	FJRPGBattleCommand ArtsCommand(int32 Slot, int32 Target, const TArray<EJRPGArtButton>& Buttons)
	{
		FJRPGBattleCommand Command = Attack(Slot, Target);
		Command.Kind = EJRPGBattleCommandKind::Arts;
		Command.ArtBytes = UJRPGArtsRules::EncodeArtBytes(Buttons);
		return Command;
	}

	/** Todo herói de jogador que ainda não comandou ataca o inimigo vivo de menor slot. */
	void AttackWithEveryone(FFight& Fight)
	{
		for (const FJRPGCombatant& C : Fight.GetCombatants())
		{
			if (C.bAlive && C.Type != EJRPGCombatantType::Enemy && C.OwnerPlayer >= 0
				&& Fight.GetState().Commands[C.SlotIndex].Kind == EJRPGBattleCommandKind::None)
			{
				Fight.SubmitCommand(Attack(C.SlotIndex, Fight.LowestLivingFoe(C)));
			}
		}
	}

	/** Anda a luta com todo mundo atacando até ela acabar (ou MaxFrames quadros). */
	int32 PlayToTheEnd(FFight& Fight, int32 MaxFrames = 60000)
	{
		int32 Frames = 0;
		while (!Fight.IsFinished() && Frames < MaxFrames)
		{
			if (Fight.GetState().Stage == EJRPGBattleStage::Commands)
			{
				AttackWithEveryone(Fight);
				continue;
			}
			Fight.StepFrame();
			++Frames;
		}
		return Frames;
	}

	/** Anda até a ação do slot acabar (o ActionEnd dele); false se a luta acabou antes. */
	bool PlayUntilActionEnds(FFight& Fight, int32 Slot, int32 MaxFrames = 5000)
	{
		bool bStarted = false;
		for (int32 Frames = 0; Frames < MaxFrames && !Fight.IsFinished(); ++Frames)
		{
			const FJRPGActionTimeline* Timeline = Fight.GetActiveTimeline();
			const bool bMine = Timeline && Timeline->SlotIndex == Slot;
			bStarted |= bMine;
			// Acabou quando a dele começou e agora não há ação, ou já é a de outro (a seguinte entra no mesmo quadro)
			if (bStarted && !bMine)
			{
				return true;
			}
			Fight.StepFrame();
		}
		return false;
	}

	int32 CountEvents(const FFight& Fight, EJRPGBattleEventType Type, int32 Slot = INDEX_NONE)
	{
		int32 Count = 0;
		for (const FJRPGBattleEvent& Event : Fight.GetEvents())
		{
			Count += (Event.Type == Type && (Slot == INDEX_NONE || Event.SlotIndex == Slot)) ? 1 : 0;
		}
		return Count;
	}

	const FJRPGBattleEvent* FirstEvent(const FFight& Fight, EJRPGBattleEventType Type, int32 Slot = INDEX_NONE)
	{
		for (const FJRPGBattleEvent& Event : Fight.GetEvents())
		{
			if (Event.Type == Type && (Slot == INDEX_NONE || Event.SlotIndex == Slot))
			{
				return &Event;
			}
		}
		return nullptr;
	}

	/** Os slots na ordem em que as ações começaram. */
	TArray<int32> ActionOrder(const FFight& Fight)
	{
		TArray<int32> Order;
		for (const FJRPGBattleEvent& Event : Fight.GetEvents())
		{
			if (Event.Type == EJRPGBattleEventType::ActionStart)
			{
				Order.Add(Event.SlotIndex);
			}
		}
		return Order;
	}

	bool SameEvents(const TArray<FJRPGBattleEvent>& A, const TArray<FJRPGBattleEvent>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const FJRPGBattleEvent& X = A[i];
			const FJRPGBattleEvent& Y = B[i];
			if (X.Type != Y.Type || X.SlotIndex != Y.SlotIndex || X.TargetSlot != Y.TargetSlot || X.Value != Y.Value
				|| X.ActionIndex != Y.ActionIndex || X.HitIndex != Y.HitIndex || X.Frame != Y.Frame || X.Flags != Y.Flags
				|| X.ArtKey != Y.ArtKey)
			{
				return false;
			}
		}
		return true;
	}

	/** Um DT_Arts de teste, em memória: uma Art Regular de 3 botões, uma que erra o voador e uma de 2 golpes. */
	UDataTable* TestArts()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FJRPGArtData::StaticStruct();

		FJRPGArtData Somersault;
		Somersault.Key = TEXT("test_somersault");
		Somersault.Character = TEXT("Vahn");
		Somersault.Kind = EJRPGArtKind::Regular;
		Somersault.Command = { Hi, Lo, Hi };
		Somersault.APGain = 9;
		Somersault.HitPower = { 20 };
		Somersault.HitTarget = { EJRPGArtHitTarget::Upper };
		Table->AddRow(Somersault.Key, Somersault);

		FJRPGArtData Sweep = Somersault;
		Sweep.Key = TEXT("test_sweep");
		Sweep.Command = { Lo, Lo, Lo };
		Sweep.APGain = 12;
		Sweep.HitPower = { 18, 18 };
		Sweep.HitTarget = { EJRPGArtHitTarget::LowerMissesFloating, EJRPGArtHitTarget::LowerMissesFloating };
		Table->AddRow(Sweep.Key, Sweep);
		return Table;
	}

// Os testes ficam dentro do namespace principal (como os das Arts); estes ajudantes só existem aqui.
}

// --- A rodada e a ordem ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleRoundTest, "JRPG.Battle.Round", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleRoundTest::RunTest(const FString& Parameters)
{
	// Dois heróis (SPD 100 e 50) contra dois monstros (80 e 20), todos de HP enorme: com os sorteios em 0 a chave é
	// SPD + 1, então a ordem é o herói 0, o monstro 2, o herói 1, o monstro 3
	FFight Fight;
	Fight.RandOverride = []() { return 0; };
	Fight.Begin({ Hero(0, TEXT("Vahn"), 100000, 100, 100), Hero(1, TEXT("Noa"), 100000, 100, 50), Foe(2, TEXT("a"), 100000, 100, 80), Foe(3, TEXT("b"), 100000, 100, 20) }, 1, false, 0);

	TestEqual(TEXT("a rodada 1 espera os comandos"), (int32)Fight.GetState().Stage, (int32)EJRPGBattleStage::Commands);
	TestEqual(TEXT("quem digita é o herói 0"), Fight.GetActiveSlot(), 0);
	TestTrue(TEXT("o primeiro comando"), Fight.SubmitCommand(Attack(0, 2)));
	TestEqual(TEXT("agora o herói 1"), Fight.GetActiveSlot(), 1);
	TestEqual(TEXT("ainda em Commands"), (int32)Fight.GetState().Stage, (int32)EJRPGBattleStage::Commands);
	TestTrue(TEXT("o último comando"), Fight.SubmitCommand(Attack(1, 3)));
	TestEqual(TEXT("a execução começou"), (int32)Fight.GetState().Stage, (int32)EJRPGBattleStage::Execution);

	// O monstro de AGL 60 e golpe de custo 28 enfileira 2 golpes (28 + 28 <= 60 < 84)
	int32 EnemySteps = 0;
	for (int32 Frames = 0; Frames < 3000 && Fight.GetState().Round == 1; ++Frames)
	{
		if (const FJRPGActionTimeline* Timeline = Fight.GetActiveTimeline())
		{
			if (Timeline->SlotIndex == 2 && EnemySteps == 0)
			{
				EnemySteps = Timeline->Steps.Num();
				TestEqual(TEXT("dois golpes na fila do monstro"), Timeline->Hits.Num(), 2);
			}
		}
		Fight.StepFrame();
	}
	TestEqual(TEXT("o monstro enfileirou dois golpes"), EnemySteps, 2);
	TArray<int32> Order = ActionOrder(Fight);
	Order.SetNum(FMath::Min(Order.Num(), 4));
	TestTrue(TEXT("a ordem da rodada 1: pela chave, SPD + 1"), Order == TArray<int32>({ 0, 2, 1, 3 }));
	TestEqual(TEXT("a rodada 2 começou"), Fight.GetState().Round, 2);
	TestEqual(TEXT("a rodada 2 abriu os comandos"), (int32)Fight.GetState().Stage, (int32)EJRPGBattleStage::Commands);
	TestEqual(TEXT("dois RoundStart"), CountEvents(Fight, EJRPGBattleEventType::RoundStart), 2);
	// O pool de AGL voltou ao máximo e os comandos foram zerados
	TestEqual(TEXT("o pool do monstro voltou"), Fight.GetCombatants()[2].CommandGauge, 60);
	TestEqual(TEXT("o comando da rodada passada saiu"), (int32)Fight.GetState().Commands[0].Kind, (int32)EJRPGBattleCommandKind::None);
	return true;
}

// --- A tabela de falhas ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleCommandOwnerTest, "JRPG.Battle.CommandOwner", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleCommandOwnerTest::RunTest(const FString& Parameters)
{
	FFight Fight;
	Fight.RandOverride = []() { return 0; };
	Fight.Begin({ Hero(0, TEXT("Vahn"), 1000, 100, 50), Hero(1, TEXT("Noa"), 1000, 100, 50), Foe(2, TEXT("a"), 1000, 100, 10) }, 1, false, 0);
	FString Why;
	auto Refused = [&](const FJRPGBattleCommand& Command, const TCHAR* What)
	{
		const bool bAccepted = Fight.SubmitCommand(Command, &Why);
		TestFalse(What, bAccepted);
		TestFalse(FString::Printf(TEXT("%s: diz por quê"), What), Why.IsEmpty());
		TestEqual(FString::Printf(TEXT("%s: nada mudou"), What), (int32)Fight.GetState().Commands[Command.Slot >= 0 && Command.Slot < 3 ? Command.Slot : 0].Kind, (int32)EJRPGBattleCommandKind::None);
	};

	// Outro jogador manda no slot 0
	FJRPGBattleCommand Intruder = Attack(0, 2);
	Intruder.PlayerIndex = 1;
	Refused(Intruder, TEXT("o jogador 1 no slot do jogador 0"));
	// Ninguém manda na IA
	FJRPGBattleCommand ForTheMonster = Attack(2, 0);
	Refused(ForTheMonster, TEXT("um comando para o monstro"));
	// O monstro como PlayerIndex -1 (o dono da IA) também não
	FJRPGBattleCommand AsAI = Attack(0, 2);
	AsAI.PlayerIndex = -1;
	Refused(AsAI, TEXT("o dono -1"));
	// Slot que não existe
	FJRPGBattleCommand Nowhere = Attack(9, 2);
	Nowhere.Slot = 9;
	TestFalse(TEXT("slot 9"), Fight.SubmitCommand(Nowhere));
	// Alvo aliado, alvo que não existe
	Refused(Attack(0, 1), TEXT("o alvo é um aliado"));
	Refused(Attack(0, 7), TEXT("o alvo não existe"));
	// Item e Magic são das Fases 7 e 8
	FJRPGBattleCommand Item = Attack(0, 2);
	Item.Kind = EJRPGBattleCommandKind::Item;
	Refused(Item, TEXT("Item"));
	FJRPGBattleCommand Magic = Attack(0, 2);
	Magic.Kind = EJRPGBattleCommandKind::Magic;
	Refused(Magic, TEXT("Magic"));
	// Bytes que não são botões, vazios, cadeia demais, e mais cara que o gauge
	FJRPGBattleCommand BadBytes = ArtsCommand(0, 2, { Hi });
	BadBytes.ArtBytes = { 9 };
	Refused(BadBytes, TEXT("byte que não é botão"));
	FJRPGBattleCommand Empty = ArtsCommand(0, 2, {});
	Refused(Empty, TEXT("cadeia vazia"));
	FJRPGBattleCommand TooLong = ArtsCommand(0, 2, TArray<EJRPGArtButton>());
	TooLong.ArtBytes.Init(0, 17);
	Refused(TooLong, TEXT("17 botões"));
	FJRPGBattleCommand TooExpensive = ArtsCommand(1, 2, { Ar, Ar, Ar, Ar, Ar, Ar, Ar, Ar, Ar, Ar });   // 300 > o gauge de 280
	Refused(TooExpensive, TEXT("mais caro que o gauge"));

	// Nada disso gastou a vez de ninguém: o dono ainda comanda, e o comando de verdade passa
	TestEqual(TEXT("a rodada segue em Commands"), (int32)Fight.GetState().Stage, (int32)EJRPGBattleStage::Commands);
	TestTrue(TEXT("o dono comanda"), Fight.SubmitCommand(Attack(0, 2)));
	// Durante a execução não se comanda
	TestTrue(TEXT("o último comando"), Fight.SubmitCommand(Attack(1, 2)));
	TestEqual(TEXT("em Execution"), (int32)Fight.GetState().Stage, (int32)EJRPGBattleStage::Execution);
	TestFalse(TEXT("comando fora de Commands"), Fight.SubmitCommand(Attack(0, 2), &Why));

	// O input: dono, vivo e carimbo que existe
	FJRPGBattleInput Input;
	Input.PlayerIndex = 0;
	Input.Slot = 0;
	Input.Frame = 0;
	TestTrue(TEXT("o parry do dono"), Fight.SubmitInput(Input));
	Input.PlayerIndex = 1;
	TestFalse(TEXT("o parry do jogador errado"), Fight.SubmitInput(Input));
	Input.PlayerIndex = 0;
	Input.Frame = Fight.GetState().Frame + 5;
	TestFalse(TEXT("um carimbo do futuro"), Fight.SubmitInput(Input));
	Input.Frame = 0;
	Input.Slot = 2;
	TestFalse(TEXT("o parry do monstro"), Fight.SubmitInput(Input));
	TestEqual(TEXT("só o aceito ficou"), Fight.GetState().Inputs.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleRetargetTest, "JRPG.Battle.Retarget", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleRetargetTest::RunTest(const FString& Parameters)
{
	// O alvo cai antes da ação: o herói mais lento é re-apontado para o inimigo vivo de menor slot
	{
		FFight Fight;
		Fight.RandOverride = []() { return 0; };
		// O herói 0 (rápido) mata o monstro 2 (HP 1); o herói 1 mirava o 2 e passa a mirar o 3
		Fight.Begin({ Hero(0, TEXT("Vahn"), 100000, 500, 100), Hero(1, TEXT("Noa"), 100000, 500, 50), Foe(2, TEXT("a"), 1, 10, 1), Foe(3, TEXT("b"), 100000, 10, 1) }, 1, false, 0);
		Fight.SubmitCommand(Attack(0, 2));
		Fight.SubmitCommand(Attack(1, 2));
		for (int32 Frames = 0; Frames < 600 && CountEvents(Fight, EJRPGBattleEventType::ActionStart, 1) == 0; ++Frames)
		{
			Fight.StepFrame();
		}
		const FJRPGBattleEvent* Second = FirstEvent(Fight, EJRPGBattleEventType::ActionStart, 1);
		if (TestNotNull(TEXT("o herói 1 agiu"), Second))
		{
			TestEqual(TEXT("re-apontado para o inimigo vivo de menor slot"), Second->TargetSlot, 3);
		}
		TestEqual(TEXT("o primeiro matou o 2"), CountEvents(Fight, EJRPGBattleEventType::Death, 2), 1);
	}

	// Um monstro sorteia outro herói vivo: nunca o que já caiu
	for (int32 Seed = 1; Seed <= 40; ++Seed)
	{
		FFight Fight;
		// Os monstros são rápidos e batem forte; o herói 0 (HP 1) cai no primeiro golpe que o sortear
		Fight.Begin({ Hero(0, TEXT("Vahn"), 1, 100, 1), Hero(1, TEXT("Noa"), 100000, 100, 1), Foe(2, TEXT("a"), 100000, 5000, 1000), Foe(3, TEXT("b"), 100000, 5000, 500) }, Seed, false, 0);
		PlayToTheEnd(Fight, 4000);
		TSet<int32> Dead;
		int32 ActionsAfterAFall = 0;
		for (const FJRPGBattleEvent& Event : Fight.GetEvents())
		{
			if (Event.Type == EJRPGBattleEventType::Death)
			{
				Dead.Add(Event.SlotIndex);
			}
			else if (Event.Type == EJRPGBattleEventType::ActionStart && Event.SlotIndex >= 2 && Dead.Num() > 0)
			{
				++ActionsAfterAFall;
				TestFalse(FString::Printf(TEXT("semente %d: ninguém mira quem já caiu"), Seed), Dead.Contains(Event.TargetSlot));
			}
		}
		TestTrue(TEXT("o herói 0 caiu e a luta seguiu"), Dead.Contains(0));
	}

	// Nenhum herói vivo para ser alvo: a luta acaba (Derrota) e o monstro seguinte não age
	{
		FFight Fight;
		Fight.RandOverride = []() { return 0; };
		Fight.Begin({ Hero(0, TEXT("Vahn"), 1, 100, 1), Foe(1, TEXT("a"), 100000, 5000, 1000), Foe(2, TEXT("b"), 100000, 5000, 500) }, 1, false, 0);
		Fight.SubmitCommand(Attack(0, 1));
		for (int32 Frames = 0; Frames < 2000 && !Fight.IsFinished(); ++Frames)
		{
			Fight.StepFrame();
		}
		TestTrue(TEXT("a luta acabou"), Fight.IsFinished());
		TestEqual(TEXT("Derrota"), (int32)Fight.GetResult(), (int32)EJRPGBattleResult::Defeat);
		TestEqual(TEXT("o segundo monstro não agiu"), CountEvents(Fight, EJRPGBattleEventType::ActionStart, 2), 0);
		TestEqual(TEXT("o herói não chegou a agir"), CountEvents(Fight, EJRPGBattleEventType::ActionStart, 0), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleAttackerDiesTest, "JRPG.Battle.AttackerDies", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleAttackerDiesTest::RunTest(const FString& Parameters)
{
	// O atacante cai antes da vez dele: o comando é descartado e nada acontece com ele
	FFight Fight;
	Fight.RandOverride = []() { return 0; };
	Fight.Begin({ Hero(0, TEXT("Vahn"), 1, 100, 1), Hero(1, TEXT("Noa"), 100000, 100, 5), Foe(2, TEXT("a"), 100000, 5000, 1000) }, 1, false, 0);
	Fight.SubmitCommand(Attack(0, 2));
	Fight.SubmitCommand(Attack(1, 2));
	for (int32 Frames = 0; Frames < 3000 && Fight.GetState().Round == 1; ++Frames)
	{
		Fight.StepFrame();
	}
	TestEqual(TEXT("o herói 0 caiu"), Fight.GetCombatants()[0].bAlive, false);
	TestEqual(TEXT("o comando dele foi descartado"), CountEvents(Fight, EJRPGBattleEventType::ActionStart, 0), 0);
	TestEqual(TEXT("o outro herói agiu"), CountEvents(Fight, EJRPGBattleEventType::ActionStart, 1), 1);
	TestEqual(TEXT("o monstro agiu na rodada"), CountEvents(Fight, EJRPGBattleEventType::ActionStart, 2), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleTiesTest, "JRPG.Battle.Ties", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleTiesTest::RunTest(const FString& Parameters)
{
	// SPD 0 em todos: a chave é 1 para todo mundo, e a ordem sai inteira do desempate sorteado do Rng da luta
	auto FirstRound = [](int32 Seed)
	{
		FFight Fight;
		Fight.Begin({ Hero(0, TEXT("Vahn"), 100000, 10, 0), Hero(1, TEXT("Noa"), 100000, 10, 0), Foe(2, TEXT("a"), 100000, 10, 0), Foe(3, TEXT("b"), 100000, 10, 0) }, Seed, false, 0);
		Fight.SubmitCommand(Attack(0, 2));
		Fight.SubmitCommand(Attack(1, 2));
		for (int32 Frames = 0; Frames < 3000 && Fight.GetState().Round == 1; ++Frames)
		{
			Fight.StepFrame();
		}
		TArray<int32> Order = ActionOrder(Fight);
		Order.SetNum(FMath::Min(Order.Num(), 4));
		return Order;
	};

	TSet<FString> Distinct;
	for (int32 Seed = 1; Seed <= 40; ++Seed)
	{
		const TArray<int32> A = FirstRound(Seed);
		const TArray<int32> B = FirstRound(Seed);
		TestTrue(FString::Printf(TEXT("semente %d: a mesma ordem toda vez"), Seed), A == B);
		TestEqual(TEXT("os quatro agem"), A.Num(), 4);
		TArray<int32> Sorted = A;
		Sorted.Sort();
		TestTrue(TEXT("cada um uma vez"), Sorted == TArray<int32>({ 0, 1, 2, 3 }));
		FString Key;
		for (const int32 Slot : A) { Key += FString::FromInt(Slot); }
		Distinct.Add(Key);
	}
	TestTrue(TEXT("o desempate de fato varia com a semente"), Distinct.Num() > 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleKernelWiringTest, "JRPG.Battle.KernelWiring", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleKernelWiringTest::RunTest(const FString& Parameters)
{
	// O ATK do herói é o BASE; o golpe dobra a peça do papel dele, pela metade (Arms a arma, High/Low o calçado). Com
	// todos os sorteios em 0 o dano sai igual ao do núcleo isolado, golpe por golpe
	auto FirstDamage = [](const TArray<EJRPGArtButton>& Buttons)
	{
		FFight Fight;
		Fight.RandOverride = []() { return 0; };
		FJRPGCombatant Vahn = Hero(0, TEXT("Vahn"), 2560, 100, 50);
		// Weapon 98, Ra-Seru 100, Shoes 86, Body 4, Head 0
		Vahn.PieceATK = { 98, 100, 86, 4, 0 };
		FJRPGCombatant Dummy = Foe(1, TEXT("dummy"), 1000000, 10, 1);
		Dummy.UDF = 40;
		Dummy.LDF = 60;
		Fight.Begin({ Vahn, Dummy }, 1, false, 0);
		Fight.SubmitCommand(Buttons.Num() == 1 && Buttons[0] == Ar ? Attack(0, 1) : ArtsCommand(0, 1, Buttons));
		PlayUntilActionEnds(Fight, 0);
		const FJRPGBattleEvent* Hit = FirstEvent(Fight, EJRPGBattleEventType::Damage, 0);
		return Hit ? Hit->Value : -1;
	};

	// O Attack é o Arms: ATK base 100 + (98 >> 1) = 149 contra a UDF 40, poder 20, juggle 1
	FJRPGMeleeInput Expected;
	Expected.BaseOffense = 149;
	Expected.AttackerHP = 2560;
	Expected.DefenderDefense = 40;
	Expected.PowerByte = 0x18;
	Expected.Juggle = 1;
	FSequenceDraw Zero({ 0 });
	const int32 ArmsDamage = UBattleFormulas::ResolveMelee(Expected, [&Zero]() { return Zero(); }).Damage;
	TestEqual(TEXT("Attack = o botão Arms, com a metade da arma"), FirstDamage({ Ar }), ArmsDamage);
	TestEqual(TEXT("o número do cálculo à mão"), ArmsDamage, 158);

	// High: o calçado (86 >> 1 = 43) contra a UDF; Low: o calçado contra a LDF 60 (byte 0x1D)
	Expected.BaseOffense = 143;
	FSequenceDraw ZeroHigh({ 0 });
	TestEqual(TEXT("High = a metade do calçado, UDF"), FirstDamage({ Hi }),
		UBattleFormulas::ResolveMelee(Expected, [&ZeroHigh]() { return ZeroHigh(); }).Damage);
	Expected.PowerByte = 0x1D;
	Expected.DefenderDefense = 60;
	FSequenceDraw ZeroLow({ 0 });
	TestEqual(TEXT("Low = a metade do calçado, LDF"), FirstDamage({ Lo }),
		UBattleFormulas::ResolveMelee(Expected, [&ZeroLow]() { return ZeroLow(); }).Damage);

	// O Ra-Seru dobra o tier: 100 + (100 >> 1) = 150
	Expected.BaseOffense = 150;
	Expected.PowerByte = 0x18;
	Expected.DefenderDefense = 40;
	FSequenceDraw ZeroSeru({ 0 });
	TestEqual(TEXT("Ra-Seru = a metade do tier"), FirstDamage({ Se }),
		UBattleFormulas::ResolveMelee(Expected, [&ZeroSeru]() { return ZeroSeru(); }).Damage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleComboTest, "JRPG.Battle.Combo", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleComboTest::RunTest(const FString& Parameters)
{
	// O último inimigo cai no meio da combo: o resto da cadeia não resolve, e a vitória sai quando a ação acaba
	{
		FFight Fight;
		Fight.RandOverride = []() { return 0; };
		Fight.Begin({ Hero(0, TEXT("Vahn"), 100000, 500, 100), Foe(1, TEXT("a"), 1, 10, 1) }, 1, false, 0);
		Fight.SubmitCommand(ArtsCommand(0, 1, { Ar, Ar, Ar, Ar }));   // quatro golpes de Arms
		for (int32 Frames = 0; Frames < 2000 && !Fight.IsFinished(); ++Frames)
		{
			Fight.StepFrame();
		}
		TestEqual(TEXT("um só golpe resolveu"), CountEvents(Fight, EJRPGBattleEventType::Damage), 1);
		TestEqual(TEXT("o inimigo caiu uma vez"), CountEvents(Fight, EJRPGBattleEventType::Death, 1), 1);
		TestTrue(TEXT("a luta acabou"), Fight.IsFinished());
		TestEqual(TEXT("Vitória"), (int32)Fight.GetResult(), (int32)EJRPGBattleResult::Victory);
		TestEqual(TEXT("a ação terminou (cortada)"), CountEvents(Fight, EJRPGBattleEventType::ActionEnd, 0), 1);
		const FJRPGBattleEvent* End = FirstEvent(Fight, EJRPGBattleEventType::ActionEnd, 0);
		const FJRPGBattleEvent* Death = FirstEvent(Fight, EJRPGBattleEventType::Death, 1);
		TestTrue(TEXT("o ActionEnd depois do Death"), End && Death && End->Frame > Death->Frame);
		TestTrue(TEXT("e antes do fim cheio da timeline"), End && Fight.GetState().Frame < 400);
	}

	// A combo de Arms contra um monstro que reage: o juggle sobe golpe a golpe (poder 20, sorteios em 0)
	{
		FFight Fight;
		Fight.RandOverride = []() { return 0; };
		FJRPGCombatant Vahn = Hero(0, TEXT("Vahn"), 2560, 100, 100);
		FJRPGCombatant Dummy = Foe(1, TEXT("dummy"), 1000000, 10, 1);
		Dummy.FlinchHigh.Beat = 40;   // 40 * 32 / 8 = 160 quadros de janela
		Dummy.FlinchHigh.Rate = 1;
		Dummy.FlinchLow = Dummy.FlinchHigh;
		Fight.Begin({ Vahn, Dummy }, 1, false, 0);
		Fight.SubmitCommand(ArtsCommand(0, 1, { Ar, Ar, Ar }));
		PlayUntilActionEnds(Fight, 0);
		TArray<int32> Damages;
		for (const FJRPGBattleEvent& Event : Fight.GetEvents())
		{
			if (Event.Type == EJRPGBattleEventType::Damage && Event.SlotIndex == 0)
			{
				Damages.Add(Event.Value);
			}
		}
		if (TestEqual(TEXT("três golpes"), Damages.Num(), 3))
		{
			for (int32 Juggle = 1; Juggle <= 3; ++Juggle)
			{
				FJRPGMeleeInput In;
				In.BaseOffense = 100;
				In.AttackerHP = 2560;
				In.DefenderDefense = 40;
				In.PowerByte = 0x18;
				In.Juggle = Juggle;
				FSequenceDraw Zero({ 0 });
				TestEqual(FString::Printf(TEXT("o golpe %d com juggle %d"), Juggle, Juggle), Damages[Juggle - 1],
					UBattleFormulas::ResolveMelee(In, [&Zero]() { return Zero(); }).Damage);
			}
			TestTrue(TEXT("cada golpe da combo bate mais que o anterior"), Damages[0] < Damages[1] && Damages[1] < Damages[2]);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleFallbackTest, "JRPG.Battle.Fallback", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleFallbackTest::RunTest(const FString& Parameters)
{
	// Sem clip nenhum (o provedor é nulo): os quadros padrão por tipo, e a luta anda
	FFight Fight;
	Fight.bLogFallbacks = false;
	Fight.RandOverride = []() { return 0; };
	Fight.Begin({ Hero(0, TEXT("Vahn"), 100000, 100, 100), Foe(1, TEXT("a"), 1000000, 10, 1) }, 1, false, 0);
	Fight.SubmitCommand(Attack(0, 1));
	const FJRPGActionTimeline* Timeline = Fight.GetActiveTimeline();
	if (!TestNotNull(TEXT("a ação começou"), Timeline))
	{
		return false;
	}
	TestTrue(TEXT("marcada como quadros padrão"), Timeline->bUsedDefaults);
	const FJRPGTimelineDefaults& D = Fight.Defaults;
	TestEqual(TEXT("o golpe cai no quadro do padrão"), Timeline->Hits.Num() == 1 ? Timeline->Hits[0].Frame : -1, D.LeadFrames + D.SwingHitFrame);
	TestEqual(TEXT("a ação dura o padrão"), Timeline->TotalFrames, D.LeadFrames + D.SwingFrames + D.TailFrames);
	PlayUntilActionEnds(Fight, 0);
	const FJRPGBattleEvent* Hit = FirstEvent(Fight, EJRPGBattleEventType::Damage, 0);
	if (TestNotNull(TEXT("o golpe resolveu"), Hit))
	{
		TestEqual(TEXT("exatamente no quadro dele"), Hit->Frame - FirstEvent(Fight, EJRPGBattleEventType::ActionStart, 0)->Frame, D.LeadFrames + D.SwingHitFrame);
	}

	// O provedor de clips: os marcadores lidos como dado mandam no quadro do golpe
	FFight WithClips;
	WithClips.RandOverride = []() { return 0; };
	WithClips.ClipTiming = [](FName, const FJRPGClipQuery& Query, FJRPGClipTiming& Out) -> bool
	{
		Out.Frames = 60;
		if (Query.ExpectedHits == 1) { Out.HitFrames = { 25 }; }
		return true;
	};
	WithClips.Begin({ Hero(0, TEXT("Vahn"), 100000, 100, 100), Foe(1, TEXT("a"), 1000000, 10, 1) }, 1, false, 0);
	WithClips.SubmitCommand(Attack(0, 1));
	const FJRPGActionTimeline* Read = WithClips.GetActiveTimeline();
	if (TestNotNull(TEXT("com o provedor"), Read))
	{
		TestEqual(TEXT("o golpe no quadro do marcador"), Read->Hits[0].Frame, WithClips.Defaults.LeadFrames + 25);
		TestFalse(TEXT("sem quadros padrão"), Read->bUsedDefaults);
	}

	// Um monstro sem nenhuma linha de golpe luta com o golpe padrão (e o log diz); o poder é o 0x0D
	FFight NoSwing;
	NoSwing.bLogFallbacks = false;
	NoSwing.RandOverride = []() { return 0; };
	FJRPGCombatant Bare = Foe(1, TEXT("bare"), 1000000, 10, 1000);
	Bare.Swings.Reset();
	NoSwing.Begin({ Hero(0, TEXT("Vahn"), 100000, 100, 1), Bare }, 1, false, 0);
	NoSwing.SubmitCommand(Attack(0, 1));
	for (int32 Frames = 0; Frames < 400 && CountEvents(NoSwing, EJRPGBattleEventType::Damage, 1) == 0; ++Frames)
	{
		NoSwing.StepFrame();
	}
	TestEqual(TEXT("o monstro sem golpe bateu mesmo assim"), CountEvents(NoSwing, EJRPGBattleEventType::Damage, 1), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleArtsInFightTest, "JRPG.Battle.ArtsInFight", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleArtsInFightTest::RunTest(const FString& Parameters)
{
	UDataTable* Arts = TestArts();
	Arts->AddToRoot();

	// Uma Art desconhecida: aprendida na luta (no combatente), com o aviso uma vez, e o AP SOBE no primeiro golpe dela
	{
		FFight Fight;
		Fight.RandOverride = []() { return 0; };
		Fight.ArtsTable = Arts;
		FJRPGCombatant Vahn = Hero(0, TEXT("Vahn"), 100000, 100, 100);
		Vahn.AP = 10;
		Fight.Begin({ Vahn, Foe(1, TEXT("a"), 1000000, 10, 1) }, 1, false, 0);
		Fight.SubmitCommand(ArtsCommand(0, 1, { Hi, Lo, Hi }));
		PlayUntilActionEnds(Fight, 0);

		TestEqual(TEXT("dois golpes simples e a Art: três golpes"), CountEvents(Fight, EJRPGBattleEventType::Damage, 0), 3);
		TestEqual(TEXT("a Art foi reconhecida"), CountEvents(Fight, EJRPGBattleEventType::ArtRecognized, 0), 1);
		TestEqual(TEXT("e aprendida, uma vez"), CountEvents(Fight, EJRPGBattleEventType::ArtLearned, 0), 1);
		TestTrue(TEXT("a lista do combatente cresceu"), Fight.GetCombatants()[0].KnownArts.Contains(TEXT("test_somersault")));
		TestEqual(TEXT("o AP subiu o APGain"), Fight.GetCombatants()[0].AP, 19);
		const FJRPGBattleEvent* Gain = FirstEvent(Fight, EJRPGBattleEventType::APChanged, 0);
		if (TestNotNull(TEXT("o aviso de AP"), Gain))
		{
			TestEqual(TEXT("o valor novo"), Gain->Value, 19);
			// o crédito vem no primeiro golpe da Art, não no começo da ação
			int32 Hits = 0;
			for (const FJRPGBattleEvent& Event : Fight.GetEvents())
			{
				if (&Event == Gain) { break; }
				Hits += Event.Type == EJRPGBattleEventType::Damage ? 1 : 0;
			}
			TestEqual(TEXT("depois dos dois golpes simples"), Hits, 2);
		}
	}

	// Cortada antes de chegar: o que não chegou nem se aprende nem se credita
	{
		FFight Fight;
		Fight.RandOverride = []() { return 0; };
		Fight.ArtsTable = Arts;
		FJRPGCombatant Vahn = Hero(0, TEXT("Vahn"), 100000, 500, 100);
		Vahn.AP = 10;
		Fight.Begin({ Vahn, Foe(1, TEXT("a"), 1, 10, 1) }, 1, false, 0);
		Fight.SubmitCommand(ArtsCommand(0, 1, { Hi, Lo, Hi }));
		for (int32 Frames = 0; Frames < 2000 && !Fight.IsFinished(); ++Frames)
		{
			Fight.StepFrame();
		}
		TestEqual(TEXT("o 1º golpe matou"), CountEvents(Fight, EJRPGBattleEventType::Damage, 0), 1);
		TestEqual(TEXT("nada aprendido"), CountEvents(Fight, EJRPGBattleEventType::ArtLearned), 0);
		TestEqual(TEXT("o AP não andou"), Fight.GetCombatants()[0].AP, 10);
		TestTrue(TEXT("a lista não cresceu"), Fight.GetCombatants()[0].KnownArts.IsEmpty());
	}

	// O golpe baixo da Art erra o monstro que voa (classe 2): o Miss sai, não há dano, e o AP credita do mesmo jeito
	{
		FFight Fight;
		Fight.RandOverride = []() { return 0; };
		Fight.ArtsTable = Arts;
		FJRPGCombatant Vahn = Hero(0, TEXT("Vahn"), 100000, 100, 100);
		Vahn.KnownArts = { TEXT("test_sweep") };
		FJRPGCombatant Flier = Foe(1, TEXT("flier"), 1000000, 10, 1);
		Flier.TargetClass = 2;
		Fight.Begin({ Vahn, Flier }, 1, false, 0);
		Fight.SubmitCommand(ArtsCommand(0, 1, { Lo, Lo, Lo }));   // as duas primeiras são golpes simples (0x1D, alcançam)
		PlayUntilActionEnds(Fight, 0);
		TestEqual(TEXT("dois golpes simples acertam"), CountEvents(Fight, EJRPGBattleEventType::Damage, 0), 2);
		TestEqual(TEXT("os dois golpes da Art erram pela altura"), CountEvents(Fight, EJRPGBattleEventType::Miss, 0), 2);
		TestEqual(TEXT("o AP creditou"), Fight.GetCombatants()[0].AP, 12);
	}
	Arts->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleSuperInFightTest, "JRPG.Battle.SuperInFight", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleSuperInFightTest::RunTest(const FString& Parameters)
{
	const UDataTable* Arts = UCoreSubsystem::ResolveDataTable(TEXT("DT_Arts"));
	if (!Arts || Arts->GetRowStruct() != FJRPGArtData::StaticStruct())
	{
		AddError(TEXT("DT_Arts (FJRPGArtData) não encontrado em /Game/Data nem no plugin"));
		return false;
	}
	const TArray<EJRPGArtButton> TriSomersaultButtons = { Hi, Lo, Hi, Hi, Hi, Lo, Hi };

	auto Fire = [&](int32 AP, TArray<FName> Known, int32& OutAP, int32& OutDamageEvents, int32& OutRecognized)
	{
		FFight Fight;
		Fight.RandOverride = []() { return 0; };
		Fight.ArtsTable = Arts;
		FJRPGCombatant Vahn = Hero(0, TEXT("Vahn"), 100000, 300, 100);
		Vahn.AP = AP;
		Vahn.KnownArts = Known;
		FJRPGCombatant Dummy = Foe(1, TEXT("dummy"), 100000000, 10, 1);
		Fight.Begin({ Vahn, Dummy }, 1, false, 0);
		Fight.SubmitCommand(ArtsCommand(0, 1, TriSomersaultButtons));
		PlayUntilActionEnds(Fight, 0);
		OutAP = Fight.GetCombatants()[0].AP;
		OutDamageEvents = CountEvents(Fight, EJRPGBattleEventType::Damage, 0);
		bool bSuper = false;
		for (const FJRPGBattleEvent& Event : Fight.GetEvents())
		{
			bSuper |= Event.Type == EJRPGBattleEventType::ArtRecognized && Event.ArtKey == TEXT("vahn_tri_somersault");
		}
		OutRecognized = bSuper ? 1 : 0;
	};

	const TArray<FName> Known = { TEXT("vahn_somersault"), TEXT("vahn_cyclone") };
	int32 AP = 0;
	int32 Hits = 0;
	int32 Super = 0;

	// Com o AP cheio a Super dispara: o AP CAI (100 -> 40) e o turno inteiro bate (as Arts de dentro não somam nada)
	Fire(100, Known, AP, Hits, Super);
	TestEqual(TEXT("a Super disparou"), Super, 1);
	TestEqual(TEXT("o AP caiu o APCost (100 - 60)"), AP, 40);
	TestTrue(TEXT("e bateu vários golpes"), Hits >= 8);

	// Sem AP para ela (50 < 60) a Super cai nas Arts dela, que somam como Arts soltas: 50 + 9 + 12 + 9 = 80
	int32 DowngradedHits = 0;
	Fire(50, Known, AP, DowngradedHits, Super);
	TestEqual(TEXT("a Super não saiu"), Super, 0);
	TestEqual(TEXT("o AP SUBIU com as Arts"), AP, 80);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleBlockInFightTest, "JRPG.Battle.BlockInFight", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleBlockInFightTest::RunTest(const FString& Parameters)
{
	// Um monstro que tem o clip de block e boa defesa de iniciativa/ATK bloqueia parte dos golpes: o Block sai, sem
	// dano; quem não tem o clip nunca bloqueia
	int32 Blocks = 0;
	int32 Hits = 0;
	int32 BlockedWithDamage = 0;
	for (int32 Seed = 1; Seed <= 60; ++Seed)
	{
		FFight Fight;
		FJRPGCombatant Vahn = Hero(0, TEXT("Vahn"), 100000, 20, 10);
		FJRPGCombatant Guard = Foe(1, TEXT("guard"), 100000000, 400, 100);
		Guard.bHasBlock = true;
		Guard.BlockClip.Beat = 5;
		Guard.BlockClip.Rate = 2;
		Fight.Begin({ Vahn, Guard }, Seed, false, 0);
		Fight.SubmitCommand(ArtsCommand(0, 1, { Ar, Ar }));
		PlayUntilActionEnds(Fight, 0);
		for (const FJRPGBattleEvent& Event : Fight.GetEvents())
		{
			if (Event.SlotIndex != 0)
			{
				continue;
			}
			Blocks += Event.Type == EJRPGBattleEventType::Block ? 1 : 0;
			Hits += Event.Type == EJRPGBattleEventType::Damage ? 1 : 0;
		}
		// um golpe bloqueado e um que bate nunca são o mesmo HitIndex
		TSet<int32> BlockedHits;
		for (const FJRPGBattleEvent& Event : Fight.GetEvents())
		{
			if (Event.SlotIndex == 0 && Event.Type == EJRPGBattleEventType::Block)
			{
				BlockedHits.Add(Event.HitIndex);
			}
		}
		for (const FJRPGBattleEvent& Event : Fight.GetEvents())
		{
			if (Event.SlotIndex == 0 && Event.Type == EJRPGBattleEventType::Damage && BlockedHits.Contains(Event.HitIndex))
			{
				++BlockedWithDamage;
			}
		}
	}
	TestTrue(TEXT("alguns golpes foram bloqueados"), Blocks > 5);
	TestTrue(TEXT("e alguns passaram"), Hits > 5);
	TestEqual(TEXT("um golpe bloqueado não dá dano"), BlockedWithDamage, 0);

	// Sem o clip de block, nunca
	FFight Fight;
	Fight.Begin({ Hero(0, TEXT("Vahn"), 100000, 20, 10), Foe(1, TEXT("plain"), 100000000, 400, 100) }, 7, false, 0);
	Fight.SubmitCommand(ArtsCommand(0, 1, { Ar, Ar, Ar, Ar }));
	PlayUntilActionEnds(Fight, 0);
	TestEqual(TEXT("sem block, sem Block"), CountEvents(Fight, EJRPGBattleEventType::Block), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleEndOnceTest, "JRPG.Battle.EndOnce", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleEndOnceTest::RunTest(const FString& Parameters)
{
	// A luta acaba uma vez só: depois da decisão não há mais eventos, comandos nem passos
	FFight Fight;
	Fight.RandOverride = []() { return 0; };
	Fight.Begin({ Hero(0, TEXT("Vahn"), 100000, 500, 100), Foe(1, TEXT("a"), 1, 10, 1) }, 1, false, 0);
	Fight.SubmitCommand(Attack(0, 1));
	for (int32 Frames = 0; Frames < 2000 && !Fight.IsFinished(); ++Frames)
	{
		Fight.StepFrame();
	}
	TestTrue(TEXT("acabou"), Fight.IsFinished());
	const int32 Events = Fight.GetEvents().Num();
	const int32 Frame = Fight.GetState().Frame;
	for (int32 i = 0; i < 100; ++i)
	{
		Fight.StepFrame();
	}
	TestEqual(TEXT("depois do fim nada produz evento"), Fight.GetEvents().Num(), Events);
	TestEqual(TEXT("e o relógio parou"), Fight.GetState().Frame, Frame);
	TestFalse(TEXT("nem se aceita comando"), Fight.SubmitCommand(Attack(0, 1)));
	TestEqual(TEXT("uma Vitória"), CountEvents(Fight, EJRPGBattleEventType::Death, 1), 1);

	// As recompensas só são pagas uma vez
	TestFalse(TEXT("ainda não pagas"), Fight.GetState().bRewardsPaid);
	TestTrue(TEXT("a primeira reivindicação paga"), Fight.ClaimRewards());
	TestFalse(TEXT("a segunda não paga nada"), Fight.ClaimRewards());
	TestTrue(TEXT("e fica marcado no estado"), Fight.GetState().bRewardsPaid);

	// Uma luta que acaba de fora (EndBattle antes da hora): congela
	FFight Halted;
	Halted.Begin({ Hero(0, TEXT("Vahn"), 100000, 100, 100), Foe(1, TEXT("a"), 100000, 10, 1) }, 1, false, 0);
	Halted.SubmitCommand(Attack(0, 1));
	Halted.Halt();
	const int32 HaltedEvents = Halted.GetEvents().Num();
	Halted.StepFrame();
	TestEqual(TEXT("congelada"), Halted.GetEvents().Num(), HaltedEvents);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleVersionRulesTest, "JRPG.Battle.VersionRules", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleVersionRulesTest::RunTest(const FString& Parameters)
{
	// Easy (diff.set 3) é a versão japonesa: a mesma luta instala o registro cru e paga o EXP inteiro. Normal é a
	// americana: o boost dos stats e o EXP x3/4. O Gimard (id 10): HP 99, stats [60 AGL, 23 ATK, 12 UDF, 15 LDF, 16 INT, 22 SPD], 55 EXP
	FJRPGEnemyData Gimard;
	Gimard.Key = TEXT("gimard");
	Gimard.BaseHP = 99;
	Gimard.BaseAGL = 60;
	Gimard.BaseATK = 23;
	Gimard.BaseUDF = 12;
	Gimard.BaseLDF = 15;
	Gimard.BaseINT = 16;
	Gimard.BaseSPD = 22;
	Gimard.EXP = 55;
	Gimard.Element = EJRPGElement::Fire;
	Gimard.TargetClass = 0;

	const FJRPGCombatant Japan = UBattleSubsystem::MakeEnemyCombatant(Gimard, {}, 1, false, EJRPGRuleset::Japan, nullptr);
	TestEqual(TEXT("Japan: o ATK cru"), Japan.ATK, 23);
	TestEqual(TEXT("Japan: a UDF crua"), Japan.UDF, 12);
	TestEqual(TEXT("Japan: a LDF crua"), Japan.LDF, 15);
	TestEqual(TEXT("Japan: o HP"), Japan.MaxHP, 99);

	// USA, encontro aleatório: UDF/LDF x7/4 (dois shifts truncando); a luta roteirizada: ATK x5/4, UDF/LDF x2
	const FJRPGCombatant UsaRandom = UBattleSubsystem::MakeEnemyCombatant(Gimard, {}, 1, false, EJRPGRuleset::USA, nullptr);
	TestEqual(TEXT("USA aleatório: UDF 12 + 6 + 3"), UsaRandom.UDF, 21);
	TestEqual(TEXT("USA aleatório: LDF 15 + 7 + 3"), UsaRandom.LDF, 25);
	TestEqual(TEXT("USA aleatório: o ATK não muda"), UsaRandom.ATK, 23);
	const FJRPGCombatant UsaScripted = UBattleSubsystem::MakeEnemyCombatant(Gimard, {}, 1, true, EJRPGRuleset::USA, nullptr);
	TestEqual(TEXT("USA roteirizada: ATK x5/4"), UsaScripted.ATK, 28);
	TestEqual(TEXT("USA roteirizada: UDF x2"), UsaScripted.UDF, 24);
	TestEqual(TEXT("USA roteirizada: LDF x2"), UsaScripted.LDF, 30);

	// O EXP: três heróis vivos, 55 de EXP cru: 55 - 55>>2 = 42 -> 14 cada na USA; o inteiro (55 -> 19) no Japão
	TestEqual(TEXT("USA: o pool"), UJRPGBattleRules::VictoryEXPPool(Gimard.EXP, EJRPGRuleset::USA), 42);
	TestEqual(TEXT("USA: 14 cada"), UJRPGBattleRules::VictoryEXPShare(UJRPGBattleRules::VictoryEXPPool(Gimard.EXP, EJRPGRuleset::USA), 3), 14);
	TestEqual(TEXT("Japan: o pool inteiro"), UJRPGBattleRules::VictoryEXPPool(Gimard.EXP, EJRPGRuleset::Japan), 55);
	TestEqual(TEXT("Japan: 19 cada"), UJRPGBattleRules::VictoryEXPShare(UJRPGBattleRules::VictoryEXPPool(Gimard.EXP, EJRPGRuleset::Japan), 3), 19);

	// A foto leva o elemento, a classe de altura e, das linhas do DT_EnemyActions, o block, o flinch e os golpes
	TArray<FJRPGEnemyActionData> Actions;
	auto AddAction = [&](int32 Index, int32 Tag, std::initializer_list<int32> Hits, int32 Cost, int32 Rate, int32 Frames)
	{
		FJRPGEnemyActionData& A = Actions.AddDefaulted_GetRef();
		A.Enemy = TEXT("gimard");
		A.ActionIndex = Index;
		A.Tag = Tag;
		A.HitFrames = Hits;
		A.Power = { Tag, 0, 0, 0 };
		A.AGLCost = Cost;
		A.Rate = Rate;
		A.FrameCount = Frames;
		A.Clip = FString::Printf(TEXT("Clip%d"), Index);
	};
	// Fora de ordem de propósito: a foto ordena pelo ActionIndex
	AddAction(7, 15, { 8 }, 32, 2, 40);
	AddAction(5, 11, { 5 }, 0, 2, 20);
	AddAction(2, 2, { 9 }, 0, 2, 30);
	AddAction(6, 13, { 7 }, 28, 2, 40);
	AddAction(4, 5, {}, 0, 2, 40);
	const FJRPGCombatant Full = UBattleSubsystem::MakeEnemyCombatant(Gimard, Actions, 3, false, EJRPGRuleset::USA, nullptr);
	TestEqual(TEXT("o elemento"), (int32)Full.Element, (int32)EJRPGElement::Fire);
	TestTrue(TEXT("tem block"), Full.bHasBlock);
	TestEqual(TEXT("o beat do block"), Full.BlockClip.Beat, 5);
	TestTrue(TEXT("tem levantar"), Full.bHasGetUp);
	TestEqual(TEXT("a janela do flinch alto"), Full.FlinchHigh.Beat, 9);
	TestEqual(TEXT("dois golpes"), Full.Swings.Num(), 2);
	TestEqual(TEXT("na ordem do arquivo"), Full.Swings.Num() == 2 ? Full.Swings[0].ActionIndex : -1, 6);
	TestEqual(TEXT("com o custo de AGL"), Full.Swings.Num() == 2 ? Full.Swings[1].AGLCost : -1, 32);
	TestEqual(TEXT("o nível"), Full.Level, 3);
	TestEqual(TEXT("o time dos inimigos"), Full.Team, 1);
	TestEqual(TEXT("o pool de AGL"), Full.MaxCommandGauge, 60);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleDeterminismTest, "JRPG.Battle.Determinism", JRPG_BATTLE_TEST_FLAGS)
bool FJRPGBattleDeterminismTest::RunTest(const FString& Parameters)
{
	UDataTable* Arts = TestArts();
	Arts->AddToRoot();

	// A mesma semente + os mesmos comandos + os mesmos inputs nos mesmos quadros dão EXATAMENTE a mesma lista de eventos
	auto Play = [Arts](int32 Seed, TArray<FJRPGBattleEvent>& OutEvents, int32& OutFrame)
	{
		FFight Fight;
		Fight.bLogFallbacks = false;
		Fight.ArtsTable = Arts;
		FJRPGCombatant Vahn = Hero(0, TEXT("Vahn"), 900, 80, 60);
		Vahn.PieceATK = { 30, 20, 10, 0, 0 };
		FJRPGCombatant Noa = Hero(1, TEXT("Noa"), 700, 60, 90);
		Noa.bLeftHanded = true;
		FJRPGCombatant Gala = Hero(2, TEXT("Gala"), 1200, 100, 30);
		FJRPGCombatant A = Foe(3, TEXT("a"), 800, 70, 55);
		A.bHasBlock = true;
		A.BlockClip.Beat = 5;
		A.BlockClip.Rate = 2;
		A.bHasGetUp = true;
		A.FlinchHigh.Beat = 20;
		A.FlinchHigh.Rate = 2;
		A.FlinchLow = A.FlinchHigh;
		FJRPGCombatant B = Foe(4, TEXT("b"), 600, 90, 75);
		B.TargetClass = 2;
		Fight.Begin({ Vahn, Noa, Gala, A, B }, Seed, false, 0);

		int32 Frames = 0;
		int32 Round = 0;
		int32 Guard = 0;
		while (!Fight.IsFinished() && Frames < 60000 && ++Guard < 200000)
		{
			if (Fight.GetState().Stage == EJRPGBattleStage::Commands)
			{
				++Round;
				// Os comandos variam por rodada: Attack, uma Art de teste, uma cadeia de Arms
				for (const FJRPGCombatant& C : Fight.GetCombatants())
				{
					if (!C.bAlive || C.OwnerPlayer < 0 || C.Type == EJRPGCombatantType::Enemy)
					{
						continue;
					}
					const int32 Target = Fight.LowestLivingFoe(C);
					switch ((Round + C.SlotIndex) % 3)
					{
					case 0:  Fight.SubmitCommand(Attack(C.SlotIndex, Target)); break;
					case 1:  Fight.SubmitCommand(ArtsCommand(C.SlotIndex, Target, { Hi, Lo, Hi })); break;
					default: Fight.SubmitCommand(ArtsCommand(C.SlotIndex, Target, { Ar, Se, Lo })); break;
					}
				}
				continue;
			}
			// Os mesmos inputs nos mesmos quadros (o parry ainda não muda o resultado, mas faz parte da entrada)
			if (Frames % 97 == 0)
			{
				FJRPGBattleInput Input;
				Input.PlayerIndex = 0;
				Input.Slot = 0;
				Input.Frame = Fight.GetState().Frame;
				Fight.SubmitInput(Input);
			}
			Fight.StepFrame();
			++Frames;
		}
		OutEvents = Fight.GetEvents();
		OutFrame = Fight.GetState().Frame;
		return Fight.IsFinished();
	};

	int32 Differing = 0;
	TArray<FJRPGBattleEvent> Previous;
	for (int32 Seed = 1; Seed <= 12; ++Seed)
	{
		TArray<FJRPGBattleEvent> First;
		TArray<FJRPGBattleEvent> Second;
		int32 FrameA = 0;
		int32 FrameB = 0;
		const bool bEndedA = Play(Seed, First, FrameA);
		const bool bEndedB = Play(Seed, Second, FrameB);
		TestTrue(FString::Printf(TEXT("semente %d: a luta acaba"), Seed), bEndedA && bEndedB);
		TestTrue(FString::Printf(TEXT("semente %d: eventos idênticos"), Seed), SameEvents(First, Second));
		TestEqual(FString::Printf(TEXT("semente %d: o mesmo quadro final"), Seed), FrameA, FrameB);
		TestTrue(TEXT("e há o que comparar"), First.Num() > 30);
		Differing += (Seed > 1 && !SameEvents(First, Previous)) ? 1 : 0;
		Previous = First;
	}
	TestTrue(TEXT("sementes diferentes dão lutas diferentes"), Differing > 6);
	Arts->RemoveFromRoot();
	return true;
}

} // namespace JRPGBattleTests

#endif // WITH_DEV_AUTOMATION_TESTS
