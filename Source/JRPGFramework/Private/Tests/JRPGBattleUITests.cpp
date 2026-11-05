// Testes do menu de comandos da luta (Battle/JRPGBattleMenu.h): sem mapa, sem WebUI. O buffer de comandos (a ordem, o
// Reselect, o Commit que para na recusa), o fluxo inteiro do menu (Start -> Command -> Attack -> Target -> ... -> Final),
// o gauge da janela de entrada, a tática -> comando (Standard / Aggressive / Defensive e o auto entre as rodadas), os nomes
// A / B dos inimigos, a fuga e a view que a tela recebe. O fim do fluxo é conferido contra o núcleo de verdade
// (FJRPGBattleResolver): o que o Begin manda tem que ser aceito. Session Frontend › Automation, filtro "JRPG.Battle.UI".

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Arts/JRPGArtsRules.h"
#include "Battle/BattleTypes.h"
#include "Battle/JRPGBattleMenu.h"
#include "Battle/JRPGBattleResolver.h"

#define JRPG_BATTLE_UI_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace JRPGBattleUITests
{
	using EKey = EJRPGBattleMenuKey;
	using EScreen = EJRPGBattleMenuScreen;

	const EJRPGArtButton Ar = EJRPGArtButton::Arms;
	const EJRPGArtButton Se = EJRPGArtButton::RaSeru;
	const EJRPGArtButton Hi = EJRPGArtButton::High;
	const EJRPGArtButton Lo = EJRPGArtButton::Low;

	/** Um herói da foto do menu: custo dos botões 30 / 42 / 54 / 54, o gauge dado, do jogador local. */
	FJRPGBattleMenuHero MakeHero(int32 Slot, const TCHAR* Name, bool bLeftHanded, int32 Gauge = 160)
	{
		FJRPGBattleMenuHero Hero;
		Hero.Slot = Slot;
		Hero.Key = FName(Name);
		Hero.Name = Name;
		Hero.RaSeruName = bLeftHanded ? TEXT("Terra") : TEXT("Meta");
		Hero.Level = 5;
		Hero.HP = Hero.MaxHP = 300;
		Hero.MP = Hero.MaxMP = 40;
		Hero.AP = 25;
		Hero.Gauge = Gauge;
		Hero.ButtonCost = { 30, 42, 54, 54 };
		Hero.bLeftHanded = bLeftHanded;
		Hero.bAlive = true;
		Hero.bNeedsCommand = true;
		return Hero;
	}

	FJRPGBattleMenuEnemy MakeEnemy(int32 Slot, const TCHAR* Key, const TCHAR* Name, bool bAlive = true)
	{
		FJRPGBattleMenuEnemy Enemy;
		Enemy.Slot = Slot;
		Enemy.Key = FName(Key);
		Enemy.Name = Name;
		Enemy.bAlive = bAlive;
		return Enemy;
	}

	/** Vahn (slot 0), Noa (1, canhota) e Gala (2) contra duas rãs (slots 3 e 4). */
	FJRPGBattleMenuContext MakeContext()
	{
		FJRPGBattleMenuContext Ctx;
		Ctx.Round = 1;
		Ctx.PlayerIndex = 0;
		Ctx.Heroes = { MakeHero(0, TEXT("Vahn"), false), MakeHero(1, TEXT("Noa"), true), MakeHero(2, TEXT("Gala"), false) };
		Ctx.Enemies = { MakeEnemy(3, TEXT("frog"), TEXT("Frog A")), MakeEnemy(4, TEXT("frog"), TEXT("Frog B")) };
		return Ctx;
	}

	int32 AsInt(EScreen Screen) { return static_cast<int32>(Screen); }
	int32 AsInt(EJRPGBattleCommandKind Kind) { return static_cast<int32>(Kind); }
	int32 AsInt(EJRPGBattleMenuRequest Request) { return static_cast<int32>(Request); }

	FJRPGBattleCommand PlainAttack(int32 Slot, int32 Target)
	{
		FJRPGBattleCommand Command;
		Command.PlayerIndex = 0;
		Command.Slot = Slot;
		Command.Kind = EJRPGBattleCommandKind::Attack;
		Command.TargetSlot = Target;
		return Command;
	}

	// --- O buffer de comandos ------------------------------------------------------------------

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleUIPlanTest, "JRPG.Battle.UI.Plan", JRPG_BATTLE_UI_TEST_FLAGS)
	bool FJRPGBattleUIPlanTest::RunTest(const FString& Parameters)
	{
		FJRPGBattleCommandPlan Plan;
		Plan.Reset({ 0, 1, 2 });
		TestEqual(TEXT("três heróis no buffer"), Plan.Num(), 3);
		TestEqual(TEXT("o cursor no primeiro"), Plan.CurrentSlot(), 0);
		TestFalse(TEXT("nada escolhido: incompleto"), Plan.IsComplete());

		// Só o herói do cursor entra; a ordem é a da party
		TestFalse(TEXT("o comando do segundo herói antes do primeiro não entra"), Plan.Set(PlainAttack(1, 3)));
		TestFalse(TEXT("um comando None não entra"), Plan.Set(FJRPGBattleCommand()));
		TestTrue(TEXT("o primeiro"), Plan.Set(PlainAttack(0, 3)));
		TestEqual(TEXT("agora o segundo"), Plan.CurrentSlot(), 1);
		TestTrue(TEXT("o segundo"), Plan.Set(PlainAttack(1, 4)));

		// Voltar apaga a escolha do herói anterior (ele vai refazê-la)
		TestTrue(TEXT("voltar ao segundo"), Plan.StepBack());
		TestEqual(TEXT("o cursor voltou"), Plan.CurrentSlot(), 1);
		TestEqual(TEXT("a escolha dele caiu"), AsInt(Plan.Entries[1].Kind), AsInt(EJRPGBattleCommandKind::None));
		TestEqual(TEXT("a do primeiro ficou"), AsInt(Plan.Entries[0].Kind), AsInt(EJRPGBattleCommandKind::Attack));
		TestTrue(TEXT("voltar ao primeiro"), Plan.StepBack());
		TestFalse(TEXT("do primeiro não há para onde voltar"), Plan.StepBack());

		// Tudo escolhido: completo
		TestTrue(TEXT("0"), Plan.Set(PlainAttack(0, 3)));
		TestTrue(TEXT("1"), Plan.Set(PlainAttack(1, 3)));
		TestTrue(TEXT("2"), Plan.Set(PlainAttack(2, 4)));
		TestTrue(TEXT("completo"), Plan.IsComplete());
		TestEqual(TEXT("o cursor passou do último"), Plan.CurrentSlot(), static_cast<int32>(INDEX_NONE));

		// O Commit manda UM A UM, na ordem da party
		TArray<int32> Sent;
		int32 RefusedIndex = 123;
		int32 Accepted = Plan.Commit([&Sent](const FJRPGBattleCommand& Command) { Sent.Add(Command.Slot); return true; }, &RefusedIndex);
		TestEqual(TEXT("os três aceitos"), Accepted, 3);
		TestTrue(TEXT("na ordem 0, 1, 2"), Sent == TArray<int32>({ 0, 1, 2 }));
		TestEqual(TEXT("nenhuma recusa"), RefusedIndex, static_cast<int32>(INDEX_NONE));

		// Uma recusa no meio: para ali, o resto não vai
		Sent.Reset();
		Accepted = Plan.Commit([&Sent](const FJRPGBattleCommand& Command) { Sent.Add(Command.Slot); return Command.Slot != 1; }, &RefusedIndex);
		TestEqual(TEXT("só o primeiro aceito"), Accepted, 1);
		TestEqual(TEXT("a recusa foi a do índice 1"), RefusedIndex, 1);
		TestTrue(TEXT("o terceiro nem foi mandado"), Sent == TArray<int32>({ 0, 1 }));

		// O Reselect zera tudo e volta ao primeiro
		Plan.Reselect();
		TestEqual(TEXT("o cursor no primeiro"), Plan.CurrentSlot(), 0);
		TestFalse(TEXT("nada escolhido"), Plan.IsComplete());
		for (const FJRPGBattleCommand& Entry : Plan.Entries)
		{
			TestEqual(TEXT("a escolha caiu"), AsInt(Entry.Kind), AsInt(EJRPGBattleCommandKind::None));
		}
		TestEqual(TEXT("os mesmos heróis"), Plan.Num(), 3);
		// Plano incompleto: o Commit não manda ninguém (o primeiro já falta)
		Sent.Reset();
		Accepted = Plan.Commit([&Sent](const FJRPGBattleCommand& Command) { Sent.Add(Command.Slot); return true; }, &RefusedIndex);
		TestEqual(TEXT("nada foi aceito"), Accepted, 0);
		TestTrue(TEXT("nada foi mandado"), Sent.IsEmpty());
		TestEqual(TEXT("parou no primeiro"), RefusedIndex, 0);
		return true;
	}

	// --- Os nomes A / B ------------------------------------------------------------------------

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleUINamesTest, "JRPG.Battle.UI.Names", JRPG_BATTLE_UI_TEST_FLAGS)
	bool FJRPGBattleUINamesTest::RunTest(const FString& Parameters)
	{
		const TArray<FString> Two = JRPGBattleMenu::MakeEnemyLabels({ FName("frog"), FName("frog"), FName("bird") }, { TEXT("Frog"), TEXT("Frog"), TEXT("Bird") });
		TestEqual(TEXT("a primeira rã"), Two[0], FString(TEXT("Frog A")));
		TestEqual(TEXT("a segunda rã"), Two[1], FString(TEXT("Frog B")));
		TestEqual(TEXT("o pássaro, sozinho, sem sufixo"), Two[2], FString(TEXT("Bird")));

		const TArray<FString> Three = JRPGBattleMenu::MakeEnemyLabels({ FName("a"), FName("b"), FName("a"), FName("a") }, { TEXT("Gimard"), TEXT("Gobu"), TEXT("Gimard"), TEXT("Gimard") });
		TestEqual(TEXT("A"), Three[0], FString(TEXT("Gimard A")));
		TestEqual(TEXT("outra espécie no meio não conta"), Three[1], FString(TEXT("Gobu")));
		TestEqual(TEXT("B"), Three[2], FString(TEXT("Gimard B")));
		TestEqual(TEXT("C"), Three[3], FString(TEXT("Gimard C")));

		// Sem nome, vale a key
		const TArray<FString> NoName = JRPGBattleMenu::MakeEnemyLabels({ FName("acid_slime") }, { FString() });
		TestEqual(TEXT("a key quando não há nome"), NoName[0], FString(TEXT("acid_slime")));
		return true;
	}

	// --- O fluxo inteiro -----------------------------------------------------------------------

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleUIFlowTest, "JRPG.Battle.UI.Flow", JRPG_BATTLE_UI_TEST_FLAGS)
	bool FJRPGBattleUIFlowTest::RunTest(const FString& Parameters)
	{
		FJRPGBattleMenu Menu;
		Menu.ResetFight();
		Menu.BeginRound(MakeContext());
		TestEqual(TEXT("abre na tela Start"), AsInt(Menu.GetScreen()), AsInt(EScreen::Start));
		TestEqual(TEXT("os três heróis no buffer"), Menu.GetPlan().Num(), 3);

		// Begin: o menu principal do primeiro herói
		TestEqual(TEXT("Begin sem pedido"), AsInt(Menu.Press(EKey::Left).Request), AsInt(EJRPGBattleMenuRequest::None));
		TestEqual(TEXT("o menu principal"), AsInt(Menu.GetScreen()), AsInt(EScreen::Command));
		TestEqual(TEXT("do Vahn"), Menu.GetPlan().CurrentSlot(), 0);

		// Item, Ra-Seru e Spirit estão apagados: não abrem nada
		for (const EKey Disabled : { EKey::Up, EKey::Right, EKey::Down })
		{
			const FJRPGBattleMenuResult Result = Menu.Press(Disabled);
			TestEqual(TEXT("o botão apagado não muda de tela"), AsInt(Menu.GetScreen()), AsInt(EScreen::Command));
			TestEqual(TEXT("o botão apagado não pede nada"), AsInt(Result.Request), AsInt(EJRPGBattleMenuRequest::None));
		}
		const FString CommandView = Menu.BuildViewJS(TEXT(""));
		TestTrue(TEXT("Item apagado na view"), CommandView.Contains(TEXT("up:{label:'Item',enabled:false")));
		TestTrue(TEXT("o Ra-Seru do Vahn é Meta, apagado"), CommandView.Contains(TEXT("right:{label:'Meta',enabled:false")));
		TestTrue(TEXT("Spirit apagado"), CommandView.Contains(TEXT("down:{label:'Spirit',enabled:false")));
		TestTrue(TEXT("Attack ligado"), CommandView.Contains(TEXT("left:{label:'Attack',enabled:true")));
		TestTrue(TEXT("as migalhas: Begin > Vahn"), CommandView.Contains(TEXT("crumbs:['Begin','Vahn']")));

		// Vahn: Attack, Arms, High, o High sai com o Cancel, e confirma com um Arms só (vira o Attack do núcleo)
		Menu.Press(EKey::Left);
		TestEqual(TEXT("a entrada de botões"), AsInt(Menu.GetScreen()), AsInt(EScreen::Attack));
		TestTrue(TEXT("migalhas: Begin > Vahn > Attack"), Menu.BuildViewJS(TEXT("")).Contains(TEXT("crumbs:['Begin','Vahn','Attack']")));
		Menu.Press(EKey::Left);    // Arms (destro)
		Menu.Press(EKey::Up);      // High
		TestTrue(TEXT("Arms, High"), Menu.GetButtons() == TArray<EJRPGArtButton>({ Ar, Hi }));
		Menu.Press(EKey::Cancel);
		TestTrue(TEXT("o Cancel tirou o último botão"), Menu.GetButtons() == TArray<EJRPGArtButton>({ Ar }));
		TestEqual(TEXT("sem sair da tela"), AsInt(Menu.GetScreen()), AsInt(EScreen::Attack));
		Menu.Press(EKey::Confirm);
		TestEqual(TEXT("o alvo"), AsInt(Menu.GetScreen()), AsInt(EScreen::Target));
		TestTrue(TEXT("o alvo mostra Frog A e Frog B"), Menu.BuildViewJS(TEXT("")).Contains(TEXT("list:[{name:'Frog A'},{name:'Frog B'}]")));

		// Cancelar o alvo volta à entrada, com os botões como estavam
		Menu.Press(EKey::Cancel);
		TestEqual(TEXT("de volta à entrada"), AsInt(Menu.GetScreen()), AsInt(EScreen::Attack));
		TestTrue(TEXT("os botões ficaram"), Menu.GetButtons() == TArray<EJRPGArtButton>({ Ar }));
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Confirm);   // a Frog A
		TestEqual(TEXT("o próximo herói começa pelo menu principal"), AsInt(Menu.GetScreen()), AsInt(EScreen::Command));
		TestEqual(TEXT("é a Noa"), Menu.GetPlan().CurrentSlot(), 1);
		TestEqual(TEXT("o Vahn: Attack na Frog A (slot 3)"), Menu.GetPlan().Entries[0].TargetSlot, 3);
		TestEqual(TEXT("o Vahn: um Arms sozinho é o Attack"), AsInt(Menu.GetPlan().Entries[0].Kind), AsInt(EJRPGBattleCommandKind::Attack));

		// Voltar do primeiro menu da Noa devolve o Vahn (a escolha dele cai); voltar dele vai à Start
		Menu.Press(EKey::Cancel);
		TestEqual(TEXT("de volta ao Vahn"), Menu.GetPlan().CurrentSlot(), 0);
		TestEqual(TEXT("a escolha dele caiu"), AsInt(Menu.GetPlan().Entries[0].Kind), AsInt(EJRPGBattleCommandKind::None));
		Menu.Press(EKey::Cancel);
		TestEqual(TEXT("do primeiro herói volta à Start"), AsInt(Menu.GetScreen()), AsInt(EScreen::Start));
		Menu.Press(EKey::Left);

		// Refaz: Vahn Attack (Arms) na Frog A; Noa (canhota: o Arms é a DIREITA) Arms + Ra-Seru; Gala Low, na Frog B
		Menu.Press(EKey::Left);
		Menu.Press(EKey::Left);
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Confirm);
		TestEqual(TEXT("Noa"), Menu.GetPlan().CurrentSlot(), 1);
		Menu.Press(EKey::Left);    // Attack
		Menu.Press(EKey::Right);   // Arms, para a canhota
		Menu.Press(EKey::Left);    // Ra-Seru
		TestTrue(TEXT("a Noa: Arms, Ra-Seru"), Menu.GetButtons() == TArray<EJRPGArtButton>({ Ar, Se }));
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Confirm);
		TestEqual(TEXT("Gala"), Menu.GetPlan().CurrentSlot(), 2);
		Menu.Press(EKey::Left);    // Attack
		Menu.Press(EKey::Down);    // Low
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Right);   // a Frog B
		Menu.Press(EKey::Confirm);
		TestEqual(TEXT("a confirmação final"), AsInt(Menu.GetScreen()), AsInt(EScreen::Final));
		TestTrue(TEXT("o plano está completo"), Menu.GetPlan().IsComplete());

		const FString FinalView = Menu.BuildViewJS(TEXT(""));
		TestTrue(TEXT("Vahn | Attack | Frog A"), FinalView.Contains(TEXT("{who:'Vahn',what:'Attack',target:'Frog A'}")));
		TestTrue(TEXT("Noa | Attack x2 | Frog A"), FinalView.Contains(TEXT("{who:'Noa',what:'Attack x2',target:'Frog A'}")));
		TestTrue(TEXT("Gala | Attack | Frog B"), FinalView.Contains(TEXT("{who:'Gala',what:'Attack',target:'Frog B'}")));
		TestTrue(TEXT("Begin e Reselect no d-pad"), FinalView.Contains(TEXT("left:{label:'Begin',enabled:true")) && FinalView.Contains(TEXT("right:{label:'Reselect',enabled:true")));

		// Os comandos do buffer: os bytes e os alvos certos
		const FJRPGBattleCommandPlan& Plan = Menu.GetPlan();
		TestEqual(TEXT("a Noa manda Arts"), AsInt(Plan.Entries[1].Kind), AsInt(EJRPGBattleCommandKind::Arts));
		TestTrue(TEXT("Arms, Ra-Seru em bytes"), Plan.Entries[1].ArtBytes == UJRPGArtsRules::EncodeArtBytes({ Ar, Se }));
		TestEqual(TEXT("a Gala manda Arts (Low)"), AsInt(Plan.Entries[2].Kind), AsInt(EJRPGBattleCommandKind::Arts));
		TestTrue(TEXT("Low em byte"), Plan.Entries[2].ArtBytes == UJRPGArtsRules::EncodeArtBytes({ Lo }));
		TestEqual(TEXT("a Gala bate na Frog B (slot 4)"), Plan.Entries[2].TargetSlot, 4);
		for (const FJRPGBattleCommand& Entry : Plan.Entries)
		{
			TestEqual(TEXT("o comando leva o jogador local"), Entry.PlayerIndex, 0);
		}

		// O Cancel da Final desfaz só a escolha do último herói
		Menu.Press(EKey::Cancel);
		TestEqual(TEXT("de volta ao menu da Gala"), AsInt(Menu.GetScreen()), AsInt(EScreen::Command));
		TestEqual(TEXT("é a Gala"), Menu.GetPlan().CurrentSlot(), 2);
		TestEqual(TEXT("as outras escolhas ficaram"), AsInt(Menu.GetPlan().Entries[1].Kind), AsInt(EJRPGBattleCommandKind::Arts));
		Menu.Press(EKey::Left);
		Menu.Press(EKey::Left);
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Confirm);
		TestEqual(TEXT("final de novo"), AsInt(Menu.GetScreen()), AsInt(EScreen::Final));

		// Reselect: tudo cai e volta ao primeiro herói
		Menu.Press(EKey::Right);
		TestEqual(TEXT("o menu principal"), AsInt(Menu.GetScreen()), AsInt(EScreen::Command));
		TestEqual(TEXT("do primeiro herói"), Menu.GetPlan().CurrentSlot(), 0);
		TestFalse(TEXT("incompleto"), Menu.GetPlan().IsComplete());
		for (const FJRPGBattleCommand& Entry : Menu.GetPlan().Entries)
		{
			TestEqual(TEXT("as escolhas caíram"), AsInt(Entry.Kind), AsInt(EJRPGBattleCommandKind::None));
		}

		// Refaz tudo com um Attack para cada um e dá o Begin: o pedido é Commit
		for (int32 Hero = 0; Hero < 3; ++Hero)
		{
			Menu.Press(EKey::Left);
			Menu.Press(EKey::Left);
			Menu.Press(EKey::Confirm);
			Menu.Press(EKey::Confirm);
		}
		TestEqual(TEXT("final"), AsInt(Menu.GetScreen()), AsInt(EScreen::Final));
		TestEqual(TEXT("o Begin pede o Commit"), AsInt(Menu.Press(EKey::Left).Request), AsInt(EJRPGBattleMenuRequest::Commit));
		return true;
	}

	// --- O gauge da janela de entrada ----------------------------------------------------------

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleUIGaugeTest, "JRPG.Battle.UI.Gauge", JRPG_BATTLE_UI_TEST_FLAGS)
	bool FJRPGBattleUIGaugeTest::RunTest(const FString& Parameters)
	{
		FJRPGBattleMenuContext Ctx = MakeContext();
		Ctx.Heroes[0].Gauge = 100;   // custos 30 / 42 / 54 / 54
		FJRPGBattleMenu Menu;
		Menu.ResetFight();
		Menu.BeginRound(Ctx);
		Menu.Press(EKey::Left);   // Begin
		Menu.Press(EKey::Left);   // Attack

		TestEqual(TEXT("o gauge cheio"), Menu.GetGaugeLeft(), 100);
		// Enter sem nada digitado não vai ao alvo
		Menu.Press(EKey::Confirm);
		TestEqual(TEXT("sem botão, sem alvo"), AsInt(Menu.GetScreen()), AsInt(EScreen::Attack));
		TestFalse(TEXT("diz por quê"), Menu.Refusal.IsEmpty());

		for (int32 i = 0; i < 3; ++i)
		{
			Menu.Press(EKey::Left);   // Arms, 30 cada
		}
		TestEqual(TEXT("três Arms: 90 gastos"), Menu.GetGaugeLeft(), 10);
		TestTrue(TEXT("sem recusa até aqui"), Menu.Refusal.IsEmpty());
		Menu.Press(EKey::Left);
		TestEqual(TEXT("o quarto não cabe"), Menu.GetButtons().Num(), 3);
		TestEqual(TEXT("o gauge ficou"), Menu.GetGaugeLeft(), 10);
		TestFalse(TEXT("a recusa aparece"), Menu.Refusal.IsEmpty());
		TestTrue(TEXT("a recusa vai na view"), Menu.BuildViewJS(TEXT("")).Contains(TEXT("refusal:'Not enough gauge")));
		TestTrue(TEXT("o botão que não cabe sai apagado no d-pad"), Menu.BuildViewJS(TEXT("")).Contains(TEXT("up:{label:'High',enabled:false,cost:54}")));

		// Cancelar devolve o custo e apaga a recusa
		Menu.Press(EKey::Cancel);
		TestEqual(TEXT("um botão a menos"), Menu.GetButtons().Num(), 2);
		TestEqual(TEXT("o custo voltou"), Menu.GetGaugeLeft(), 40);
		TestTrue(TEXT("a recusa saiu"), Menu.Refusal.IsEmpty());
		const FString View = Menu.BuildViewJS(TEXT(""));
		TestTrue(TEXT("o gauge na view"), View.Contains(TEXT("gauge:100,left:40,slots:3,slotsLeft:1")));
		TestTrue(TEXT("os botões digitados com o custo"), View.Contains(TEXT("chips:[{label:'Arms',cost:30},{label:'Arms',cost:30}]")));

		// Cancelar até esvaziar e mais um: volta ao menu principal
		Menu.Press(EKey::Cancel);
		Menu.Press(EKey::Cancel);
		TestEqual(TEXT("vazio"), Menu.GetButtons().Num(), 0);
		TestEqual(TEXT("o gauge inteiro"), Menu.GetGaugeLeft(), 100);
		Menu.Press(EKey::Cancel);
		TestEqual(TEXT("volta ao menu do herói"), AsInt(Menu.GetScreen()), AsInt(EScreen::Command));
		return true;
	}

	// --- Fuga ----------------------------------------------------------------------------------

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleUIEscapeTest, "JRPG.Battle.UI.Escape", JRPG_BATTLE_UI_TEST_FLAGS)
	bool FJRPGBattleUIEscapeTest::RunTest(const FString& Parameters)
	{
		FJRPGBattleMenu Menu;
		Menu.ResetFight();
		Menu.BeginRound(MakeContext());

		Menu.Press(EKey::Right);
		TestEqual(TEXT("a pergunta"), AsInt(Menu.GetScreen()), AsInt(EScreen::Escape));
		Menu.Press(EKey::Cancel);
		TestEqual(TEXT("cancelar volta à Start"), AsInt(Menu.GetScreen()), AsInt(EScreen::Start));
		Menu.Press(EKey::Right);
		Menu.Press(EKey::Right);   // o Cancel do d-pad
		TestEqual(TEXT("o botão Cancel também volta"), AsInt(Menu.GetScreen()), AsInt(EScreen::Start));
		Menu.Press(EKey::Right);
		TestEqual(TEXT("Confirm pede a fuga"), AsInt(Menu.Press(EKey::Left).Request), AsInt(EJRPGBattleMenuRequest::Escape));

		// Luta roteirizada: não se foge
		FJRPGBattleMenuContext Scripted = MakeContext();
		Scripted.bCanEscape = false;
		Menu.BeginRound(Scripted);
		TestTrue(TEXT("o Escape sai apagado"), Menu.BuildViewJS(TEXT("")).Contains(TEXT("right:{label:'Escape',enabled:false")));
		Menu.Press(EKey::Right);
		TestEqual(TEXT("fica na Start"), AsInt(Menu.GetScreen()), AsInt(EScreen::Start));
		TestFalse(TEXT("e diz por quê"), Menu.Refusal.IsEmpty());
		return true;
	}

	// --- Tática -> comando ---------------------------------------------------------------------

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleUITacticsTest, "JRPG.Battle.UI.Tactics", JRPG_BATTLE_UI_TEST_FLAGS)
	bool FJRPGBattleUITacticsTest::RunTest(const FString& Parameters)
	{
		FJRPGBattleMenuHero Hero = MakeHero(0, TEXT("Vahn"), false, 160);

		// Standard: um golpe de Arms (o que o JRPG.Battle.Auto faz)
		FJRPGBattleCommand Standard = JRPGBattleTactics::ChooseCommand(EJRPGBattleTactic::Standard, Hero, 3, 0);
		TestEqual(TEXT("Standard ataca"), AsInt(Standard.Kind), AsInt(EJRPGBattleCommandKind::Attack));
		TestEqual(TEXT("no alvo dado"), Standard.TargetSlot, 3);
		TestEqual(TEXT("do slot dele"), Standard.Slot, 0);
		TestEqual(TEXT("com o jogador que mandou"), Standard.PlayerIndex, 0);

		// Defensive: sem Spirit no núcleo, é a Standard
		const FJRPGBattleCommand Defensive = JRPGBattleTactics::ChooseCommand(EJRPGBattleTactic::Defensive, Hero, 3, 0);
		TestEqual(TEXT("a Defensive cai no Attack"), AsInt(Defensive.Kind), AsInt(EJRPGBattleCommandKind::Attack));
		TestEqual(TEXT("no mesmo alvo"), Defensive.TargetSlot, Standard.TargetSlot);

		// Aggressive sem Arts: o botão mais barato quantas vezes cabe (160 / 30 = 5)
		const TArray<EJRPGArtButton> Filler = JRPGBattleTactics::LongestAffordableChain(Hero);
		TestEqual(TEXT("cinco Arms no gauge de 160"), Filler.Num(), 5);
		TestTrue(TEXT("todos Arms"), Filler == TArray<EJRPGArtButton>({ Ar, Ar, Ar, Ar, Ar }));
		const FJRPGBattleCommand Aggressive = JRPGBattleTactics::ChooseCommand(EJRPGBattleTactic::Aggressive, Hero, 3, 0);
		TestEqual(TEXT("a cadeia vai como Arts"), AsInt(Aggressive.Kind), AsInt(EJRPGBattleCommandKind::Arts));
		TestEqual(TEXT("com cinco botões"), Aggressive.ArtBytes.Num(), 5);

		// Com Artes conhecidas: a mais longa que cabe, completada com o que sobra
		Hero.KnownArtCommands.Add(TArray<EJRPGArtButton>({ Ar, Se }));        // 72
		Hero.KnownArtCommands.Add(TArray<EJRPGArtButton>({ Ar, Ar, Se }));    // 102
		Hero.KnownArtCommands.Add(TArray<EJRPGArtButton>({ Hi, Lo, Hi }));    // 162: não cabe em 160
		const TArray<EJRPGArtButton> WithArt = JRPGBattleTactics::LongestAffordableChain(Hero);
		TestTrue(TEXT("a Art de três botões e um Arms de sobra (58 -> 28)"), WithArt == TArray<EJRPGArtButton>({ Ar, Ar, Se, Ar }));
		int32 Cost = 0;
		for (const EJRPGArtButton Button : WithArt)
		{
			Cost += Hero.CostOf(Button);
		}
		TestTrue(TEXT("a cadeia cabe no gauge"), Cost <= Hero.Gauge);

		// O gauge é respeitado de ponta a ponta: a cadeia mandada é aceita pelo núcleo (ver o teste Resolver)
		Hero.Gauge = 20;
		TestTrue(TEXT("20 de gauge não paga nem um botão"), JRPGBattleTactics::LongestAffordableChain(Hero).IsEmpty());
		TestEqual(TEXT("então ataca"), AsInt(JRPGBattleTactics::ChooseCommand(EJRPGBattleTactic::Aggressive, Hero, 3, 0).Kind), AsInt(EJRPGBattleCommandKind::Attack));
		Hero.Gauge = 0;
		TestTrue(TEXT("sem gauge o auto não inventa uma cadeia sem limite"), JRPGBattleTactics::LongestAffordableChain(Hero).IsEmpty());
		Hero.Gauge = 1000;
		TestEqual(TEXT("o teto é de 16 botões"), JRPGBattleTactics::LongestAffordableChain(Hero).Num(), UJRPGArtsRules::MaxChainButtons);

		// Um botão só: Arms é o Attack do núcleo; qualquer outro é Arts
		TestEqual(TEXT("Arms sozinho é Attack"), AsInt(JRPGBattleTactics::MakeChainCommand(Hero, { Ar }, 3, 0).Kind), AsInt(EJRPGBattleCommandKind::Attack));
		TestEqual(TEXT("Low sozinho é Arts"), AsInt(JRPGBattleTactics::MakeChainCommand(Hero, { Lo }, 3, 0).Kind), AsInt(EJRPGBattleCommandKind::Arts));

		TestEqual(TEXT("o nome na tela"), FString(JRPGBattleTactics::Name(EJRPGBattleTactic::Aggressive)), FString(TEXT("Aggressive")));
		return true;
	}

	// --- A tela Tactics e o auto entre as rodadas ----------------------------------------------

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleUIAutoTest, "JRPG.Battle.UI.Auto", JRPG_BATTLE_UI_TEST_FLAGS)
	bool FJRPGBattleUIAutoTest::RunTest(const FString& Parameters)
	{
		FJRPGBattleMenuContext Ctx = MakeContext();
		Ctx.Heroes[0].KnownArtCommands.Add(TArray<EJRPGArtButton>({ Ar, Ar, Se }));

		FJRPGBattleMenu Menu;
		Menu.ResetFight();
		Menu.BeginRound(Ctx);
		TestFalse(TEXT("manual por padrão"), Menu.IsAutoOn());
		TestEqual(TEXT("todos Standard"), static_cast<int32>(Menu.GetTactic(FName("Vahn"))), static_cast<int32>(EJRPGBattleTactic::Standard));

		// A tela Tactics: as opções são Aggressive / Standard / Defensive, e cada herói começa na Standard
		Menu.Press(EKey::Up);
		TestEqual(TEXT("a tela Tactics"), AsInt(Menu.GetScreen()), AsInt(EScreen::Tactics));
		TestTrue(TEXT("migalha Tactics"), Menu.BuildViewJS(TEXT("")).Contains(TEXT("crumbs:['Tactics']")));
		TestTrue(TEXT("as opções, na ordem"), Menu.BuildViewJS(TEXT("")).Contains(TEXT("options:['Aggressive','Standard','Defensive']")));
		TestTrue(TEXT("todos na Standard (opção 1)"), Menu.BuildViewJS(TEXT("")).Contains(TEXT("rows:[{name:'Vahn',pick:1},{name:'Noa',pick:1},{name:'Gala',pick:1}]")));

		Menu.Press(EKey::Left);    // Vahn: Aggressive
		Menu.Press(EKey::Down);
		Menu.Press(EKey::Down);    // Gala
		Menu.Press(EKey::Right);   // Gala: Defensive
		// Cancelar volta sem mexer em nada
		Menu.Press(EKey::Cancel);
		TestEqual(TEXT("o Cancel volta à Start"), AsInt(Menu.GetScreen()), AsInt(EScreen::Start));
		TestFalse(TEXT("sem ligar o auto"), Menu.IsAutoOn());
		TestEqual(TEXT("e sem guardar a escolha"), static_cast<int32>(Menu.GetTactic(FName("Vahn"))), static_cast<int32>(EJRPGBattleTactic::Standard));

		// De novo, e confirma
		Menu.Press(EKey::Up);
		Menu.Press(EKey::Left);    // Vahn: Aggressive
		Menu.Press(EKey::Down);
		Menu.Press(EKey::Down);    // Gala
		Menu.Press(EKey::Right);   // Gala: Defensive
		const FJRPGBattleMenuResult Confirmed = Menu.Press(EKey::Confirm);
		TestEqual(TEXT("confirmar liga o auto e pede a rodada"), AsInt(Confirmed.Request), AsInt(EJRPGBattleMenuRequest::AutoRound));
		TestTrue(TEXT("auto ligado"), Menu.IsAutoOn());
		TestEqual(TEXT("Vahn Aggressive"), static_cast<int32>(Menu.GetTactic(FName("Vahn"))), static_cast<int32>(EJRPGBattleTactic::Aggressive));
		TestEqual(TEXT("Noa Standard"), static_cast<int32>(Menu.GetTactic(FName("Noa"))), static_cast<int32>(EJRPGBattleTactic::Standard));
		TestEqual(TEXT("Gala Defensive"), static_cast<int32>(Menu.GetTactic(FName("Gala"))), static_cast<int32>(EJRPGBattleTactic::Defensive));

		// A rodada do auto: um comando por herói, pela tática dele, no inimigo vivo de menor slot
		TestTrue(TEXT("o auto monta a rodada"), Menu.PlanAuto());
		const FJRPGBattleCommandPlan& Plan = Menu.GetPlan();
		TestTrue(TEXT("completo"), Plan.IsComplete());
		TestEqual(TEXT("Vahn: a Art conhecida mais Arms"), AsInt(Plan.Entries[0].Kind), AsInt(EJRPGBattleCommandKind::Arts));
		TestTrue(TEXT("Arms, Arms, Ra-Seru, Arms"), Plan.Entries[0].ArtBytes == UJRPGArtsRules::EncodeArtBytes({ Ar, Ar, Se, Ar }));
		TestEqual(TEXT("Noa: Standard ataca"), AsInt(Plan.Entries[1].Kind), AsInt(EJRPGBattleCommandKind::Attack));
		TestEqual(TEXT("Gala: Defensive hoje ataca"), AsInt(Plan.Entries[2].Kind), AsInt(EJRPGBattleCommandKind::Attack));
		for (const FJRPGBattleCommand& Entry : Plan.Entries)
		{
			TestEqual(TEXT("no inimigo vivo de menor slot"), Entry.TargetSlot, 3);
			TestEqual(TEXT("com o jogador local"), Entry.PlayerIndex, 0);
		}

		// A próxima rodada: as táticas e o auto PERSISTEM; a Start mostra a contagem e só o Cancel vale nela
		FJRPGBattleMenuContext Next = Ctx;
		Next.Round = 2;
		Next.Enemies[0].bAlive = false;   // a Frog A caiu: o alvo do auto passa a ser a B
		Menu.BeginRound(Next);
		TestTrue(TEXT("o auto continua ligado"), Menu.IsAutoOn());
		TestTrue(TEXT("e na contagem"), Menu.IsAutoPending());
		TestEqual(TEXT("a tática persistiu"), static_cast<int32>(Menu.GetTactic(FName("Vahn"))), static_cast<int32>(EJRPGBattleTactic::Aggressive));
		TestTrue(TEXT("a view mostra a contagem"), Menu.BuildViewJS(TEXT("")).Contains(TEXT("autobattle:{on:true,pending:true}")));
		Menu.Press(EKey::Left);
		TestEqual(TEXT("na contagem o Begin não vale"), AsInt(Menu.GetScreen()), AsInt(EScreen::Start));
		TestEqual(TEXT("nem o Escape"), AsInt(Menu.Press(EKey::Right).Request), AsInt(EJRPGBattleMenuRequest::None));
		TestTrue(TEXT("o plano segue vazio de escolhas"), !Menu.GetPlan().IsComplete());
		TestTrue(TEXT("o auto da rodada 2"), Menu.PlanAuto());
		TestEqual(TEXT("agora na Frog B (slot 4)"), Menu.GetPlan().Entries[1].TargetSlot, 4);

		// O Cancel na contagem desliga o auto e deixa a Start no manual, com as táticas guardadas
		Menu.BeginRound(Next);
		TestTrue(TEXT("na contagem de novo"), Menu.IsAutoPending());
		TestEqual(TEXT("o Cancel desliga o auto"), AsInt(Menu.Press(EKey::Cancel).Request), AsInt(EJRPGBattleMenuRequest::AutoOff));
		TestFalse(TEXT("auto desligado"), Menu.IsAutoOn());
		TestFalse(TEXT("sem contagem"), Menu.IsAutoPending());
		TestEqual(TEXT("as táticas ficaram para a próxima vez"), static_cast<int32>(Menu.GetTactic(FName("Vahn"))), static_cast<int32>(EJRPGBattleTactic::Aggressive));
		Menu.Press(EKey::Left);
		TestEqual(TEXT("o manual volta a valer"), AsInt(Menu.GetScreen()), AsInt(EScreen::Command));
		Menu.BeginRound(Next);
		TestFalse(TEXT("a rodada seguinte já é manual"), Menu.IsAutoPending());

		// Uma luta nova zera tudo
		Menu.ResetFight();
		TestFalse(TEXT("auto zerado"), Menu.IsAutoOn());
		TestEqual(TEXT("táticas zeradas"), static_cast<int32>(Menu.GetTactic(FName("Vahn"))), static_cast<int32>(EJRPGBattleTactic::Standard));

		// Sem inimigo vivo o auto não monta nada
		FJRPGBattleMenuContext Nobody = Ctx;
		Nobody.Enemies[0].bAlive = false;
		Nobody.Enemies[1].bAlive = false;
		Menu.BeginRound(Nobody);
		TestFalse(TEXT("sem alvo, sem plano"), Menu.PlanAuto());
		return true;
	}

	// --- O que o Begin manda é aceito pelo núcleo ----------------------------------------------

	FJRPGCombatant CoreHero(int32 Slot, FName Key)
	{
		FJRPGCombatant C;
		C.SlotIndex = Slot;
		C.Side = EJRPGBattleSide::Party;
		C.Type = EJRPGCombatantType::Character;
		C.Team = 0;
		C.OwnerPlayer = 0;
		C.Key = Key;
		C.HP = C.MaxHP = 1000;
		C.ATK = 100;
		C.UDF = 50;
		C.LDF = 50;
		C.SPD = 50;
		C.AGL = 160;
		C.MaxCommandGauge = C.CommandGauge = 160;
		C.PieceATK.Init(0, 5);
		C.ButtonCost = { 30, 42, 54, 54 };
		C.bHasRaSeru = true;
		return C;
	}

	FJRPGCombatant CoreFoe(int32 Slot, FName Key)
	{
		FJRPGCombatant C;
		C.SlotIndex = Slot;
		C.Side = EJRPGBattleSide::Enemy;
		C.Type = EJRPGCombatantType::Enemy;
		C.Team = 1;
		C.OwnerPlayer = -1;
		C.Key = Key;
		C.HP = C.MaxHP = 1000;
		C.ATK = 100;
		C.UDF = 40;
		C.LDF = 60;
		C.SPD = 20;
		C.AGL = 60;
		C.MaxCommandGauge = C.CommandGauge = 60;
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

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGBattleUIResolverTest, "JRPG.Battle.UI.Resolver", JRPG_BATTLE_UI_TEST_FLAGS)
	bool FJRPGBattleUIResolverTest::RunTest(const FString& Parameters)
	{
		// Três heróis e duas rãs no núcleo de verdade; o menu monta o que o Begin manda
		FJRPGBattleResolver Fight;
		Fight.RandOverride = []() { return 0; };
		Fight.Begin({ CoreHero(0, TEXT("Vahn")), CoreHero(1, TEXT("Noa")), CoreHero(2, TEXT("Gala")), CoreFoe(3, TEXT("frog")), CoreFoe(4, TEXT("frog")) }, 1, false, 0);
		TestEqual(TEXT("o núcleo espera o primeiro herói"), Fight.GetActiveSlot(), 0);

		FJRPGBattleMenuContext Ctx = MakeContext();
		FJRPGBattleMenu Menu;
		Menu.ResetFight();
		Menu.BeginRound(Ctx);

		// Vahn: Arms, Ra-Seru, High (126 de 160) na Frog B; Noa: Attack na Frog A; Gala: Low, Low (108) na Frog A
		Menu.Press(EKey::Left);
		Menu.Press(EKey::Left);    // Vahn: Attack
		Menu.Press(EKey::Left);    // Arms
		Menu.Press(EKey::Right);   // Ra-Seru
		Menu.Press(EKey::Up);      // High
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Right);   // Frog B
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Left);    // Noa: Attack
		Menu.Press(EKey::Right);   // Arms (canhota)
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Left);    // Gala: Attack
		Menu.Press(EKey::Down);
		Menu.Press(EKey::Down);    // Low, Low
		Menu.Press(EKey::Confirm);
		Menu.Press(EKey::Confirm);
		TestEqual(TEXT("a tela final"), AsInt(Menu.GetScreen()), AsInt(EScreen::Final));
		TestEqual(TEXT("o Begin pede o Commit"), AsInt(Menu.Press(EKey::Left).Request), AsInt(EJRPGBattleMenuRequest::Commit));

		// Antes do Begin nada foi ao núcleo
		for (const FJRPGBattleCommand& Stored : Fight.GetState().Commands)
		{
			TestEqual(TEXT("o núcleo não recebeu nada antes do Begin"), AsInt(Stored.Kind), AsInt(EJRPGBattleCommandKind::None));
		}
		TestEqual(TEXT("ainda em Commands"), static_cast<int32>(Fight.GetState().Stage), static_cast<int32>(EJRPGBattleStage::Commands));

		// O Begin: um a um, em ordem, e o último dispara a execução
		TArray<int32> Sent;
		const int32 Accepted = Menu.GetPlan().Commit([&Fight, &Sent](const FJRPGBattleCommand& Chosen)
		{
			Sent.Add(Chosen.Slot);
			return Fight.SubmitCommand(Chosen);
		});
		TestEqual(TEXT("o núcleo aceitou os três"), Accepted, 3);
		TestTrue(TEXT("na ordem da party"), Sent == TArray<int32>({ 0, 1, 2 }));
		TestEqual(TEXT("a execução começou"), static_cast<int32>(Fight.GetState().Stage), static_cast<int32>(EJRPGBattleStage::Execution));
		TestEqual(TEXT("o Vahn: Arts"), AsInt(Fight.GetState().Commands[0].Kind), AsInt(EJRPGBattleCommandKind::Arts));
		TestTrue(TEXT("com os bytes digitados"), Fight.GetState().Commands[0].ArtBytes == UJRPGArtsRules::EncodeArtBytes({ Ar, Se, Hi }));
		TestEqual(TEXT("na Frog B"), Fight.GetState().Commands[0].TargetSlot, 4);
		TestEqual(TEXT("a Noa: Attack"), AsInt(Fight.GetState().Commands[1].Kind), AsInt(EJRPGBattleCommandKind::Attack));
		TestEqual(TEXT("a Gala: Arts"), AsInt(Fight.GetState().Commands[2].Kind), AsInt(EJRPGBattleCommandKind::Arts));
		TestEqual(TEXT("na Frog A"), Fight.GetState().Commands[2].TargetSlot, 3);

		// A cadeia do auto (Aggressive) também cabe no gauge do núcleo
		FJRPGBattleResolver Second;
		Second.RandOverride = []() { return 0; };
		Second.Begin({ CoreHero(0, TEXT("Vahn")), CoreFoe(1, TEXT("frog")) }, 1, false, 0);
		FJRPGBattleMenuHero AutoHero = MakeHero(0, TEXT("Vahn"), false, 160);
		AutoHero.KnownArtCommands.Add(TArray<EJRPGArtButton>({ Hi, Lo, Ar }));   // 138 de 160, não sobra um botão
		const FJRPGBattleCommand AutoCommand = JRPGBattleTactics::ChooseCommand(EJRPGBattleTactic::Aggressive, AutoHero, 1, 0);
		TestTrue(TEXT("o núcleo aceita a cadeia do auto"), Second.SubmitCommand(AutoCommand));
		return true;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
