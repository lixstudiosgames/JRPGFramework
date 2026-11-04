#include "Arts/JRPGArtsRules.h"
#include "Arts/JRPGArtSequence.h"
#include "Engine/DataTable.h"

namespace
{
	/** Uma linha do DT_Arts com a posição dela na tabela (desempate estável). */
	struct FArtsRowRef
	{
		FName Key;
		const FJRPGArtData* Row = nullptr;
		int32 TableIndex = 0;
	};

	/** Uma Art casada pelo tokenizer, em posições da ENTRADA (não da fila). */
	struct FArtsToken
	{
		FName Key;
		const FJRPGArtData* Row = nullptr;
		int32 First = 0;
		int32 Last = 0;

		/** Onde ela entra no turno: a Regular no último botão (o starter), a Hyper no primeiro. */
		int32 Anchor() const { return Row->Kind == EJRPGArtKind::Hyper ? First : Last; }
	};

	/** Uma Super / Miracle achada na entrada. */
	struct FArtsSpecial
	{
		FName Key;
		const FJRPGArtData* Row = nullptr;
		int32 TableIndex = 0;
		int32 A = 0;
		int32 B = 0;
		int32 Anchor = 0;
		TArray<int32> Inside;   // índices em Tokens
	};

	/** Uma ocorrência tirada do reconhecimento (uma Hyper sem AP naquele ponto). */
	struct FArtsExcluded
	{
		FName Key;
		int32 Last = 0;
	};

	bool IsTokenKind(EJRPGArtKind Kind) { return Kind == EJRPGArtKind::Regular || Kind == EJRPGArtKind::Hyper; }
	bool IsSpanKind(EJRPGArtKind Kind) { return Kind == EJRPGArtKind::Super || Kind == EJRPGArtKind::Miracle; }

	/** As linhas do herói, na ordem da tabela. */
	TArray<FArtsRowRef> HeroRows(const UDataTable* Arts, FName Character)
	{
		TArray<FArtsRowRef> Out;
		if (!Arts)
		{
			return Out;
		}
		int32 Index = 0;
		for (const TPair<FName, uint8*>& Pair : Arts->GetRowMap())
		{
			const FJRPGArtData* Row = reinterpret_cast<const FJRPGArtData*>(Pair.Value);
			if (Row && Row->Character == Character)
			{
				Out.Add({ Pair.Key, Row, Index });
			}
			++Index;
		}
		return Out;
	}

	/**
	 * As Arts que o tokenizer procura, na ordem da grade do original: as do disco por Action Constant
	 * (a Miracle e as três Hyper têm as menores, depois as Regular — as de mais botões primeiro, como o
	 * disco as numera); as novas (ActionConstant 0) depois, as mais longas primeiro, e então a tabela.
	 */
	TArray<FArtsRowRef> TokenArts(const UDataTable* Arts, FName Character)
	{
		TArray<FArtsRowRef> Out = HeroRows(Arts, Character).FilterByPredicate([](const FArtsRowRef& R)
		{
			return IsTokenKind(R.Row->Kind) && !R.Row->Command.IsEmpty();
		});
		Out.StableSort([](const FArtsRowRef& X, const FArtsRowRef& Y)
		{
			const bool bNewX = X.Row->ActionConstant <= 0;
			const bool bNewY = Y.Row->ActionConstant <= 0;
			if (bNewX != bNewY)
			{
				return !bNewX;
			}
			if (!bNewX && X.Row->ActionConstant != Y.Row->ActionConstant)
			{
				return X.Row->ActionConstant < Y.Row->ActionConstant;
			}
			if (X.Row->Command.Num() != Y.Row->Command.Num())
			{
				return X.Row->Command.Num() > Y.Row->Command.Num();
			}
			return X.TableIndex < Y.TableIndex;
		});
		return Out;
	}

	/**
	 * O tokenizer do original (FUN_801EED1C, 0x801EF2EC..0x801EF858; RE crates/art/src/tokenize.rs),
	 * sobre uma fila que guarda de que botão da entrada cada entrada veio:
	 *
	 *   para start = fim .. 0:  para cada Art, na ordem da grade:
	 *     busca ingênua a partir de start; num casamento completo o último botão vira o starter, a
	 *     constante da Art entra logo depois dele, e a busca recomeça em start + 1
	 *
	 * Os botões antes do último continuam na fila — por isso uma Art pode começar no meio de outra.
	 * A Hyper marca a faixa inteira (o braço especial do original escreve o 0x1A no começo dela e os
	 * clips por cima): nenhuma outra Art casa dentro dela depois. CanMatch decide no casamento
	 * (conhecida, aprendida no uso, Ra-Seru); uma ocorrência recusada segue a busca como um erro.
	 */
	void Tokenize(const TArray<FArtsRowRef>& Arts, const TArray<EJRPGArtButton>& Buttons, const TArray<FArtsExcluded>& Excluded,
		TFunctionRef<bool(const FArtsRowRef&)> CanMatch, TArray<FArtsToken>& Out)
	{
		enum class EQ : uint8 { Button, Starter, Art, Covered };
		struct FQ
		{
			EQ Type = EQ::Button;
			EJRPGArtButton Button = EJRPGArtButton::Arms;
			int32 Origin = INDEX_NONE;
		};

		TArray<FQ> Q;
		Q.Reserve(Buttons.Num() * 2);
		for (int32 i = 0; i < Buttons.Num(); ++i)
		{
			Q.Add({ EQ::Button, Buttons[i], i });
		}

		// As inserções caem sempre em Pos + 1 >= Start, então as posições abaixo de Start não mudam
		for (int32 Start = Buttons.Num() - 1; Start >= 0; --Start)
		{
			for (const FArtsRowRef& Art : Arts)
			{
				const TArray<EJRPGArtButton>& Cmd = Art.Row->Command;
				const int32 Len = Cmd.Num();
				int32 Pos = Start;
				int32 Matched = 0;
				while (Pos < Q.Num())
				{
					if (Q[Pos].Type == EQ::Button && Q[Pos].Button == Cmd[Matched])
					{
						if (++Matched == Len)
						{
							const int32 FirstQ = Pos - Len + 1;
							const int32 First = Q[FirstQ].Origin;
							const int32 Last = Q[Pos].Origin;
							const bool bExcluded = Excluded.ContainsByPredicate([&](const FArtsExcluded& E)
							{
								return E.Key == Art.Key && E.Last == Last;
							});
							if (bExcluded || !CanMatch(Art))
							{
								// Esta ocorrência não vale: a busca ingênua segue do começo seguinte
								Pos = FirstQ + 1;
								Matched = 0;
								continue;
							}
							Out.Add({ Art.Key, Art.Row, First, Last });
							if (Art.Row->Kind == EJRPGArtKind::Hyper)
							{
								for (int32 j = FirstQ; j < Pos; ++j)
								{
									Q[j].Type = EQ::Covered;
								}
							}
							Q[Pos].Type = EQ::Starter;
							Q.Insert(FQ{ EQ::Art, EJRPGArtButton::Arms, INDEX_NONE }, Pos + 1);
							Matched = 0;
							Pos = Start;
						}
					}
					else
					{
						Pos -= Matched;
						Matched = 0;
					}
					++Pos;
				}
			}
		}
		// Cada posição recebe no máximo um starter: o fim é único
		Out.Sort([](const FArtsToken& X, const FArtsToken& Y) { return X.Last < Y.Last; });
	}

	bool Contains(const TArray<EJRPGArtButton>& Buttons, int32 At, const TArray<EJRPGArtButton>& Cmd)
	{
		if (Cmd.IsEmpty() || At < 0 || At + Cmd.Num() > Buttons.Num())
		{
			return false;
		}
		for (int32 i = 0; i < Cmd.Num(); ++i)
		{
			if (Buttons[At + i] != Cmd[i])
			{
				return false;
			}
		}
		return true;
	}

	TArray<FName> OwnArts(const TArray<FArtsRowRef>& Catalog, const FJRPGArtData& Special)
	{
		TArray<FArtsToken> Tokens;
		Tokenize(Catalog, Special.Command, {}, [](const FArtsRowRef&) { return true; }, Tokens);
		TArray<FName> Out;
		for (const FArtsToken& T : Tokens)
		{
			Out.Add(T.Key);
		}
		return Out;
	}

	/** O especial está destravado para este turno? (A Super ChainKnown: toda Art do Command conhecida ANTES.) */
	bool IsSpecialUnlocked(const FArtsRowRef& Special, const FJRPGArtsContext& Context, const TArray<FArtsRowRef>& Catalog,
		TMap<FName, TArray<FName>>& OwnCache)
	{
		switch (UJRPGArtsRules::EffectiveUnlock(*Special.Row))
		{
		case EJRPGArtUnlock::OnUse:
			return true;
		case EJRPGArtUnlock::Taught:
			return Context.KnownArts.Contains(Special.Key);
		case EJRPGArtUnlock::RaSeru:
			return Context.bHasRaSeru;
		case EJRPGArtUnlock::ChainKnown:
		{
			TArray<FName>* Own = OwnCache.Find(Special.Key);
			if (!Own)
			{
				Own = &OwnCache.Add(Special.Key, OwnArts(Catalog, *Special.Row));
			}
			for (const FName& Key : *Own)
			{
				if (!Context.KnownArts.Contains(Key))
				{
					return false;
				}
			}
			return true;
		}
		default:
			return false;
		}
	}

	/** Uma Art solta (Regular / Hyper) pode casar? Sorteia o aprender-no-uso uma vez por Art. */
	bool CanTokenMatch(const FArtsRowRef& Art, const FJRPGArtsContext& Context, FRandomStream& Rng, TMap<FName, bool>& Rolls)
	{
		const bool bKnown = Context.KnownArts.Contains(Art.Key);
		const EJRPGArtUnlock Unlock = UJRPGArtsRules::EffectiveUnlock(*Art.Row);
		const bool bByKindHyper = Art.Row->Unlock == EJRPGArtUnlock::ByKind && Art.Row->Kind == EJRPGArtKind::Hyper;
		switch (Unlock)
		{
		case EJRPGArtUnlock::OnUse:
		{
			if (bKnown)
			{
				return true;
			}
			if (const bool* Rolled = Rolls.Find(Art.Key))
			{
				return *Rolled;
			}
			bool bLearn = Context.LearnOnUseChance >= 100;
			if (!bLearn && Context.LearnOnUseChance > 0)
			{
				bLearn = static_cast<int32>(Rng.GetUnsignedInt() % 100u) < Context.LearnOnUseChance;
			}
			Rolls.Add(Art.Key, bLearn);
			return bLearn;
		}
		case EJRPGArtUnlock::Taught:
			// A Hyper do original sai só com o Ra-Seru (o marcador ctx +0x25F também barra as Hyper)
			return bKnown && (!bByKindHyper || Context.bHasRaSeru);
		case EJRPGArtUnlock::RaSeru:
			return Context.bHasRaSeru;
		case EJRPGArtUnlock::ChainKnown:
			// Sem sentido numa Art solta (o validador avisa): vale como Taught
			return bKnown;
		default:
			return false;
		}
	}

	bool IsInside(const FArtsToken& T, const FArtsSpecial& S) { return T.First >= S.A && T.Last <= S.B; }
}

// --- Botões ----------------------------------------------------------------------------------

EJRPGArtButton UJRPGArtsRules::ButtonForPad(bool bLeftHanded, EJRPGPadButton Pad)
{
	switch (Pad)
	{
	case EJRPGPadButton::Left:  return bLeftHanded ? EJRPGArtButton::RaSeru : EJRPGArtButton::Arms;
	case EJRPGPadButton::Right: return bLeftHanded ? EJRPGArtButton::Arms : EJRPGArtButton::RaSeru;
	case EJRPGPadButton::Up:    return EJRPGArtButton::High;
	default:                    return EJRPGArtButton::Low;
	}
}

EJRPGArtUnlock UJRPGArtsRules::EffectiveUnlock(const FJRPGArtData& Art)
{
	if (Art.Unlock != EJRPGArtUnlock::ByKind)
	{
		return Art.Unlock;
	}
	switch (Art.Kind)
	{
	case EJRPGArtKind::Regular: return EJRPGArtUnlock::OnUse;
	case EJRPGArtKind::Hyper:   return EJRPGArtUnlock::Taught;
	case EJRPGArtKind::Super:   return EJRPGArtUnlock::ChainKnown;
	case EJRPGArtKind::Miracle: return EJRPGArtUnlock::RaSeru;
	default:                    return EJRPGArtUnlock::ByKind;   // Part: nunca se digita
	}
}

FString UJRPGArtsRules::ButtonName(EJRPGArtButton Button)
{
	switch (Button)
	{
	case EJRPGArtButton::Arms:   return TEXT("Arms");
	case EJRPGArtButton::RaSeru: return TEXT("Ra-Seru");
	case EJRPGArtButton::High:   return TEXT("High");
	default:                     return TEXT("Low");
	}
}

bool UJRPGArtsRules::ParseButtons(const FString& Text, bool bLeftHanded, TArray<EJRPGArtButton>& OutButtons)
{
	OutButtons.Reset();
	// As setas viram palavras soltas; vírgula, ponto e vírgula e espaço separam
	FString Clean = Text;
	Clean.ReplaceInline(TEXT("←"), TEXT(" left "));
	Clean.ReplaceInline(TEXT("→"), TEXT(" right "));
	Clean.ReplaceInline(TEXT("↑"), TEXT(" up "));
	Clean.ReplaceInline(TEXT("↓"), TEXT(" down "));
	Clean.ReplaceInline(TEXT(","), TEXT(" "));
	Clean.ReplaceInline(TEXT(";"), TEXT(" "));
	TArray<FString> Words;
	Clean.ParseIntoArrayWS(Words);
	for (FString Word : Words)
	{
		Word = Word.ToLower().Replace(TEXT("-"), TEXT(""));
		if (Word == TEXT("arms") || Word == TEXT("a"))
		{
			OutButtons.Add(EJRPGArtButton::Arms);
		}
		else if (Word == TEXT("raseru") || Word == TEXT("r") || Word == TEXT("seru"))
		{
			OutButtons.Add(EJRPGArtButton::RaSeru);
		}
		else if (Word == TEXT("high") || Word == TEXT("h"))
		{
			OutButtons.Add(EJRPGArtButton::High);
		}
		else if (Word == TEXT("low") || Word == TEXT("l"))
		{
			OutButtons.Add(EJRPGArtButton::Low);
		}
		else if (Word == TEXT("left"))
		{
			OutButtons.Add(ButtonForPad(bLeftHanded, EJRPGPadButton::Left));
		}
		else if (Word == TEXT("right"))
		{
			OutButtons.Add(ButtonForPad(bLeftHanded, EJRPGPadButton::Right));
		}
		else if (Word == TEXT("up"))
		{
			OutButtons.Add(EJRPGArtButton::High);
		}
		else if (Word == TEXT("down"))
		{
			OutButtons.Add(EJRPGArtButton::Low);
		}
		else
		{
			OutButtons.Reset();
			return false;
		}
	}
	return true;
}

TArray<uint8> UJRPGArtsRules::EncodeArtBytes(const TArray<EJRPGArtButton>& Buttons)
{
	TArray<uint8> Out;
	Out.Reserve(Buttons.Num());
	for (const EJRPGArtButton Button : Buttons)
	{
		Out.Add(static_cast<uint8>(Button));
	}
	return Out;
}

bool UJRPGArtsRules::DecodeArtBytes(const TArray<uint8>& Bytes, TArray<EJRPGArtButton>& OutButtons)
{
	OutButtons.Reset();
	// Regra 22: o payload não é confiável — byte fora dos quatro botões ou cadeia longa demais recusa tudo
	if (Bytes.Num() > MaxChainButtons)
	{
		return false;
	}
	for (const uint8 Byte : Bytes)
	{
		if (Byte > static_cast<uint8>(EJRPGArtButton::Low))
		{
			OutButtons.Reset();
			return false;
		}
		OutButtons.Add(static_cast<EJRPGArtButton>(Byte));
	}
	return true;
}

// --- Janela de entrada -----------------------------------------------------------------------

FJRPGArtsInputWindow UJRPGArtsRules::OpenInputWindow(FName Character, int32 Gauge, int32 ButtonCost)
{
	FJRPGArtsInputWindow Window;
	Window.Character = Character;
	Window.Gauge = FMath::Max(Gauge, 0);
	Window.GaugeLeft = Window.Gauge;
	Window.ButtonCost.Init(FMath::Max(ButtonCost, 0), 4);
	return Window;
}

namespace
{
	int32 CostOf(const FJRPGArtsInputWindow& Window, EJRPGArtButton Button)
	{
		const int32 Index = static_cast<int32>(Button);
		return Window.ButtonCost.IsValidIndex(Index) ? Window.ButtonCost[Index] : UJRPGArtsRules::DefaultButtonCost;
	}
}

bool UJRPGArtsRules::PushInputButton(FJRPGArtsInputWindow& Window, EJRPGArtButton Button)
{
	if (Window.Buttons.Num() >= MaxChainButtons)
	{
		return false;
	}
	if (Window.Gauge > 0)
	{
		const int32 Cost = CostOf(Window, Button);
		if (Window.GaugeLeft < Cost)
		{
			return false;
		}
		Window.GaugeLeft -= Cost;
	}
	Window.Buttons.Add(Button);
	return true;
}

bool UJRPGArtsRules::PopInputButton(FJRPGArtsInputWindow& Window)
{
	if (Window.Buttons.IsEmpty())
	{
		return false;
	}
	const EJRPGArtButton Button = Window.Buttons.Pop();
	if (Window.Gauge > 0)
	{
		Window.GaugeLeft = FMath::Min(Window.GaugeLeft + CostOf(Window, Button), Window.Gauge);
	}
	return true;
}

bool UJRPGArtsRules::CanPushAnyButton(const FJRPGArtsInputWindow& Window)
{
	if (Window.Buttons.Num() >= MaxChainButtons)
	{
		return false;
	}
	if (Window.Gauge <= 0)
	{
		return true;
	}
	for (int32 b = 0; b < 4; ++b)
	{
		if (Window.GaugeLeft >= CostOf(Window, static_cast<EJRPGArtButton>(b)))
		{
			return true;
		}
	}
	return false;
}

// --- Reconhecimento --------------------------------------------------------------------------

TArray<FJRPGRecognizedArt> UJRPGArtsRules::TokenizeAll(const UDataTable* Arts, FName Character, const TArray<EJRPGArtButton>& Buttons)
{
	TArray<FArtsToken> Tokens;
	Tokenize(TokenArts(Arts, Character), Buttons, {}, [](const FArtsRowRef&) { return true; }, Tokens);
	TArray<FJRPGRecognizedArt> Out;
	for (const FArtsToken& T : Tokens)
	{
		FJRPGRecognizedArt& R = Out.AddDefaulted_GetRef();
		R.ArtKey = T.Key;
		R.Kind = T.Row->Kind;
		R.FirstButton = T.First;
		R.LastButton = T.Last;
	}
	return Out;
}

TArray<FName> UJRPGArtsRules::GetSpecialOwnArts(const UDataTable* Arts, FName SpecialKey)
{
	const FJRPGArtData* Row = Arts ? Arts->FindRow<FJRPGArtData>(SpecialKey, TEXT("ArtsRules"), false) : nullptr;
	return Row ? OwnArts(TokenArts(Arts, Row->Character), *Row) : TArray<FName>();
}

FJRPGArtChain UJRPGArtsRules::BuildChain(const UDataTable* Arts, const FJRPGArtsContext& Context,
	const TArray<EJRPGArtButton>& InButtons, FRandomStream& Rng)
{
	FJRPGArtChain Chain;
	Chain.Character = Context.Character;
	const int32 MaxAP = FMath::Max(Context.MaxAP, 0);
	Chain.APStart = FMath::Clamp(Context.AP, 0, MaxAP);
	Chain.APEnd = Chain.APStart;

	TArray<EJRPGArtButton> Buttons = InButtons;
	if (Buttons.Num() > MaxChainButtons)
	{
		Buttons.SetNum(MaxChainButtons);
	}
	Chain.Buttons = Buttons;
	const int32 N = Buttons.Num();
	if (!Arts || N == 0)
	{
		for (int32 p = 0; p < N; ++p)
		{
			FJRPGArtChainEntry& Swing = Chain.Entries.AddDefaulted_GetRef();
			Swing.Type = EJRPGArtChainStepType::Swing;
			Swing.Button = Buttons[p];
			Swing.FirstButton = Swing.LastButton = p;
			Swing.APBefore = Swing.APAfter = Chain.APStart;
		}
		return Chain;
	}

	const TArray<FArtsRowRef> Catalog = TokenArts(Arts, Context.Character);
	TArray<FArtsRowRef> SpanArts = HeroRows(Arts, Context.Character).FilterByPredicate([](const FArtsRowRef& R)
	{
		return IsSpanKind(R.Row->Kind) && !R.Row->Command.IsEmpty();
	});

	TMap<FName, bool> Rolls;                       // aprender-no-uso: um sorteio por Art, reaproveitado
	TMap<FName, TArray<FName>> OwnCache;
	TArray<FArtsExcluded> Excluded;                    // Hyper sem AP naquele ponto: os botões dela voltam
	TArray<FJRPGArtDowngrade> HyperDowngrades;     // ...e a nota fica, mesmo depois de refazer

	// Tirar uma Hyper só muda o que termina no começo dela ou depois, então refazer chega nas mesmas
	// decisões até ali; cada volta tira uma ocorrência, e há no máximo uma por botão
	for (int32 Pass = 0; Pass <= N; ++Pass)
	{
		TArray<FArtsToken> Tokens;
		Tokenize(Catalog, Buttons, Excluded, [&](const FArtsRowRef& Art) { return CanTokenMatch(Art, Context, Rng, Rolls); }, Tokens);

		// --- Super / Miracle: casam pelo Command; as Arts dentro da faixa são delas -------------------
		TArray<FJRPGArtDowngrade> Locked;
		TArray<FArtsSpecial> Candidates;
		for (const FArtsRowRef& Span : SpanArts)
		{
			const int32 Len = Span.Row->Command.Num();
			for (int32 A = 0; A + Len <= N; ++A)
			{
				if (!Contains(Buttons, A, Span.Row->Command))
				{
					continue;
				}
				if (!IsSpecialUnlocked(Span, Context, Catalog, OwnCache))
				{
					FJRPGArtDowngrade& Note = Locked.AddDefaulted_GetRef();
					Note.ArtKey = Span.Key;
					Note.Reason = EJRPGArtDowngradeReason::Locked;
					Note.FirstButton = A;
					Note.LastButton = A + Len - 1;
					Note.APCost = Span.Row->APCost;
					continue;
				}
				FArtsSpecial& S = Candidates.AddDefaulted_GetRef();
				S.Key = Span.Key;
				S.Row = Span.Row;
				S.TableIndex = Span.TableIndex;
				S.A = A;
				S.B = A + Len - 1;
			}
		}
		// Sobrepostos: o que começa antes, depois o mais longo, depois a ordem da tabela
		Candidates.Sort([](const FArtsSpecial& X, const FArtsSpecial& Y)
		{
			if (X.A != Y.A)
			{
				return X.A < Y.A;
			}
			if (X.B - X.A != Y.B - Y.A)
			{
				return X.B - X.A > Y.B - Y.A;
			}
			return X.TableIndex < Y.TableIndex;
		});
		// Os sobrepostos ficam todos: quem decide é a caminhada (só um especial que DISPAROU tira os que
		// se sobrepõem a ele; um que caiu por AP vira as Arts dele e o próximo ainda é checado)
		TArray<FArtsSpecial> Specials;
		for (FArtsSpecial& C : Candidates)
		{
			for (int32 t = 0; t < Tokens.Num(); ++t)
			{
				if (IsInside(Tokens[t], C))
				{
					C.Inside.Add(t);
				}
			}
			// A Super entra onde a primeira Art dela entra (os botões antes são os golpes da Art, como no
			// original: o "find" começa no starter dela); a Miracle troca a faixa inteira
			C.Anchor = C.A;
			if (C.Row->Kind == EJRPGArtKind::Super && !C.Inside.IsEmpty())
			{
				C.Anchor = MAX_int32;
				for (const int32 t : C.Inside)
				{
					C.Anchor = FMath::Min(C.Anchor, Tokens[t].Anchor());
				}
			}
			Specials.Add(MoveTemp(C));
		}

		// --- O turno, da esquerda para a direita, com o AP correndo --------------------------------
		TArray<FJRPGArtChainEntry> Entries;
		TArray<FJRPGArtDowngrade> Downgrades;
		TArray<FName> NewlyLearned;
		TArray<bool> Suppress;
		Suppress.Init(false, N);
		TArray<int32> FiredBy;                    // por token: o especial que disparou por cima (INDEX_NONE = solto)
		FiredBy.Init(INDEX_NONE, Tokens.Num());
		TArray<int32> TokenAt;
		TokenAt.Init(INDEX_NONE, N);
		for (int32 t = 0; t < Tokens.Num(); ++t)
		{
			TokenAt[Tokens[t].Anchor()] = t;
		}
		// Por posição, os especiais que entram ali, na ordem dos candidatos (começa antes, mais longo, tabela)
		TArray<TArray<int32>> SpecialsAt;
		SpecialsAt.SetNum(N);
		for (int32 s = 0; s < Specials.Num(); ++s)
		{
			SpecialsAt[Specials[s].Anchor].Add(s);
		}
		TArray<int32> Fired;                      // os especiais que dispararam


		int32 Running = Chain.APStart;
		bool bRedo = false;

		auto MarkNew = [&](FName Key, FJRPGArtChainEntry* Entry)
		{
			if (!Context.KnownArts.Contains(Key) && !NewlyLearned.Contains(Key))
			{
				NewlyLearned.Add(Key);
				if (Entry)
				{
					Entry->bNewlyLearned = true;
				}
			}
		};

		for (int32 p = 0; p < N && !bRedo; ++p)
		{
			bool bEmitted = false;
			const int32 Ti = TokenAt[p];
			const TArray<int32>& Here = SpecialsAt[p];

			auto DoSpecial = [&](int32 s)
			{
				const FArtsSpecial& S = Specials[s];
				// Os botões já são de um especial que disparou
				const bool bTaken = Fired.ContainsByPredicate([&](int32 f) { return !(S.B < Specials[f].A || S.A > Specials[f].B); });
				if (bTaken)
				{
					return;
				}
				const int32 Cost = FMath::Max(S.Row->APCost, 0);
				if (Running < Cost)
				{
					FJRPGArtDowngrade& Note = Downgrades.AddDefaulted_GetRef();
					Note.ArtKey = S.Key;
					Note.Reason = EJRPGArtDowngradeReason::NotEnoughAP;
					Note.FirstButton = S.A;
					Note.LastButton = S.B;
					Note.APAtPoint = Running;
					Note.APCost = Cost;
					return;
				}
				FJRPGArtChainEntry& E = Entries.AddDefaulted_GetRef();
				E.Type = EJRPGArtChainStepType::Special;
				E.ArtKey = S.Key;
				E.FirstButton = S.A;
				E.LastButton = S.B;
				E.APBefore = Running;
				E.APAfter = Running - Cost;
				for (const int32 t : S.Inside)
				{
					E.InsideArts.Add(Tokens[t].Key);
					FiredBy[t] = s;
				}
				Running -= Cost;
				bEmitted = true;
				Fired.Add(s);
				// A Super aprendida no uso (Unlock = OnUse) entra na lista como uma Regular
				if (UJRPGArtsRules::EffectiveUnlock(*S.Row) == EJRPGArtUnlock::OnUse)
				{
					MarkNew(S.Key, &E);
				}
				const int32 From = S.Row->Kind == EJRPGArtKind::Super ? S.Anchor + 1 : S.A;
				for (int32 q = From; q <= S.B; ++q)
				{
					Suppress[q] = true;
				}
			};

			auto DoToken = [&](int32 t)
			{
				const FArtsToken& T = Tokens[t];
				if (FiredBy[t] != INDEX_NONE)
				{
					return;   // dentro de um especial que disparou: é ele
				}
				if (T.Row->Kind == EJRPGArtKind::Hyper)
				{
					const int32 Cost = FMath::Max(T.Row->APCost, 0);
					if (Running < Cost)
					{
						// Não paga: vira os botões dela (golpes / as Regular dentro) e a cadeia é refeita
						FJRPGArtDowngrade& Note = HyperDowngrades.AddDefaulted_GetRef();
						Note.ArtKey = T.Key;
						Note.Reason = EJRPGArtDowngradeReason::NotEnoughAP;
						Note.FirstButton = T.First;
						Note.LastButton = T.Last;
						Note.APAtPoint = Running;
						Note.APCost = Cost;
						Excluded.Add({ T.Key, T.Last });
						bRedo = true;
						return;
					}
					FJRPGArtChainEntry& E = Entries.AddDefaulted_GetRef();
					E.Type = EJRPGArtChainStepType::Art;
					E.ArtKey = T.Key;
					E.FirstButton = T.First;
					E.LastButton = T.Last;
					E.APBefore = Running;
					E.APAfter = Running - Cost;
					Running -= Cost;
					MarkNew(T.Key, &E);
					for (int32 q = T.First + 1; q <= T.Last; ++q)
					{
						Suppress[q] = true;
					}
				}
				else
				{
					FJRPGArtChainEntry& E = Entries.AddDefaulted_GetRef();
					E.Type = EJRPGArtChainStepType::Art;
					E.ArtKey = T.Key;
					E.FirstButton = T.First;
					E.LastButton = T.Last;
					E.APBefore = Running;
					E.APAfter = FMath::Min(Running + FMath::Max(T.Row->APGain, 0), MaxAP);
					Running = E.APAfter;
					MarkNew(T.Key, &E);
				}
				bEmitted = true;
			};

			const bool bTokenInside = Ti != INDEX_NONE && Here.ContainsByPredicate([&](int32 s) { return IsInside(Tokens[Ti], Specials[s]); });
			if (bTokenInside)
			{
				for (const int32 s : Here)
				{
					DoSpecial(s);
				}
				DoToken(Ti);
			}
			else
			{
				// Uma Art que começa antes da faixa e termina nela toca antes do especial
				if (Ti != INDEX_NONE)
				{
					DoToken(Ti);
				}
				if (!bRedo)
				{
					for (const int32 s : Here)
					{
						DoSpecial(s);
					}
				}
			}
			if (bRedo)
			{
				break;
			}
			if (!bEmitted && !Suppress[p])
			{
				FJRPGArtChainEntry& Swing = Entries.AddDefaulted_GetRef();
				Swing.Type = EJRPGArtChainStepType::Swing;
				Swing.Button = Buttons[p];
				Swing.FirstButton = Swing.LastButton = p;
				Swing.APBefore = Swing.APAfter = Running;
			}
		}
		if (bRedo)
		{
			continue;
		}

		// --- Pronto ------------------------------------------------------------------------------
		// Uma Art aprendida dentro de um especial que disparou (só numa Super que não pede a cadeia)
		// também entra: os botões dela foram feitos
		for (int32 t = 0; t < Tokens.Num(); ++t)
		{
			if (FiredBy[t] != INDEX_NONE)
			{
				MarkNew(Tokens[t].Key, nullptr);
			}
		}
		TArray<FName> FlaggedRecognized;
		for (int32 t = 0; t < Tokens.Num(); ++t)
		{
			const FArtsToken& T = Tokens[t];
			FJRPGRecognizedArt& R = Chain.Recognized.AddDefaulted_GetRef();
			R.ArtKey = T.Key;
			R.Kind = T.Row->Kind;
			R.FirstButton = T.First;
			R.LastButton = T.Last;
			R.InsideSpecial = FiredBy[t] != INDEX_NONE ? Specials[FiredBy[t]].Key : NAME_None;
			// O aviso de "aprendida" vai para a primeira vez que ela aparece (o reorder do original)
			if (NewlyLearned.Contains(T.Key) && !FlaggedRecognized.Contains(T.Key))
			{
				R.bNewlyLearned = true;
				FlaggedRecognized.Add(T.Key);
			}
		}
		Chain.Entries = MoveTemp(Entries);
		Chain.Downgrades = HyperDowngrades;
		Chain.Downgrades.Append(Downgrades);
		Chain.Downgrades.Append(Locked);
		Chain.Downgrades.StableSort([](const FJRPGArtDowngrade& X, const FJRPGArtDowngrade& Y)
		{
			return X.FirstButton != Y.FirstButton ? X.FirstButton < Y.FirstButton : X.LastButton < Y.LastButton;
		});
		Chain.NewlyLearned = MoveTemp(NewlyLearned);
		Chain.APEnd = Running;
		return Chain;
	}

	// Não chega aqui (cada volta tira uma ocorrência); se chegar, os botões como golpes
	UE_LOG(LogTemp, Error, TEXT("ArtsRules: a cadeia de %s não fechou"), *Context.Character.ToString());
	Chain.Entries.Reset();
	for (int32 p = 0; p < N; ++p)
	{
		FJRPGArtChainEntry& Swing = Chain.Entries.AddDefaulted_GetRef();
		Swing.Type = EJRPGArtChainStepType::Swing;
		Swing.Button = Buttons[p];
		Swing.FirstButton = Swing.LastButton = p;
		Swing.APBefore = Swing.APAfter = Chain.APStart;
	}
	return Chain;
}

TArray<FJRPGArtAvailability> UJRPGArtsRules::GetSpecialsAvailability(const UDataTable* Arts, const FJRPGArtsContext& Context,
	const TArray<EJRPGArtButton>& TypedSoFar)
{
	// O AP no fim do que já foi digitado: o especial digitado a seguir é checado contra ele. A prévia
	// não sorteia (um Rng fixo): com a chance padrão de 100 não há sorteio nenhum
	FRandomStream PreviewRng(1);
	const FJRPGArtChain Chain = BuildChain(Arts, Context, TypedSoFar, PreviewRng);
	const TArray<FArtsRowRef> Catalog = TokenArts(Arts, Context.Character);
	TMap<FName, TArray<FName>> OwnCache;

	TArray<FJRPGArtAvailability> Out;
	for (const FArtsRowRef& Ref : HeroRows(Arts, Context.Character))
	{
		const EJRPGArtKind Kind = Ref.Row->Kind;
		if (Kind != EJRPGArtKind::Hyper && Kind != EJRPGArtKind::Super && Kind != EJRPGArtKind::Miracle)
		{
			continue;
		}
		FJRPGArtAvailability& A = Out.AddDefaulted_GetRef();
		A.ArtKey = Ref.Key;
		A.Kind = Kind;
		A.APCost = Ref.Row->APCost;
		A.APAvailable = Chain.APEnd;
		A.bAffordable = Chain.APEnd >= Ref.Row->APCost;
		if (Kind == EJRPGArtKind::Hyper)
		{
			// A mesma regra do casamento, sem o sorteio (OnUse conta como destravada)
			TMap<FName, bool> NoRolls;
			FJRPGArtsContext Sure = Context;
			Sure.LearnOnUseChance = 100;
			A.bUnlocked = CanTokenMatch(Ref, Sure, PreviewRng, NoRolls);
		}
		else
		{
			A.bUnlocked = IsSpecialUnlocked(Ref, Context, Catalog, OwnCache);
		}
	}
	return Out;
}

// --- Para tocar ------------------------------------------------------------------------------

void UJRPGArtsRules::AppendChainSteps(const FJRPGArtChain& Chain, bool bLeftHanded, TArray<FJRPGArtStep>& Steps)
{
	for (const FJRPGArtChainEntry& Entry : Chain.Entries)
	{
		switch (Entry.Type)
		{
		case EJRPGArtChainStepType::Swing:
			UJRPGArtSequenceLibrary::AppendButtonSwing(Chain.Character, Entry.Button, bLeftHanded, Steps);
			break;
		default:
			// Os golpes antes do último botão já são linhas Swing da cadeia: aqui só o starter + a Art
			// (ou a Sequence: a Hyper, a Super e a Miracle)
			UJRPGArtSequenceLibrary::AppendArtSteps(Entry.ArtKey, bLeftHanded, false, Steps);
			break;
		}
	}
}

FString UJRPGArtsRules::DescribeChain(const FJRPGArtChain& Chain)
{
	auto Name = [](FName Key)
	{
		const FJRPGArtData* Row = UJRPGArtSequenceLibrary::FindArt(Key);
		return Row && !Row->DisplayName.IsEmpty() ? Row->DisplayName.ToString() : Key.ToString();
	};
	FString Out;
	for (const FJRPGArtChainEntry& E : Chain.Entries)
	{
		switch (E.Type)
		{
		case EJRPGArtChainStepType::Swing:
			Out += FString::Printf(TEXT("  %s\n"), *ButtonName(E.Button));
			break;
		case EJRPGArtChainStepType::Art:
			Out += FString::Printf(TEXT("  [%d-%d] %s%s  AP %d -> %d\n"), E.FirstButton + 1, E.LastButton + 1,
				*Name(E.ArtKey), E.bNewlyLearned ? TEXT(" (learned!)") : TEXT(""), E.APBefore, E.APAfter);
			break;
		default:
		{
			FString Inside;
			for (const FName& Key : E.InsideArts)
			{
				Inside += (Inside.IsEmpty() ? TEXT("") : TEXT(", ")) + Name(Key);
			}
			Out += FString::Printf(TEXT("  [%d-%d] ** %s **  AP %d -> %d  (%s)\n"), E.FirstButton + 1, E.LastButton + 1,
				*Name(E.ArtKey), E.APBefore, E.APAfter, *Inside);
			break;
		}
		}
	}
	for (const FJRPGArtDowngrade& D : Chain.Downgrades)
	{
		Out += D.Reason == EJRPGArtDowngradeReason::Locked
			? FString::Printf(TEXT("  x %s: locked\n"), *Name(D.ArtKey))
			: FString::Printf(TEXT("  x %s: needs %d AP, had %d\n"), *Name(D.ArtKey), D.APCost, D.APAtPoint);
	}
	Out += FString::Printf(TEXT("  AP %d -> %d"), Chain.APStart, Chain.APEnd);
	return Out;
}
