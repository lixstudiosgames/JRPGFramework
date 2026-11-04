// Testes das Arts (BATTLE_ROADMAP.md › Phase 6 › Acceptance): sem mapa, sobre o DT_Arts do projeto
// (/Game/Data/DT_Arts, ou o do plugin) e cópias dele com linhas a mais. Session Frontend › Automation,
// filtro "JRPG.Arts".

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Arts/ArtsData.h"
#include "Arts/ArtsSubsystem.h"
#include "Arts/JRPGArtsRules.h"
#include "Arts/JRPGArtsValidator.h"
#include "Core/CoreSubsystem.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Party/JRPGPartyTypes.h"
#include "Save/JRPGSaveGame.h"
#include "UObject/Package.h"

#define JRPG_ARTS_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace JRPGArtsTests
{
	const EJRPGArtButton Ar = EJRPGArtButton::Arms;
	const EJRPGArtButton Se = EJRPGArtButton::RaSeru;
	const EJRPGArtButton Hi = EJRPGArtButton::High;
	const EJRPGArtButton Lo = EJRPGArtButton::Low;

	/** Os 7 botões da Tri-Somersault: ↑↓↑↑↑↓↑. */
	TArray<EJRPGArtButton> TriSomersault() { return { Hi, Lo, Hi, Hi, Hi, Lo, Hi }; }

	const UDataTable* RealArts(FAutomationTestBase& Test)
	{
		const UDataTable* Arts = UCoreSubsystem::ResolveDataTable(TEXT("DT_Arts"));
		if (!Arts || Arts->GetRowStruct() != FJRPGArtData::StaticStruct())
		{
			Test.AddError(TEXT("DT_Arts (FJRPGArtData) não encontrado em /Game/Data nem no plugin"));
			return nullptr;
		}
		return Arts;
	}

	/** Uma cópia do DT_Arts para acrescentar linhas sem mexer no asset. */
	UDataTable* CopyArts(const UDataTable* Source)
	{
		UDataTable* Copy = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Copy->RowStruct = FJRPGArtData::StaticStruct();
		for (const TPair<FName, uint8*>& Pair : Source->GetRowMap())
		{
			Copy->AddRow(Pair.Key, *reinterpret_cast<const FJRPGArtData*>(Pair.Value));
		}
		return Copy;
	}

	/** Todas as Arts do herói na tabela (menos as Parts). */
	TArray<FName> AllArts(const UDataTable* Arts, FName Hero)
	{
		TArray<FName> Out;
		for (const TPair<FName, uint8*>& Pair : Arts->GetRowMap())
		{
			const FJRPGArtData* Row = reinterpret_cast<const FJRPGArtData*>(Pair.Value);
			if (Row->Character == Hero && Row->Kind != EJRPGArtKind::Part)
			{
				Out.Add(Pair.Key);
			}
		}
		return Out;
	}

	FJRPGArtsContext MakeCtx(FName Hero, const TArray<FName>& Known, int32 AP)
	{
		FJRPGArtsContext C;
		C.Character = Hero;
		C.KnownArts = Known;
		C.AP = AP;
		return C;
	}

	FJRPGArtChain Build(const UDataTable* Arts, const FJRPGArtsContext& C, const TArray<EJRPGArtButton>& Buttons)
	{
		FRandomStream Rng(7);
		return UJRPGArtsRules::BuildChain(Arts, C, Buttons, Rng);
	}

	/** As keys dos especiais que dispararam e das Arts soltas, em ordem. */
	TArray<FName> Played(const FJRPGArtChain& Chain)
	{
		TArray<FName> Out;
		for (const FJRPGArtChainEntry& E : Chain.Entries)
		{
			if (E.Type != EJRPGArtChainStepType::Swing)
			{
				Out.Add(E.ArtKey);
			}
		}
		return Out;
	}

	bool Fired(const FJRPGArtChain& Chain, FName Special)
	{
		return Chain.Entries.ContainsByPredicate([&](const FJRPGArtChainEntry& E)
		{
			return E.Type == EJRPGArtChainStepType::Special && E.ArtKey == Special;
		});
	}

	const FJRPGArtDowngrade* Downgrade(const FJRPGArtChain& Chain, FName Key)
	{
		return Chain.Downgrades.FindByPredicate([&](const FJRPGArtDowngrade& D) { return D.ArtKey == Key; });
	}

	FString Keys(const TArray<FName>& List)
	{
		FString Out;
		for (const FName& Key : List)
		{
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(", ")) + Key.ToString();
		}
		return Out;
	}
}

using namespace JRPGArtsTests;

// --- Botões ----------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGArtsButtonsTest, "JRPG.Arts.Buttons", JRPG_ARTS_TEST_FLAGS)
bool FJRPGArtsButtonsTest::RunTest(const FString& Parameters)
{
	// Destro: Arms na esquerda. A Noa, canhota: Arms na direita
	TestEqual(TEXT("Vahn ←"), UJRPGArtsRules::ButtonForPad(false, EJRPGPadButton::Left), Ar);
	TestEqual(TEXT("Vahn →"), UJRPGArtsRules::ButtonForPad(false, EJRPGPadButton::Right), Se);
	TestEqual(TEXT("Noa ←"), UJRPGArtsRules::ButtonForPad(true, EJRPGPadButton::Left), Se);
	TestEqual(TEXT("Noa →"), UJRPGArtsRules::ButtonForPad(true, EJRPGPadButton::Right), Ar);
	TestEqual(TEXT("↑"), UJRPGArtsRules::ButtonForPad(true, EJRPGPadButton::Up), Hi);
	TestEqual(TEXT("↓"), UJRPGArtsRules::ButtonForPad(false, EJRPGPadButton::Down), Lo);

	TArray<EJRPGArtButton> Parsed;
	TestTrue(TEXT("lê palavras"), UJRPGArtsRules::ParseButtons(TEXT("Arms, Ra-Seru, high low"), false, Parsed));
	TestTrue(TEXT("palavras"), Parsed == TArray<EJRPGArtButton>({ Ar, Se, Hi, Lo }));
	TestTrue(TEXT("lê setas"), UJRPGArtsRules::ParseButtons(TEXT("→ → ↑ ↑"), true, Parsed));
	TestTrue(TEXT("setas da Noa"), Parsed == TArray<EJRPGArtButton>({ Ar, Ar, Hi, Hi }));
	TestFalse(TEXT("recusa palavra que não é botão"), UJRPGArtsRules::ParseButtons(TEXT("Arms Jump"), false, Parsed));

	// Os bytes do commit: ida e volta; byte que não é botão ou cadeia longa demais recusa tudo
	TArray<EJRPGArtButton> Back;
	TestTrue(TEXT("bytes ida e volta"), UJRPGArtsRules::DecodeArtBytes(UJRPGArtsRules::EncodeArtBytes({ Lo, Se, Hi }), Back)
		&& Back == TArray<EJRPGArtButton>({ Lo, Se, Hi }));
	TestFalse(TEXT("byte 9 recusado"), UJRPGArtsRules::DecodeArtBytes({ 0, 9 }, Back));
	TestTrue(TEXT("recusado não deixa nada"), Back.IsEmpty());
	TArray<uint8> TooLong;
	TooLong.Init(0, UJRPGArtsRules::MaxChainButtons + 1);
	TestFalse(TEXT("17 bytes recusados"), UJRPGArtsRules::DecodeArtBytes(TooLong, Back));

	// A janela: gauge 100 a 30 por botão cabe 3; desfazer devolve o custo
	FJRPGArtsInputWindow Window = UJRPGArtsRules::OpenInputWindow(TEXT("Vahn"), 100, 30);
	TestTrue(TEXT("1º"), UJRPGArtsRules::PushInputButton(Window, Hi));
	TestTrue(TEXT("2º"), UJRPGArtsRules::PushInputButton(Window, Lo));
	TestTrue(TEXT("3º"), UJRPGArtsRules::PushInputButton(Window, Hi));
	TestFalse(TEXT("4º não cabe"), UJRPGArtsRules::PushInputButton(Window, Hi));
	TestFalse(TEXT("nada mais cabe"), UJRPGArtsRules::CanPushAnyButton(Window));
	TestTrue(TEXT("desfaz"), UJRPGArtsRules::PopInputButton(Window));
	TestEqual(TEXT("devolveu 30"), Window.GaugeLeft, 40);
	return true;
}

// --- Reconhecimento --------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGArtsRecognitionTest, "JRPG.Arts.Recognition", JRPG_ARTS_TEST_FLAGS)
bool FJRPGArtsRecognitionTest::RunTest(const FString& Parameters)
{
	const UDataTable* Arts = RealArts(*this);
	if (!Arts)
	{
		return false;
	}

	// Vahn, Arms Ra-Seru Arms (o disco: ← → ←) = Hyper Elbow: dois golpes e a Art, aprendida no uso
	{
		const FJRPGArtChain Chain = Build(Arts, MakeCtx(TEXT("Vahn"), {}, 0), { Ar, Se, Ar });
		TestEqual(TEXT("Hyper Elbow: 3 linhas"), Chain.Entries.Num(), 3);
		if (Chain.Entries.Num() == 3)
		{
			TestEqual(TEXT("golpe"), Chain.Entries[0].Type, EJRPGArtChainStepType::Swing);
			TestEqual(TEXT("golpe"), Chain.Entries[1].Type, EJRPGArtChainStepType::Swing);
			TestEqual(TEXT("a Art"), Chain.Entries[2].ArtKey, FName(TEXT("vahn_hyper_elbow")));
			TestTrue(TEXT("aprendida neste uso"), Chain.Entries[2].bNewlyLearned);
		}
		TestTrue(TEXT("NewlyLearned"), Chain.NewlyLearned == TArray<FName>({ TEXT("vahn_hyper_elbow") }));
		TestEqual(TEXT("Regular soma APGain"), Chain.APEnd, 9);
	}

	// Noa: as mesmas setas ficam espelhadas; → → ↑ ↑ = Arms Arms High High = Mirage Lancer
	{
		TArray<EJRPGArtButton> Buttons;
		for (const EJRPGPadButton Pad : { EJRPGPadButton::Right, EJRPGPadButton::Right, EJRPGPadButton::Up, EJRPGPadButton::Up })
		{
			Buttons.Add(UJRPGArtsRules::ButtonForPad(true, Pad));
		}
		const FJRPGArtChain Chain = Build(Arts, MakeCtx(TEXT("Noa"), {}, 0), Buttons);
		TestTrue(TEXT("Noa espelhada: Mirage Lancer"), Played(Chain) == TArray<FName>({ TEXT("noa_mirage_lancer") }));
	}

	// Sobreposição: os 7 botões da Tri-Somersault fazem Somersault (1-3), Cyclone (2-5), Somersault (5-7)
	{
		const TArray<FJRPGRecognizedArt> Tokens = UJRPGArtsRules::TokenizeAll(Arts, TEXT("Vahn"), TriSomersault());
		TestEqual(TEXT("três Arts"), Tokens.Num(), 3);
		if (Tokens.Num() == 3)
		{
			TestTrue(TEXT("Somersault 0-2"), Tokens[0].ArtKey == TEXT("vahn_somersault") && Tokens[0].FirstButton == 0 && Tokens[0].LastButton == 2);
			TestTrue(TEXT("Cyclone 1-4"), Tokens[1].ArtKey == TEXT("vahn_cyclone") && Tokens[1].FirstButton == 1 && Tokens[1].LastButton == 4);
			TestTrue(TEXT("Somersault 4-6"), Tokens[2].ArtKey == TEXT("vahn_somersault") && Tokens[2].FirstButton == 4 && Tokens[2].LastButton == 6);
		}
	}

	// A mesma Art duas vezes, desconhecida: aprendida uma vez, o aviso na primeira
	{
		const FJRPGArtChain Chain = Build(Arts, MakeCtx(TEXT("Vahn"), {}, 0), { Hi, Lo, Hi, Lo, Hi, Lo, Hi });
		TestTrue(TEXT("uma vez só"), Chain.NewlyLearned == TArray<FName>({ TEXT("vahn_somersault") }));
		int32 Flagged = 0;
		for (const FJRPGArtChainEntry& E : Chain.Entries)
		{
			Flagged += E.bNewlyLearned ? 1 : 0;
		}
		TestEqual(TEXT("um aviso"), Flagged, 1);
	}

	// Hyper desconhecida: os botões não ensinam nada (golpes); sabida (livro), sai
	{
		const FName TornadoFlame(TEXT("vahn_tornado_flame_hyper"));
		const FJRPGArtChain Unknown = Build(Arts, MakeCtx(TEXT("Vahn"), {}, 100), { Se, Se, Ar });
		TestTrue(TEXT("Hyper desconhecida não sai"), Played(Unknown).IsEmpty());
		TestTrue(TEXT("e não é aprendida"), Unknown.NewlyLearned.IsEmpty());
		const FJRPGArtChain Known = Build(Arts, MakeCtx(TEXT("Vahn"), { TornadoFlame }, 100), { Se, Se, Ar });
		TestTrue(TEXT("Hyper ensinada sai"), Played(Known) == TArray<FName>({ TornadoFlame }));
		TestEqual(TEXT("Hyper gasta APCost"), Known.APEnd, 70);
		// A Hyper não tem golpes antes: só ela
		TestEqual(TEXT("Hyper: uma linha"), Known.Entries.Num(), 1);
		// Sem Ra-Seru o original não deixa a Hyper sair
		FJRPGArtsContext NoSeru = MakeCtx(TEXT("Vahn"), { TornadoFlame }, 100);
		NoSeru.bHasRaSeru = false;
		TestTrue(TEXT("sem Ra-Seru"), Played(Build(Arts, NoSeru, { Se, Se, Ar })).IsEmpty());
	}

	// Super: com a Cyclone desconhecida, ela é aprendida e as três Arts tocam sem a Super; no turno
	// seguinte (Cyclone já sabida) a Super sai
	{
		const FName Tri(TEXT("vahn_tri_somersault"));
		const FJRPGArtChain First = Build(Arts, MakeCtx(TEXT("Vahn"), { TEXT("vahn_somersault") }, 100), TriSomersault());
		TestFalse(TEXT("1º turno: sem a Super"), Fired(First, Tri));
		TestTrue(TEXT("1º turno: aprende a Cyclone"), First.NewlyLearned == TArray<FName>({ TEXT("vahn_cyclone") }));
		TestTrue(TEXT("1º turno: as três Arts"), Played(First) == TArray<FName>({ TEXT("vahn_somersault"), TEXT("vahn_cyclone"), TEXT("vahn_somersault") }));
		const FJRPGArtDowngrade* Locked = Downgrade(First, Tri);
		TestTrue(TEXT("1º turno: a Super aparece como travada"), Locked && Locked->Reason == EJRPGArtDowngradeReason::Locked);

		const FJRPGArtChain Second = Build(Arts, MakeCtx(TEXT("Vahn"), { TEXT("vahn_somersault"), TEXT("vahn_cyclone") }, 100), TriSomersault());
		TestTrue(TEXT("2º turno: a Super sai"), Fired(Second, Tri));
		TestEqual(TEXT("2º turno: gasta 60"), Second.APEnd, 40);
	}

	// Power Slash não sai com a Slash Kick (a última Art, fora da Sequence dela) desconhecida
	{
		const FName PowerSlash(TEXT("vahn_power_slash"));
		const FJRPGArtData* Row = Arts->FindRow<FJRPGArtData>(PowerSlash, TEXT("Test"), false);
		if (TestNotNull(TEXT("Power Slash"), Row))
		{
			TArray<FName> Known = AllArts(Arts, TEXT("Vahn"));
			Known.Remove(TEXT("vahn_slash_kick"));
			TestFalse(TEXT("sem a Slash Kick"), Fired(Build(Arts, MakeCtx(TEXT("Vahn"), Known, 100), Row->Command), PowerSlash));
			TestTrue(TEXT("com ela"), Fired(Build(Arts, MakeCtx(TEXT("Vahn"), AllArts(Arts, TEXT("Vahn")), 100), Row->Command), PowerSlash));
		}
	}
	return true;
}

// --- As Supers do disco ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGArtsSuperChainsTest, "JRPG.Arts.SuperChains", JRPG_ARTS_TEST_FLAGS)
bool FJRPGArtsSuperChainsTest::RunTest(const FString& Parameters)
{
	const UDataTable* Arts = RealArts(*this);
	if (!Arts)
	{
		return false;
	}
	// O Command de cada Super tokeniza na cadeia do original (o "find"), não na Sequence
	for (const TPair<FName, TArray<FName>>& Pair : UJRPGArtsValidator::DiscSuperChains())
	{
		const TArray<FName> Own = UJRPGArtsRules::GetSpecialOwnArts(Arts, Pair.Key);
		TestTrue(FString::Printf(TEXT("%s: %s (original %s)"), *Pair.Key.ToString(), *Keys(Own), *Keys(Pair.Value)), Own == Pair.Value);
	}
	return true;
}

// --- AP na cadeia ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGArtsAPTest, "JRPG.Arts.AP", JRPG_ARTS_TEST_FLAGS)
bool FJRPGArtsAPTest::RunTest(const FString& Parameters)
{
	const UDataTable* Real = RealArts(*this);
	if (!Real)
	{
		return false;
	}
	const FName Tri(TEXT("vahn_tri_somersault"));

	// Uma Regular de +15 para o 4º exemplo (o Vahn não tem nenhuma): Low ×4
	UDataTable* Arts = CopyArts(Real);
	FJRPGArtData Plus15;
	Plus15.Key = TEXT("vahn_test_plus15");
	Plus15.Character = TEXT("Vahn");
	Plus15.Kind = EJRPGArtKind::Regular;
	Plus15.Command = { Lo, Lo, Lo, Lo };
	Plus15.APGain = 15;
	Arts->AddRow(Plus15.Key, Plus15);
	const TArray<FName> Known = AllArts(Arts, TEXT("Vahn"));

	// 1. AP 50, ↑↓↑↑↑↓↑ (60): não paga; Somersault, Cyclone, Somersault tocam e somam (50 + 9 + 12 + 9)
	{
		const FJRPGArtChain Chain = Build(Arts, MakeCtx(TEXT("Vahn"), Known, 50), TriSomersault());
		TestFalse(TEXT("1: sem a Super"), Fired(Chain, Tri));
		const FJRPGArtDowngrade* D = Downgrade(Chain, Tri);
		TestTrue(TEXT("1: caiu por AP, com 50"), D && D->Reason == EJRPGArtDowngradeReason::NotEnoughAP && D->APAtPoint == 50);
		TestEqual(TEXT("1: AP no fim"), Chain.APEnd, 80);
	}
	// 2. AP 50, uma Art solta (+12) e então ↑↓↑↑↑↓↑: 62 >= 60 quando a Super chega; termina com 2
	{
		TArray<EJRPGArtButton> Buttons = { Hi, Hi, Lo, Lo };   // Hurricane, +12
		Buttons.Append(TriSomersault());
		const FJRPGArtChain Chain = Build(Arts, MakeCtx(TEXT("Vahn"), Known, 50), Buttons);
		TestTrue(TEXT("2: a Super sai"), Fired(Chain, Tri));
		const FJRPGArtChainEntry* SuperEntry = Chain.Entries.FindByPredicate([&](const FJRPGArtChainEntry& E) { return E.ArtKey == Tri; });
		TestTrue(TEXT("2: com 62"), SuperEntry && SuperEntry->APBefore == 62);
		TestEqual(TEXT("2: AP no fim"), Chain.APEnd, 2);
	}
	// 3. AP 50, ↑↓↑↑↑↓↑ e então a Art solta (+12): a de depois não paga a Super
	{
		TArray<EJRPGArtButton> Buttons = TriSomersault();
		Buttons.Append({ Lo, Lo, Lo, Hi });                      // Cross-Kick, +12
		const FJRPGArtChain Chain = Build(Arts, MakeCtx(TEXT("Vahn"), Known, 50), Buttons);
		TestFalse(TEXT("3: sem a Super"), Fired(Chain, Tri));
		TestTrue(TEXT("3: as Arts dela e a solta"), Played(Chain) == TArray<FName>({ TEXT("vahn_somersault"), TEXT("vahn_cyclone"),
			TEXT("vahn_somersault"), TEXT("vahn_cross_kick") }));
		TestEqual(TEXT("3: AP no fim"), Chain.APEnd, 92);
	}
	// 4. AP 15, Tornado Flame (30) e então uma Art solta (+15): a Hyper cai para os botões
	{
		const FName TornadoFlame(TEXT("vahn_tornado_flame_hyper"));
		const FJRPGArtChain Chain = Build(Arts, MakeCtx(TEXT("Vahn"), Known, 15), { Se, Se, Ar, Lo, Lo, Lo, Lo });
		TestTrue(TEXT("4: só a Art solta"), Played(Chain) == TArray<FName>({ TEXT("vahn_test_plus15") }));
		const FJRPGArtDowngrade* D = Downgrade(Chain, TornadoFlame);
		TestTrue(TEXT("4: Tornado Flame caiu com 15"), D && D->Reason == EJRPGArtDowngradeReason::NotEnoughAP && D->APAtPoint == 15);
		TestEqual(TEXT("4: AP no fim"), Chain.APEnd, 30);
		TestEqual(TEXT("4: os três botões viraram golpes"), Chain.Entries.Num(), 7);
	}
	// Duas Tri-Somersault que dividem botões (↑↓↑↑↑↓↑↑↑↓↑), AP 50: a 1ª cai, as Arts dela levam o AP a
	// 71 antes da 2ª, que sai (71 - 60 = 11). Uma que caiu não tira a vez da seguinte
	{
		const FJRPGArtChain Chain = Build(Arts, MakeCtx(TEXT("Vahn"), Known, 50), { Hi, Lo, Hi, Hi, Hi, Lo, Hi, Hi, Hi, Lo, Hi });
		const FJRPGArtChainEntry* SuperEntry = Chain.Entries.FindByPredicate([&](const FJRPGArtChainEntry& E) { return E.ArtKey == Tri; });
		TestTrue(TEXT("sobrepostas: a 2ª sai com 71"), SuperEntry && SuperEntry->APBefore == 71 && SuperEntry->FirstButton == 4);
		TestEqual(TEXT("sobrepostas: AP no fim"), Chain.APEnd, 11);
		const FJRPGArtDowngrade* D = Downgrade(Chain, Tri);
		TestTrue(TEXT("sobrepostas: a 1ª caiu com 50"), D && D->FirstButton == 0 && D->APAtPoint == 50);
	}
	// O menu: com 30 de AP a Tornado Flame (30) acende, a Tri-Somersault (60) não
	{
		const TArray<FJRPGArtAvailability> List = UJRPGArtsRules::GetSpecialsAvailability(Arts, MakeCtx(TEXT("Vahn"), Known, 30), {});
		const FJRPGArtAvailability* TF = List.FindByPredicate([](const FJRPGArtAvailability& X) { return X.ArtKey == TEXT("vahn_tornado_flame_hyper"); });
		const FJRPGArtAvailability* TS = List.FindByPredicate([&](const FJRPGArtAvailability& X) { return X.ArtKey == Tri; });
		TestTrue(TEXT("Tornado Flame disponível"), TF && TF->bUnlocked && TF->bAffordable);
		TestTrue(TEXT("Tri-Somersault sem AP"), TS && TS->bUnlocked && !TS->bAffordable);
		// A lista acompanha o que já foi digitado: duas Arts de +15 antes (30 + 15 + 15 = 60) e ela acende
		const TArray<FJRPGArtAvailability> After = UJRPGArtsRules::GetSpecialsAvailability(Arts, MakeCtx(TEXT("Vahn"), Known, 30),
			{ Lo, Lo, Lo, Lo, Se, Lo, Lo, Lo, Lo });
		const FJRPGArtAvailability* TS2 = After.FindByPredicate([&](const FJRPGArtAvailability& X) { return X.ArtKey == Tri; });
		TestTrue(TEXT("depois de duas Arts de +15, a Tri-Somersault acende"), TS2 && TS2->bAffordable && TS2->APAvailable == 60);
	}
	return true;
}

// --- Uma Art nova é uma linha ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGArtsNewSuperTest, "JRPG.Arts.NewSuper", JRPG_ARTS_TEST_FLAGS)
bool FJRPGArtsNewSuperTest::RunTest(const FString& Parameters)
{
	const UDataTable* Real = RealArts(*this);
	if (!Real)
	{
		return false;
	}
	// Power Punch e mais duas Power Punch no fim, chamada de Super: uma linha, nenhum código
	UDataTable* Arts = CopyArts(Real);
	FJRPGArtData NewSuper;
	NewSuper.Key = TEXT("vahn_triple_punch");
	NewSuper.Character = TEXT("Vahn");
	NewSuper.DisplayName = FText::FromString(TEXT("Triple Punch"));
	NewSuper.Kind = EJRPGArtKind::Super;
	NewSuper.Command = { Ar, Ar, Lo, Ar, Ar, Lo, Ar, Ar, Lo };
	NewSuper.APCost = 60;
	NewSuper.Sequence = { TEXT("ArtStarter"), TEXT("vahn_power_punch"), TEXT("SwingD"), TEXT("ArtStarter"), TEXT("vahn_power_punch"),
		TEXT("SwingD"), TEXT("SpecialStarter"), TEXT("vahn_power_punch") };
	Arts->AddRow(NewSuper.Key, NewSuper);

	TestTrue(TEXT("as Arts dela são três Power Punch"), UJRPGArtsRules::GetSpecialOwnArts(Arts, NewSuper.Key)
		== TArray<FName>({ TEXT("vahn_power_punch"), TEXT("vahn_power_punch"), TEXT("vahn_power_punch") }));

	// Sem a Power Punch conhecida não sai; com ela, sai e as Power Punch dentro não somam AP
	const FJRPGArtChain Locked = Build(Arts, MakeCtx(TEXT("Vahn"), {}, 100), NewSuper.Command);
	TestFalse(TEXT("travada sem a Power Punch"), Fired(Locked, NewSuper.Key));
	const FJRPGArtChain Chain = Build(Arts, MakeCtx(TEXT("Vahn"), { TEXT("vahn_power_punch") }, 100), NewSuper.Command);
	TestTrue(TEXT("sai"), Fired(Chain, NewSuper.Key));
	TestEqual(TEXT("gasta 60, as de dentro não somam"), Chain.APEnd, 40);
	TestEqual(TEXT("dois golpes e a Super"), Chain.Entries.Num(), 3);

	// O validador aceita a linha
	TArray<FJRPGArtIssue> Issues;
	UJRPGArtsValidator::ValidateRow(Arts, NewSuper.Key, *Arts->FindRow<FJRPGArtData>(NewSuper.Key, TEXT("Test")), false, Issues);
	for (const FJRPGArtIssue& Issue : Issues)
	{
		if (Issue.Severity == EJRPGArtIssueSeverity::Error)
		{
			AddError(FString::Printf(TEXT("validador: %s"), *Issue.Message));
		}
	}
	return true;
}

// --- Validador -------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGArtsValidatorTest, "JRPG.Arts.Validator", JRPG_ARTS_TEST_FLAGS)
bool FJRPGArtsValidatorTest::RunTest(const FString& Parameters)
{
	const UDataTable* Real = RealArts(*this);
	if (!Real)
	{
		return false;
	}
	// O DT_Arts do projeto: nenhum erro (os avisos ficam listados)
	for (const FJRPGArtIssue& Issue : UJRPGArtsValidator::ValidateArts(Real, true))
	{
		const FString Line = FString::Printf(TEXT("%s: %s"), *Issue.ArtKey.ToString(), *Issue.Message);
		if (Issue.Severity == EJRPGArtIssueSeverity::Error)
		{
			AddError(Line);
		}
		else
		{
			AddInfo(TEXT("aviso — ") + Line);
		}
	}

	// Uma cópia quebrada de propósito: Command repetido e passo de Sequence que não existe
	UDataTable* Broken = CopyArts(Real);
	FJRPGArtData Dup = *Real->FindRow<FJRPGArtData>(TEXT("vahn_somersault"), TEXT("Test"));
	Dup.Key = TEXT("vahn_somersault_copy");
	Broken->AddRow(Dup.Key, Dup);
	FJRPGArtData BadSequence = *Real->FindRow<FJRPGArtData>(TEXT("vahn_fire_blow"), TEXT("Test"));
	BadSequence.Key = TEXT("vahn_fire_blow_bad");
	BadSequence.Command = { Lo, Lo, Lo, Lo, Lo };
	BadSequence.Sequence = { TEXT("SpecialStarter"), TEXT("vahn_does_not_exist") };
	Broken->AddRow(BadSequence.Key, BadSequence);

	const TArray<FJRPGArtIssue> Issues = UJRPGArtsValidator::ValidateArts(Broken, false);
	auto Has = [&](FName Key, const TCHAR* Text)
	{
		return Issues.ContainsByPredicate([&](const FJRPGArtIssue& I)
		{
			return I.ArtKey == Key && I.Severity == EJRPGArtIssueSeverity::Error && I.Message.Contains(Text);
		});
	};
	TestTrue(TEXT("Command repetido"), Has(TEXT("vahn_somersault_copy"), TEXT("mesmo Command")));
	TestTrue(TEXT("passo que não existe"), Has(TEXT("vahn_fire_blow_bad"), TEXT("vahn_does_not_exist")));
	return true;
}

// --- Aprender e o save -----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGArtsLearnTest, "JRPG.Arts.Learn", JRPG_ARTS_TEST_FLAGS)
bool FJRPGArtsLearnTest::RunTest(const FString& Parameters)
{
	if (!RealArts(*this))
	{
		return false;
	}
	// O caminho do livro / NPC: a Hyper entra na lista uma vez; Art de outro herói ou Part, não
	FJRPGPartyMember Vahn;
	Vahn.Key = TEXT("Vahn");
	const FName TornadoFlame(TEXT("vahn_tornado_flame_hyper"));
	TestFalse(TEXT("antes não sabe"), Vahn.LearnedArts.Contains(TornadoFlame));
	TestTrue(TEXT("aprende"), UArtsSubsystem::AddLearnedArt(Vahn, TornadoFlame));
	TestTrue(TEXT("HasLearnedArt"), Vahn.LearnedArts.Contains(TornadoFlame));
	TestFalse(TEXT("de novo: nada novo"), UArtsSubsystem::AddLearnedArt(Vahn, TornadoFlame));
	TestFalse(TEXT("Art da Noa"), UArtsSubsystem::AddLearnedArt(Vahn, TEXT("noa_frost_breath")));
	TestFalse(TEXT("Part"), UArtsSubsystem::AddLearnedArt(Vahn, TEXT("vahn_tri_somersault_finisher")));
	TestEqual(TEXT("uma Art na lista"), Vahn.LearnedArts.Num(), 1);

	// A lista sobrevive ao save: grava e lê o UJRPGSaveGame em memória
	UJRPGSaveGame* Save = NewObject<UJRPGSaveGame>();
	Save->Party.Add(Vahn);
	TArray<uint8> Bytes;
	TestTrue(TEXT("grava"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	const UJRPGSaveGame* Loaded = Cast<UJRPGSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (TestNotNull(TEXT("lê"), Loaded))
	{
		TestTrue(TEXT("a lista voltou"), Loaded->Party.Num() == 1 && Loaded->Party[0].LearnedArts == Vahn.LearnedArts);
	}
	return true;
}

// --- Determinismo ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJRPGArtsDeterminismTest, "JRPG.Arts.Determinism", JRPG_ARTS_TEST_FLAGS)
bool FJRPGArtsDeterminismTest::RunTest(const FString& Parameters)
{
	const UDataTable* Arts = RealArts(*this);
	if (!Arts)
	{
		return false;
	}
	// A chance de aprender abaixo de 100 sorteia do Rng passado: mesma semente, mesmo turno
	FJRPGArtsContext C = MakeCtx(TEXT("Vahn"), {}, 0);
	C.LearnOnUseChance = 50;
	TArray<EJRPGArtButton> Buttons = TriSomersault();
	Buttons.Append({ Hi, Hi, Lo, Lo, Lo, Lo, Lo, Hi });
	for (int32 Seed = 1; Seed <= 8; ++Seed)
	{
		FRandomStream First(Seed);
		FRandomStream Second(Seed);
		const FJRPGArtChain X = UJRPGArtsRules::BuildChain(Arts, C, Buttons, First);
		const FJRPGArtChain Y = UJRPGArtsRules::BuildChain(Arts, C, Buttons, Second);
		TestTrue(FString::Printf(TEXT("semente %d"), Seed), Played(X) == Played(Y) && X.NewlyLearned == Y.NewlyLearned
			&& X.APEnd == Y.APEnd && X.Entries.Num() == Y.Entries.Num());
	}
	// Chance 0: nada é aprendido no uso, e o Rng não decide nada
	C.LearnOnUseChance = 0;
	FRandomStream Rng(3);
	TestTrue(TEXT("chance 0"), UJRPGArtsRules::BuildChain(Arts, C, Buttons, Rng).NewlyLearned.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
