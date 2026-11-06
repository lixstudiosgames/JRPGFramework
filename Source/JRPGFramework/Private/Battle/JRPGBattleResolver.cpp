#include "Battle/JRPGBattleResolver.h"
#include "Battle/BattleFormulas.h"
#include "Arts/JRPGArtsRules.h"
#include "Arts/ArtsData.h"
#include "Arts/JRPGArtSequence.h"
#include "Engine/DataTable.h"

namespace
{
	/** Quantas vezes sorteia um alvo vivo antes de pegar o primeiro vivo (o laço do original não tem limite). */
	constexpr int32 MaxTargetDraws = 64;

	/**
	 * Teto de segurança (o Pump não avança quadros: sem ele, uma luta que nunca monta ação giraria para sempre).
	 * Só rodadas vazias por FALTA DE PLANO contam (nenhum combatente com ação possível por falta de dado); alto de
	 * propósito, e o encerramento sai no log com o motivo.
	 */
	constexpr int32 MaxEmptyRounds = 64;

	/** O mesmo teto para as rodadas em que só condições (sono, atordoamento...) tiraram os turnos; condição sem prazo pode durar muito. */
	constexpr int32 MaxConditionRounds = 1024;

	/**
	 * A janela de uma reação em quadros lógicos, sem passar do clip: o beat do disco pode estar além do último quadro
	 * dele (cort_juggernaut_02_Damaged: beat 10 num clip de 7 quadros), e o clip acaba antes. FrameCount 0 = desconhecido.
	 */
	int32 ReactionFrames(const FJRPGBattleReactionClip& Clip, int32 GameSpeed)
	{
		const int32 Beat = Clip.FrameCount > 0 ? FMath::Min(Clip.Beat, Clip.FrameCount) : Clip.Beat;
		return UBattleFormulas::ReactionWindow(Beat, GameSpeed, Clip.Rate);
	}

	/** Quadros do padrão (dados na velocidade normal, escala 8) na escala do passo: mais devagar dura mais. */
	int32 Scaled(int32 Frames, int32 GameSpeed)
	{
		const int32 Speed = FMath::Max(GameSpeed, 1);
		return (Frames * UBattleFormulas::NormalGameSpeed + Speed - 1) / Speed;
	}

	/** Qual reação se mede (a duração vem do combatente, ou do padrão quando ele não traz). */
	enum class EReactionLength : uint8
	{
		FlinchHigh,
		FlinchLow,
		Block,
		Knockdown,
		GetUp,
		Dying,
		DeathFade
	};

	/**
	 * A duração de uma reação do combatente em quadros lógicos na escala 8: a do dado dele (FJRPGCombatant::Reactions) ou, em 0, o
	 * padrão (herói: o Vahn; monstro: a mediana do disco). Um flinch que falta cai no outro (o monstro sem um dos dois clips
	 * reage com o que tem).
	 */
	int32 ReactionLengthOf(const FJRPGTimelineDefaults& Defaults, const FJRPGCombatant& C, EReactionLength Kind)
	{
		const FJRPGBattleReactionLengths& R = C.Reactions;
		const bool bEnemy = C.Type == EJRPGCombatantType::Enemy;
		auto Pick = [](int32 Own, int32 Fallback) { return Own > 0 ? Own : Fallback; };
		switch (Kind)
		{
		case EReactionLength::FlinchHigh:
			return Pick(R.FlinchHighFrames, Pick(R.FlinchLowFrames, bEnemy ? Defaults.EnemyFlinchHighFrames : Defaults.HeroFlinchFrames));
		case EReactionLength::FlinchLow:
			return Pick(R.FlinchLowFrames, Pick(R.FlinchHighFrames, bEnemy ? Defaults.EnemyFlinchLowFrames : Defaults.HeroFlinchFrames));
		case EReactionLength::Block:
			return Pick(R.BlockFrames, bEnemy ? Defaults.EnemyBlockFrames : Defaults.HeroBlockFrames);
		case EReactionLength::Knockdown:
			return Pick(R.KnockdownFrames, bEnemy ? Defaults.EnemyKnockdownFrames : Defaults.HeroKnockdownFrames);
		case EReactionLength::GetUp:
			return Pick(R.GetUpFrames, bEnemy ? Defaults.EnemyGetUpFrames : Defaults.HeroGetUpFrames);
		case EReactionLength::Dying:
			return Pick(R.DyingFrames, Defaults.HeroDyingFrames);
		default:
			return Pick(R.DeathFadeFrames, Defaults.EnemyDeathFadeFrames);
		}
	}

	/** O botão de Art que um clip de golpe do herói representa (o SwingClipName ao contrário). */
	EJRPGArtButton ButtonForSwing(const FString& Clip, bool bLeftHanded)
	{
		if (Clip == TEXT("SwingU"))
		{
			return EJRPGArtButton::High;
		}
		if (Clip == TEXT("SwingD"))
		{
			return EJRPGArtButton::Low;
		}
		const bool bRight = Clip == TEXT("SwingR");
		// Destro: L é o Arms e R o Ra-Seru. Canhoto (Noa): o contrário
		return (bRight != bLeftHanded) ? EJRPGArtButton::RaSeru : EJRPGArtButton::Arms;
	}

	/**
	 * Os golpes de um golpe simples do botão: os bytes de poder (o perfil do equipamento, senão o byte padrão do herói) e, se o
	 * perfil traz, os quadros de golpe do clip (beats) e o rate. Sem beats, o quadro padrão da timeline.
	 */
	void FillSwingHits(const FJRPGCombatant& Hero, EJRPGArtButton Button, TArray<int32>& OutBytes, TArray<int32>& OutBeats, int32& OutRate)
	{
		const int32 Index = static_cast<int32>(Button);
		if (Hero.SwingProfiles.IsValidIndex(Index) && !Hero.SwingProfiles[Index].PowerBytes.IsEmpty())
		{
			const FJRPGBattleSwingProfile& Profile = Hero.SwingProfiles[Index];
			OutBytes = Profile.PowerBytes;
			if (Profile.Beats.Num() == Profile.PowerBytes.Num())
			{
				OutBeats = Profile.Beats;
				OutRate = FMath::Max(Profile.Rate, 1);
			}
			return;
		}
		OutBytes.Add(UBattleFormulas::SwingPowerByte(Hero, Button));
	}

	EJRPGHitSource SourceForButton(EJRPGArtButton Button)
	{
		switch (Button)
		{
		case EJRPGArtButton::Arms:   return EJRPGHitSource::Arms;
		case EJRPGArtButton::RaSeru: return EJRPGHitSource::RaSeru;
		case EJRPGArtButton::High:   return EJRPGHitSource::High;
		default:                     return EJRPGHitSource::Low;
		}
	}

	FName SwingClipFor(EJRPGArtButton Button, bool bLeftHanded)
	{
		return FName(*UJRPGArtSequenceLibrary::SwingClipName(Button, bLeftHanded));
	}

	int32 ButtonCostOf(const FJRPGCombatant& Hero, EJRPGArtButton Button)
	{
		const int32 Index = static_cast<int32>(Button);
		return Hero.ButtonCost.IsValidIndex(Index) ? Hero.ButtonCost[Index] : UJRPGArtsRules::DefaultButtonCost;
	}

	FString EventTypeName(EJRPGBattleEventType Type)
	{
		return StaticEnum<EJRPGBattleEventType>()->GetNameStringByValue(static_cast<int64>(Type));
	}
}

/** Um passo da ação ainda sem tempo: o que toca, com os bytes de poder dos golpes dele. */
struct FJRPGBattleResolver::FPlanStep
{
	EJRPGActionStepKind Kind = EJRPGActionStepKind::Swing;
	FName ClipName;
	FName ArtKey;
	int32 Entry = -1;
	int32 Speed = UBattleFormulas::NormalGameSpeed;
	TArray<int32> HitBytes;
	/** Os quadros de golpe do clip (beats) do golpe simples, lidos do equipamento; vazio = o quadro padrão. */
	TArray<int32> HitBeats;
	int32 BeatRate = 2;
	EJRPGHitSource HitSource = EJRPGHitSource::Arms;
};

// =================================================================================================
// Começo
// =================================================================================================

void FJRPGBattleResolver::Begin(const TArray<FJRPGCombatant>& InCombatants, int32 InSeed, bool bInScriptedFight, int32 InFormationAdvantage)
{
	State = FJRPGBattleState();
	Events.Reset();
	Timelines.Reset();
	LoggedFallbacks.Reset();
	EmptyRounds = 0;
	ConditionRounds = 0;
	bActionStartedThisRound = false;
	bTurnLostToConditionThisRound = false;
	bHoldExecution = false;

	State.Combatants = InCombatants;
	for (int32 i = 0; i < State.Combatants.Num(); ++i)
	{
		FJRPGCombatant& C = State.Combatants[i];
		C.SlotIndex = i;
		C.bAlive = C.HP > 0;
		C.InitiativeKey = 0;
		C.JuggleCounter = 0;
		C.ReactionUntilFrame = 0;
		C.ReactionHoldUntilFrame = 0;
		C.bReactionIsBlock = false;
		C.bGuarding = false;
		C.CommandGauge = C.MaxCommandGauge;
		C.TargetSlot = -1;
	}
	State.Seed = InSeed;
	State.Rng.Initialize(InSeed);
	State.bScriptedFight = bInScriptedFight;
	// Uma luta roteirizada não rola vantagem de formação (ctx+0x287)
	State.FormationAdvantage = bInScriptedFight ? 0 : InFormationAdvantage;
	State.Commands.SetNum(State.Combatants.Num());
	State.Round = 0;

	if (!CheckFinish())
	{
		StartRound();
		Pump();
	}
}

void FJRPGBattleResolver::SetHoldExecution(bool bHold)
{
	const bool bWasHeld = bHoldExecution;
	bHoldExecution = bHold;
	if (bWasHeld && !bHold)
	{
		Pump();
	}
}

void FJRPGBattleResolver::Halt()
{
	State.Action.bActive = false;
	State.ActiveSlot = -1;
	State.Stage = EJRPGBattleStage::Finished;
}

bool FJRPGBattleResolver::ClaimRewards()
{
	if (State.bRewardsPaid)
	{
		return false;
	}
	State.bRewardsPaid = true;
	return true;
}

// =================================================================================================
// A rodada
// =================================================================================================

void FJRPGBattleResolver::StartRound()
{
	++State.Round;
	State.Stage = EJRPGBattleStage::Commands;
	State.TurnOrder.Reset();
	State.Inputs.Reset();
	State.ActiveSlot = -1;
	bActionStartedThisRound = false;
	bTurnLostToConditionThisRound = false;

	for (int32 i = 0; i < State.Combatants.Num(); ++i)
	{
		State.Commands[i] = FJRPGBattleCommand();
		State.Commands[i].Slot = i;
	}

	for (FJRPGCombatant& C : State.Combatants)
	{
		C.InitiativeKey = 0;
		// O Spirit vale do commit até a varredura desta virada de rodada (FUN_801D88CC zera a categoria aqui)
		const bool bWasSpirit = C.bGuarding;
		C.bGuarding = false;
		if (!C.bAlive)
		{
			continue;
		}
		// O pool da AGL (FUN_801D88CC): quem fez Spirit na rodada que acabou abre esta com o gauge estendido, min(Max * 7/5 + 8,
		// 288); quem comandou ataque / Arts e todo monstro volta ao máximo
		C.CommandGauge = bWasSpirit && C.MaxCommandGauge > 0
			? FMath::Min(C.MaxCommandGauge * 7 / 5 + 8, FMath::Max(UBattleFormulas::SpiritGaugeCap, C.MaxCommandGauge))
			: C.MaxCommandGauge;
		// Quem mirava um alvo que caiu passa a mirar o vivo de menor slot (FUN_801DB8B4)
		if (!IsLivingFoe(C, C.TargetSlot))
		{
			C.TargetSlot = LowestLivingFoe(C);
		}
	}
	Emit(EJRPGBattleEventType::RoundStart, -1, -1, State.Round);

	// O herói sem dono (a IA, o aliado automático) ataca o inimigo de menor slot
	for (FJRPGCombatant& C : State.Combatants)
	{
		if (C.bAlive && C.Type != EJRPGCombatantType::Enemy && C.OwnerPlayer < 0)
		{
			FJRPGBattleCommand& Command = State.Commands[C.SlotIndex];
			Command.Slot = C.SlotIndex;
			Command.PlayerIndex = -1;
			Command.Kind = EJRPGBattleCommandKind::Attack;
			Command.TargetSlot = C.TargetSlot;
		}
	}
}

bool FJRPGBattleResolver::NeedsAnyCommand(int32* OutSlot) const
{
	for (const FJRPGCombatant& C : State.Combatants)
	{
		if (C.bAlive && C.Type != EJRPGCombatantType::Enemy && C.OwnerPlayer >= 0
			&& State.Commands.IsValidIndex(C.SlotIndex) && State.Commands[C.SlotIndex].Kind == EJRPGBattleCommandKind::None)
		{
			if (OutSlot)
			{
				*OutSlot = C.SlotIndex;
			}
			return true;
		}
	}
	return false;
}

void FJRPGBattleResolver::BeginExecution()
{
	State.Stage = EJRPGBattleStage::Execution;
	State.ActiveSlot = -1;
	SeedInitiative();
}

void FJRPGBattleResolver::SeedInitiative()
{
	// Uma chave por combatente, em ordem de slot, do Rng da luta. O original sorteia para os sete slots; aqui só
	// para os vivos (quem caiu tem a chave 0). O lado em desvantagem de formação perde as chaves da rodada 1,
	// mas o sorteio é gasto do mesmo jeito
	for (FJRPGCombatant& C : State.Combatants)
	{
		if (!C.bAlive)
		{
			C.InitiativeKey = 0;
			continue;
		}
		const bool bHero = C.Type != EJRPGCombatantType::Enemy;
		int32 Key = UBattleFormulas::InitiativeKey(bHero, C.SPD, C.HP, C.MaxHP, HasCondition(C, TEXT("Slow")),
			[this]() { return Rand15(); });
		if (State.Round == 1)
		{
			if ((State.FormationAdvantage == 1 && C.Side == EJRPGBattleSide::Party)
				|| (State.FormationAdvantage == 2 && C.Side == EJRPGBattleSide::Enemy))
			{
				Key = 0;
			}
		}
		C.InitiativeKey = Key;
	}
}

void FJRPGBattleResolver::Pump()
{
	while (!State.bFinished && State.Stage != EJRPGBattleStage::Finished)
	{
		if (State.Stage == EJRPGBattleStage::Commands)
		{
			int32 Slot = INDEX_NONE;
			if (NeedsAnyCommand(&Slot))
			{
				State.ActiveSlot = Slot;
				return;
			}
			if (bHoldExecution)
			{
				// Todos comandaram, mas a cena ainda não está de pé: a execução começa quando soltarem
				State.ActiveSlot = -1;
				return;
			}
			BeginExecution();
			continue;
		}
		if (State.Stage != EJRPGBattleStage::Execution)
		{
			return;
		}
		if (State.Action.bActive)
		{
			return;
		}
		// Depois de uma ação a luta espera (a reação do alvo, a faixa Done, o teste de fim e os passos de antes da próxima):
		// o StepFrame solta isto quando o quadro chega
		if (State.bFinishCheckPending || State.Frame < State.NextActionNotBefore)
		{
			return;
		}
		if (PickNext())
		{
			if (State.Action.bActive)
			{
				return;
			}
			continue;   // o escolhido caiu antes de agir, ou a ação foi descartada: o próximo
		}
		// Ninguém mais tem chave: a rodada acaba
		if (!bActionStartedThisRound)
		{
			if (bTurnLostToConditionThisRound)
			{
				// Os turnos se perderam por condição (não é falta de dado): a rodada passa e as condições com prazo vencem
				EmptyRounds = 0;
				if (++ConditionRounds >= MaxConditionRounds)
				{
					UE_LOG(LogTemp, Error, TEXT("BattleResolver: %d rodadas seguidas em que só condições tiraram os turnos (alguma sem prazo?) — a luta foi ABORTADA e nada é escrito no roster"), ConditionRounds);
					Finish(EJRPGBattleResult::Aborted);
					return;
				}
			}
			else
			{
				// Ninguém montou ação por falta de dado (alvo, cadeia): avisa a cada rodada e só aborta no teto alto
				++EmptyRounds;
				UE_LOG(LogTemp, Warning, TEXT("BattleResolver: a rodada %d acabou sem nenhuma ação (%d/%d rodadas seguidas) — nenhum combatente tem plano possível; dado faltando?"),
					State.Round, EmptyRounds, MaxEmptyRounds);
				if (EmptyRounds >= MaxEmptyRounds)
				{
					UE_LOG(LogTemp, Error, TEXT("BattleResolver: %d rodadas seguidas sem nenhuma ação por falta de plano — a luta foi ABORTADA e nada é escrito no roster"), EmptyRounds);
					Finish(EJRPGBattleResult::Aborted);
					return;
				}
			}
		}
		else
		{
			EmptyRounds = 0;
			ConditionRounds = 0;
		}
		EndRound();
		StartRound();
	}
}

void FJRPGBattleResolver::EndRound()
{
	// As condições com duração perdem uma rodada e saem quando acaba
	for (FJRPGCombatant& C : State.Combatants)
	{
		for (int32 i = C.Conditions.Num() - 1; i >= 0; --i)
		{
			FJRPGBattleCondition& Condition = C.Conditions[i];
			if (Condition.RoundsLeft > 0 && --Condition.RoundsLeft == 0)
			{
				C.Conditions.RemoveAt(i);
			}
		}
	}
}

bool FJRPGBattleResolver::PickNext()
{
	TArray<int32> Keys;
	Keys.Reserve(State.Combatants.Num());
	for (FJRPGCombatant& C : State.Combatants)
	{
		// A varredura dos caídos: a chave de quem morreu antes de agir é zerada antes de cada escolha
		if (!C.bAlive)
		{
			C.InitiativeKey = 0;
		}
		Keys.Add(C.InitiativeKey);
	}
	const int32 Pick = UBattleFormulas::PickByInitiative(Keys, [this]() { return Rand15(); });
	if (Pick == INDEX_NONE)
	{
		return false;
	}
	FJRPGCombatant& C = State.Combatants[Pick];
	C.InitiativeKey = 0;   // gasta a chave
	State.TurnOrder.Add(Pick);
	if (C.bAlive)
	{
		StartAction(Pick);
	}
	return true;
}

// =================================================================================================
// Comandos
// =================================================================================================

bool FJRPGBattleResolver::SubmitCommand(const FJRPGBattleCommand& Command, FString* OutWhy)
{
	auto Refuse = [OutWhy](const FString& Why)
	{
		if (OutWhy)
		{
			*OutWhy = Why;
		}
		return false;
	};

	if (State.Stage != EJRPGBattleStage::Commands)
	{
		return Refuse(TEXT("a rodada não está em Commands"));
	}
	if (!State.Combatants.IsValidIndex(Command.Slot))
	{
		return Refuse(FString::Printf(TEXT("slot %d não existe"), Command.Slot));
	}
	const FJRPGCombatant& C = State.Combatants[Command.Slot];
	// O dono confere: nenhum jogador manda no slot de outro, e ninguém manda na IA (regra 22)
	if (C.OwnerPlayer < 0 || C.OwnerPlayer != Command.PlayerIndex || C.Type == EJRPGCombatantType::Enemy)
	{
		return Refuse(FString::Printf(TEXT("o jogador %d não é dono do slot %d"), Command.PlayerIndex, Command.Slot));
	}
	if (!C.bAlive)
	{
		return Refuse(FString::Printf(TEXT("o slot %d está caído"), Command.Slot));
	}
	if (Command.Kind != EJRPGBattleCommandKind::Attack && Command.Kind != EJRPGBattleCommandKind::Arts
		&& Command.Kind != EJRPGBattleCommandKind::Guard)
	{
		return Refuse(TEXT("só Attack, Arts e Spirit (Guard) existem hoje (Item e Escape são a Fase 7, Magic a 8)"));
	}
	// O Spirit não tem alvo: o TargetSlot do comando é ignorado
	if (Command.Kind != EJRPGBattleCommandKind::Guard && !IsLivingFoe(C, Command.TargetSlot))
	{
		return Refuse(FString::Printf(TEXT("o alvo %d não é um adversário vivo"), Command.TargetSlot));
	}

	if (Command.Kind == EJRPGBattleCommandKind::Arts)
	{
		TArray<EJRPGArtButton> Buttons;
		if (!UJRPGArtsRules::DecodeArtBytes(Command.ArtBytes, Buttons) || Buttons.IsEmpty())
		{
			return Refuse(TEXT("os bytes da cadeia não são botões (ou estão vazios, ou passam de 16)"));
		}
		// A janela de entrada: cada botão gasta o custo dele do gauge da AGL; sem gauge, só o limite de botões
		if (C.MaxCommandGauge > 0)
		{
			int32 Cost = 0;
			for (const EJRPGArtButton Button : Buttons)
			{
				Cost += ButtonCostOf(C, Button);
			}
			if (Cost > C.CommandGauge)
			{
				return Refuse(FString::Printf(TEXT("a cadeia custa %d e o gauge tem %d"), Cost, C.CommandGauge));
			}
		}
	}

	FJRPGBattleCommand& Stored = State.Commands[Command.Slot];
	Stored = Command;
	if (Stored.Kind == EJRPGBattleCommandKind::Attack || Stored.Kind == EJRPGBattleCommandKind::Guard)
	{
		Stored.ArtBytes.Reset();   // o Attack é o Arms sozinho; o Spirit não tem botões
	}
	// A postura de Spirit começa no commit (a categoria 4 do original vale desde o menu, antes da vez dele); outro comando a desfaz
	State.Combatants[Command.Slot].bGuarding = Stored.Kind == EJRPGBattleCommandKind::Guard;
	if (Stored.Kind != EJRPGBattleCommandKind::Guard)
	{
		State.Combatants[Command.Slot].TargetSlot = Command.TargetSlot;
	}

	Pump();
	return true;
}

bool FJRPGBattleResolver::SubmitInput(const FJRPGBattleInput& Input, FString* OutWhy)
{
	auto Refuse = [OutWhy](const FString& Why)
	{
		if (OutWhy)
		{
			*OutWhy = Why;
		}
		return false;
	};

	if (State.Stage != EJRPGBattleStage::Execution)
	{
		return Refuse(TEXT("os inputs só valem durante a execução"));
	}
	if (!State.Combatants.IsValidIndex(Input.Slot))
	{
		return Refuse(FString::Printf(TEXT("slot %d não existe"), Input.Slot));
	}
	const FJRPGCombatant& C = State.Combatants[Input.Slot];
	if (C.OwnerPlayer < 0 || C.OwnerPlayer != Input.PlayerIndex)
	{
		return Refuse(FString::Printf(TEXT("o jogador %d não é dono do slot %d"), Input.PlayerIndex, Input.Slot));
	}
	if (!C.bAlive)
	{
		return Refuse(FString::Printf(TEXT("o slot %d está caído"), Input.Slot));
	}
	if (Input.Frame < 0 || Input.Frame > State.Frame)
	{
		return Refuse(FString::Printf(TEXT("o quadro %d do input não existe ainda (a luta está no %d)"), Input.Frame, State.Frame));
	}
	State.Inputs.Add(Input);
	return true;
}

bool FJRPGBattleResolver::SubmitApproach(const FJRPGApproachInput& Approach, FString* OutWhy)
{
	auto Refuse = [OutWhy](const FString& Why)
	{
		if (OutWhy)
		{
			*OutWhy = Why;
		}
		return false;
	};
	if (State.Stage != EJRPGBattleStage::Execution || !State.Action.bActive)
	{
		return Refuse(TEXT("nenhuma ação em curso"));
	}
	if (Approach.ActionIndex != State.Action.Timeline.ActionIndex)
	{
		return Refuse(FString::Printf(TEXT("a aproximação é da ação %d, a em curso é a %d"), Approach.ActionIndex, State.Action.Timeline.ActionIndex));
	}
	if (State.Action.bApproachReceived || State.Action.bApproachSpent)
	{
		return Refuse(TEXT("a ação já recebeu (ou já gastou) a aproximação dela"));
	}
	FJRPGApproachInput Clamped = Approach;
	Clamped.AttackRamp = FMath::Clamp(Approach.AttackRamp, 0, 0x800);
	Clamped.GuardRamp = FMath::Clamp(Approach.GuardRamp, 0, 0x7FFF);
	State.Action.bApproachReceived = true;
	State.Action.AttackRamp = Clamped.AttackRamp;
	State.Action.GuardRamp = Clamped.GuardRamp;
	State.Approaches.Add(Clamped);
	return true;
}

bool FJRPGBattleResolver::IsWaitingForApproach() const
{
	const FJRPGActiveAction& Action = State.Action;
	if (!bRequireApproach || State.bFinished || State.Stage != EJRPGBattleStage::Execution || !Action.bActive
		|| Action.bApproachReceived || Action.bApproachSpent || Action.NextHit != 0 || Action.Timeline.Hits.IsEmpty())
	{
		return false;
	}
	// Segura antes do primeiro passo (o pawn ainda não começou o golpe) ou do primeiro golpe, o que vier antes
	int32 Due = Action.Timeline.Hits[0].Frame;
	if (!Action.Timeline.Steps.IsEmpty())
	{
		Due = FMath::Min(Due, Action.Timeline.Steps[0].StartFrame);
	}
	return Action.Elapsed + 1 >= Due;
}

// =================================================================================================
// Começar e acabar uma ação
// =================================================================================================

void FJRPGBattleResolver::StartAction(int32 Slot)
{
	FJRPGCombatant& C = State.Combatants[Slot];

	// As condições do começo do turno: o dano por turno e o turno perdido
	if (!RunConditions(C, true))
	{
		// Perdeu o turno por condição (e não morreu): a rodada vazia que vier daí não é falta de dado
		if (C.bAlive)
		{
			bTurnLostToConditionThisRound = true;
		}
		if (!C.bAlive)
		{
			// Morreu da condição do começo do turno: a queda dele tem o mesmo ritmo do fim de uma ação (a reação, a faixa Done,
			// o teste de fim e o passo até o próximo), não pula direto para o próximo agente
			ScheduleFinishCheck();
		}
		else
		{
			CheckFinish();
		}
		return;
	}

	int32 Target = INDEX_NONE;
	FJRPGArtChain Chain;
	FJRPGActionTimeline Timeline;
	const FJRPGBattleCommand Command = State.Commands.IsValidIndex(Slot) ? State.Commands[Slot] : FJRPGBattleCommand();

	const bool bPlanned = C.Type == EJRPGCombatantType::Enemy
		? PlanMonsterAction(C, Target, Timeline)
		: PlanHeroAction(C, Command, Target, Chain, Timeline);
	if (!bPlanned)
	{
		// O alvo caiu e não há outro, ou a cadeia veio vazia: a ação é descartada e nada é gasto
		UE_LOG(LogTemp, Log, TEXT("BattleResolver: a ação de %s foi descartada (sem alvo vivo ou sem cadeia)"), *SlotName(Slot));
		return;
	}

	Timeline.ActionIndex = State.NextActionIndex++;
	Timeline.SlotIndex = Slot;
	Timeline.StartFrame = State.Frame;

	FJRPGActiveAction& Action = State.Action;
	Action = FJRPGActiveAction();
	Action.bActive = true;
	Action.Slot = Slot;
	Action.TargetSlot = Target;
	Action.Command = Command;
	Action.Chain = Chain;
	Action.Timeline = Timeline;
	Timelines.Add(Timeline);   // a cópia da apresentação: o índice é o ActionIndex
	Action.Elapsed = 0;
	Action.ComboDamage.Init(0, State.Combatants.Num());
	Action.ComboHits.Init(0, State.Combatants.Num());

	C.TargetSlot = Target;
	State.ActiveSlot = Slot;
	bActionStartedThisRound = true;
	EmptyRounds = 0;
	ConditionRounds = 0;
	Emit(EJRPGBattleEventType::ActionStart, Slot, Target, Target);

	// O que cai no quadro 0 (um golpe no primeiro quadro, o aviso da primeira Art)
	ProcessDue();
	if (State.Action.bActive && State.Action.Elapsed >= State.Action.Timeline.TotalFrames)
	{
		EndAction(false);
	}
}

void FJRPGBattleResolver::EndAction(bool bCancelled)
{
	const int32 Slot = State.Action.Slot;

	// Seguro: quem ficou com o HP em 0 sem que algum golpe escrevesse o total (nenhum golpe que conecte sobrou) cai agora
	for (FJRPGCombatant& C : State.Combatants)
	{
		if (C.bAlive && C.HP <= 0)
		{
			KillCombatant(C, Slot);
		}
	}
	// O resumo da combo (o HIT / TOTAL do original): quantos golpes acertaram cada alvo e o dano de todos somado, inclusive
	// o que passou do HP. Sai também com 1 golpe só (Count 1): a apresentação mostra então só o Damage, o DAMAGE do original
	for (int32 i = 0; i < State.Action.ComboHits.Num(); ++i)
	{
		if (State.Action.ComboHits[i] >= 1 && State.Action.ComboDamage.IsValidIndex(i))
		{
			Emit(EJRPGBattleEventType::ComboTotal, Slot, i, State.Action.ComboDamage[i], -1, 0, NAME_None, State.Action.ComboHits[i]);
		}
	}
	Emit(EJRPGBattleEventType::ActionEnd, Slot, State.Action.TargetSlot, bCancelled ? 1 : 0);
	State.Action.bActive = false;
	State.ActiveSlot = -1;

	// As condições do fim do turno de quem agiu
	if (State.Combatants.IsValidIndex(Slot) && State.Combatants[Slot].bAlive)
	{
		RunConditions(State.Combatants[Slot], false);
	}

	ScheduleFinishCheck();
}

void FJRPGBattleResolver::ScheduleFinishCheck()
{
	// O atacante está livre (o ActionEnd saiu), mas a luta não: o estado 0x20 do original segura até o alvo voltar ao idle (ou a
	// queda / o fade acabar), a faixa Done gasta 60 vsyncs, e só aí o 0x5A testa a vitória e a derrota; a próxima ação entra uns
	// passos depois. A reação conta do golpe que a começou, então a parte que já tocou durante a ação não é esperada de novo
	int32 ReactionDone = 0;
	for (const FJRPGCombatant& C : State.Combatants)
	{
		ReactionDone = FMath::Max(ReactionDone, C.ReactionHoldUntilFrame);
	}
	State.FinishCheckFrame = FMath::Max(State.Frame, ReactionDone) + FMath::Max(Defaults.DoneBandFrames, 0);
	State.NextActionNotBefore = State.FinishCheckFrame + FMath::Max(Defaults.PassGapFrames, 0);
	State.bFinishCheckPending = true;
}

bool FJRPGBattleResolver::CheckFinish()
{
	if (State.bFinished)
	{
		return true;
	}
	bool bPartyAlive = false;
	bool bFoeAlive = false;
	for (const FJRPGCombatant& C : State.Combatants)
	{
		if (C.bAlive)
		{
			(C.Team == 0 ? bPartyAlive : bFoeAlive) = true;
		}
	}
	// A vitória é "um time inteiro caído", não "os inimigos morreram"; o time 0 é o do jogador local
	if (!bPartyAlive)
	{
		Finish(EJRPGBattleResult::Defeat);
		return true;
	}
	if (!bFoeAlive)
	{
		Finish(EJRPGBattleResult::Victory);
		return true;
	}
	return false;
}

void FJRPGBattleResolver::Finish(EJRPGBattleResult Result)
{
	State.bFinished = true;
	State.Result = Result;
	State.Stage = EJRPGBattleStage::Finished;
	State.ActiveSlot = -1;
	State.Action.bActive = false;
}

void FJRPGBattleResolver::StepFrame()
{
	if (State.bFinished || State.Stage != EJRPGBattleStage::Execution)
	{
		return;
	}
	++State.Frame;
	if (!State.Action.bActive)
	{
		// O teste de vitória / derrota vem depois da reação do alvo e da faixa Done (o estado 0x5A), nunca no meio da ação
		if (State.bFinishCheckPending && State.Frame >= State.FinishCheckFrame)
		{
			State.bFinishCheckPending = false;
			if (CheckFinish())
			{
				return;
			}
		}
		// A próxima ação só depois dos passos do fim da anterior (Pump também confere)
		if (State.Frame >= State.NextActionNotBefore)
		{
			Pump();
		}
		return;
	}
	++State.Action.Elapsed;
	ProcessDue();
	if (State.Action.bActive && State.Action.Elapsed >= State.Action.Timeline.TotalFrames)
	{
		EndAction(false);
	}
}

// =================================================================================================
// Planejar a ação
// =================================================================================================

bool FJRPGBattleResolver::PlanHeroAction(const FJRPGCombatant& Hero, const FJRPGBattleCommand& Command, int32& OutTarget,
	FJRPGArtChain& OutChain, FJRPGActionTimeline& OutTimeline)
{
	// O Spirit: o herói se concentra no lugar (o alvo é ele mesmo, para a cena não andar nem mirar ninguém)
	if (Command.Kind == EJRPGBattleCommandKind::Guard)
	{
		OutTarget = Hero.SlotIndex;
		ComposeSpiritTimeline(Hero, OutTimeline);
		return !OutTimeline.Steps.IsEmpty();
	}

	// O alvo que caiu antes da ação: o vivo de menor slot; nenhum = a ação é descartada
	OutTarget = IsLivingFoe(Hero, Command.TargetSlot) ? Command.TargetSlot : LowestLivingFoe(Hero);
	if (OutTarget == INDEX_NONE)
	{
		return false;
	}

	TArray<EJRPGArtButton> Buttons;
	if (Command.Kind == EJRPGBattleCommandKind::Arts)
	{
		if (!UJRPGArtsRules::DecodeArtBytes(Command.ArtBytes, Buttons) || Buttons.IsEmpty())
		{
			return false;
		}
	}
	else
	{
		Buttons.Add(EJRPGArtButton::Arms);   // o Attack é um golpe do botão Arms
	}

	// A cadeia sai do snapshot do herói e do Rng da luta; reconhece de novo a partir dos botões
	FJRPGArtsContext Context;
	Context.Character = Hero.Key;
	Context.KnownArts = Hero.KnownArts;
	Context.bHasRaSeru = Hero.bHasRaSeru;
	Context.AP = Hero.AP;
	Context.MaxAP = Hero.MaxAP;
	Context.LearnOnUseChance = Hero.LearnOnUseChance;
	OutChain = UJRPGArtsRules::BuildChain(ArtsTable, Context, Buttons, State.Rng);
	if (OutChain.Entries.IsEmpty())
	{
		return false;
	}

	// Os passos da cadeia, na ordem que o turno toca (o espelho de UJRPGArtsRules::AppendChainSteps, sem carregar clip)
	TArray<FPlanStep> Steps;
	for (int32 e = 0; e < OutChain.Entries.Num(); ++e)
	{
		const FJRPGArtChainEntry& Entry = OutChain.Entries[e];
		if (Entry.Type == EJRPGArtChainStepType::Swing)
		{
			FPlanStep& Step = Steps.AddDefaulted_GetRef();
			Step.Kind = EJRPGActionStepKind::Swing;
			Step.Entry = e;
			Step.ClipName = SwingClipFor(Entry.Button, Hero.bLeftHanded);
			FillSwingHits(Hero, Entry.Button, Step.HitBytes, Step.HitBeats, Step.BeatRate);
			Step.HitSource = SourceForButton(Entry.Button);
		}
		else
		{
			AppendArtSteps(Hero, e, Entry.ArtKey, Steps, Entry.bNewlyLearned);
		}
	}
	ComposeHeroTimeline(Hero, OutChain, Steps, OutTimeline);
	return !OutTimeline.Steps.IsEmpty();
}

void FJRPGBattleResolver::ComposeSpiritTimeline(const FJRPGCombatant& Hero, FJRPGActionTimeline& Out)
{
	// A faixa do Spirit do original (estados 0x46..0x48): o clip 0x10, a espera de 0x20 e a subida do gauge, tudo no lugar. Aqui a
	// duração é a do clip (lida como dado, como a dos golpes); o gauge estendido e a postura são regra, não tempo
	static const FName SpiritClip(TEXT("Spirit"));
	FJRPGClipTiming Timing;
	FJRPGClipQuery Query;
	Query.Kind = EJRPGActionStepKind::Spirit;
	Query.ClipName = SpiritClip;
	Query.GameSpeed = UBattleFormulas::NormalGameSpeed;
	Query.ExpectedHits = 0;
	const bool bFound = ClipTiming && ClipTiming(Hero.Key, Query, Timing) && Timing.Frames > 0;
	if (!bFound)
	{
		Out.bUsedDefaults = true;
		LogFallback(TEXT("spirit"), TEXT("o clip Spirit do herói não foi achado: a duração padrão (FJRPGTimelineDefaults::SpiritFrames)"));
	}
	const int32 Frames = FMath::Max(bFound ? Timing.Frames : Defaults.SpiritFrames, 1);

	Out.LeadFrames = 0;
	FJRPGActionStep& Step = Out.Steps.AddDefaulted_GetRef();
	Step.Kind = EJRPGActionStepKind::Spirit;
	Step.StartFrame = 0;
	Step.EndFrame = Frames;
	Step.ClipName = SpiritClip;
	Step.GameSpeed = UBattleFormulas::NormalGameSpeed;
	Step.EntryIndex = -1;
	Out.TotalFrames = Frames + Defaults.TailFrames;

	// O AP do turno de Spirit (a faixa Done paga 0x20 no lugar de 8), no fim do clip
	const int32 NewAP = FMath::Clamp(Hero.AP + FMath::Max(Defaults.SpiritAPGain, 0), 0, FMath::Max(Hero.MaxAP, 0));
	if (NewAP != Hero.AP)
	{
		FJRPGActionMark& Mark = Out.Marks.AddDefaulted_GetRef();
		Mark.Frame = FMath::Max(Frames - 1, 0);
		Mark.Kind = EJRPGActionMarkKind::SetAP;
		Mark.Value = NewAP;
	}
}

void FJRPGBattleResolver::AppendArtSteps(const FJRPGCombatant& Hero, int32 Entry, FName ArtKey, TArray<FPlanStep>& Steps, bool bNewlyLearned) const
{
	const FJRPGArtData* Row = ArtsTable ? ArtsTable->FindRow<FJRPGArtData>(ArtKey, TEXT("BattleResolver"), false) : nullptr;
	if (!Row)
	{
		UE_LOG(LogTemp, Warning, TEXT("BattleResolver: a Art %s não está no DT_Arts"), *ArtKey.ToString());
		return;
	}

	// A Super e a Miracle: a Sequence é o turno inteiro. A Hyper e a Regular com Sequence trazem o starter nela
	if (Row->Kind == EJRPGArtKind::Super || Row->Kind == EJRPGArtKind::Miracle || !Row->Sequence.IsEmpty())
	{
		for (const FName& Step : Row->Sequence)
		{
			ExpandSequenceStep(Hero, Entry, Step, ArtKey, *Row, 0, Steps);
		}
		return;
	}

	// A Regular sem Sequence: o starter e o clip dela (os golpes dos botões antes já são entradas Swing da cadeia). A Art
	// descoberta agora entra pelo starter especial, como no original (o 0x1A no lugar do 0x19: o dash com a luta congelada, o
	// "NEW", e a Art no modo especial); a conhecida, pelo starter comum
	FPlanStep& Starter = Steps.AddDefaulted_GetRef();
	Starter.Kind = bNewlyLearned ? EJRPGActionStepKind::SpecialStarter : EJRPGActionStepKind::ArtStarter;
	Starter.Entry = Entry;
	Starter.ClipName = bNewlyLearned ? TEXT("SpecialStarter") : TEXT("ArtStarter");
	Starter.ArtKey = ArtKey;

	FPlanStep& Art = Steps.AddDefaulted_GetRef();
	Art.Kind = EJRPGActionStepKind::Art;
	Art.Entry = Entry;
	Art.ClipName = ArtKey;
	Art.ArtKey = ArtKey;
	Art.HitSource = EJRPGHitSource::Art;
	for (int32 i = 0; i < Row->HitPower.Num(); ++i)
	{
		const EJRPGArtHitTarget Target = Row->HitTarget.IsValidIndex(i) ? Row->HitTarget[i] : EJRPGArtHitTarget::Upper;
		Art.HitBytes.Add(UBattleFormulas::ArtPowerByte(Row->HitPower[i], Target));
	}
}

void FJRPGBattleResolver::ExpandSequenceStep(const FJRPGCombatant& Hero, int32 Entry, FName Step, FName SelfKey, const FJRPGArtData& Self,
	int32 Depth, TArray<FPlanStep>& Steps) const
{
	auto AddArt = [&](FName Key, const FJRPGArtData& Row)
	{
		FPlanStep& Art = Steps.AddDefaulted_GetRef();
		Art.Kind = EJRPGActionStepKind::Art;
		Art.Entry = Entry;
		Art.ClipName = Key;
		Art.ArtKey = Key;
		Art.HitSource = EJRPGHitSource::Art;
		for (int32 i = 0; i < Row.HitPower.Num(); ++i)
		{
			const EJRPGArtHitTarget Target = Row.HitTarget.IsValidIndex(i) ? Row.HitTarget[i] : EJRPGArtHitTarget::Upper;
			Art.HitBytes.Add(UBattleFormulas::ArtPowerByte(Row.HitPower[i], Target));
		}
	};

	if (Step == SelfKey)
	{
		AddArt(SelfKey, Self);
		return;
	}
	if (const FJRPGArtData* Other = ArtsTable ? ArtsTable->FindRow<FJRPGArtData>(Step, TEXT("BattleResolver"), false) : nullptr)
	{
		if (!Other->Anim.IsNull() || Other->Sequence.IsEmpty() || Depth >= 4)
		{
			AddArt(Step, *Other);
			return;
		}
		for (const FName& Inner : Other->Sequence)
		{
			ExpandSequenceStep(Hero, Entry, Inner, Step, *Other, Depth + 1, Steps);
		}
		return;
	}

	// Um clip básico do herói: SwingL..U, ArtStarter, SpecialStarter
	const FString Name = Step.ToString();
	FPlanStep& Basic = Steps.AddDefaulted_GetRef();
	Basic.Entry = Entry;
	Basic.ClipName = Step;
	if (Name == TEXT("ArtStarter"))
	{
		Basic.Kind = EJRPGActionStepKind::ArtStarter;
		Basic.ArtKey = SelfKey;
	}
	else if (Name == TEXT("SpecialStarter"))
	{
		Basic.Kind = EJRPGActionStepKind::SpecialStarter;
		Basic.ArtKey = SelfKey;
	}
	else
	{
		// Um conector da Super: um golpe simples do botão que o clip representa
		const EJRPGArtButton Button = ButtonForSwing(Name, Hero.bLeftHanded);
		Basic.Kind = EJRPGActionStepKind::Swing;
		FillSwingHits(Hero, Button, Basic.HitBytes, Basic.HitBeats, Basic.BeatRate);
		Basic.HitSource = SourceForButton(Button);
	}
}

void FJRPGBattleResolver::ComposeHeroTimeline(const FJRPGCombatant& Hero, const FJRPGArtChain& Chain, TArray<FPlanStep>& Steps,
	FJRPGActionTimeline& Out)
{
	// A escala de velocidade de cada passo (UJRPGArtSequenceLibrary::ApplyGameSpeeds): 8 nos golpes e no ArtStarter,
	// 4 numa Art, e o SpecialStarter liga o modo especial, em que as Arts vão a 2. O próprio SpecialStarter é o dash a 2 (o braço
	// 0x1A de FUN_8004AD80 põe 0 em todos e 2 em quem age, 0x8004B728..0x8004B750). O SpeedScale da linha troca a regra
	bool bSpecialMode = false;
	for (FPlanStep& Step : Steps)
	{
		switch (Step.Kind)
		{
		case EJRPGActionStepKind::SpecialStarter:
			Step.Speed = 2;
			bSpecialMode = true;
			break;
		case EJRPGActionStepKind::Art:
		{
			// Depois do SpecialStarter, toda Art a 2. (A leitura do RE de que a trava cai no commit seguinte, só a primeira Art a 2,
			// deixou a Miracle rápida demais no teste do usuário: a regra de antes volta; o SpeedScale da linha afina uma Art só)
			Step.Speed = bSpecialMode ? 2 : 4;
			const FJRPGArtData* Row = ArtsTable ? ArtsTable->FindRow<FJRPGArtData>(Step.ArtKey, TEXT("BattleResolver"), false) : nullptr;
			if (Row && Row->SpeedScale > 0)
			{
				Step.Speed = Row->SpeedScale;
			}
			break;
		}
		default:
			Step.Speed = UBattleFormulas::NormalGameSpeed;
			break;
		}
	}

	Out.LeadFrames = Defaults.LeadFrames;
	int32 Cursor = Out.LeadFrames;
	for (int32 i = 0; i < Steps.Num(); ++i)
	{
		const FPlanStep& Plan = Steps[i];

		// O tempo do clip, lido como dado; sem ele (ou sem os marcadores) valem os padrões, registrados
		FJRPGClipTiming Timing;
		FJRPGClipQuery Query;
		Query.Kind = Plan.Kind;
		Query.ClipName = Plan.ClipName;
		Query.ArtKey = Plan.ArtKey;
		Query.GameSpeed = Plan.Speed;
		Query.ExpectedHits = Plan.HitBytes.Num();
		const bool bFound = ClipTiming && ClipTiming(Hero.Key, Query, Timing);

		int32 Frames = 0;
		int32 Cut = -1;
		TArray<int32> HitFrames;
		switch (Plan.Kind)
		{
		case EJRPGActionStepKind::Swing:
			Frames = bFound && Timing.Frames > 0 ? Timing.Frames : Scaled(Defaults.SwingFrames, Plan.Speed);
			if (bFound && !Timing.HitFrames.IsEmpty() && Timing.HitFrames.Num() == Plan.HitBytes.Num())
			{
				HitFrames = Timing.HitFrames;
			}
			else if (!Plan.HitBeats.IsEmpty() && Plan.HitBeats.Num() == Plan.HitBytes.Num())
			{
				// O quadro de golpe do registro de golpe do equipamento (DT_HeroSwings), na mesma conta dos monstros: o beat B
				// cai no quadro B - 1 do clip, e o clip anda no rate dele. O clip cobre o último golpe
				for (const int32 Beat : Plan.HitBeats)
				{
					HitFrames.Add(UBattleFormulas::HitStepForBeat(Beat, Plan.Speed, Plan.BeatRate));
				}
				Frames = FMath::Max(Frames, HitFrames.Last() + 1);
			}
			else
			{
				HitFrames.Add(Scaled(Defaults.SwingHitFrame, Plan.Speed));
				Out.bUsedDefaults = true;
				LogFallback(TEXT("swing"), TEXT("golpe simples sem marcador de golpe no clip: quadros padrão (FJRPGTimelineDefaults)"));
			}
			// Onde o golpe seguinte da cadeia entra no meio deste clip: o Chain Cut do clip, se ele traz; senão o do original, o
			// quadro B + 3 do primeiro beat do registro de golpe (o gate +0x76 de todo golpe padrão do herói é 0; BATTLE_TIMING.md,
			// seção 2), na escala e no rate do passo; e sem beat lido, o corte padrão
			if (bFound && Timing.CutFrame > 0)
			{
				Cut = Timing.CutFrame;
			}
			else if (!Plan.HitBeats.IsEmpty() && Plan.HitBeats.Num() == Plan.HitBytes.Num() && Plan.HitBeats[0] > 0)
			{
				Cut = UBattleFormulas::ClipFramesToSteps(Plan.HitBeats[0] + 3, Plan.Speed, Plan.BeatRate);
			}
			else
			{
				Cut = Scaled(Defaults.SwingCutFrame, Plan.Speed);
			}
			break;
		case EJRPGActionStepKind::ArtStarter:
			Frames = bFound && Timing.Frames > 0 ? Timing.Frames : Scaled(Defaults.ArtStarterFrames, Plan.Speed);
			break;
		case EJRPGActionStepKind::SpecialStarter:
			Frames = bFound && Timing.Frames > 0 ? Timing.Frames : Scaled(Defaults.SpecialStarterFrames, Plan.Speed);
			break;
		default:
		{
			const int32 N = Plan.HitBytes.Num();
			if (bFound && Timing.Frames > 0 && Timing.HitFrames.Num() == N)
			{
				Frames = Timing.Frames;
				HitFrames = Timing.HitFrames;
			}
			else
			{
				// Os N golpes espaçados no clip: o tempo até o primeiro, entre eles e a cauda depois do último
				const int32 Lead = Scaled(Defaults.ArtLeadFrames, Plan.Speed);
				const int32 Per = Scaled(Defaults.ArtFramesPerHit, Plan.Speed);
				for (int32 k = 0; k < N; ++k)
				{
					HitFrames.Add(Lead + k * Per);
				}
				Frames = Lead + FMath::Max(N - 1, 0) * Per + Scaled(Defaults.ArtTailFrames, Plan.Speed);
				Out.bUsedDefaults = true;
				LogFallback(FString::Printf(TEXT("art:%s"), *Plan.ArtKey.ToString()),
					FString::Printf(TEXT("a Art %s sem marcadores de golpe no clip (%d golpe(s) em HitPower): golpes espaçados pelo padrão"),
						*Plan.ArtKey.ToString(), N));
			}
			break;
		}
		}

		// Com outro passo depois, o clip é cortado no Chain Cut (nunca antes do último golpe dele)
		int32 End = Frames;
		const bool bHasNext = i + 1 < Steps.Num();
		if (bHasNext && Cut > 0 && Cut < Frames)
		{
			const int32 AfterLastHit = HitFrames.IsEmpty() ? 0 : HitFrames.Last() + 1;
			End = FMath::Max(Cut, AfterLastHit);
		}

		FJRPGActionStep& Step = Out.Steps.AddDefaulted_GetRef();
		Step.Kind = Plan.Kind;
		Step.StartFrame = Cursor;
		Step.EndFrame = Cursor + End;
		Step.ClipName = Plan.ClipName;
		Step.ArtKey = Plan.ArtKey;
		Step.GameSpeed = Plan.Speed;
		Step.EntryIndex = Plan.Entry;

		for (int32 k = 0; k < HitFrames.Num() && k < Plan.HitBytes.Num(); ++k)
		{
			FJRPGActionHit& Hit = Out.Hits.AddDefaulted_GetRef();
			Hit.Frame = Cursor + HitFrames[k];
			Hit.PowerByte = Plan.HitBytes[k];
			Hit.Source = Plan.Kind == EJRPGActionStepKind::Art ? EJRPGHitSource::Art : Plan.HitSource;
			Hit.StepIndex = i;
			Hit.ChainEntry = Plan.Entry;
			Hit.bFirstOfEntry = !Out.Hits.ContainsByPredicate([&](const FJRPGActionHit& Other)
			{
				return &Other != &Hit && Other.ChainEntry == Plan.Entry;
			});
		}
		Cursor += End;
	}
	Out.TotalFrames = Cursor + Defaults.TailFrames;

	// O que a ação faz fora dos golpes: o aviso da Art, o que se aprende e o AP que anda (o plano da cadeia)
	TSet<FName> LearnedMarked;
	for (int32 e = 0; e < Chain.Entries.Num(); ++e)
	{
		const FJRPGArtChainEntry& Entry = Chain.Entries[e];
		if (Entry.Type == EJRPGArtChainStepType::Swing)
		{
			continue;
		}
		int32 StartFrame = MAX_int32;
		for (const FJRPGActionStep& Step : Out.Steps)
		{
			if (Step.EntryIndex == e)
			{
				StartFrame = FMath::Min(StartFrame, Step.StartFrame);
			}
		}
		if (StartFrame == MAX_int32)
		{
			continue;   // a Art não está no DT_Arts: sem passo, sem aviso nem AP
		}
		int32 FirstHit = MAX_int32;
		for (const FJRPGActionHit& Hit : Out.Hits)
		{
			if (Hit.ChainEntry == e)
			{
				FirstHit = FMath::Min(FirstHit, Hit.Frame);
			}
		}

		auto AddMark = [&Out](int32 Frame, EJRPGActionMarkKind Kind, FName Key, int32 Value)
		{
			FJRPGActionMark& Mark = Out.Marks.AddDefaulted_GetRef();
			Mark.Frame = Frame;
			Mark.Kind = Kind;
			Mark.ArtKey = Key;
			Mark.Value = Value;
		};

		AddMark(StartFrame, EJRPGActionMarkKind::ArtRecognized, Entry.ArtKey, e);
		if (Entry.bNewlyLearned && !LearnedMarked.Contains(Entry.ArtKey))
		{
			LearnedMarked.Add(Entry.ArtKey);
			AddMark(StartFrame, EJRPGActionMarkKind::ArtLearned, Entry.ArtKey, e);
		}
		if (Entry.Type == EJRPGArtChainStepType::Special)
		{
			// As Arts de dentro de uma Super que disparou também foram feitas: as novas entram na lista
			for (const FName& Inside : Entry.InsideArts)
			{
				if (Chain.NewlyLearned.Contains(Inside) && !LearnedMarked.Contains(Inside))
				{
					LearnedMarked.Add(Inside);
					AddMark(StartFrame, EJRPGActionMarkKind::ArtLearned, Inside, e);
				}
			}
		}
		if (Entry.APAfter != Entry.APBefore)
		{
			// Um especial gasta quando começa; uma Regular soma no primeiro golpe dela (a cortada pela morte do
			// inimigo ainda soma, a que não chegou lá não)
			const bool bSpends = Entry.APAfter < Entry.APBefore || Entry.Type == EJRPGArtChainStepType::Special;
			const int32 Frame = bSpends || FirstHit == MAX_int32 ? StartFrame : FirstHit;
			AddMark(Frame, EJRPGActionMarkKind::SetAP, Entry.ArtKey, Entry.APAfter);
		}
	}
	Out.Marks.StableSort([](const FJRPGActionMark& A, const FJRPGActionMark& B) { return A.Frame < B.Frame; });
}

bool FJRPGBattleResolver::PlanMonsterAction(FJRPGCombatant& Monster, int32& OutTarget, FJRPGActionTimeline& OutTimeline)
{
	// O alvo: um herói vivo sorteado por igual (rand % party), sorteando de novo por cima dos caídos (FUN_801E7320)
	OutTarget = DrawLivingFoe(Monster);
	if (OutTarget == INDEX_NONE)
	{
		return false;
	}

	// Os golpes: sorteia entre os candidatos enquanto o pool de AGL paga o custo de cada um (FUN_801E9FD4)
	TArray<FJRPGBattleActionEntry> Queue;
	if (Monster.Swings.IsEmpty())
	{
		FJRPGBattleActionEntry Default;
		// Sem ação do disco: o pawn toca o primeiro golpe do inimigo (Attacks[0]), não o clip 0 (Idle)
		Default.ActionIndex = INDEX_NONE;
		Default.Tag = 0x0D;
		Default.Power.Add(0x0D);
		Default.Rate = 2;
		Default.Clip = TEXT("Attack1");
		Queue.Add(Default);
		LogFallback(FString::Printf(TEXT("noswing:%s"), *Monster.Key.ToString()),
			FString::Printf(TEXT("%s não tem golpe no DT_EnemyActions: um golpe padrão (poder 0x0D, quadros padrão)"), *Monster.Key.ToString()));
	}
	else
	{
		TArray<int32> Costs;
		for (const FJRPGBattleActionEntry& Swing : Monster.Swings)
		{
			Costs.Add(Swing.AGLCost);
		}
		TArray<int32> Picks = UBattleFormulas::EnemySwingQueue(Costs, Monster.CommandGauge, [this]() { return Rand15(); });
		if (Picks.IsEmpty())
		{
			// O pool não paga nem o mais barato: o original ainda dá um golpe (o orçamento tem piso de 1)
			int32 Cheapest = 0;
			for (int32 i = 1; i < Costs.Num(); ++i)
			{
				if (Costs[i] < Costs[Cheapest])
				{
					Cheapest = i;
				}
			}
			Picks.Add(Cheapest);
		}
		int32 Spent = 0;
		for (const int32 Pick : Picks)
		{
			Queue.Add(Monster.Swings[Pick]);
			Spent += Costs[Pick];
		}
		Monster.CommandGauge = FMath::Max(Monster.CommandGauge - Spent, 0);
	}
	ComposeEnemyTimeline(Monster, Queue, OutTimeline);
	return !OutTimeline.Steps.IsEmpty();
}

void FJRPGBattleResolver::ComposeEnemyTimeline(const FJRPGCombatant& Monster, const TArray<FJRPGBattleActionEntry>& Queue, FJRPGActionTimeline& Out)
{
	Out.LeadFrames = Defaults.LeadFrames;
	int32 Cursor = Out.LeadFrames;
	for (int32 j = 0; j < Queue.Num(); ++j)
	{
		const FJRPGBattleActionEntry& Entry = Queue[j];
		const int32 Rate = FMath::Max(Entry.Rate, 1);

		int32 Frames = Entry.FrameCount > 0
			? UBattleFormulas::ClipFramesToSteps(Entry.FrameCount, UBattleFormulas::NormalGameSpeed, Rate)
			: Defaults.EnemySwingFrames;
		if (Entry.FrameCount <= 0)
		{
			Out.bUsedDefaults = true;
			LogFallback(FString::Printf(TEXT("enemyframes:%s"), *Monster.Key.ToString()),
				FString::Printf(TEXT("%s: o golpe não traz o tamanho do clip: quadros padrão"), *Monster.Key.ToString()));
		}

		// O byte de poder de cada golpe: o do índice dele; um 0 no meio repete o primeiro (o 1º byte é também a tag)
		const int32 FirstPower = Entry.Power.IsValidIndex(0) && Entry.Power[0] != 0 ? Entry.Power[0] : Entry.Tag;
		auto PowerFor = [&](int32 HitIndex)
		{
			return Entry.Power.IsValidIndex(HitIndex) && Entry.Power[HitIndex] != 0 ? Entry.Power[HitIndex] : FirstPower;
		};

		// Os golpes: o beat B dispara no quadro B - 1 do clip (HitStepForBeat). No original o núcleo só dispara os beats que
		// o cursor alcança (quadro <= FrameCount - 1), então um beat além do clip NUNCA dispara (o disco tem quatro assim:
		// koru_05/06 em 200, rogue_05 em 255): é descartado, e o clip NÃO é esticado até ele
		TArray<int32> HitFrames;
		int32 BeatsOnDisc = 0;
		int32 BeatsUnreachable = 0;
		for (const int32 Beat : Entry.HitFrames)
		{
			if (Beat <= 0)
			{
				continue;
			}
			++BeatsOnDisc;
			if (Entry.FrameCount > 0 && Beat - 1 >= Entry.FrameCount)
			{
				++BeatsUnreachable;
				continue;
			}
			HitFrames.Add(UBattleFormulas::HitStepForBeat(Beat, UBattleFormulas::NormalGameSpeed, Rate));
		}
		if (BeatsOnDisc == 0)
		{
			// A entrada não traz quadro de golpe nenhum: o golpe padrão
			HitFrames.Add(Defaults.EnemySwingHitFrame);
			// O quadro padrão é invenção nossa, não dado do disco: o clip cobre ele (não é o caso do beat do disco além do clip)
			Frames = FMath::Max(Frames, HitFrames.Last() + 1);
			Out.bUsedDefaults = true;
			LogFallback(FString::Printf(TEXT("enemyhit:%s"), *Monster.Key.ToString()),
				FString::Printf(TEXT("%s: o golpe não traz quadro de golpe: um golpe nos quadros padrão"), *Monster.Key.ToString()));
		}
		else if (HitFrames.IsEmpty())
		{
			// Todos os beats estão além do clip: a ação toca o clip e não causa dano (no disco, o dano desses chefes vem de
			// script / magia — Fase 8). Continua elegível como golpe: o sorteio paga o AGL dela igual
			LogFallback(FString::Printf(TEXT("enemyunreachable:%s:%d"), *Monster.Key.ToString(), Entry.ActionIndex),
				FString::Printf(TEXT("%s: a ação %d tem %d quadro(s) de golpe além do clip de %d quadros: o clip toca sem golpe nem dano (o original também)"),
					*Monster.Key.ToString(), Entry.ActionIndex, BeatsUnreachable, Entry.FrameCount));
		}

		// Com outro golpe na fila e o gate em 0, o próximo entra no meio do clip (beat + 2 < quadro) e o que ficar
		// depois do corte se perde, como no original
		int32 End = Frames;
		if (j + 1 < Queue.Num() && Entry.Gate == 0 && !HitFrames.IsEmpty() && !Entry.HitFrames.IsEmpty() && Entry.HitFrames[0] > 0)
		{
			const int32 Cut = UBattleFormulas::ClipFramesToSteps(Entry.HitFrames[0] + 3, UBattleFormulas::NormalGameSpeed, Rate);
			if (Cut < Frames)
			{
				End = FMath::Max(Cut, HitFrames[0] + 1);
			}
		}

		FJRPGActionStep& Step = Out.Steps.AddDefaulted_GetRef();
		Step.Kind = EJRPGActionStepKind::EnemySwing;
		Step.StartFrame = Cursor;
		Step.EndFrame = Cursor + End;
		Step.ClipName = Entry.Clip;
		Step.GameSpeed = UBattleFormulas::NormalGameSpeed;
		Step.EntryIndex = Entry.ActionIndex;

		for (int32 h = 0; h < HitFrames.Num(); ++h)
		{
			if (HitFrames[h] >= End)
			{
				continue;
			}
			FJRPGActionHit& Hit = Out.Hits.AddDefaulted_GetRef();
			Hit.Frame = Cursor + HitFrames[h];
			Hit.PowerByte = PowerFor(h);
			Hit.Source = EJRPGHitSource::Monster;
			Hit.StepIndex = j;
			Hit.ChainEntry = -1;
			Hit.bFirstOfEntry = h == 0;
		}
		Cursor += End;
	}
	Out.TotalFrames = Cursor + Defaults.TailFrames;
}

int32 FJRPGBattleResolver::DrawLivingFoe(const FJRPGCombatant& Of)
{
	TArray<int32> Foes;
	bool bAnyAlive = false;
	for (const FJRPGCombatant& C : State.Combatants)
	{
		if (C.Team != Of.Team)
		{
			Foes.Add(C.SlotIndex);
			bAnyAlive |= C.bAlive;
		}
	}
	if (!bAnyAlive)
	{
		return INDEX_NONE;
	}
	for (int32 Try = 0; Try < MaxTargetDraws; ++Try)
	{
		const int32 Slot = Foes[Rand15() % Foes.Num()];
		if (State.Combatants[Slot].bAlive)
		{
			return Slot;
		}
	}
	return LowestLivingFoe(Of);
}

// =================================================================================================
// Resolver
// =================================================================================================

void FJRPGBattleResolver::ProcessDue()
{
	while (State.Action.bActive)
	{
		FJRPGActiveAction& Action = State.Action;
		const TArray<FJRPGActionMark>& Marks = Action.Timeline.Marks;
		const TArray<FJRPGActionHit>& Hits = Action.Timeline.Hits;
		const bool bMark = Marks.IsValidIndex(Action.NextMark) && Marks[Action.NextMark].Frame <= Action.Elapsed;
		bool bHit = Hits.IsValidIndex(Action.NextHit) && Hits[Action.NextHit].Frame <= Action.Elapsed;
		// PvP: o primeiro golpe espera a aproximação da autoridade (as marcas seguem; o quadro não anda até ela chegar)
		if (bHit && bRequireApproach && Action.NextHit == 0 && !Action.bApproachReceived && !Action.bApproachSpent)
		{
			bHit = false;
		}
		if (!bMark && !bHit)
		{
			return;
		}
		// No mesmo quadro, o aviso e o AP vêm antes do golpe (o crédito é "no primeiro golpe")
		if (bMark && (!bHit || Marks[Action.NextMark].Frame <= Hits[Action.NextHit].Frame))
		{
			const FJRPGActionMark Mark = Marks[Action.NextMark];
			++Action.NextMark;
			ApplyMark(Mark);
		}
		else
		{
			const int32 Index = Action.NextHit;
			++Action.NextHit;
			ResolveHit(Index);
		}
	}
}

void FJRPGBattleResolver::ApplyMark(const FJRPGActionMark& Mark)
{
	FJRPGCombatant& C = State.Combatants[State.Action.Slot];
	switch (Mark.Kind)
	{
	case EJRPGActionMarkKind::SetAP:
	{
		const int32 NewAP = FMath::Clamp(Mark.Value, 0, FMath::Max(C.MaxAP, 0));
		if (NewAP != C.AP)
		{
			C.AP = NewAP;
			Emit(EJRPGBattleEventType::APChanged, C.SlotIndex, C.SlotIndex, NewAP);
		}
		break;
	}
	case EJRPGActionMarkKind::ArtRecognized:
		Emit(EJRPGBattleEventType::ArtRecognized, C.SlotIndex, State.Action.TargetSlot, Mark.Value, -1, 0, Mark.ArtKey);
		break;
	case EJRPGActionMarkKind::ArtLearned:
		// O aprendizado fica no combatente (a resolução não escreve no roster); o EndBattle grava pelo UArtsSubsystem
		if (!C.KnownArts.Contains(Mark.ArtKey))
		{
			C.KnownArts.Add(Mark.ArtKey);
		}
		Emit(EJRPGBattleEventType::ArtLearned, C.SlotIndex, State.Action.TargetSlot, Mark.Value, -1, 0, Mark.ArtKey);
		break;
	}
}

void FJRPGBattleResolver::ResolveHit(int32 HitIndex)
{
	FJRPGActiveAction& Action = State.Action;
	const FJRPGActionHit Hit = Action.Timeline.Hits[HitIndex];
	FJRPGCombatant& Attacker = State.Combatants[Action.Slot];
	if (!Attacker.bAlive)
	{
		return;
	}

	// O original NUNCA muda de alvo no meio da ação (o alvo é fixado no começo dela, actor[+0x1DD]; o re-sorteio
	// FUN_801DB124 só roda ao despachar a ação, com o alvo já morto) e NUNCA corta a cadeia: soma o combo e escreve o total
	// no último golpe que conecta, então todos os golpes resolvem no mesmo alvo e o dano que passa do HP se perde. Aqui o
	// alvo só cai nesse último golpe (ver bAppliesTotal); este retorno é só para um alvo que caiu por outro caminho
	if (!IsLivingFoe(Attacker, Action.TargetSlot))
	{
		return;
	}
	FJRPGCombatant& Defender = State.Combatants[Action.TargetSlot];
	const bool bHeroAttacker = Attacker.Type != EJRPGCombatantType::Enemy;
	const int32 Speed = Action.Timeline.Steps.IsValidIndex(Hit.StepIndex) ? Action.Timeline.Steps[Hit.StepIndex].GameSpeed : UBattleFormulas::NormalGameSpeed;
	int32 Flags = 0;
	if (!UBattleFormulas::TargetsUpper(Hit.PowerByte))
	{
		Flags |= FJRPGBattleEventFlag::LowHit;
	}
	if (Hit.Source == EJRPGHitSource::Art)
	{
		Flags |= FJRPGBattleEventFlag::ArtHit;
	}

	// O miss pela altura (só atacante da party): não sorteia nada, não mexe em nada
	if (UBattleFormulas::LimbMisses(bHeroAttacker, Hit.PowerByte, Defender.TargetClass))
	{
		Emit(EJRPGBattleEventType::Miss, Attacker.SlotIndex, Defender.SlotIndex, 0, HitIndex, Flags, NAME_None, 0, Speed);
		return;
	}

	// O juggle: o golpe que cai enquanto a reação do alvo ainda corre soma um; fora da janela volta a 1
	const bool bReacting = State.Frame < Defender.ReactionUntilFrame;
	const bool bInBlockWindow = bReacting && Defender.bReactionIsBlock;
	if (bInBlockWindow)
	{
		Defender.JuggleCounter = 1;
	}
	else if (bReacting)
	{
		++Defender.JuggleCounter;
	}
	else
	{
		Defender.JuggleCounter = 1;
	}

	// O bloqueio: só o alvo que tem o clip de block e ainda está de pé (o segundo portão do original é o total
	// acumulado do combo < HP; com o HP já abatido a cada golpe isso é HP > 0). Os dois portões vêm antes dos sorteios
	if (Defender.bHasBlock && Defender.HP > 0)
	{
		FJRPGBlockInput BlockInput;
		BlockInput.AttackerSPD = Attacker.SPD;
		BlockInput.AttackerATK = Attacker.ATK;
		BlockInput.DefenderSPD = Defender.SPD;
		BlockInput.DefenderATK = Defender.ATK;
		BlockInput.PowerByte = Hit.PowerByte;
		BlockInput.bDefenderSpirit = Defender.bGuarding;
		BlockInput.bAttackerArt = Hit.Source == EJRPGHitSource::Art;
		// Os termos da aproximação: só o primeiro golpe que passa pelo núcleo os gasta (0x801EC888 no block, 0x801EE3C4 no acerto)
		if (!State.Action.bApproachSpent)
		{
			BlockInput.AttackRamp = State.Action.AttackRamp;
			BlockInput.GuardRamp = State.Action.GuardRamp;
		}
		bool bBlocked = UBattleFormulas::RollBlock(BlockInput, [this]() { return Rand15(); });
		// Dentro da janela da reação o sorteio é sobrescrito: quem está no block segue bloqueando, quem está num
		// flinch não bloqueia (é o que deixa a combo acertar)
		if (bInBlockWindow)
		{
			bBlocked = true;
		}
		else if (bReacting)
		{
			bBlocked = false;
		}
		if (bBlocked)
		{
			State.Action.bApproachSpent = true;
			Defender.JuggleCounter = 1;
			Defender.bReactionIsBlock = true;
			Defender.ReactionUntilFrame = State.Frame + ReactionFrames(Defender.BlockClip, Speed);
			// A ação seguinte espera o block acabar, na velocidade em que ele foi tomado
			Defender.ReactionHoldUntilFrame = State.Frame + Scaled(ReactionLengthOf(Defaults, Defender, EReactionLength::Block), Speed);
			Emit(EJRPGBattleEventType::Block, Attacker.SlotIndex, Defender.SlotIndex, 0, HitIndex, Flags, NAME_None, 0, Speed);
			return;
		}
	}

	// O golpe acerta: Base Offense = ATK base + a dobra do golpe; a defesa é a metade que o byte de poder escolhe
	FJRPGMeleeInput Melee;
	Melee.BaseOffense = Attacker.ATK + (bHeroAttacker ? UBattleFormulas::EquipmentFold(Hit.Source, Attacker.PieceATK) : 0);
	Melee.AttackerHP = Attacker.HP;
	Melee.DefenderDefense = UBattleFormulas::TargetsUpper(Hit.PowerByte) ? Defender.UDF : Defender.LDF;
	Melee.PowerByte = Hit.PowerByte;
	Melee.Juggle = Defender.JuggleCounter;
	Melee.bArt = Hit.Source == EJRPGHitSource::Art;
	Melee.ElementPercent = UBattleFormulas::ElementPercent(Attacker.Element, Defender.Element);
	Melee.bDefenderGuarding = Defender.bGuarding;
	if (!State.Action.bApproachSpent)
	{
		Melee.AttackRamp = State.Action.AttackRamp;
		Melee.GuardRamp = State.Action.GuardRamp;
		State.Action.bApproachSpent = true;
	}
	const int32 Damage = UBattleFormulas::ResolveMelee(Melee, [this]() { return Rand15(); }).Damage;

	// O HP nunca passa de 0 e o excesso se perde (o original soma o combo e escreve max(HP - total, 0)); o total do combo
	// guarda o dano de TODOS os golpes, o que passou do HP também (é o que o TOTAL do original mostra)
	Defender.HP = FMath::Max(Defender.HP - Damage, 0);
	if (Action.ComboDamage.IsValidIndex(Defender.SlotIndex))
	{
		Action.ComboDamage[Defender.SlotIndex] += Damage;
	}
	if (Action.ComboHits.IsValidIndex(Defender.SlotIndex))
	{
		++Action.ComboHits[Defender.SlotIndex];
	}

	// A reação: o golpe que mata ou, em quem tem o clip de levantar, que passa de ¼ do HP máximo num só combo (ou
	// deixa o HP abaixo de ¼) derruba (sem janela de juggle); os outros dão o flinch, alto ou baixo, e abrem a janela.
	// O original só decide o knockdown no golpe que ESCREVE o total do combo (o último que conecta no alvo; RE:
	// FUN_801EC3E4 0x801EE380..0x801EE3B4, "kill_check" de melee_reaction_entry): os golpes de antes só dão flinch, mesmo
	// com o total já passando de ¼. Aqui o HP cai a cada golpe, mas o knockdown segue a regra do original
	const bool bDead = Defender.HP <= 0;
	const bool bComboTooBig = Action.ComboDamage.IsValidIndex(Defender.SlotIndex)
		&& Action.ComboDamage[Defender.SlotIndex] > (Defender.MaxHP >> 2);
	const bool bAppliesTotal = IsLastConnectingHit(HitIndex, bHeroAttacker, Defender);
	// O alvo com o HP em 0 só cai no golpe que escreve o total: até lá fica de pé (flinch), apanhando o resto da cadeia
	const bool bFalls = bDead && bAppliesTotal;
	const bool bKnockdown = bFalls || (bAppliesTotal && Defender.bHasGetUp && (bComboTooBig || Defender.HP < (Defender.MaxHP >> 2)));
	Defender.bReactionIsBlock = false;
	if (bKnockdown)
	{
		Defender.ReactionUntilFrame = 0;
		Flags |= FJRPGBattleEventFlag::Knockdown;
		// Cai e levanta: a ação seguinte espera o knockdown E o levantar (o estado 0x20 só solta com o alvo de volta ao idle). Quem
		// morre troca isto pela queda no KillCombatant, logo abaixo
		Defender.ReactionHoldUntilFrame = State.Frame + Scaled(ReactionLengthOf(Defaults, Defender, EReactionLength::Knockdown)
			+ ReactionLengthOf(Defaults, Defender, EReactionLength::GetUp), Speed);
	}
	else
	{
		// Cada flinch cai no outro quando falta o clip dele
		const bool bUpper = UBattleFormulas::TargetsUpper(Hit.PowerByte);
		const FJRPGBattleReactionClip& Primary = bUpper ? Defender.FlinchHigh : Defender.FlinchLow;
		const FJRPGBattleReactionClip& Other = bUpper ? Defender.FlinchLow : Defender.FlinchHigh;
		const FJRPGBattleReactionClip& Flinch = Primary.Beat > 0 ? Primary : Other;
		Defender.ReactionUntilFrame = State.Frame + ReactionFrames(Flinch, Speed);
		// Um flinch por cima de outro recomeça o clip: a espera conta do último golpe
		Defender.ReactionHoldUntilFrame = State.Frame + Scaled(ReactionLengthOf(Defaults, Defender,
			bUpper ? EReactionLength::FlinchHigh : EReactionLength::FlinchLow), Speed);
	}

	Emit(EJRPGBattleEventType::Damage, Attacker.SlotIndex, Defender.SlotIndex, Damage, HitIndex, Flags, NAME_None, 0, Speed);

	// O AP de quem apanha (o "spirit gauge fill" do original, FUN_801EC3E4 0x801EDB80..0x801EDCC8; battle-formulas.md):
	// max(1, dano x 100 / HP máximo) a cada golpe que acerta, até o máximo. TODO: o AP Boost (+1/4) e o passivo 0x100 (+1/10)
	// quando os passivos de acessório existirem
	if (Defender.Side == EJRPGBattleSide::Party && Damage > 0 && Defender.MaxHP > 0)
	{
		const int32 Gain = FMath::Max(1, static_cast<int32>(static_cast<int64>(Damage) * 100 / Defender.MaxHP));
		const int32 NewAP = FMath::Clamp(Defender.AP + Gain, 0, FMath::Max(Defender.MaxAP, 0));
		if (NewAP != Defender.AP)
		{
			Defender.AP = NewAP;
			Emit(EJRPGBattleEventType::APChanged, Defender.SlotIndex, Defender.SlotIndex, NewAP);
		}
	}

	if (bFalls)
	{
		KillCombatant(Defender, Attacker.SlotIndex, Speed);
	}
}

void FJRPGBattleResolver::KillCombatant(FJRPGCombatant& Of, int32 KilledBy, int32 GameSpeed)
{
	Of.HP = 0;
	Of.bAlive = false;
	Of.InitiativeKey = 0;
	Of.ReactionUntilFrame = 0;
	// A queda que a ação seguinte espera: o herói cai e chega à entrada 8 (o knockdown + a entrada 7; a 8 repete para sempre),
	// o monstro cai e o corpo some no fade de derrota (o terceiro teste do estado 0x20). Conta do quadro da queda
	int32 Fade = 0;
	int32 Hold = Scaled(ReactionLengthOf(Defaults, Of, EReactionLength::Knockdown), GameSpeed);
	if (Of.Type == EJRPGCombatantType::Enemy)
	{
		Fade = ReactionLengthOf(Defaults, Of, EReactionLength::DeathFade);
		Hold += Fade;   // o fade anda por passo, não pela escala da Art
	}
	else
	{
		Hold += Scaled(ReactionLengthOf(Defaults, Of, EReactionLength::Dying), GameSpeed);
	}
	Of.ReactionHoldUntilFrame = State.Frame + Hold;
	Emit(EJRPGBattleEventType::Death, Of.SlotIndex, KilledBy, Fade, -1, 0, NAME_None, 0, GameSpeed);
	// Nada é cortado aqui, nem quando é o último inimigo: o original só testa a vitória no fim da ação (o estado 0x5A,
	// battle-action.md), então a cadeia inteira bate. A luta acaba quando a ação acaba (EndAction -> CheckFinish)
}

bool FJRPGBattleResolver::IsLastConnectingHit(int32 HitIndex, bool bHeroAttacker, const FJRPGCombatant& Defender) const
{
	// O original soma o combo e escreve o total no último golpe que ainda conecta com o alvo: os golpes que sobram só
	// contam se a classe de altura do alvo deixa passar o byte deles (a mesma regra do miss, a olhada adiante do modo
	// de aplicar, RE: battle-action-queue.md, s2)
	const TArray<FJRPGActionHit>& Hits = State.Action.Timeline.Hits;
	for (int32 i = HitIndex + 1; i < Hits.Num(); ++i)
	{
		if (!UBattleFormulas::LimbMisses(bHeroAttacker, Hits[i].PowerByte, Defender.TargetClass))
		{
			return false;
		}
	}
	return true;
}

bool FJRPGBattleResolver::RunConditions(FJRPGCombatant& Of, bool bTurnStart)
{
	bool bCanAct = true;
	for (const FJRPGBattleCondition& Condition : TArray<FJRPGBattleCondition>(Of.Conditions))
	{
		if (Condition.bActsAtTurnStart != bTurnStart)
		{
			continue;
		}
		if (Condition.DamagePerTurn > 0 && Of.bAlive)
		{
			Of.HP = FMath::Max(Of.HP - Condition.DamagePerTurn, 0);
			Emit(EJRPGBattleEventType::Damage, Of.SlotIndex, Of.SlotIndex, Condition.DamagePerTurn);
			if (Of.HP <= 0)
			{
				KillCombatant(Of, Of.SlotIndex);
			}
		}
		if (Condition.bSkipsTurn && bTurnStart)
		{
			bCanAct = false;
		}
	}
	return bCanAct && Of.bAlive;
}

// =================================================================================================
// Ajudas
// =================================================================================================

bool FJRPGBattleResolver::IsLivingFoe(const FJRPGCombatant& Of, int32 Slot) const
{
	return State.Combatants.IsValidIndex(Slot) && State.Combatants[Slot].bAlive && State.Combatants[Slot].Team != Of.Team;
}

int32 FJRPGBattleResolver::LowestLivingFoe(const FJRPGCombatant& Of) const
{
	for (const FJRPGCombatant& C : State.Combatants)
	{
		if (C.bAlive && C.Team != Of.Team)
		{
			return C.SlotIndex;
		}
	}
	return INDEX_NONE;
}

bool FJRPGBattleResolver::HasCondition(const FJRPGCombatant& Of, FName Condition) const
{
	return Of.Conditions.ContainsByPredicate([Condition](const FJRPGBattleCondition& C) { return C.Name == Condition; });
}

int32 FJRPGBattleResolver::Rand15()
{
	return RandOverride ? RandOverride() : UBattleFormulas::Rand15(State.Rng);
}

void FJRPGBattleResolver::Emit(EJRPGBattleEventType Type, int32 Slot, int32 Target, int32 Value, int32 HitIndex, int32 Flags, FName ArtKey, int32 Count,
	int32 GameSpeed)
{
	FJRPGBattleEvent& Event = Events.AddDefaulted_GetRef();
	Event.Type = Type;
	Event.SlotIndex = Slot;
	Event.TargetSlot = Target;
	Event.Value = Value;
	Event.ActionIndex = State.Action.bActive ? State.Action.Timeline.ActionIndex : -1;
	Event.HitIndex = HitIndex;
	Event.Frame = State.Frame;
	Event.Flags = Flags;
	Event.ArtKey = ArtKey;
	Event.Count = Count;
	Event.GameSpeed = GameSpeed;
}

void FJRPGBattleResolver::LogFallback(const FString& Key, const FString& Message)
{
	if (bLogFallbacks && !LoggedFallbacks.Contains(Key))
	{
		LoggedFallbacks.Add(Key);
		UE_LOG(LogTemp, Log, TEXT("BattleResolver: %s"), *Message);
	}
}

FString FJRPGBattleResolver::SlotName(int32 Slot) const
{
	return State.Combatants.IsValidIndex(Slot)
		? FString::Printf(TEXT("%s#%d"), *State.Combatants[Slot].Key.ToString(), Slot)
		: FString::Printf(TEXT("#%d"), Slot);
}

FString FJRPGBattleResolver::DescribeEvent(const FJRPGBattleEvent& Event) const
{
	const FString When = FString::Printf(TEXT("[f%d a%d]"), Event.Frame, Event.ActionIndex);
	switch (Event.Type)
	{
	case EJRPGBattleEventType::RoundStart:
		return FString::Printf(TEXT("%s ===== round %d ====="), *When, Event.Value);
	case EJRPGBattleEventType::ActionStart:
		return FString::Printf(TEXT("%s %s acts on %s"), *When, *SlotName(Event.SlotIndex), *SlotName(Event.TargetSlot));
	case EJRPGBattleEventType::Damage:
		return FString::Printf(TEXT("%s %s hits %s for %d%s%s"), *When, *SlotName(Event.SlotIndex), *SlotName(Event.TargetSlot), Event.Value,
			(Event.Flags & FJRPGBattleEventFlag::ArtHit) ? TEXT(" (Art)") : TEXT(""),
			(Event.Flags & FJRPGBattleEventFlag::Knockdown) ? TEXT(" - knocked down") : TEXT(""));
	case EJRPGBattleEventType::Miss:
		return FString::Printf(TEXT("%s %s misses %s (wrong height)"), *When, *SlotName(Event.SlotIndex), *SlotName(Event.TargetSlot));
	case EJRPGBattleEventType::Block:
		return FString::Printf(TEXT("%s %s blocks %s"), *When, *SlotName(Event.TargetSlot), *SlotName(Event.SlotIndex));
	case EJRPGBattleEventType::Heal:
		return FString::Printf(TEXT("%s %s healed %d"), *When, *SlotName(Event.TargetSlot), Event.Value);
	case EJRPGBattleEventType::Death:
		return FString::Printf(TEXT("%s %s falls"), *When, *SlotName(Event.SlotIndex));
	case EJRPGBattleEventType::APChanged:
		return FString::Printf(TEXT("%s %s AP -> %d"), *When, *SlotName(Event.SlotIndex), Event.Value);
	case EJRPGBattleEventType::ArtRecognized:
		return FString::Printf(TEXT("%s %s performs %s"), *When, *SlotName(Event.SlotIndex), *Event.ArtKey.ToString());
	case EJRPGBattleEventType::ArtLearned:
		return FString::Printf(TEXT("%s %s learned %s!"), *When, *SlotName(Event.SlotIndex), *Event.ArtKey.ToString());
	case EJRPGBattleEventType::ComboTotal:
		return FString::Printf(TEXT("%s %s combo on %s: %d hits, %d total"), *When, *SlotName(Event.SlotIndex), *SlotName(Event.TargetSlot), Event.Count, Event.Value);
	case EJRPGBattleEventType::ActionEnd:
		return FString::Printf(TEXT("%s %s ends%s"), *When, *SlotName(Event.SlotIndex), Event.Value != 0 ? TEXT(" (cut short)") : TEXT(""));
	default:
		return FString::Printf(TEXT("%s %s"), *When, *EventTypeName(Event.Type));
	}
}
