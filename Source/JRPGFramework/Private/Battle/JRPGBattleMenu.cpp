#include "Battle/JRPGBattleMenu.h"
#include "Arts/JRPGArtsRules.h"
#include "UI/WebUIScripting.h"

namespace
{
	constexpr int32 TacticCount = 3;

	/** A ordem das opções na tela Tactics (a posição na tela é o índice aqui, não o valor do enum). */
	const EJRPGBattleTactic TacticOrder[TacticCount] = { EJRPGBattleTactic::Aggressive, EJRPGBattleTactic::Standard, EJRPGBattleTactic::Defensive };

	int32 DisplayIndexOf(EJRPGBattleTactic Tactic)
	{
		for (int32 i = 0; i < TacticCount; ++i)
		{
			if (TacticOrder[i] == Tactic)
			{
				return i;
			}
		}
		return 1;
	}

	/** A tela mudou: o resultado com o som e, opcionalmente, o pedido. */
	FJRPGBattleMenuResult Changed(const TCHAR* Sfx, EJRPGBattleMenuRequest Request = EJRPGBattleMenuRequest::None)
	{
		FJRPGBattleMenuResult Result;
		Result.bChanged = true;
		Result.Request = Request;
		Result.Sfx = Sfx;
		return Result;
	}

	/** O aperto não valeu (botão apagado, gauge cheio): só o som de recusa, a tela mostra o motivo se houver. */
	FJRPGBattleMenuResult Refused(const TCHAR* Sfx = TEXT("Cancel"))
	{
		FJRPGBattleMenuResult Result;
		Result.bChanged = true;
		Result.Sfx = Sfx;
		return Result;
	}

	int32 CheapestCost(const FJRPGBattleMenuHero& Hero, EJRPGArtButton& OutButton)
	{
		OutButton = EJRPGArtButton::Arms;
		int32 Best = Hero.CostOf(EJRPGArtButton::Arms);
		for (int32 b = 1; b < 4; ++b)
		{
			const int32 Cost = Hero.CostOf(static_cast<EJRPGArtButton>(b));
			if (Cost < Best)
			{
				Best = Cost;
				OutButton = static_cast<EJRPGArtButton>(b);
			}
		}
		return FMath::Max(Best, 1);
	}

	FString Lit(const FString& Text)
	{
		return JRPGWebUI::ToJSStringLiteral(Text);
	}

	const TCHAR* Bool(bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	const TCHAR* ScreenName(EJRPGBattleMenuScreen Screen)
	{
		switch (Screen)
		{
		case EJRPGBattleMenuScreen::Escape:  return TEXT("escape");
		case EJRPGBattleMenuScreen::Tactics: return TEXT("tactics");
		case EJRPGBattleMenuScreen::Command: return TEXT("command");
		case EJRPGBattleMenuScreen::Attack:  return TEXT("attack");
		case EJRPGBattleMenuScreen::Target:  return TEXT("target");
		case EJRPGBattleMenuScreen::Final:   return TEXT("final");
		case EJRPGBattleMenuScreen::Start:
		default:                             return TEXT("start");
		}
	}

	/** Um botão do d-pad na view: {label, enabled, cost}; cost -1 = sem custo. */
	FString PadButton(const FString& Label, bool bEnabled, int32 Cost = -1)
	{
		return FString::Printf(TEXT("{label:%s,enabled:%s,cost:%d}"), *Lit(Label), Bool(bEnabled), Cost);
	}
}

// =================================================================================================
// Os dados
// =================================================================================================

int32 FJRPGBattleMenuHero::CostOf(EJRPGArtButton Button) const
{
	const int32 Index = static_cast<int32>(Button);
	return ButtonCost.IsValidIndex(Index) ? ButtonCost[Index] : UJRPGArtsRules::DefaultButtonCost;
}

const FJRPGBattleMenuHero* FJRPGBattleMenuContext::FindHero(int32 Slot) const
{
	return Heroes.FindByPredicate([Slot](const FJRPGBattleMenuHero& Hero) { return Hero.Slot == Slot; });
}

const FJRPGBattleMenuEnemy* FJRPGBattleMenuContext::FindEnemy(int32 Slot) const
{
	return Enemies.FindByPredicate([Slot](const FJRPGBattleMenuEnemy& Enemy) { return Enemy.Slot == Slot; });
}

int32 FJRPGBattleMenuContext::LowestLivingEnemySlot() const
{
	int32 Lowest = INDEX_NONE;
	for (const FJRPGBattleMenuEnemy& Enemy : Enemies)
	{
		if (Enemy.bAlive && (Lowest == INDEX_NONE || Enemy.Slot < Lowest))
		{
			Lowest = Enemy.Slot;
		}
	}
	return Lowest;
}

TArray<int32> FJRPGBattleMenuContext::LivingEnemyIndices() const
{
	TArray<int32> Living;
	for (int32 i = 0; i < Enemies.Num(); ++i)
	{
		if (Enemies[i].bAlive)
		{
			Living.Add(i);
		}
	}
	return Living;
}

TArray<int32> FJRPGBattleMenuContext::HeroSlotsNeedingCommand() const
{
	TArray<int32> Slots;
	for (const FJRPGBattleMenuHero& Hero : Heroes)
	{
		if (Hero.bAlive && Hero.bNeedsCommand)
		{
			Slots.Add(Hero.Slot);
		}
	}
	Slots.Sort();
	return Slots;
}

TArray<FString> JRPGBattleMenu::MakeEnemyLabels(const TArray<FName>& Keys, const TArray<FString>& Names)
{
	TArray<FString> Labels;
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		const FString Base = Names.IsValidIndex(i) && !Names[i].IsEmpty() ? Names[i] : Keys[i].ToString();
		// Quantos da mesma espécie há, e qual deles é este (a ordem da lista): só vira A / B / C se há mais de um
		int32 SameKind = 0;
		int32 Index = 0;
		for (int32 j = 0; j < Keys.Num(); ++j)
		{
			if (Keys[j] == Keys[i])
			{
				++SameKind;
				if (j < i)
				{
					++Index;
				}
			}
		}
		if (SameKind > 1)
		{
			Labels.Add(Base + TEXT(" ") + (Index < 26 ? FString::Chr(static_cast<TCHAR>(TEXT('A') + Index)) : FString::FromInt(Index + 1)));
		}
		else
		{
			Labels.Add(Base);
		}
	}
	return Labels;
}

// =================================================================================================
// O buffer de comandos
// =================================================================================================

void FJRPGBattleCommandPlan::Reset(const TArray<int32>& Slots)
{
	HeroSlots = Slots;
	Entries.Reset();
	for (const int32 HeroSlot : HeroSlots)
	{
		FJRPGBattleCommand& Empty = Entries.AddDefaulted_GetRef();
		Empty.Slot = HeroSlot;
	}
	Cursor = 0;
}

bool FJRPGBattleCommandPlan::Set(const FJRPGBattleCommand& Command)
{
	if (!HeroSlots.IsValidIndex(Cursor) || Command.Slot != HeroSlots[Cursor] || Command.Kind == EJRPGBattleCommandKind::None)
	{
		return false;
	}
	Entries[Cursor] = Command;
	++Cursor;
	return true;
}

bool FJRPGBattleCommandPlan::StepBack()
{
	if (Cursor <= 0)
	{
		return false;
	}
	--Cursor;
	// Vai refazer a escolha dele
	Entries[Cursor] = FJRPGBattleCommand();
	Entries[Cursor].Slot = HeroSlots[Cursor];
	return true;
}

void FJRPGBattleCommandPlan::Reselect()
{
	// Uma cópia: Reset atribui a lista, e atribuir uma lista a ela mesma não é o que se quer
	const TArray<int32> Slots = HeroSlots;
	Reset(Slots);
}

bool FJRPGBattleCommandPlan::IsComplete() const
{
	if (HeroSlots.IsEmpty() || Cursor < HeroSlots.Num())
	{
		return false;
	}
	for (const FJRPGBattleCommand& Entry : Entries)
	{
		if (Entry.Kind == EJRPGBattleCommandKind::None)
		{
			return false;
		}
	}
	return true;
}

int32 FJRPGBattleCommandPlan::Commit(TFunctionRef<bool(const FJRPGBattleCommand&)> Submit, int32* OutRefusedIndex) const
{
	if (OutRefusedIndex)
	{
		*OutRefusedIndex = INDEX_NONE;
	}
	int32 Accepted = 0;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		// Um herói sem comando (o plano não está completo) ou uma recusa do núcleo: para aqui, o resto não vai
		if (Entries[i].Kind == EJRPGBattleCommandKind::None || !Submit(Entries[i]))
		{
			if (OutRefusedIndex)
			{
				*OutRefusedIndex = i;
			}
			break;
		}
		++Accepted;
	}
	return Accepted;
}

// =================================================================================================
// A tática -> o comando
// =================================================================================================

const TCHAR* JRPGBattleTactics::Name(EJRPGBattleTactic Tactic)
{
	switch (Tactic)
	{
	case EJRPGBattleTactic::Aggressive: return TEXT("Aggressive");
	case EJRPGBattleTactic::Defensive:  return TEXT("Defensive");
	case EJRPGBattleTactic::Standard:
	default:                            return TEXT("Standard");
	}
}

TArray<EJRPGArtButton> JRPGBattleTactics::LongestAffordableChain(const FJRPGBattleMenuHero& Hero)
{
	TArray<EJRPGArtButton> Chain;
	if (Hero.Gauge <= 0)
	{
		return Chain;
	}

	// A Art conhecida mais longa que cabe no gauge; no empate a que gasta mais dele
	int32 ChainCost = 0;
	for (const TArray<EJRPGArtButton>& ArtCommand : Hero.KnownArtCommands)
	{
		if (ArtCommand.IsEmpty() || ArtCommand.Num() > UJRPGArtsRules::MaxChainButtons)
		{
			continue;
		}
		int32 Cost = 0;
		for (const EJRPGArtButton Button : ArtCommand)
		{
			Cost += Hero.CostOf(Button);
		}
		if (Cost > Hero.Gauge)
		{
			continue;
		}
		if (ArtCommand.Num() > Chain.Num() || (ArtCommand.Num() == Chain.Num() && Cost > ChainCost))
		{
			Chain = ArtCommand;
			ChainCost = Cost;
		}
	}

	// O que sobra do gauge vira o botão mais barato, quantas vezes couber
	EJRPGArtButton Cheapest = EJRPGArtButton::Arms;
	const int32 CheapestButtonCost = CheapestCost(Hero, Cheapest);
	int32 Left = Hero.Gauge - ChainCost;
	while (Chain.Num() < UJRPGArtsRules::MaxChainButtons && Left >= CheapestButtonCost)
	{
		Chain.Add(Cheapest);
		Left -= CheapestButtonCost;
	}
	return Chain;
}

FJRPGBattleCommand JRPGBattleTactics::MakeChainCommand(const FJRPGBattleMenuHero& Hero, const TArray<EJRPGArtButton>& Buttons, int32 TargetSlot, int32 PlayerIndex)
{
	FJRPGBattleCommand Chosen;
	Chosen.PlayerIndex = PlayerIndex;
	Chosen.Slot = Hero.Slot;
	Chosen.TargetSlot = TargetSlot;
	if (Buttons.Num() <= 1 && (Buttons.IsEmpty() || Buttons[0] == EJRPGArtButton::Arms))
	{
		// Um golpe de Arms sozinho é o Attack do núcleo
		Chosen.Kind = EJRPGBattleCommandKind::Attack;
	}
	else
	{
		Chosen.Kind = EJRPGBattleCommandKind::Arts;
		Chosen.ArtBytes = UJRPGArtsRules::EncodeArtBytes(Buttons);
	}
	return Chosen;
}

FJRPGBattleCommand JRPGBattleTactics::MakeSpiritCommand(const FJRPGBattleMenuHero& Hero, int32 TargetSlot, int32 PlayerIndex)
{
	FJRPGBattleCommand Chosen;
	Chosen.PlayerIndex = PlayerIndex;
	Chosen.Slot = Hero.Slot;
	Chosen.TargetSlot = TargetSlot;
	Chosen.Kind = EJRPGBattleCommandKind::Guard;
	return Chosen;
}

FJRPGBattleCommand JRPGBattleTactics::ChooseCommand(EJRPGBattleTactic Tactic, const FJRPGBattleMenuHero& Hero, int32 TargetSlot, int32 PlayerIndex)
{
	switch (Tactic)
	{
	case EJRPGBattleTactic::Aggressive:
		{
			const TArray<EJRPGArtButton> Chain = LongestAffordableChain(Hero);
			if (!Chain.IsEmpty())
			{
				return MakeChainCommand(Hero, Chain, TargetSlot, PlayerIndex);
			}
			break;
		}
	case EJRPGBattleTactic::Defensive:
		// O Spirit: defende nesta rodada e abre a próxima com o gauge estendido
		return MakeSpiritCommand(Hero, TargetSlot, PlayerIndex);
	case EJRPGBattleTactic::Standard:
	default:
		break;
	}
	return MakeChainCommand(Hero, { EJRPGArtButton::Arms }, TargetSlot, PlayerIndex);
}

// =================================================================================================
// O menu
// =================================================================================================

void FJRPGBattleMenu::ResetFight()
{
	Tactics.Reset();
	LastButtons.Reset();
	bAuto = false;
	bAutoPending = false;
	Ctx = FJRPGBattleMenuContext();
	Plan = FJRPGBattleCommandPlan();
	Screen = EJRPGBattleMenuScreen::Start;
	Buttons.Reset();
	Refusal.Reset();
}

void FJRPGBattleMenu::BeginRound(const FJRPGBattleMenuContext& InContext)
{
	Ctx = InContext;
	Plan.Reset(Ctx.HeroSlotsNeedingCommand());
	Screen = EJRPGBattleMenuScreen::Start;
	Buttons.Reset();
	GaugeLeft = 0;
	TargetCursor = 0;
	TacticRow = 0;
	Refusal.Reset();
	// O auto persiste entre as rodadas: a Start mostra a contagem e quem controla manda a rodada quando ela acaba
	bAutoPending = bAuto && !Plan.IsEmpty();
}

void FJRPGBattleMenu::TurnAutoOff()
{
	bAuto = false;
	bAutoPending = false;
}

const FJRPGBattleMenuHero* FJRPGBattleMenu::FocusHero() const
{
	return Ctx.FindHero(Plan.CurrentSlot());
}

int32 FJRPGBattleMenu::GetFocusHeroSlot() const
{
	const FJRPGBattleMenuHero* Hero = FocusHero();
	return Hero ? Hero->Slot : INDEX_NONE;
}

int32 FJRPGBattleMenu::GetTargetCursorSlot() const
{
	if (Screen != EJRPGBattleMenuScreen::Target)
	{
		return INDEX_NONE;
	}
	const TArray<int32> Living = Ctx.LivingEnemyIndices();
	return Living.IsValidIndex(TargetCursor) ? Ctx.Enemies[Living[TargetCursor]].Slot : INDEX_NONE;
}

FJRPGBattleMenuResult FJRPGBattleMenu::Press(EJRPGBattleMenuKey Key)
{
	switch (Screen)
	{
	case EJRPGBattleMenuScreen::Start:   return PressStart(Key);
	case EJRPGBattleMenuScreen::Escape:  return PressEscape(Key);
	case EJRPGBattleMenuScreen::Tactics: return PressTactics(Key);
	case EJRPGBattleMenuScreen::Command: return PressCommand(Key);
	case EJRPGBattleMenuScreen::Attack:  return PressAttack(Key);
	case EJRPGBattleMenuScreen::Target:  return PressTarget(Key);
	case EJRPGBattleMenuScreen::Final:   return PressFinal(Key);
	default:                             return FJRPGBattleMenuResult();
	}
}

FJRPGBattleMenuResult FJRPGBattleMenu::Pick(int32 A, int32 B)
{
	if (bAutoPending)
	{
		return FJRPGBattleMenuResult();
	}
	if (Screen == EJRPGBattleMenuScreen::Target)
	{
		const TArray<int32> Living = Ctx.LivingEnemyIndices();
		if (!Living.IsValidIndex(A))
		{
			return FJRPGBattleMenuResult();
		}
		TargetCursor = A;
		return ConfirmTarget();
	}
	if (Screen == EJRPGBattleMenuScreen::Tactics)
	{
		const int32 HeroCount = Ctx.Heroes.Num();
		if (A == HeroCount)
		{
			return ConfirmTactics();
		}
		if (A < 0 || A > HeroCount || B < 0 || B >= TacticCount || !TacticPicks.IsValidIndex(A))
		{
			return FJRPGBattleMenuResult();
		}
		TacticRow = A;
		TacticPicks[A] = B;
		return Changed(TEXT("Next"));
	}
	return FJRPGBattleMenuResult();
}

// --- Start -----------------------------------------------------------------------------------

FJRPGBattleMenuResult FJRPGBattleMenu::PressStart(EJRPGBattleMenuKey Key)
{
	if (bAutoPending)
	{
		// Na contagem do auto só o Cancel vale: desliga e a Start fica no manual
		if (Key == EJRPGBattleMenuKey::Cancel)
		{
			TurnAutoOff();
			return Changed(TEXT("Cancel"), EJRPGBattleMenuRequest::AutoOff);
		}
		return FJRPGBattleMenuResult();
	}

	switch (Key)
	{
	case EJRPGBattleMenuKey::Left:
		// Begin: os menus de comando, do primeiro herói
		if (Plan.IsEmpty())
		{
			return Refused();
		}
		Plan.Reselect();
		Refusal.Reset();
		Screen = EJRPGBattleMenuScreen::Command;
		return Changed(TEXT("Select"));
	case EJRPGBattleMenuKey::Up:
		EnterTactics();
		return Changed(TEXT("Select"));
	case EJRPGBattleMenuKey::Right:
		if (!Ctx.bCanEscape)
		{
			Refusal = TEXT("There is no escaping this fight.");
			return Refused();
		}
		Refusal.Reset();
		Screen = EJRPGBattleMenuScreen::Escape;
		return Changed(TEXT("Select"));
	default:
		return FJRPGBattleMenuResult();
	}
}

// --- Escape ----------------------------------------------------------------------------------

FJRPGBattleMenuResult FJRPGBattleMenu::PressEscape(EJRPGBattleMenuKey Key)
{
	switch (Key)
	{
	case EJRPGBattleMenuKey::Left:
	case EJRPGBattleMenuKey::Confirm:
		// O pedido é de quem controla: ele confere a luta (uma roteirizada não foge) e a termina
		return Changed(TEXT("Select"), EJRPGBattleMenuRequest::Escape);
	case EJRPGBattleMenuKey::Right:
	case EJRPGBattleMenuKey::Cancel:
		Screen = EJRPGBattleMenuScreen::Start;
		return Changed(TEXT("Cancel"));
	default:
		return FJRPGBattleMenuResult();
	}
}

// --- Tactics ---------------------------------------------------------------------------------

void FJRPGBattleMenu::EnterTactics()
{
	TacticPicks.Reset();
	for (const FJRPGBattleMenuHero& Hero : Ctx.Heroes)
	{
		TacticPicks.Add(DisplayIndexOf(Tactics.FindRef(Hero.Key)));
	}
	TacticRow = 0;
	Refusal.Reset();
	Screen = EJRPGBattleMenuScreen::Tactics;
}

FJRPGBattleMenuResult FJRPGBattleMenu::PressTactics(EJRPGBattleMenuKey Key)
{
	// As linhas são os heróis e, por último, o botão Confirm
	const int32 Rows = Ctx.Heroes.Num() + 1;
	switch (Key)
	{
	case EJRPGBattleMenuKey::Up:
		TacticRow = (TacticRow + Rows - 1) % Rows;
		return Changed(TEXT("Next"));
	case EJRPGBattleMenuKey::Down:
		TacticRow = (TacticRow + 1) % Rows;
		return Changed(TEXT("Next"));
	case EJRPGBattleMenuKey::Left:
	case EJRPGBattleMenuKey::Right:
		if (!TacticPicks.IsValidIndex(TacticRow))
		{
			return FJRPGBattleMenuResult();   // na linha do Confirm não há opção
		}
		TacticPicks[TacticRow] = (TacticPicks[TacticRow] + (Key == EJRPGBattleMenuKey::Right ? 1 : TacticCount - 1)) % TacticCount;
		return Changed(TEXT("Next"));
	case EJRPGBattleMenuKey::Confirm:
		return ConfirmTactics();
	case EJRPGBattleMenuKey::Cancel:
		// Volta ao Start sem mexer em nada: as táticas e o auto ficam como estavam
		Screen = EJRPGBattleMenuScreen::Start;
		return Changed(TEXT("Cancel"));
	default:
		return FJRPGBattleMenuResult();
	}
}

FJRPGBattleMenuResult FJRPGBattleMenu::ConfirmTactics()
{
	for (int32 i = 0; i < Ctx.Heroes.Num() && TacticPicks.IsValidIndex(i); ++i)
	{
		Tactics.Add(Ctx.Heroes[i].Key, TacticOrder[FMath::Clamp(TacticPicks[i], 0, TacticCount - 1)]);
	}
	bAuto = true;
	bAutoPending = false;
	Screen = EJRPGBattleMenuScreen::Start;
	return Changed(TEXT("Select"), EJRPGBattleMenuRequest::AutoRound);
}

bool FJRPGBattleMenu::PlanAuto()
{
	const int32 TargetSlot = Ctx.LowestLivingEnemySlot();
	if (TargetSlot == INDEX_NONE || Plan.IsEmpty())
	{
		return false;
	}
	Plan.Reselect();
	for (int32 i = 0; i < Plan.Num(); ++i)
	{
		const FJRPGBattleMenuHero* Hero = Ctx.FindHero(Plan.HeroSlots[i]);
		if (!Hero || !Plan.Set(JRPGBattleTactics::ChooseCommand(Tactics.FindRef(Hero->Key), *Hero, TargetSlot, Ctx.PlayerIndex)))
		{
			Plan.Reselect();
			return false;
		}
	}
	return Plan.IsComplete();
}

// --- Command (o menu principal de um herói) ----------------------------------------------------

FJRPGBattleMenuResult FJRPGBattleMenu::PressCommand(EJRPGBattleMenuKey Key)
{
	switch (Key)
	{
	case EJRPGBattleMenuKey::Left:
		{
			// Attack: a janela de entrada, o gauge cheio
			const FJRPGBattleMenuHero* Hero = FocusHero();
			if (!Hero)
			{
				return Refused();
			}
			// A janela abre com a última cadeia dele (o que couber): repetir a Art é só confirmar
			PrefillButtons(*Hero);
			Refusal.Reset();
			Screen = EJRPGBattleMenuScreen::Attack;
			return Changed(TEXT("Select"));
		}
	case EJRPGBattleMenuKey::Up:
		// TODO(Fase 7): Item abre a lista de itens, depois o item, depois em quem usar (o núcleo recusa o Item hoje)
		return Refused();
	case EJRPGBattleMenuKey::Right:
		// TODO(Fase 8): Ra-Seru abre a lista de magias
		return Refused();
	case EJRPGBattleMenuKey::Down:
		{
			// Spirit: sem alvo nem botões; guarda e vai para o próximo herói (o último vai direto à confirmação final, como o
			// braço de baixo do original vai ao 0x6E)
			const FJRPGBattleMenuHero* Hero = FocusHero();
			if (!Hero || !bSpiritEnabled)
			{
				return Refused();
			}
			if (!Plan.Set(JRPGBattleTactics::MakeSpiritCommand(*Hero, Ctx.LowestLivingEnemySlot(), Ctx.PlayerIndex)))
			{
				Refusal = TEXT("That command did not fit.");
				return Refused();
			}
			Refusal.Reset();
			Screen = Plan.IsComplete() ? EJRPGBattleMenuScreen::Final : EJRPGBattleMenuScreen::Command;
			return Changed(TEXT("Select"));
		}
	case EJRPGBattleMenuKey::Cancel:
		// Volta ao herói anterior (a escolha dele cai: vai refazê-la); do primeiro, volta à Start
		if (!Plan.StepBack())
		{
			Screen = EJRPGBattleMenuScreen::Start;
		}
		Refusal.Reset();
		return Changed(TEXT("Cancel"));
	default:
		return FJRPGBattleMenuResult();
	}
}

// --- Attack (a janela de entrada) --------------------------------------------------------------

void FJRPGBattleMenu::PrefillButtons(const FJRPGBattleMenuHero& Hero)
{
	Buttons.Reset();
	GaugeLeft = Hero.Gauge;
	const TArray<EJRPGArtButton>* Last = LastButtons.Find(Hero.Key);
	if (!Last)
	{
		return;
	}
	// As mesmas regras da digitação (PressAttack): os 16 botões e o gauge desta rodada; o que não cabe fica de fora
	for (const EJRPGArtButton Button : *Last)
	{
		const int32 Cost = Hero.CostOf(Button);
		if (Buttons.Num() >= UJRPGArtsRules::MaxChainButtons || (Hero.Gauge > 0 && Cost > GaugeLeft))
		{
			break;
		}
		Buttons.Add(Button);
		if (Hero.Gauge > 0)
		{
			GaugeLeft -= Cost;
		}
	}
}

FJRPGBattleMenuResult FJRPGBattleMenu::PressAttack(EJRPGBattleMenuKey Key)
{
	const FJRPGBattleMenuHero* Hero = FocusHero();
	if (!Hero)
	{
		Screen = EJRPGBattleMenuScreen::Command;
		return Changed(TEXT("Cancel"));
	}

	switch (Key)
	{
	case EJRPGBattleMenuKey::Left:
	case EJRPGBattleMenuKey::Up:
	case EJRPGBattleMenuKey::Right:
	case EJRPGBattleMenuKey::Down:
		{
			// A direção é o botão pela mão do herói (o Arms da Noa, canhota, é a direita)
			const EJRPGPadButton Pad = Key == EJRPGBattleMenuKey::Left ? EJRPGPadButton::Left
				: Key == EJRPGBattleMenuKey::Up ? EJRPGPadButton::Up
				: Key == EJRPGBattleMenuKey::Right ? EJRPGPadButton::Right : EJRPGPadButton::Down;
			const EJRPGArtButton Pressed = UJRPGArtsRules::ButtonForPad(Hero->bLeftHanded, Pad);
			if (Buttons.Num() >= UJRPGArtsRules::MaxChainButtons)
			{
				Refusal = TEXT("The chain is full.");
				return Refused();
			}
			const int32 Cost = Hero->CostOf(Pressed);
			// A regra do núcleo (FJRPGBattleResolver::SubmitCommand): a cadeia inteira tem que caber no gauge da rodada
			if (Hero->Gauge > 0 && Cost > GaugeLeft)
			{
				Refusal = TEXT("Not enough gauge for that button.");
				return Refused();
			}
			Buttons.Add(Pressed);
			if (Hero->Gauge > 0)
			{
				GaugeLeft -= Cost;
			}
			Refusal.Reset();
			return Changed(TEXT("Next"));
		}
	case EJRPGBattleMenuKey::Confirm:
		if (Buttons.IsEmpty())
		{
			Refusal = TEXT("Enter at least one command.");
			return Refused();
		}
		if (Ctx.LivingEnemyIndices().IsEmpty())
		{
			Refusal = TEXT("There is nobody to attack.");
			return Refused();
		}
		TargetCursor = 0;
		Refusal.Reset();
		Screen = EJRPGBattleMenuScreen::Target;
		return Changed(TEXT("Select"));
	case EJRPGBattleMenuKey::Cancel:
		Refusal.Reset();
		if (!Buttons.IsEmpty())
		{
			// Tira o último botão e devolve o custo dele
			const EJRPGArtButton Removed = Buttons.Pop();
			if (Hero->Gauge > 0)
			{
				GaugeLeft = FMath::Min(GaugeLeft + Hero->CostOf(Removed), Hero->Gauge);
			}
		}
		else
		{
			Screen = EJRPGBattleMenuScreen::Command;
		}
		return Changed(TEXT("Cancel"));
	default:
		return FJRPGBattleMenuResult();
	}
}

// --- Target ----------------------------------------------------------------------------------

FJRPGBattleMenuResult FJRPGBattleMenu::PressTarget(EJRPGBattleMenuKey Key)
{
	const int32 LivingCount = Ctx.LivingEnemyIndices().Num();
	switch (Key)
	{
	case EJRPGBattleMenuKey::Left:
	case EJRPGBattleMenuKey::Up:
		if (LivingCount > 0)
		{
			TargetCursor = (TargetCursor + LivingCount - 1) % LivingCount;
		}
		return Changed(TEXT("Next"));
	case EJRPGBattleMenuKey::Right:
	case EJRPGBattleMenuKey::Down:
		if (LivingCount > 0)
		{
			TargetCursor = (TargetCursor + 1) % LivingCount;
		}
		return Changed(TEXT("Next"));
	case EJRPGBattleMenuKey::Confirm:
		return ConfirmTarget();
	case EJRPGBattleMenuKey::Cancel:
		// De volta à entrada de botões, do jeito que estava
		Screen = EJRPGBattleMenuScreen::Attack;
		return Changed(TEXT("Cancel"));
	default:
		return FJRPGBattleMenuResult();
	}
}

FJRPGBattleMenuResult FJRPGBattleMenu::ConfirmTarget()
{
	const FJRPGBattleMenuHero* Hero = FocusHero();
	const TArray<int32> Living = Ctx.LivingEnemyIndices();
	if (!Hero || !Living.IsValidIndex(TargetCursor))
	{
		Refusal = TEXT("There is nobody to attack.");
		return Refused();
	}
	const int32 TargetSlot = Ctx.Enemies[Living[TargetCursor]].Slot;
	const FJRPGBattleCommand Chosen = JRPGBattleTactics::MakeChainCommand(*Hero, Buttons, TargetSlot, Ctx.PlayerIndex);
	if (!Plan.Set(Chosen))
	{
		Refusal = TEXT("That command did not fit.");
		return Refused();
	}
	// A cadeia que valeu fica guardada: a próxima janela de entrada dele já abre com ela
	LastButtons.Add(Hero->Key, Buttons);
	Buttons.Reset();
	GaugeLeft = 0;
	Refusal.Reset();
	// O próximo herói começa pelo menu principal; sem próximo, a confirmação final
	Screen = Plan.IsComplete() ? EJRPGBattleMenuScreen::Final : EJRPGBattleMenuScreen::Command;
	return Changed(TEXT("Select"));
}

// --- Final -----------------------------------------------------------------------------------

FJRPGBattleMenuResult FJRPGBattleMenu::PressFinal(EJRPGBattleMenuKey Key)
{
	switch (Key)
	{
	case EJRPGBattleMenuKey::Left:
	case EJRPGBattleMenuKey::Confirm:
		// Begin: manda tudo, na ordem, e a rodada roda (quem controla faz isso; a tela só pede)
		return Changed(TEXT("Select"), EJRPGBattleMenuRequest::Commit);
	case EJRPGBattleMenuKey::Right:
		// Reselect: as escolhas caem e volta ao primeiro herói
		Plan.Reselect();
		Buttons.Reset();
		Refusal.Reset();
		Screen = EJRPGBattleMenuScreen::Command;
		return Changed(TEXT("Cancel"));
	case EJRPGBattleMenuKey::Cancel:
		// Desfaz só a escolha do último herói
		Plan.StepBack();
		Refusal.Reset();
		Screen = EJRPGBattleMenuScreen::Command;
		return Changed(TEXT("Cancel"));
	default:
		return FJRPGBattleMenuResult();
	}
}

// =================================================================================================
// A view
// =================================================================================================

FString FJRPGBattleMenu::BuildViewJS(const FString& Sfx) const
{
	const FJRPGBattleMenuHero* Focus = (Screen == EJRPGBattleMenuScreen::Command || Screen == EJRPGBattleMenuScreen::Attack
		|| Screen == EJRPGBattleMenuScreen::Target) ? FocusHero() : nullptr;

	FString Out = TEXT("{");
	Out += FString::Printf(TEXT("screen:%s,round:%d,sfx:%s,"), *Lit(ScreenName(Screen)), Ctx.Round, *Lit(Sfx));

	// As migalhas: onde o jogador está ("Begin > Vahn > Attack")
	TArray<FString> Crumbs;
	switch (Screen)
	{
	case EJRPGBattleMenuScreen::Escape:
		Crumbs.Add(JRPGBattleMenuText::EscapeLabel);
		break;
	case EJRPGBattleMenuScreen::Tactics:
		Crumbs.Add(JRPGBattleMenuText::TacticsLabel);
		break;
	case EJRPGBattleMenuScreen::Command:
	case EJRPGBattleMenuScreen::Attack:
	case EJRPGBattleMenuScreen::Target:
		Crumbs.Add(JRPGBattleMenuText::BeginLabel);
		if (Focus)
		{
			Crumbs.Add(Focus->Name);
		}
		if (Screen != EJRPGBattleMenuScreen::Command)
		{
			Crumbs.Add(JRPGBattleMenuText::AttackLabel);
		}
		if (Screen == EJRPGBattleMenuScreen::Target)
		{
			Crumbs.Add(JRPGBattleMenuText::TargetLabel);
		}
		break;
	case EJRPGBattleMenuScreen::Final:
		Crumbs.Add(JRPGBattleMenuText::BeginLabel);
		break;
	case EJRPGBattleMenuScreen::Start:
	default:
		break;
	}
	Out += TEXT("crumbs:[");
	for (int32 i = 0; i < Crumbs.Num(); ++i)
	{
		Out += (i > 0 ? TEXT(",") : TEXT("")) + Lit(Crumbs[i]);
	}
	Out += TEXT("],");

	// O painel do grupo
	Out += TEXT("party:[");
	int32 FocusIndex = INDEX_NONE;
	for (int32 i = 0; i < Ctx.Heroes.Num(); ++i)
	{
		const FJRPGBattleMenuHero& Hero = Ctx.Heroes[i];
		if (Focus && Focus->Slot == Hero.Slot)
		{
			FocusIndex = i;
		}
		Out += FString::Printf(TEXT("%s{name:%s,lv:%d,hp:%d,maxhp:%d,mp:%d,maxmp:%d,ap:%d,maxap:%d,alive:%s}"),
			i > 0 ? TEXT(",") : TEXT(""), *Lit(Hero.Name), Hero.Level, Hero.HP, Hero.MaxHP, Hero.MP, Hero.MaxMP, Hero.AP, Hero.MaxAP, Bool(Hero.bAlive));
	}
	Out += FString::Printf(TEXT("],focus:%d,"), FocusIndex);

	// O d-pad: {left, up, right, down}, cada um {label, enabled, cost} ou null (não desenhado)
	FString Left = TEXT("null");
	FString Up = TEXT("null");
	FString Right = TEXT("null");
	FString Down = TEXT("null");
	switch (Screen)
	{
	case EJRPGBattleMenuScreen::Start:
		Left = PadButton(JRPGBattleMenuText::BeginLabel, !Plan.IsEmpty());
		Up = PadButton(JRPGBattleMenuText::TacticsLabel, true);
		Right = PadButton(JRPGBattleMenuText::EscapeLabel, Ctx.bCanEscape);
		break;
	case EJRPGBattleMenuScreen::Escape:
		Left = PadButton(JRPGBattleMenuText::ConfirmLabel, true);
		Right = PadButton(JRPGBattleMenuText::CancelLabel, true);
		break;
	case EJRPGBattleMenuScreen::Command:
		Left = PadButton(JRPGBattleMenuText::AttackLabel, true);
		Up = PadButton(JRPGBattleMenuText::ItemLabel, bItemEnabled);
		Right = PadButton(Focus && !Focus->RaSeruName.IsEmpty() ? Focus->RaSeruName : FString(JRPGBattleMenuText::RaSeruFallbackLabel), bRaSeruEnabled);
		Down = PadButton(JRPGBattleMenuText::SpiritLabel, bSpiritEnabled);
		break;
	case EJRPGBattleMenuScreen::Attack:
		if (Focus)
		{
			// Cada direção é um botão, pela mão do herói; apaga o que o gauge que sobra não paga
			auto ButtonPad = [this, Focus](EJRPGPadButton Pad)
			{
				const EJRPGArtButton Button = UJRPGArtsRules::ButtonForPad(Focus->bLeftHanded, Pad);
				const int32 Cost = Focus->CostOf(Button);
				const bool bFits = Buttons.Num() < UJRPGArtsRules::MaxChainButtons && (Focus->Gauge <= 0 || Cost <= GaugeLeft);
				return PadButton(UJRPGArtsRules::ButtonName(Button), bFits, Cost);
			};
			Left = ButtonPad(EJRPGPadButton::Left);
			Up = ButtonPad(EJRPGPadButton::Up);
			Right = ButtonPad(EJRPGPadButton::Right);
			Down = ButtonPad(EJRPGPadButton::Down);
		}
		break;
	case EJRPGBattleMenuScreen::Final:
		Left = PadButton(JRPGBattleMenuText::BeginLabel, true);
		Right = PadButton(JRPGBattleMenuText::ReselectLabel, true);
		break;
	default:
		break;
	}
	Out += FString::Printf(TEXT("dpad:{left:%s,up:%s,right:%s,down:%s},"), *Left, *Up, *Right, *Down);

	Out += FString::Printf(TEXT("refusal:%s,"), *Lit(Refusal));

	// A janela de entrada: os botões digitados, o gauge e quantos botões ainda cabem
	if ((Screen == EJRPGBattleMenuScreen::Attack || Screen == EJRPGBattleMenuScreen::Target) && Focus)
	{
		EJRPGArtButton Unused = EJRPGArtButton::Arms;
		const int32 Cheapest = CheapestCost(*Focus, Unused);
		const bool bLimited = Focus->Gauge > 0;
		const int32 Slots = bLimited ? FMath::Min(Focus->Gauge / Cheapest, UJRPGArtsRules::MaxChainButtons) : UJRPGArtsRules::MaxChainButtons;
		const int32 SlotsLeft = bLimited ? FMath::Max(0, FMath::Min(GaugeLeft / Cheapest, UJRPGArtsRules::MaxChainButtons - Buttons.Num())) : UJRPGArtsRules::MaxChainButtons - Buttons.Num();
		Out += TEXT("art:{chips:[");
		for (int32 i = 0; i < Buttons.Num(); ++i)
		{
			Out += FString::Printf(TEXT("%s{label:%s,cost:%d}"), i > 0 ? TEXT(",") : TEXT(""), *Lit(UJRPGArtsRules::ButtonName(Buttons[i])), Focus->CostOf(Buttons[i]));
		}
		Out += FString::Printf(TEXT("],gauge:%d,left:%d,slots:%d,slotsLeft:%d},"), bLimited ? Focus->Gauge : 0, bLimited ? GaugeLeft : 0, Slots, SlotsLeft);
	}
	else
	{
		Out += TEXT("art:null,");
	}

	// A lista de consulta das Arts (o painel do Triângulo), na tela de comando e na janela de entrada: o nome, o AP que dá ou
	// tira, e o comando como setas pela mão do herói (o Arms da Noa, canhota, é a direita)
	if (Focus && (Screen == EJRPGBattleMenuScreen::Command || Screen == EJRPGBattleMenuScreen::Attack))
	{
		auto DirFor = [Focus](EJRPGArtButton Button) -> const TCHAR*
		{
			if (UJRPGArtsRules::ButtonForPad(Focus->bLeftHanded, EJRPGPadButton::Left) == Button)  { return TEXT("left"); }
			if (UJRPGArtsRules::ButtonForPad(Focus->bLeftHanded, EJRPGPadButton::Right) == Button) { return TEXT("right"); }
			if (UJRPGArtsRules::ButtonForPad(Focus->bLeftHanded, EJRPGPadButton::Up) == Button)    { return TEXT("up"); }
			return TEXT("down");
		};
		Out += TEXT("artlist:[");
		for (int32 i = 0; i < Focus->ArtList.Num(); ++i)
		{
			const FJRPGBattleMenuArt& Art = Focus->ArtList[i];
			FString Dirs;
			for (int32 k = 0; k < Art.Buttons.Num(); ++k)
			{
				Dirs += FString::Printf(TEXT("%s%s"), k > 0 ? TEXT(",") : TEXT(""), *Lit(DirFor(Art.Buttons[k])));
			}
			Out += FString::Printf(TEXT("%s{name:%s,ap:%d,cost:%s,dirs:[%s]}"), i > 0 ? TEXT(",") : TEXT(""), *Lit(Art.Name), Art.AP,
				Bool(Art.bCost), *Dirs);
		}
		Out += TEXT("],");
	}
	else
	{
		Out += TEXT("artlist:null,");
	}

	// Os alvos (só os vivos), com o cursor
	if (Screen == EJRPGBattleMenuScreen::Target)
	{
		Out += TEXT("targets:{list:[");
		const TArray<int32> Living = Ctx.LivingEnemyIndices();
		for (int32 i = 0; i < Living.Num(); ++i)
		{
			Out += FString::Printf(TEXT("%s{name:%s}"), i > 0 ? TEXT(",") : TEXT(""), *Lit(Ctx.Enemies[Living[i]].Name));
		}
		Out += FString::Printf(TEXT("],cursor:%d},"), TargetCursor);
	}
	else
	{
		Out += TEXT("targets:null,");
	}

	// As Tactics: uma linha por herói, as três opções, e a linha do Confirm
	if (Screen == EJRPGBattleMenuScreen::Tactics)
	{
		Out += TEXT("tactics:{rows:[");
		for (int32 i = 0; i < Ctx.Heroes.Num(); ++i)
		{
			Out += FString::Printf(TEXT("%s{name:%s,pick:%d}"), i > 0 ? TEXT(",") : TEXT(""), *Lit(Ctx.Heroes[i].Name), TacticPicks.IsValidIndex(i) ? TacticPicks[i] : 1);
		}
		Out += TEXT("],options:[");
		for (int32 i = 0; i < TacticCount; ++i)
		{
			Out += (i > 0 ? TEXT(",") : TEXT("")) + Lit(JRPGBattleTactics::Name(TacticOrder[i]));
		}
		Out += FString::Printf(TEXT("],row:%d,confirmRow:%d,confirm:%s},"), TacticRow, Ctx.Heroes.Num(), *Lit(JRPGBattleMenuText::ConfirmLabel));
	}
	else
	{
		Out += TEXT("tactics:null,");
	}

	// O resumo da tela Final: quem faz o quê em quem
	Out += TEXT("summary:[");
	if (Screen == EJRPGBattleMenuScreen::Final)
	{
		for (int32 i = 0; i < Plan.Entries.Num(); ++i)
		{
			const FJRPGBattleCommand& Entry = Plan.Entries[i];
			const FJRPGBattleMenuHero* Who = Ctx.FindHero(Entry.Slot);
			const FJRPGBattleMenuEnemy* Whom = Ctx.FindEnemy(Entry.TargetSlot);
			// Uma cadeia de mais de um botão aparece como "Attack x3"; o Spirit, sem alvo
			const bool bSpirit = Entry.Kind == EJRPGBattleCommandKind::Guard;
			FString What = bSpirit ? FString(JRPGBattleMenuText::SpiritLabel) : FString(JRPGBattleMenuText::AttackLabel);
			if (Entry.Kind == EJRPGBattleCommandKind::Arts && Entry.ArtBytes.Num() > 1)
			{
				What += FString::Printf(TEXT(" x%d"), Entry.ArtBytes.Num());
			}
			Out += FString::Printf(TEXT("%s{who:%s,what:%s,target:%s}"), i > 0 ? TEXT(",") : TEXT(""),
				*Lit(Who ? Who->Name : FString()), *Lit(What), *Lit(!bSpirit && Whom ? Whom->Name : FString()));
		}
	}
	Out += TEXT("],");

	Out += FString::Printf(TEXT("autobattle:{on:%s,pending:%s}"), Bool(bAuto), Bool(bAutoPending));
	Out += TEXT("}");
	return Out;
}
